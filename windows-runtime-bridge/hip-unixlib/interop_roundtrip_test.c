/* Real, standalone end-to-end interop test: does data actually flow
 * correctly across the D3D12<->HIP shared-memory boundary, in both
 * directions - not just "does exporting a handle succeed" (already
 * covered by vkd3d-proton's own test_external_memory_fd_* suite).
 *
 * This is the gap identified in docs/linux-support-spec.md's own
 * review of this project's test coverage: every existing test checks
 * mechanics (does CreateSharedHandle succeed, does the handle look
 * fd-shaped) but none of them write a known value on one side and
 * read it back on the other through the real shared memory. This is
 * that test - built once the gap was identified, not before.
 *
 * Exercises the exact real production path:
 *   1. A real D3D12 device creates a D3D12_HEAP_FLAG_SHARED buffer
 *      (same shape create_shared_buffer_desc() in vkd3d-proton's own
 *      test suite uses) and writes a known pattern into it via a real
 *      GPU copy from an upload buffer.
 *   2. CreateSharedHandle() exports it - this project's own
 *      vkd3d-proton patch (VKD3D_CONFIG=external_memory_fd) hands
 *      back a real Linux fd.
 *   3. This project's own amdhip64_7.dll (built by ../hip-unixlib,
 *      the exact same shim danielblnc's runtime is bridged through)
 *      imports that fd via the real, live hipImportExternalMemory/
 *      hipExternalMemoryGetMappedBuffer path - using
 *      HIP_EXT_MEM_TYPE_D3D12_RESOURCE with the fd smuggled through
 *      handle.win32.handle, exactly matching danielblnc's own real
 *      usage (see ext_mem.h).
 *   4. hipMemcpy reads the buffer back through the real device
 *      pointer HIP handed back, and this test compares it against the
 *      pattern D3D12 wrote - proving direction 1 (D3D12 write -> HIP
 *      read) actually works, not just that the handle exists.
 *   5. Direction 2 (HIP write -> D3D12 read) is tested the same way,
 *      reversed: hipMemcpy writes a new pattern through the HIP
 *      device pointer, then a real D3D12 copy into a CPU-readable
 *      readback buffer confirms it landed correctly.
 *
 * Requires: VKD3D_CONFIG=external_memory_fd (this project's
 * vkd3d-proton patch's opt-in flag) and WINEDLLOVERRIDES pointing
 * amdhip64_7 at this project's own built DLL - the same two things a
 * real game launch needs. Run under Wine, memory-capped and
 * wall-clock-limited per this project's own safety convention for
 * anything that creates a real D3D12/Vulkan device.
 *
 * No proprietary code: this test only exercises this project's own
 * shim and patch, plus real, public D3D12/HIP APIs. It has nothing to
 * do with danielblnc's actual kernels or runtime.
 *
 * STATUS (docs/linux-support-spec.md \xc2\xa746): code-complete and
 * compiles/links cleanly (`make interop_roundtrip_test.exe`), but NOT
 * YET RUNTIME-VERIFIED. A bare `wine` invocation needs the exact same
 * patched d3d12.dll/d3d12core.dll/vulkan-1.dll this project's own
 * vkd3d-proton build produces, correctly paired with a Wine build
 * carrying the wine-proton winevulkan patch - assembling that by hand
 * outside a real, already-working Proton prefix proved genuinely
 * fiddly (missing wined3d/dxgi dependencies, DLL-override ambiguity),
 * and one such attempt (`proton run` triggering a file-resync)
 * actually reverted the real game's own live prefix's d3d12.dll to
 * Wine's built-in stub - caught via checksum comparison and fixed by
 * restoring from the known-good redist build, but a real reminder that
 * this needs a genuinely disposable test environment, not
 * experimentation against the live prefix.
 *
 * Safest real verification path once someone is at the machine with
 * time to spend on it: either (a) build a throwaway Wine prefix from
 * scratch via `WINEPREFIX=/tmp/scratch-prefix wineboot`, install
 * vkd3d-proton's own patched d3d12.dll/d3d12core.dll plus the
 * wine-proton-patched vulkan-1.dll into its system32 (never touching
 * the real game's prefix at all), or (b) drop this .exe into the real
 * game's own bin/x64/ folder next to the deployed amdhip64_7.dll and
 * run it once via the game's own known-working Proton launch (through
 * Steam, or `proton run` from a clean state with no prior wineserver
 * for that prefix already running) - the same environment already
 * confirmed live, repeatedly, to have both patches working correctly. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>

#include <stdio.h>
#include <stdint.h>

/* Mirrors windows-runtime-bridge/hip-unixlib/ext_mem.h exactly - not re-included
 * directly since that header is written to be Wine/HIP-independent
 * (buildable with a plain host compiler for its own unit tests) and
 * this file needs the real Windows PE build instead. Byte-for-byte
 * identical layout - see ext_mem.h's own comment for why this exact
 * shape is the real HIP ABI danielblnc's binary uses. */
struct hip_ext_mem_handle_desc
{
    int type;
    union {
        int fd;
        struct { void *handle; const void *name; } win32;
        const void *nv_sci_buf_object;
    } handle;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};

struct hip_ext_mem_buffer_desc
{
    unsigned long long offset;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};

#define HIP_EXT_MEM_TYPE_D3D12_RESOURCE 5
#define HIP_MEMCPY_HOST_TO_DEVICE 1
#define HIP_MEMCPY_DEVICE_TO_HOST 2

typedef int (__cdecl *hipImportExternalMemory_t)(void **, const void *);
typedef int (__cdecl *hipExternalMemoryGetMappedBuffer_t)(void **, void *, const void *);
typedef int (__cdecl *hipDestroyExternalMemory_t)(void *);
typedef int (__cdecl *hipMemcpy_t)(void *, const void *, size_t, int);
typedef int (__cdecl *hipDeviceSynchronize_t)(void);
typedef const char *(__cdecl *hipGetErrorString_t)(int);

static hipGetErrorString_t p_hipGetErrorString;

static void check_hr(const char *step, HRESULT hr)
{
    printf("  %-40s -> hr=%#lx\n", step, (unsigned long)hr);
    if (FAILED(hr))
    {
        printf("FATAL: %s failed.\n", step);
        exit(1);
    }
}

static void check_hip(const char *step, int ret)
{
    printf("  %-40s -> ret=%d (%s)\n", step, ret,
            p_hipGetErrorString ? p_hipGetErrorString(ret) : "?");
    if (ret != 0)
    {
        printf("FATAL: %s failed.\n", step);
        exit(1);
    }
}

#define BUF_SIZE (64 * sizeof(float))

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
    HMODULE d3d12_mod, hip_mod;
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
    ID3D12Resource *upload_buf, *shared_buf, *readback_buf;
    D3D12_RANGE map_range;
    float *mapped, pattern_a[64], pattern_b[64], result[64];
    unsigned int i;
    D3D12_RESOURCE_BARRIER barrier;
    HANDLE shared_handle;
    int fd;

    hipImportExternalMemory_t p_hipImportExternalMemory;
    hipExternalMemoryGetMappedBuffer_t p_hipExternalMemoryGetMappedBuffer;
    hipDestroyExternalMemory_t p_hipDestroyExternalMemory;
    hipMemcpy_t p_hipMemcpy;
    hipDeviceSynchronize_t p_hipDeviceSynchronize;
    struct hip_ext_mem_handle_desc handle_desc;
    struct hip_ext_mem_buffer_desc buffer_desc;
    void *ext_mem, *dev_ptr;

    printf("=== D3D12<->HIP real interop round-trip test ===\n\n");

    for (i = 0; i < 64; i++)
    {
        pattern_a[i] = (float)i + 0.5f;         /* D3D12 -> HIP direction */
        pattern_b[i] = (float)(1000 + i) + 0.25f; /* HIP -> D3D12 direction */
    }

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

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    check_hr("CreateFence", hr);
    fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);

    /* --- upload buffer: real known pattern, D3D12-side --- */
    memset(&heap_props, 0, sizeof(heap_props));
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = BUF_SIZE;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&upload_buf);
    check_hr("CreateCommittedResource(upload)", hr);

    map_range.Begin = 0; map_range.End = 0;
    hr = ID3D12Resource_Map(upload_buf, 0, &map_range, (void **)&mapped);
    check_hr("Map(upload)", hr);
    memcpy(mapped, pattern_a, BUF_SIZE);
    ID3D12Resource_Unmap(upload_buf, 0, NULL);

    /* --- the real shared buffer - same shape production uses --- */
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_SHARED, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&shared_buf);
    check_hr("CreateCommittedResource(shared)", hr);

    /* --- real GPU copy: upload -> shared, matching production's own copy pattern --- */
    ID3D12GraphicsCommandList_CopyBufferRegion(list, shared_buf, 0, upload_buf, 0, BUF_SIZE);
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = shared_buf;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
    hr = ID3D12GraphicsCommandList_Close(list);
    check_hr("Close(list)", hr);
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList *const *)&list);
    wait_for_queue(queue, fence, fence_event, &fence_value);
    printf("  wrote known pattern via real D3D12 GPU copy (upload -> shared)\n");

    /* --- export the real fd, same as this project's own vkd3d-proton patch does for production --- */
    hr = ID3D12Device_CreateSharedHandle(device, (ID3D12DeviceChild *)shared_buf, NULL, GENERIC_ALL, NULL, &shared_handle);
    check_hr("CreateSharedHandle", hr);
    fd = (int)(INT_PTR)shared_handle;
    printf("  exported fd = %d\n", fd);

    /* --- load THIS PROJECT'S OWN amdhip64_7.dll - the real shim, not a mock --- */
    hip_mod = LoadLibraryA("amdhip64_7.dll");
    if (!hip_mod) { printf("FATAL: LoadLibraryA(amdhip64_7.dll) failed.\n"); return 1; }
    p_hipImportExternalMemory = (void *)GetProcAddress(hip_mod, "hipImportExternalMemory");
    p_hipExternalMemoryGetMappedBuffer = (void *)GetProcAddress(hip_mod, "hipExternalMemoryGetMappedBuffer");
    p_hipDestroyExternalMemory = (void *)GetProcAddress(hip_mod, "hipDestroyExternalMemory");
    p_hipMemcpy = (void *)GetProcAddress(hip_mod, "hipMemcpy");
    p_hipDeviceSynchronize = (void *)GetProcAddress(hip_mod, "hipDeviceSynchronize");
    p_hipGetErrorString = (void *)GetProcAddress(hip_mod, "hipGetErrorString");
    if (!p_hipImportExternalMemory || !p_hipExternalMemoryGetMappedBuffer || !p_hipMemcpy)
    {
        printf("FATAL: required hip* exports missing from amdhip64_7.dll.\n");
        return 1;
    }

    /* --- real import, exactly matching danielblnc's own real usage (ext_mem.h) --- */
    memset(&handle_desc, 0, sizeof(handle_desc));
    handle_desc.type = HIP_EXT_MEM_TYPE_D3D12_RESOURCE;
    handle_desc.handle.win32.handle = (void *)(INT_PTR)fd;
    handle_desc.size = BUF_SIZE;
    check_hip("hipImportExternalMemory", p_hipImportExternalMemory(&ext_mem, &handle_desc));

    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.offset = 0;
    buffer_desc.size = BUF_SIZE;
    check_hip("hipExternalMemoryGetMappedBuffer", p_hipExternalMemoryGetMappedBuffer(&dev_ptr, ext_mem, &buffer_desc));
    printf("  dev_ptr = %p\n", dev_ptr);

    /* === direction 1: D3D12 wrote it, does HIP see the same bytes? === */
    check_hip("hipMemcpy(D2H readback)", p_hipMemcpy(result, dev_ptr, BUF_SIZE, HIP_MEMCPY_DEVICE_TO_HOST));
    {
        int ok = 1;
        for (i = 0; i < 64; i++)
            if (result[i] != pattern_a[i]) { ok = 0; break; }
        printf("\nDIRECTION 1 (D3D12 write -> HIP read): %s\n", ok ? "PASS" : "FAIL");
        if (!ok)
        {
            printf("  mismatch at element %u: wrote %f, HIP read back %f\n", i, pattern_a[i], result[i]);
            return 1;
        }
    }

    /* === direction 2: HIP writes it, does D3D12 see the same bytes? === */
    check_hip("hipMemcpy(H2D write)", p_hipMemcpy(dev_ptr, pattern_b, BUF_SIZE, HIP_MEMCPY_HOST_TO_DEVICE));
    check_hip("hipDeviceSynchronize", p_hipDeviceSynchronize());

    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&readback_buf);
    check_hr("CreateCommittedResource(readback)", hr);

    hr = ID3D12CommandAllocator_Reset(allocator);
    check_hr("Reset(allocator)", hr);
    hr = ID3D12GraphicsCommandList_Reset(list, allocator, NULL);
    check_hr("Reset(list)", hr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, readback_buf, 0, shared_buf, 0, BUF_SIZE);
    hr = ID3D12GraphicsCommandList_Close(list);
    check_hr("Close(list) #2", hr);
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList *const *)&list);
    wait_for_queue(queue, fence, fence_event, &fence_value);

    hr = ID3D12Resource_Map(readback_buf, 0, NULL, (void **)&mapped);
    check_hr("Map(readback)", hr);
    {
        int ok = 1;
        for (i = 0; i < 64; i++)
            if (mapped[i] != pattern_b[i]) { ok = 0; break; }
        printf("\nDIRECTION 2 (HIP write -> D3D12 read): %s\n", ok ? "PASS" : "FAIL");
        if (!ok)
            printf("  mismatch at element %u: HIP wrote %f, D3D12 read back %f\n", i, pattern_b[i], mapped[i]);
        map_range.Begin = 0; map_range.End = 0;
        ID3D12Resource_Unmap(readback_buf, 0, &map_range);
        if (!ok) return 1;
    }

    p_hipDestroyExternalMemory(ext_mem);
    CloseHandle(shared_handle);
    ID3D12Resource_Release(readback_buf);
    ID3D12Resource_Release(shared_buf);
    ID3D12Resource_Release(upload_buf);
    ID3D12Fence_Release(fence);
    CloseHandle(fence_event);
    ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocator);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device);

    printf("\n=== both directions PASS - real data round-trips correctly across the shared-memory boundary ===\n");
    return 0;
}
