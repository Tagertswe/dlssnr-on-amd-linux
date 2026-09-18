/* PE-side (Windows/Wine) half of the amdhip64_7 unixlib module.
 *
 * Ports windows-runtime-bridge/hip-stub's Rust implementation onto the Wine-unixlib
 * mechanism confirmed compatible with Proton 11.0 (see
 * docs/linux-support-spec.md). Device/memory/kernel-launch functions
 * call through WINE_UNIX_CALL into native.c's real HIP implementation;
 * functions with no real implementation yet remain stubs returning
 * "not supported", matching the prior Rust version's parity exactly.
 *
 * No proprietary code, weights, or logic from NVIDIA or danielblnc -
 * only public HIP API names wired to a real forwarding relay or, where
 * not yet implemented, trivial "unsupported" responses.
 */

#include <windows.h>
#include "wine/unixlib.h"
#include "unixlib.h"
#include "ext_mem.h"
#include "memcpy_kind.h"
#include "last_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *log_file;

static void log_msg(const char *fmt, ...)
{
    if (!log_file)
    {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        char *slash = strrchr(exe_path, '\\');
        if (slash) *(slash + 1) = 0;
        char log_path[MAX_PATH + 32];
        snprintf(log_path, sizeof(log_path), "%samdhip64_7_unixlib_pe.log", exe_path);
        log_file = fopen(log_path, "a");
    }
    if (!log_file) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fputc('\n', log_file);
    fflush(log_file);
}

typedef int hipError_t;
#define HIP_SUCCESS 0
#define HIP_ERROR_NO_DEVICE 100
#define HIP_ERROR_NOT_SUPPORTED 801
/* HIP_MEMCPY_* kind constants now live in memcpy_kind.h. */

static int last_error = HIP_SUCCESS;
static int set_last_error(int code) { last_error = code; return code; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (__wine_init_unix_call())
        {
            log_msg("__wine_init_unix_call() failed");
            return FALSE;
        }
        log_msg("amdhip64_7 unixlib PE side initialized");
    }
    return TRUE;
}

/* ---------------------------------------------------------------------
 * Fat-binary bootstrap
 * ------------------------------------------------------------------- */

__declspec(dllexport) void *__cdecl __hipRegisterFatBinary(const void *data)
{
    struct args_register_fat_binary a = { .data = data };
    WINE_UNIX_CALL(HIP_CALL_REGISTER_FAT_BINARY, &a);
    log_msg("__hipRegisterFatBinary(data=%p) -> token=%p", data, a.fatbin_token);
    return a.fatbin_token;
}

__declspec(dllexport) void __cdecl __hipRegisterFunction(
    void *fatbin_token, const void *host_fn, const char *device_fn, const char *device_name,
    int thread_limit, void *tid, void *bid, void *b_dim, void *g_dim, int *w_size)
{
    (void)device_fn; (void)thread_limit; (void)tid; (void)bid; (void)b_dim; (void)g_dim; (void)w_size;
    struct args_register_function a = { .fatbin_token = fatbin_token, .host_fn = host_fn, .device_name = device_name };
    WINE_UNIX_CALL(HIP_CALL_REGISTER_FUNCTION, &a);
    log_msg("__hipRegisterFunction(host_fn=%p, device_name=%s)", host_fn, device_name);
}

__declspec(dllexport) void __cdecl __hipRegisterVar(void *fatbin_token, const void *host_var, const char *device_name)
{
    /* docs/linux-support-spec.md's g_e4m3_lut finding: this used to be a
     * pure no-op, so hipMemcpyToSymbol had no way to find which real
     * device global a given host_var corresponded to - now resolves it
     * via native.c's unix_register_var (hipModuleGetGlobal), same
     * pattern as __hipRegisterFunction/hipModuleGetFunction above. */
    struct args_register_var a = { .fatbin_token = fatbin_token, .host_var = host_var, .device_name = device_name };
    WINE_UNIX_CALL(HIP_CALL_REGISTER_VAR, &a);
    log_msg("__hipRegisterVar(host_var=%p, device_name=%s)", host_var, device_name);
}

__declspec(dllexport) void __cdecl __hipUnregisterFatBinary(void *fatbin_token)
{
    struct args_unregister_fat_binary a = { .fatbin_token = fatbin_token };
    WINE_UNIX_CALL(HIP_CALL_UNREGISTER_FAT_BINARY, &a);
}

struct dim3_pe { unsigned int x, y, z; };

#define CALL_CONFIG_STACK_SIZE 16
static CRITICAL_SECTION call_config_lock;
static BOOL call_config_lock_init;
static struct
{
    struct dim3_pe grid, block;
    size_t shared_mem;
    void *stream;
} call_config_stack[CALL_CONFIG_STACK_SIZE];
static int call_config_depth;

static void ensure_call_config_lock(void)
{
    if (!call_config_lock_init)
    {
        InitializeCriticalSection(&call_config_lock);
        call_config_lock_init = TRUE;
    }
}

__declspec(dllexport) int __cdecl __hipPushCallConfiguration(
    const struct dim3_pe *grid_dim, const struct dim3_pe *block_dim, size_t shared_mem, void *stream)
{
    if (!grid_dim || !block_dim)
    {
        log_msg("__hipPushCallConfiguration: null grid/block pointer");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    ensure_call_config_lock();
    EnterCriticalSection(&call_config_lock);
    if (call_config_depth >= CALL_CONFIG_STACK_SIZE)
    {
        LeaveCriticalSection(&call_config_lock);
        log_msg("__hipPushCallConfiguration: stack overflow");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    call_config_stack[call_config_depth].grid = *grid_dim;
    call_config_stack[call_config_depth].block = *block_dim;
    call_config_stack[call_config_depth].shared_mem = shared_mem;
    call_config_stack[call_config_depth].stream = stream;
    call_config_depth++;
    LeaveCriticalSection(&call_config_lock);
    return HIP_SUCCESS;
}

__declspec(dllexport) int __cdecl __hipPopCallConfiguration(
    struct dim3_pe *grid_dim, struct dim3_pe *block_dim, size_t *shared_mem, void *stream)
{
    ensure_call_config_lock();
    EnterCriticalSection(&call_config_lock);
    if (call_config_depth <= 0)
    {
        LeaveCriticalSection(&call_config_lock);
        log_msg("__hipPopCallConfiguration: stack empty");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    call_config_depth--;
    if (grid_dim) *grid_dim = call_config_stack[call_config_depth].grid;
    if (block_dim) *block_dim = call_config_stack[call_config_depth].block;
    if (shared_mem) *shared_mem = call_config_stack[call_config_depth].shared_mem;
    if (stream) *(void **)stream = call_config_stack[call_config_depth].stream;
    LeaveCriticalSection(&call_config_lock);
    return HIP_SUCCESS;
}

/* ---------------------------------------------------------------------
 * Device / runtime queries - real
 * ------------------------------------------------------------------- */

__declspec(dllexport) int __cdecl hipGetDeviceCount(int *count)
{
    struct args_get_device_count a = {0};
    WINE_UNIX_CALL(HIP_CALL_GET_DEVICE_COUNT, &a);
    if (count) *count = a.count;
    log_msg("hipGetDeviceCount() -> %d (real)", a.count);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NO_DEVICE);
}

__declspec(dllexport) int __cdecl hipGetDevicePropertiesR0600(void *props, int device)
{
    struct args_get_device_properties a = { .props = props, .device = device };
    WINE_UNIX_CALL(HIP_CALL_GET_DEVICE_PROPERTIES, &a);
    log_msg("hipGetDevicePropertiesR0600(device=%d) -> ret=%d (real)", device, a.ret);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipSetDevice(int device)
{
    struct args_set_device a = { .device = device };
    WINE_UNIX_CALL(HIP_CALL_SET_DEVICE, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NO_DEVICE);
}

__declspec(dllexport) int __cdecl hipDeviceSynchronize(void)
{
    struct args_device_synchronize a = {0};
    /* See native.c's unix_device_synchronize for why this got logging -
     * a real hang was found live with zero visibility from either side
     * of the shim. Logged here too (PE side) so a hang inside the
     * WINE_UNIX_CALL dispatch itself, not just inside the real HIP
     * call on the native side, would also be distinguishable. */
    log_msg("hipDeviceSynchronize: entering (blocking call)");
    WINE_UNIX_CALL(HIP_CALL_DEVICE_SYNCHRONIZE, &a);
    log_msg("hipDeviceSynchronize -> ret=%d (real)", a.ret);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipDriverGetVersion(int *version)
{
    struct args_driver_get_version a = {0};
    WINE_UNIX_CALL(HIP_CALL_DRIVER_GET_VERSION, &a);
    if (version) *version = a.version;
    return HIP_SUCCESS;
}

__declspec(dllexport) int __cdecl hipRuntimeGetVersion(int *version)
{
    struct args_runtime_get_version a = {0};
    WINE_UNIX_CALL(HIP_CALL_RUNTIME_GET_VERSION, &a);
    if (version) *version = a.version;
    return HIP_SUCCESS;
}

__declspec(dllexport) int __cdecl hipGetLastError(void)
{
    /* docs/linux-support-spec.md: real hipGetLastError()/cudaGetLastError()
     * semantics reset the sticky state to success after reading it -
     * this used to just read `last_error` and return it, so a single
     * real failure anywhere poisoned every later check for the rest of
     * the process's life. See last_error.h for the real live evidence
     * that found this (389 identical stale reads from one real
     * failure). */
    int ret = hip_get_last_error_and_reset(last_error, HIP_SUCCESS, &last_error);
    log_msg("hipGetLastError() -> %d", ret);
    return ret;
}

__declspec(dllexport) const char *__cdecl hipGetErrorString(int error)
{
    struct args_get_error_string a = { .error = error };
    WINE_UNIX_CALL(HIP_CALL_GET_ERROR_STRING, &a);
    /* Real HIP's error string lives in libamdhip64.so's own mapped
     * memory, in this same process - directly returnable, no copy.
     * Logging the requested error code here is deliberate: this is
     * the most direct way to identify exactly which hipError_t value
     * is behind whatever text danielblnc's runtime goes on to print
     * (e.g. "operation not supported" - see
     * docs/linux-support-spec.md \xc2\xa730), since this function is very
     * likely how he turns a failing hipError_t from some other call
     * into that text in the first place. */
    log_msg("hipGetErrorString(error=%d) -> \"%s\"", error, a.ret_str ? a.ret_str : "(null)");
    return a.ret_str;
}

/* ---------------------------------------------------------------------
 * Memory - real
 * ------------------------------------------------------------------- */

__declspec(dllexport) int __cdecl hipMalloc(void **ptr, size_t size)
{
    struct args_malloc a = { .size = size };
    WINE_UNIX_CALL(HIP_CALL_MALLOC, &a);
    if (ptr) *ptr = a.ptr;
    log_msg("hipMalloc(size=%zu) -> %p (real)", size, a.ptr);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NO_DEVICE);
}

__declspec(dllexport) int __cdecl hipFree(void *ptr)
{
    if (!ptr) return HIP_SUCCESS;
    struct args_free a = { .ptr = ptr };
    WINE_UNIX_CALL(HIP_CALL_FREE, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

static int memcpy_forward(void *dst, const void *src, size_t size, int kind)
{
    if (!hip_memcpy_kind_supported(kind))
    {
        log_msg("hipMemcpy: unsupported kind %d", kind);
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    struct args_memcpy a = { .dst = dst, .src = src, .size = size, .kind = kind };
    WINE_UNIX_CALL(HIP_CALL_MEMCPY, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipMemcpy(void *dst, const void *src, size_t size, int kind)
{
    return memcpy_forward(dst, src, size, kind);
}

/* docs/linux-support-spec.md: hipMemcpyAsync used to discard `stream`
 * and run synchronously on memcpy_forward's sync path, same convention
 * as hipMemsetAsync below - corrected, real live evidence: danielblnc's
 * runtime times its own async work (copies, sets, kernel launches) by
 * recording HIP events on its own real stream around them, then reading
 * elapsed time - every single measurement came back 0.0 ms despite real
 * work happening, because that work was always running on the null
 * stream instead of the caller's stream, so an event on the (empty)
 * caller's stream always completed instantly. Real async, on the real
 * stream, now. */
__declspec(dllexport) int __cdecl hipMemcpyAsync(void *dst, const void *src, size_t size, int kind, void *stream)
{
    if (!hip_memcpy_kind_supported(kind))
    {
        log_msg("hipMemcpyAsync: unsupported kind %d", kind);
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    struct args_memcpy a = { .dst = dst, .src = src, .size = size, .kind = kind, .stream = stream };
    WINE_UNIX_CALL(HIP_CALL_MEMCPY_ASYNC, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

/* ---------------------------------------------------------------------
 * Symbol / device-global memcpy - real
 * ------------------------------------------------------------------- */

__declspec(dllexport) int __cdecl hipMemcpyToSymbol(const void *symbol, const void *src, size_t size, size_t offset, int kind)
{
    /* docs/linux-support-spec.md's g_e4m3_lut finding: now real -
     * __hipRegisterVar above resolves `symbol` to a real device global
     * via hipModuleGetGlobal, and native.c's unix_memcpy_to_symbol
     * looks that resolution up and forwards a real hipMemcpy at
     * device_ptr+offset. Same kind gate as memcpy_forward, since this
     * is still fundamentally a memcpy. */
    if (!hip_memcpy_kind_supported(kind))
    {
        log_msg("hipMemcpyToSymbol: unsupported kind %d", kind);
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    struct args_memcpy_to_symbol a = { .symbol = symbol, .src = src, .size = size, .offset = offset, .kind = kind };
    WINE_UNIX_CALL(HIP_CALL_MEMCPY_TO_SYMBOL, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

/* docs/linux-support-spec.md: hipMemset/hipMemsetAsync were unconditional
 * stubs with zero logging anywhere in either log - found live to be
 * called 45+/10 times respectively in a single ~20s session, each one
 * silently poisoning the shim's sticky last_error the same way the
 * memcpy-kind-3 bug once did. Real hipMemset exists in ROCm and is
 * forwarded here the same way hipMemcpy already is. hipMemsetAsync
 * used to also run this same synchronous path, discarding its stream -
 * corrected below, same real-evidence reasoning as hipMemcpyAsync's own
 * comment (danielblnc's own event-based timing measuring 0.0 ms for
 * every real async op). */
static int memset_forward(void *ptr, int value, size_t size)
{
    struct args_memset a = { .ptr = ptr, .value = value, .size = size };
    WINE_UNIX_CALL(HIP_CALL_MEMSET, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipMemset(void *ptr, int value, size_t size)
{
    return memset_forward(ptr, value, size);
}

__declspec(dllexport) int __cdecl hipMemsetAsync(void *ptr, int value, size_t size, void *stream)
{
    struct args_memset a = { .ptr = ptr, .value = value, .size = size, .stream = stream };
    WINE_UNIX_CALL(HIP_CALL_MEMSET_ASYNC, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipEventCreate(void **event)
{
    struct args_event_create a = { .flags = 0 };
    WINE_UNIX_CALL(HIP_CALL_EVENT_CREATE, &a);
    if (event) *event = a.event;
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipEventCreateWithFlags(void **event, unsigned int flags)
{
    struct args_event_create a = { .flags = flags };
    WINE_UNIX_CALL(HIP_CALL_EVENT_CREATE, &a);
    if (event) *event = a.event;
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipEventQuery(void *event)
{
    struct args_event_query a = { .event = event };
    WINE_UNIX_CALL(HIP_CALL_EVENT_QUERY, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipStreamCreateWithFlags(void **stream, unsigned int flags)
{
    struct args_stream_create a = { .flags = flags };
    WINE_UNIX_CALL(HIP_CALL_STREAM_CREATE, &a);
    if (stream) *stream = a.stream;
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipStreamSynchronize(void *stream)
{
    struct args_stream_synchronize a = { .stream = stream };
    WINE_UNIX_CALL(HIP_CALL_STREAM_SYNCHRONIZE, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipEventRecord(void *event, void *stream)
{
    struct args_event_record a = { .event = event, .stream = stream };
    WINE_UNIX_CALL(HIP_CALL_EVENT_RECORD, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipEventSynchronize(void *event)
{
    struct args_event_synchronize a = { .event = event };
    WINE_UNIX_CALL(HIP_CALL_EVENT_SYNCHRONIZE, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipEventElapsedTime(float *ms, void *start, void *stop)
{
    struct args_event_elapsed_time a = { .start = start, .stop = stop };
    WINE_UNIX_CALL(HIP_CALL_EVENT_ELAPSED_TIME, &a);
    if (ms) *ms = a.ms;
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

static const char *ext_mem_type_name(int type)
{
    switch (type)
    {
        case 1: return "OpaqueFd";
        case 2: return "OpaqueWin32";
        case 3: return "OpaqueWin32Kmt";
        case 4: return "D3D12Heap";
        case 5: return "D3D12Resource";
        case 6: return "D3D11Resource";
        case 7: return "D3D11ResourceKmt";
        case 8: return "NvSciBuf";
        default: return "unknown";
    }
}

__declspec(dllexport) int __cdecl hipImportExternalMemory(void **ext_mem, const void *handle_desc)
{
    if (ext_mem) *ext_mem = NULL;
    if (!handle_desc)
    {
        log_msg("hipImportExternalMemory: handle_desc is null");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    const struct hip_ext_mem_handle_desc *d = handle_desc;
    int fd = hip_ext_mem_extract_fd(d);
    log_msg(
        "hipImportExternalMemory: type=%d (%s) handle=%p name=%p fd=%d size=%llu flags=%u -> extracted fd=%d",
        d->type, ext_mem_type_name(d->type),
        d->handle.win32.handle, d->handle.win32.name,
        d->handle.fd, d->size, d->flags, fd
    );
    if (fd < 0)
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);

    struct args_import_external_memory a = { .fd = fd, .size = d->size, .flags = d->flags };
    WINE_UNIX_CALL(HIP_CALL_IMPORT_EXTERNAL_MEMORY, &a);
    if (ext_mem) *ext_mem = a.ext_mem;
    log_msg("hipImportExternalMemory: -> ext_mem=%p, ret=%d (real)", a.ext_mem, a.ret);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipExternalMemoryGetMappedBuffer(void **dev_ptr, void *ext_mem, const void *buffer_desc)
{
    if (dev_ptr) *dev_ptr = NULL;
    if (!buffer_desc)
    {
        log_msg("hipExternalMemoryGetMappedBuffer: buffer_desc is null");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    const struct hip_ext_mem_buffer_desc *d = buffer_desc;
    struct args_external_memory_get_mapped_buffer a =
        { .ext_mem = ext_mem, .offset = d->offset, .size = d->size, .flags = d->flags };
    WINE_UNIX_CALL(HIP_CALL_EXTERNAL_MEMORY_GET_MAPPED_BUFFER, &a);
    if (dev_ptr) *dev_ptr = a.dev_ptr;
    log_msg("hipExternalMemoryGetMappedBuffer(ext_mem=%p, offset=%llu, size=%llu) -> dev_ptr=%p, ret=%d (real)",
            ext_mem, d->offset, d->size, a.dev_ptr, a.ret);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) int __cdecl hipDestroyExternalMemory(void *ext_mem)
{
    struct args_destroy_external_memory a = { .ext_mem = ext_mem };
    WINE_UNIX_CALL(HIP_CALL_DESTROY_EXTERNAL_MEMORY, &a);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}

/* ---------------------------------------------------------------------
 * Kernel execution - real
 * ------------------------------------------------------------------- */

/* docs/linux-support-spec.md: `stream` used to be discarded here, so
 * every real kernel launched on the real, but ignored, null stream
 * instead - see native.c's unix_memcpy_async comment for the live
 * evidence (danielblnc's own event-based timing reading 0.0 ms for
 * every job) that made this worth fixing, not just theoretical
 * correctness. */
__declspec(dllexport) int __cdecl hipLaunchKernel(
    const void *function, const struct dim3_pe *grid_dim, const struct dim3_pe *block_dim,
    void **args, size_t shared_mem, void *stream)
{
    if (!grid_dim || !block_dim || !args || !args[0])
    {
        log_msg("hipLaunchKernel: null grid/block/args pointer");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    struct args_launch_kernel a = {
        .host_fn = function,
        .grid = { grid_dim->x, grid_dim->y, grid_dim->z },
        .block = { block_dim->x, block_dim->y, block_dim->z },
        .args = args[0],
        .shared_mem = (unsigned int)shared_mem,
        .stream = stream,
    };
    WINE_UNIX_CALL(HIP_CALL_LAUNCH_KERNEL, &a);
    log_msg("hipLaunchKernel(function=%p, stream=%p) -> ret=%d (real)", function, stream, a.ret);
    return a.ret == HIP_SUCCESS ? HIP_SUCCESS : set_last_error(HIP_ERROR_NOT_SUPPORTED);
}
