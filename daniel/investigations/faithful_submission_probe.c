/* Standalone experiment: does a *properly* fence-guaranteed, ring-buffer
 * ("deferred") command-list submission - waiting only when a ring slot
 * needs reuse, not after every single ExecuteCommandLists call, matching
 * how a real game actually submits work - ever produce a stale or
 * dropped result under this system's real vkd3d-proton/Wine/RADV stack?
 *
 * Why this exists (docs/linux-support-spec.md \xc2\xa761): this project's
 * earlier probes tested two much more idealized cases -
 * readback_coherency_probe.c always did a full blocking wait after every
 * single submission (\xc2\xa751, ruled out memory-coherency issues), and
 * submission_timing_probe.c issued N submissions with NO waits at all in
 * between (\xc2\xa752, didn't reproduce anything, but was arguably too
 * unrealistic in the other direction - no real game submits with zero
 * synchronization discipline either). Real games use exactly the pattern
 * this probe tests: a ring of command-list slots reused round-robin,
 * waiting on a slot's fence only when that slot is about to be reused,
 * never after every single submission. This is a materially different,
 * more realistic middle ground neither earlier probe covered.
 *
 * The ring-buffer submission logic (RingSubmit/RingFlush below) is
 * adapted (C port, restructured to this project's style, not copied
 * verbatim) from guentra/dlss5-amd-hip-linux's src/native_game_submission.h
 * (its `NativeGameSubmission` class, deferred=true mode), MIT licensed:
 *   https://github.com/guentra/dlss5-amd-hip-linux
 *   Copyright (c) guentra - MIT License (see that repo's LICENSE)
 * The producer-ordering check uses this project's own snapshot_gate.c,
 * itself adapted from the same repo's native_snapshot_gate.h.
 *
 * Sequence: an 8-slot ring of allocators/command lists on one DIRECT
 * queue. 40 total submissions (5 full ring wraps), each one: CPU writes
 * a known marker into a shared UPLOAD buffer at its own offset, a real
 * GPU CopyBufferRegion copies it into its own offset in one READBACK
 * buffer, `ExecuteCommandLists` with exactly that one list. A ring slot
 * is only fence-waited when it's about to be reused (matching real
 * deferred submission - most submissions issue with zero CPU wait). A
 * real snapshot_gate check runs per-submission (trivially satisfied here
 * since each ExecuteCommandLists call carries exactly one list - the
 * check exists as real, working, ported logic, not padding). A final
 * real blocking Flush() waits for the last fence value before any
 * readback is trusted. Only then are all 40 marker slots checked.
 *
 * No proprietary code: real, public D3D12 APIs plus this project's own
 * ported MIT-licensed logic. Nothing to do with danielblnc's actual
 * kernels or runtime - this tests vkd3d-proton's own submission
 * behavior in isolation, same as this project's other investigations/
 * probes.
 *
 * Build:
 *   winegcc -o faithful_submission_probe.exe faithful_submission_probe.c \
 *       -b x86_64-windows --no-default-config \
 *       -L/usr/lib/x86_64-linux-gnu/wine/x86_64-windows -ld3d12
 *
 * STATUS: run only as a standalone synthetic probe (same safe pattern as
 * this project's other investigations/ probes - dropped into the real
 * game's bin/x64/ and run via the real, working Proton prefix directly
 * through `wine`, memory-capped and wall-clock-limited, WITHOUT actually
 * launching the real game). Explicitly NOT run against the live
 * Cyberpunk 2077 process itself without separate, explicit go-ahead. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>

#include <stdio.h>
#include <stdint.h>

/* Mirrors snapshot_gate.h/.c exactly - not #included directly since
 * that file is written to be Wine/D3D12-independent for its own unit
 * tests (plain host gcc), and this file needs the real Windows PE
 * build instead. Identical logic. */
static int snapshot_batch_match(uint32_t producer_thread, uint32_t current_thread,
        UINT_PTR producer_list, const UINT_PTR *lists, size_t count)
{
    size_t i;
    if (!producer_list || !lists || !count || count > 64 || producer_thread != current_thread)
        return 0;
    if (lists[count - 1] != producer_list)
        return 0;
    for (i = 0; i + 1 < count; i++)
        if (lists[i] == producer_list)
            return 0;
    return 1;
}

#define RING_SLOTS 8
#define TOTAL_SUBMISSIONS 40
#define MARKER_BASE 9000u

static void check_hr(const char *step, HRESULT hr)
{
    if (FAILED(hr))
    {
        printf("FATAL: %s failed, hr=%#lx\n", step, (unsigned long)hr);
        exit(1);
    }
}

/* --- Ring-buffer deferred submission, adapted from NativeGameSubmission
 * (deferred=true path) --- */
struct RingSubmission
{
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12Fence *fence;
    HANDLE event;
    UINT64 value;
    ID3D12CommandAllocator *allocators[RING_SLOTS];
    ID3D12GraphicsCommandList *lists[RING_SLOTS];
    UINT64 ring_values[RING_SLOTS];
};

static void ring_wait_value(struct RingSubmission *r, UINT64 target)
{
    HRESULT hr;
    if (ID3D12Fence_GetCompletedValue(r->fence) >= target)
        return;
    hr = ID3D12Fence_SetEventOnCompletion(r->fence, target, r->event);
    check_hr("SetEventOnCompletion", hr);
    if (WaitForSingleObject(r->event, 30000) != WAIT_OBJECT_0)
    {
        printf("FATAL: ring slot wait timed out (target=%llu)\n", (unsigned long long)target);
        exit(1);
    }
}

static void ring_create(struct RingSubmission *r, ID3D12CommandQueue *queue)
{
    HRESULT hr;
    int i;
    memset(r, 0, sizeof(*r));
    r->queue = queue;
    hr = ID3D12CommandQueue_GetDevice(queue, &IID_ID3D12Device, (void **)&r->device);
    check_hr("GetDevice", hr);
    hr = ID3D12Device_CreateFence(r->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&r->fence);
    check_hr("CreateFence", hr);
    r->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (i = 0; i < RING_SLOTS; i++)
    {
        hr = ID3D12Device_CreateCommandAllocator(r->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                &IID_ID3D12CommandAllocator, (void **)&r->allocators[i]);
        check_hr("CreateCommandAllocator(ring)", hr);
        hr = ID3D12Device_CreateCommandList(r->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                r->allocators[i], NULL, &IID_ID3D12GraphicsCommandList, (void **)&r->lists[i]);
        check_hr("CreateCommandList(ring)", hr);
        hr = ID3D12GraphicsCommandList_Close(r->lists[i]);
        check_hr("Close(ring)", hr);
    }
}

/* Returns the fence value this submission was signaled with, and via
 * *gate_ok whether the real snapshot_gate check passed for it (always
 * true here - one list per ExecuteCommandLists call - but run for real,
 * not skipped). */
static UINT64 ring_submit(struct RingSubmission *r, ID3D12Resource *readback,
        ID3D12Resource *upload, UINT64 offset, int *gate_ok)
{
    UINT slot = (UINT)(r->value % RING_SLOTS);
    ID3D12GraphicsCommandList *list;
    HRESULT hr;
    UINT_PTR batch[1];

    if (r->ring_values[slot])
        ring_wait_value(r, r->ring_values[slot]);

    list = r->lists[slot];
    hr = ID3D12CommandAllocator_Reset(r->allocators[slot]);
    check_hr("Reset(allocator)", hr);
    hr = ID3D12GraphicsCommandList_Reset(list, r->allocators[slot], NULL);
    check_hr("Reset(list)", hr);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, readback, offset, upload, offset, sizeof(UINT32));
    hr = ID3D12GraphicsCommandList_Close(list);
    check_hr("Close(list)", hr);

    batch[0] = (UINT_PTR)list;
    ID3D12CommandQueue_ExecuteCommandLists(r->queue, 1, (ID3D12CommandList *const *)&list);
    *gate_ok = snapshot_batch_match(GetCurrentThreadId(), GetCurrentThreadId(), (UINT_PTR)list, batch, 1);

    ++r->value;
    hr = ID3D12CommandQueue_Signal(r->queue, r->fence, r->value);
    check_hr("Signal", hr);
    r->ring_values[slot] = r->value;
    return r->value;
}

static void ring_flush(struct RingSubmission *r)
{
    if (r->value)
        ring_wait_value(r, r->value);
}

int main(void)
{
    HMODULE d3d12_mod;
    HRESULT (WINAPI *pD3D12CreateDevice)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    HRESULT hr;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *upload_buf, *readback_buf;
    D3D12_RANGE map_range;
    UINT32 *upload_mapped, *readback_mapped;
    int i, mismatches = 0, gate_failures = 0, first_mismatch = -1, first_expected = 0, first_got = 0;
    struct RingSubmission ring;

    printf("=== Faithful ring-buffer submission probe: %d submissions, %d-slot ring ===\n\n",
            TOTAL_SUBMISSIONS, RING_SLOTS);
    printf("(ring-buffer + fence logic adapted from guentra/dlss5-amd-hip-linux, MIT licensed)\n\n");

    d3d12_mod = LoadLibraryA("d3d12.dll");
    if (!d3d12_mod) { printf("FATAL: LoadLibraryA(d3d12.dll) failed.\n"); return 1; }
    pD3D12CreateDevice = (void *)GetProcAddress(d3d12_mod, "D3D12CreateDevice");

    hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    check_hr("D3D12CreateDevice", hr);

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue(device, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    check_hr("CreateCommandQueue", hr);

    ring_create(&ring, queue);

    memset(&heap_props, 0, sizeof(heap_props));
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = TOTAL_SUBMISSIONS * sizeof(UINT32);
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

    map_range.Begin = 0; map_range.End = 0;
    hr = ID3D12Resource_Map(upload_buf, 0, &map_range, (void **)&upload_mapped);
    check_hr("Map(upload)", hr);
    for (i = 0; i < TOTAL_SUBMISSIONS; i++)
        upload_mapped[i] = MARKER_BASE + (UINT32)i;
    ID3D12Resource_Unmap(upload_buf, 0, NULL);

    for (i = 0; i < TOTAL_SUBMISSIONS; i++)
    {
        int gate_ok = 0;
        ring_submit(&ring, readback_buf, upload_buf, (UINT64)i * sizeof(UINT32), &gate_ok);
        if (!gate_ok)
            gate_failures++;
    }

    /* The real point of the test: a genuine blocking wait for the very
     * last fence value, exactly matching Flush() semantics - only after
     * this returns is any readback considered safe to trust. */
    ring_flush(&ring);

    map_range.Begin = 0; map_range.End = TOTAL_SUBMISSIONS * sizeof(UINT32);
    hr = ID3D12Resource_Map(readback_buf, 0, &map_range, (void **)&readback_mapped);
    check_hr("Map(readback)", hr);
    for (i = 0; i < TOTAL_SUBMISSIONS; i++)
    {
        UINT32 expected = MARKER_BASE + (UINT32)i;
        if (readback_mapped[i] != expected)
        {
            if (mismatches == 0) { first_mismatch = i; first_expected = (int)expected; first_got = (int)readback_mapped[i]; }
            mismatches++;
        }
    }
    map_range.Begin = 0; map_range.End = 0;
    ID3D12Resource_Unmap(readback_buf, 0, &map_range);

    printf("submissions: %d, ring slots: %d, gate check failures: %d, readback mismatches: %d\n",
            TOTAL_SUBMISSIONS, RING_SLOTS, gate_failures, mismatches);
    if (mismatches)
        printf("first mismatch at index %d: expected %d, got %d (after a real Flush())\n",
                first_mismatch, first_expected, first_got);

    if (!gate_failures && !mismatches)
        printf("\n=== PASS: all %d ring-buffer submissions, with only slot-reuse waits (not "
               "per-submission), resolved correctly after a real Flush(). ===\n", TOTAL_SUBMISSIONS);
    else
        printf("\n=== FAIL: a properly fence-guaranteed, correctly-ordered ring-buffer submission "
               "still produced a wrong result on this system. ===\n");

    ID3D12Resource_Release(readback_buf);
    ID3D12Resource_Release(upload_buf);
    ID3D12Device_Release(device);
    ID3D12CommandQueue_Release(queue);

    return (mismatches || gate_failures) ? 1 : 0;
}
