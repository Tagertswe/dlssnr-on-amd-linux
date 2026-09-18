/* Standalone experiment: when N D3D12 command lists are submitted back
 * to back on one queue with NO wait in between (the realistic pattern -
 * a real game does not blocking-wait between every ExecuteCommandLists
 * call), does the GPU complete them in submission order, and how long
 * does each one actually take to become visible to a CPU spin-poll?
 *
 * Why this exists (docs/linux-support-spec.md \xc2\xa751): the memory-
 * coherency theory behind \xc2\xa750e's deterministic capture-drop pattern
 * was tested and ruled out - readback_coherency_probe.c proved a real,
 * fence-confirmed GPU write is always immediately visible to the CPU.
 * That leaves scheduling/timing as the remaining candidate, but "the
 * candidate" was actually two different things conflated together:
 *   (a) true reordering - a later submission's result becoming visible
 *       before an earlier one's, a real correctness bug, or
 *   (c) plain latency - everything completes strictly in order, just
 *       slower under vkd3d-proton/RADV than danielblnc's own polling
 *       budget assumes (tuned against native Windows/WDDM timing), so
 *       a check that runs "too soon" sees a real but not-yet-arrived
 *       result and calls it dropped.
 * "captured word 1, submitted 3" being identical on every single run
 * for the same job indices fits (c) at least as naturally as (a) - a
 * consistent latency gap produces exactly this kind of deterministic
 * "always short by the same amount" pattern. This probe measures both
 * directly: does completion order ever violate submission order, and
 * how long does each submission actually take to land.
 *
 * Sequence: N command lists, each copying a distinct known marker into
 * its own offset in one READBACK buffer, submitted with N separate
 * ExecuteCommandLists calls in a tight loop, no fence wait between any
 * of them. Immediately after issuing all N, spin-poll the (already
 * mapped, held open) READBACK buffer directly - no repeated Map/Unmap
 * (readback_coherency_probe.c already ruled out any per-read visibility
 * gap) - recording, for each slot, the first wall-clock timestamp
 * (QueryPerformanceCounter) its value becomes correct, within a bounded
 * budget. A slot that never resolves within budget is logged as timed
 * out; slots resolving out of submission order are flagged explicitly.
 *
 * No proprietary code: real, public D3D12 APIs only, same device/queue/
 * fence setup as daniel/hip-unixlib/interop_roundtrip_test.c and this
 * project's own readback_coherency_probe.c. Nothing to do with
 * danielblnc's actual kernels, shaders or runtime - this tests
 * vkd3d-proton's own submission/completion behavior in isolation.
 *
 * Build (same caveats as readback_coherency_probe.c - needs this
 * project's patched d3d12.dll/d3d12core.dll on the WINEPREFIX):
 *   winegcc -o submission_timing_probe.exe submission_timing_probe.c \
 *       -b x86_64-windows --no-default-config \
 *       -L/usr/lib/x86_64-linux-gnu/wine/x86_64-windows -ld3d12
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>

#include <stdio.h>
#include <stdint.h>

#define N_SUBMISSIONS 8
#define POLL_BUDGET_MS 1000.0
#define MARKER_BASE 7000u

static void check_hr(const char *step, HRESULT hr)
{
    if (FAILED(hr))
    {
        printf("FATAL: %s failed, hr=%#lx\n", step, (unsigned long)hr);
        exit(1);
    }
}

static double qpc_ms_since(LARGE_INTEGER start, LARGE_INTEGER freq)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) * 1000.0 / (double)freq.QuadPart;
}

int main(void)
{
    HMODULE d3d12_mod;
    HRESULT (WINAPI *pD3D12CreateDevice)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocators[N_SUBMISSIONS];
    ID3D12GraphicsCommandList *lists[N_SUBMISSIONS];
    ID3D12Fence *fence;
    HANDLE fence_event;
    UINT64 fence_value = 0;
    HRESULT hr;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *upload_buf, *readback_buf;
    D3D12_RANGE map_range;
    UINT32 *upload_mapped, *readback_mapped;
    int i;
    LARGE_INTEGER freq, submit_start;
    double resolved_at_ms[N_SUBMISSIONS];
    int resolved[N_SUBMISSIONS] = {0};
    int out_of_order = 0, last_resolved_index = -1, timed_out = 0;

    printf("=== Submission-order/timing probe: %d back-to-back ExecuteCommandLists, no waits ===\n\n", N_SUBMISSIONS);

    QueryPerformanceFrequency(&freq);

    d3d12_mod = LoadLibraryA("d3d12.dll");
    if (!d3d12_mod) { printf("FATAL: LoadLibraryA(d3d12.dll) failed.\n"); return 1; }
    pD3D12CreateDevice = (void *)GetProcAddress(d3d12_mod, "D3D12CreateDevice");

    hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    check_hr("D3D12CreateDevice", hr);

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    check_hr("CreateCommandQueue", hr);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    check_hr("CreateFence", hr);
    fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);

    memset(&heap_props, 0, sizeof(heap_props));
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = N_SUBMISSIONS * sizeof(UINT32);
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&upload_buf);
    check_hr("CreateCommittedResource(upload)", hr);

    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&readback_buf);
    check_hr("CreateCommittedResource(readback)", hr);

    /* Write all N markers up front, then record N independent command
     * lists (one allocator each - none get reset/reused until the very
     * end, so nothing here can accidentally serialize the submissions). */
    map_range.Begin = 0; map_range.End = 0;
    hr = ID3D12Resource_Map(upload_buf, 0, &map_range, (void **)&upload_mapped);
    check_hr("Map(upload)", hr);
    for (i = 0; i < N_SUBMISSIONS; i++)
        upload_mapped[i] = MARKER_BASE + (UINT32)i;
    ID3D12Resource_Unmap(upload_buf, 0, NULL);

    for (i = 0; i < N_SUBMISSIONS; i++)
    {
        hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                &IID_ID3D12CommandAllocator, (void **)&allocators[i]);
        check_hr("CreateCommandAllocator", hr);
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i], NULL,
                &IID_ID3D12GraphicsCommandList, (void **)&lists[i]);
        check_hr("CreateCommandList", hr);
        ID3D12GraphicsCommandList_CopyBufferRegion(lists[i], readback_buf, (UINT64)i * sizeof(UINT32),
                upload_buf, (UINT64)i * sizeof(UINT32), sizeof(UINT32));
        hr = ID3D12GraphicsCommandList_Close(lists[i]);
        check_hr("Close(list)", hr);
    }

    /* Hold the readback buffer mapped for the whole spin-poll window -
     * readback_coherency_probe.c already ruled out any per-read
     * visibility gap, and this mirrors how a real spin-poll wait
     * (docs/linux-support-spec.md's "inline: predicated spin slices")
     * would actually read memory: once, directly, repeatedly. */
    hr = ID3D12Resource_Map(readback_buf, 0, NULL, (void **)&readback_mapped);
    check_hr("Map(readback, held open)", hr);

    QueryPerformanceCounter(&submit_start);
    for (i = 0; i < N_SUBMISSIONS; i++)
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList *const *)&lists[i]);
    /* Deliberately NO wait here - this is the point of the probe. */

    printf("issued all %d ExecuteCommandLists calls, spin-polling (budget %.0f ms)...\n\n",
            N_SUBMISSIONS, POLL_BUDGET_MS);

    while (qpc_ms_since(submit_start, freq) < POLL_BUDGET_MS)
    {
        for (i = 0; i < N_SUBMISSIONS; i++)
        {
            if (!resolved[i] && readback_mapped[i] == MARKER_BASE + (UINT32)i)
            {
                resolved[i] = 1;
                resolved_at_ms[i] = qpc_ms_since(submit_start, freq);
                if (i < last_resolved_index)
                    out_of_order = 1;
                if (i > last_resolved_index)
                    last_resolved_index = i;
            }
        }
        int all_done = 1;
        for (i = 0; i < N_SUBMISSIONS; i++)
            if (!resolved[i]) { all_done = 0; break; }
        if (all_done)
            break;
    }

    /* Make sure the GPU is actually done before tearing anything down,
     * regardless of what the spin-poll observed. */
    ++fence_value;
    ID3D12CommandQueue_Signal(queue, fence, fence_value);
    if (ID3D12Fence_GetCompletedValue(fence) < fence_value)
    {
        ID3D12Fence_SetEventOnCompletion(fence, fence_value, fence_event);
        WaitForSingleObject(fence_event, INFINITE);
    }

    map_range.Begin = 0; map_range.End = 0;
    ID3D12Resource_Unmap(readback_buf, 0, &map_range);

    printf("--- per-slot results (submission order 0..%d) ---\n", N_SUBMISSIONS - 1);
    for (i = 0; i < N_SUBMISSIONS; i++)
    {
        if (resolved[i])
            printf("  slot %d: resolved at %.3f ms\n", i, resolved_at_ms[i]);
        else
        {
            printf("  slot %d: TIMED OUT (never resolved within %.0f ms)\n", i, POLL_BUDGET_MS);
            timed_out = 1;
        }
    }

    printf("\n");
    if (timed_out)
        printf("=== At least one submission never resolved within the poll budget - "
               "a real latency/pacing gap exists under this stack. ===\n");
    if (out_of_order)
        printf("=== A later submission resolved before an earlier one - "
               "real reordering, not just latency. ===\n");
    if (!timed_out && !out_of_order)
        printf("=== All %d submissions resolved strictly in order within budget - "
               "no reordering, no timeout observed on this system for this pattern. ===\n", N_SUBMISSIONS);

    for (i = 0; i < N_SUBMISSIONS; i++)
    {
        ID3D12GraphicsCommandList_Release(lists[i]);
        ID3D12CommandAllocator_Release(allocators[i]);
    }
    ID3D12Resource_Release(readback_buf);
    ID3D12Resource_Release(upload_buf);
    ID3D12Fence_Release(fence);
    CloseHandle(fence_event);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device);

    return (timed_out || out_of_order) ? 1 : 0;
}
