/* Standalone experiment: does mapping a D3D12_HEAP_TYPE_READBACK buffer
 * after a real, fence-signaled GPU copy reliably return the GPU's latest
 * write under this system's real vkd3d-proton/Wine/RADV stack - or can
 * the CPU-visible view still be stale?
 *
 * Why this exists (docs/linux-support-spec.md \xc2\xa750e): danielblnc's
 * runtime reports a deterministic, repeatable "capture never landed"
 * pattern (same job indices fail the same way, every single run) when
 * checking whether the game actually executed a recorded command list -
 * it works by writing a "word" from the GPU and polling it from the CPU.
 * Two competing explanations were raised for this: (a) vkd3d-proton
 * reorders/batches ExecuteCommandLists relative to native Windows, or
 * (b) the CPU-visible read of a GPU-written buffer isn't properly
 * invalidated/coherent under this Vulkan translation, so the CPU can see
 * a stale value even after the GPU write genuinely completed. This probe
 * tests (b) directly and in isolation, with no ambiguity about *whether*
 * the GPU finished - unlike danielblnc's own check, this one does a real
 * blocking ID3D12Fence wait before every single read, so if a stale value
 * is ever observed here, that is unambiguous proof of a real memory-
 * visibility bug, independent of any question about danielblnc's own
 * wait-shader timing.
 *
 * Sequence, repeated for a real, moderately large iteration count:
 *   1. CPU writes a known marker into an UPLOAD buffer.
 *   2. A real GPU CopyBufferRegion copies it into a READBACK buffer.
 *   3. ExecuteCommandLists, then a REAL BLOCKING fence wait (unlike
 *      danielblnc's own polling scheme, this cannot return early).
 *   4. CPU Maps the READBACK buffer and reads the value back.
 *   5. Compare against the marker just written - any mismatch here,
 *      after step 3's real completed-fence guarantee, is a coherency bug,
 *      not a scheduling/reordering one.
 *
 * No proprietary code: real, public D3D12 APIs only, structured after
 * daniel/hip-unixlib/interop_roundtrip_test.c's own device/queue/fence
 * setup. Nothing to do with danielblnc's actual kernels or runtime.
 *
 * Build (needs this project's patched d3d12.dll/d3d12core.dll on the
 * PATH/prefix - same disposable-prefix or real-game-folder caveat as
 * interop_roundtrip_test.c, see its own STATUS comment):
 *   winegcc -o readback_coherency_probe.exe readback_coherency_probe.c \
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

#define ITERATIONS 200

static void check_hr(const char *step, HRESULT hr)
{
    if (FAILED(hr))
    {
        printf("FATAL: %s failed, hr=%#lx\n", step, (unsigned long)hr);
        exit(1);
    }
}

static void wait_for_queue(ID3D12CommandQueue *queue, ID3D12Fence *fence, HANDLE event, UINT64 *value)
{
    HRESULT hr;
    ++*value;
    hr = ID3D12CommandQueue_Signal(queue, fence, *value);
    check_hr("Signal", hr);
    if (ID3D12Fence_GetCompletedValue(fence) < *value)
    {
        hr = ID3D12Fence_SetEventOnCompletion(fence, *value, event);
        check_hr("SetEventOnCompletion", hr);
        WaitForSingleObject(event, INFINITE);
    }
}

int main(void)
{
    HMODULE d3d12_mod;
    HRESULT (WINAPI *pD3D12CreateDevice)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
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
    unsigned int i, mismatches = 0, first_mismatch = 0, first_expected = 0, first_got = 0;

    printf("=== READBACK-heap coherency probe: %d real fence-waited GPU write/CPU read cycles ===\n\n", ITERATIONS);

    d3d12_mod = LoadLibraryA("d3d12.dll");
    if (!d3d12_mod) { printf("FATAL: LoadLibraryA(d3d12.dll) failed.\n"); return 1; }
    pD3D12CreateDevice = (void *)GetProcAddress(d3d12_mod, "D3D12CreateDevice");

    hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    check_hr("D3D12CreateDevice", hr);

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    check_hr("CreateCommandQueue", hr);

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&allocator);
    check_hr("CreateCommandAllocator", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
            &IID_ID3D12GraphicsCommandList, (void **)&list);
    check_hr("CreateCommandList", hr);
    ID3D12GraphicsCommandList_Close(list);

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    check_hr("CreateFence", hr);
    fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);

    memset(&heap_props, 0, sizeof(heap_props));
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = sizeof(UINT32);
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

    for (i = 0; i < ITERATIONS; i++)
    {
        UINT32 marker = 1000u + i; /* nonzero, distinct each iteration, easy to spot a stale/wrong read */

        map_range.Begin = 0; map_range.End = 0;
        hr = ID3D12Resource_Map(upload_buf, 0, &map_range, (void **)&upload_mapped);
        check_hr("Map(upload)", hr);
        *upload_mapped = marker;
        ID3D12Resource_Unmap(upload_buf, 0, NULL);

        hr = ID3D12CommandAllocator_Reset(allocator);
        check_hr("Reset(allocator)", hr);
        hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
        check_hr("Reset(list)", hr);
        ID3D12GraphicsCommandList_CopyBufferRegion(list, readback_buf, 0, upload_buf, 0, sizeof(UINT32));
        hr = ID3D12GraphicsCommandList_Close(list);
        check_hr("Close(list)", hr);
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList *const *)&list);

        /* Real blocking fence wait - by the time this returns, the GPU
         * copy is unambiguously complete. No polling, no timeout, no
         * assumption about scheduling. */
        wait_for_queue(queue, fence, fence_event, &fence_value);

        map_range.Begin = 0; map_range.End = sizeof(UINT32);
        hr = ID3D12Resource_Map(readback_buf, 0, &map_range, (void **)&readback_mapped);
        check_hr("Map(readback)", hr);
        UINT32 got = *readback_mapped;
        map_range.Begin = 0; map_range.End = 0;
        ID3D12Resource_Unmap(readback_buf, 0, &map_range);

        if (got != marker)
        {
            if (mismatches == 0) { first_mismatch = i; first_expected = marker; first_got = got; }
            mismatches++;
        }
    }

    printf("iterations: %d, mismatches: %u\n", ITERATIONS, mismatches);
    if (mismatches)
    {
        printf("first mismatch at iteration %u: expected %u, CPU read %u (after a real completed fence wait)\n",
                first_mismatch, first_expected, first_got);
        printf("\n=== FAIL: a real GPU write, fence-confirmed complete, was not visible to the CPU. "
               "This is a real memory-coherency bug, not a scheduling/reordering issue. ===\n");
    }
    else
    {
        printf("\n=== PASS: every one of %d fence-waited GPU writes was immediately visible to the CPU. "
               "The READBACK-heap coherency theory is ruled out on this system. ===\n", ITERATIONS);
    }

    ID3D12Resource_Release(readback_buf);
    ID3D12Resource_Release(upload_buf);
    ID3D12Fence_Release(fence);
    CloseHandle(fence_event);
    ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocator);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device);

    return mismatches ? 1 : 0;
}
