/* Native (Linux) side of the amdhip64_7 unixlib module.
 *
 * Loads the real ROCm/HIP runtime via dlopen/dlsym (no ROCm dev package
 * - headers, unversioned .so symlink - is installed on the build/target
 * machine; only the runtime libamdhip64.so.<N> itself, hence dlopen
 * rather than link-time linking, same reasoning as the earlier
 * socket-IPC daemon's hip_sys.rs).
 *
 * Pointers received here (device pointers, argument-struct pointers,
 * the fat-binary data pointer) are real addresses in this same process
 * - Wine runs PE code and this native code in one shared address space,
 * confirmed empirically before this file was written (see
 * docs/linux-support-spec.md). No serialization, no address translation.
 */

#include "wine/unixlib.h"
#include "unixlib.h"
#include "registry.h"
#include "version_query.h"
#include "ext_mem.h"
#include "hip_forward.h"
#include "alloc_cap.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0)
#endif

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int hipError_t;
typedef int hipMemcpyKind;

#define HIP_SUCCESS 0
#define HIP_MEMCPY_HOST_TO_DEVICE 1
#define HIP_MEMCPY_DEVICE_TO_HOST 2

/* Exact byte size of the "R0600" hipDeviceProp_t ABI, confirmed via a
 * real compiler against the public ROCm/HIP + ROCm/clr headers (see
 * docs/linux-support-spec.md \xc2\xa78 item 8 / \xc2\xa712 for how). AMD deliberately
 * freezes this layout forever under the R0600 name specifically so
 * callers built against different ROCm releases stay ABI-compatible. */
#define HIP_DEVICE_PROP_R0600_SIZE 1472

static void *hip_handle;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef hipError_t (*hipMalloc_t)(void **, size_t);
typedef hipError_t (*hipFree_t)(void *);
typedef hipError_t (*hipMemcpy_t)(void *, const void *, size_t, hipMemcpyKind);
typedef hipError_t (*hipMemset_t)(void *, int, size_t);
typedef hipError_t (*hipMemcpyAsync_t)(void *, const void *, size_t, hipMemcpyKind, void *);
typedef hipError_t (*hipMemsetAsync_t)(void *, int, size_t, void *);
typedef const char *(*hipGetErrorString_t)(hipError_t);
typedef hipError_t (*hipGetDeviceCount_t)(int *);
typedef hipError_t (*hipSetDevice_t)(int);
typedef hipError_t (*hipDriverGetVersion_t)(int *);
typedef hipError_t (*hipRuntimeGetVersion_t)(int *);
typedef hipError_t (*hipImportExternalMemory_t)(void **, const struct hip_ext_mem_handle_desc *);
typedef hipError_t (*hipExternalMemoryGetMappedBuffer_t)(void **, void *, const struct hip_ext_mem_buffer_desc *);
typedef hipError_t (*hipDestroyExternalMemory_t)(void *);
typedef hipError_t (*hipEventCreate_t)(void **);
typedef hipError_t (*hipEventCreateWithFlags_t)(void **, unsigned int);
typedef hipError_t (*hipEventRecord_t)(void *, void *);
typedef hipError_t (*hipEventSynchronize_t)(void *);
typedef hipError_t (*hipEventQuery_t)(void *);
typedef hipError_t (*hipEventElapsedTime_t)(float *, void *, void *);
typedef hipError_t (*hipStreamCreateWithFlags_t)(void **, unsigned int);
typedef hipError_t (*hipStreamSynchronize_t)(void *);
typedef hipError_t (*hipDeviceSynchronize_t)(void);
typedef hipError_t (*hipGetDevicePropertiesR0600_t)(void *, int);
typedef hipError_t (*hipModuleLoadData_t)(void **, const void *);
typedef hipError_t (*hipModuleGetFunction_t)(void **, void *, const char *);
typedef hipError_t (*hipModuleGetGlobal_t)(void **, size_t *, void *, const char *);
typedef hipError_t (*hipModuleLaunchKernel_t)(void *, unsigned int, unsigned int, unsigned int,
                                              unsigned int, unsigned int, unsigned int,
                                              unsigned int, void *, void **, void **);

static hipMalloc_t p_hipMalloc;
static hipFree_t p_hipFree;
static hipMemcpy_t p_hipMemcpy;
static hipMemset_t p_hipMemset;
static hipMemcpyAsync_t p_hipMemcpyAsync;
static hipMemsetAsync_t p_hipMemsetAsync;
static hipGetErrorString_t p_hipGetErrorString;
static hipGetDeviceCount_t p_hipGetDeviceCount;
static hipSetDevice_t p_hipSetDevice;
static hipDriverGetVersion_t p_hipDriverGetVersion;
static hipRuntimeGetVersion_t p_hipRuntimeGetVersion;
static hipImportExternalMemory_t p_hipImportExternalMemory;
static hipExternalMemoryGetMappedBuffer_t p_hipExternalMemoryGetMappedBuffer;
static hipDestroyExternalMemory_t p_hipDestroyExternalMemory;
static hipEventCreate_t p_hipEventCreate;
static hipEventCreateWithFlags_t p_hipEventCreateWithFlags;
static hipEventRecord_t p_hipEventRecord;
static hipEventSynchronize_t p_hipEventSynchronize;
static hipEventQuery_t p_hipEventQuery;
static hipEventElapsedTime_t p_hipEventElapsedTime;
static hipStreamCreateWithFlags_t p_hipStreamCreateWithFlags;
static hipStreamSynchronize_t p_hipStreamSynchronize;
static hipDeviceSynchronize_t p_hipDeviceSynchronize;
static hipGetDevicePropertiesR0600_t p_hipGetDevicePropertiesR0600;
static hipModuleLoadData_t p_hipModuleLoadData;
static hipModuleGetFunction_t p_hipModuleGetFunction;
static hipModuleGetGlobal_t p_hipModuleGetGlobal;
static hipModuleLaunchKernel_t p_hipModuleLaunchKernel;

static FILE *log_file;

static void log_msg(const char *fmt, ...)
{
    if (!log_file)
        log_file = fopen("/tmp/amdhip64_7_unixlib.log", "a");
    if (!log_file)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fputc('\n', log_file);
    fflush(log_file);
}

static int ensure_loaded(void)
{
    pthread_mutex_lock(&init_mutex);
    if (hip_handle)
    {
        pthread_mutex_unlock(&init_mutex);
        return 1;
    }

    /* This .so is loaded inside the sandboxed game process (pressure-
     * vessel/bwrap). Confirmed directly against a real Steam-launched
     * session (not a manually-scripted `proton run`, which does not go
     * through the same container wrapping and had been masking this):
     * the container does NOT bind-mount /usr/lib/x86_64-linux-gnu from
     * the host at that path - the real host filesystem is instead
     * mounted under /run/host/. Try that first; keep the plain host
     * path and bare sonames as fallbacks for non-containerized
     * invocations (this project's own test harnesses) and in case a
     * future pressure-vessel version changes this again. */
    /* docs/linux-support-spec.md \xc2\xa743/\xc2\xa744: AMD's official installer
     * (amdgpu-install, used to test the real §40c version-gap theory)
     * deliberately does NOT touch /usr/lib/x86_64-linux-gnu -
     * versioned ROCm releases install side-by-side under
     * /opt/rocm-<version>/lib, with /opt/rocm normally a stable,
     * version-agnostic symlink to whichever one is "current" - EXCEPT
     * that symlink doesn't actually resolve inside the real sandboxed
     * game process: /opt/rocm -> /etc/alternatives/rocm (Debian's
     * "alternatives" system, an *absolute* path outside /opt), and
     * pressure-vessel's PRESSURE_VESSEL_FILESYSTEMS_RO/RW cannot bind
     * anything under /etc at all - confirmed live, not assumed: with
     * /opt bound in, /proc/<pid>/root/etc/alternatives/ doesn't exist
     * in the sandbox's mount namespace even when explicitly listed.
     * So the versioned directory has to be targeted directly, bypassing
     * the symlink - real ROCm version numbers do change over time, so
     * this candidate needs updating alongside any future ROCm upgrade
     * tested this way (kept separate from the generic /opt/rocm entries
     * below for exactly that reason - update this one line, not the
     * pattern). Checked first so an upgraded ROCm install is actually
     * picked up instead of silently continuing to load the older
     * /usr/lib copy - real, live-confirmed behavior, not a
     * hypothetical: after installing ROCm 7.2.4 this way,
     * /usr/lib/x86_64-linux-gnu/libamdhip64.so.7 was still the old
     * 7.1.52801 build, and even with /opt bound in, the /opt/rocm
     * symlink itself never resolved inside the game's own sandbox. */
    const char *candidates[] = {
        "/opt/rocm-7.2.4/lib/libamdhip64.so.7",
        "/run/host/opt/rocm-7.2.4/lib/libamdhip64.so.7",
        "/run/host/opt/rocm/lib/libamdhip64.so.7",
        "/run/host/opt/rocm/lib/libamdhip64.so.6",
        "/opt/rocm/lib/libamdhip64.so.7",
        "/opt/rocm/lib/libamdhip64.so.6",
        "/run/host/usr/lib/x86_64-linux-gnu/libamdhip64.so.7",
        "/run/host/usr/lib/x86_64-linux-gnu/libamdhip64.so.6",
        "/usr/lib/x86_64-linux-gnu/libamdhip64.so.7",
        "/usr/lib/x86_64-linux-gnu/libamdhip64.so.6",
        "libamdhip64.so.7", "libamdhip64.so.6", "libamdhip64.so", NULL };

    /* libamdhip64.so.7 itself resolving (via the /run/host path above)
     * is not enough - its own DT_NEEDED dependencies (libhsa-runtime64,
     * libhsakmt, libdrm_amdgpu, even plain libc/libstdc++) live only on
     * the real host inside this sandbox too, confirmed empirically
     * against a real Steam-launched session's own mount namespace.
     * glibc's dynamic linker consults LD_LIBRARY_PATH live for each
     * dlopen's dependency resolution, not just at process startup, so
     * setting it here (before our dlopen below) fixes the whole
     * transitive chain in one step instead of hand-listing every
     * dependency path. */
    const char *existing_ld_path = getenv("LD_LIBRARY_PATH");
    char new_ld_path[4096];
    if (existing_ld_path && *existing_ld_path)
        snprintf(new_ld_path, sizeof(new_ld_path),
                "/opt/rocm-7.2.4/lib:/run/host/opt/rocm-7.2.4/lib:/run/host/opt/rocm/lib:/opt/rocm/lib:/run/host/usr/lib/x86_64-linux-gnu:/run/host/lib/x86_64-linux-gnu:%s",
                existing_ld_path);
    else
        snprintf(new_ld_path, sizeof(new_ld_path),
                "/opt/rocm-7.2.4/lib:/run/host/opt/rocm-7.2.4/lib:/run/host/opt/rocm/lib:/opt/rocm/lib:/run/host/usr/lib/x86_64-linux-gnu:/run/host/lib/x86_64-linux-gnu");
    setenv("LD_LIBRARY_PATH", new_ld_path, 1);
    log_msg("ensure_loaded: LD_LIBRARY_PATH set to %s", new_ld_path);

    for (int i = 0; candidates[i]; i++)
    {
        hip_handle = dlopen(candidates[i], RTLD_NOW);
        if (hip_handle)
        {
            log_msg("loaded %s", candidates[i]);
            break;
        }
    }
    if (!hip_handle)
    {
        log_msg("dlopen failed for all candidates: %s", dlerror());
        pthread_mutex_unlock(&init_mutex);
        return 0;
    }

    p_hipMalloc = (hipMalloc_t)dlsym(hip_handle, "hipMalloc");
    p_hipFree = (hipFree_t)dlsym(hip_handle, "hipFree");
    p_hipMemcpy = (hipMemcpy_t)dlsym(hip_handle, "hipMemcpy");
    p_hipMemset = (hipMemset_t)dlsym(hip_handle, "hipMemset");
    p_hipMemcpyAsync = (hipMemcpyAsync_t)dlsym(hip_handle, "hipMemcpyAsync");
    p_hipMemsetAsync = (hipMemsetAsync_t)dlsym(hip_handle, "hipMemsetAsync");
    p_hipGetErrorString = (hipGetErrorString_t)dlsym(hip_handle, "hipGetErrorString");
    p_hipGetDeviceCount = (hipGetDeviceCount_t)dlsym(hip_handle, "hipGetDeviceCount");
    p_hipSetDevice = (hipSetDevice_t)dlsym(hip_handle, "hipSetDevice");
    p_hipDriverGetVersion = (hipDriverGetVersion_t)dlsym(hip_handle, "hipDriverGetVersion");
    p_hipRuntimeGetVersion = (hipRuntimeGetVersion_t)dlsym(hip_handle, "hipRuntimeGetVersion");
    p_hipImportExternalMemory = (hipImportExternalMemory_t)dlsym(hip_handle, "hipImportExternalMemory");
    p_hipExternalMemoryGetMappedBuffer =
        (hipExternalMemoryGetMappedBuffer_t)dlsym(hip_handle, "hipExternalMemoryGetMappedBuffer");
    p_hipDestroyExternalMemory = (hipDestroyExternalMemory_t)dlsym(hip_handle, "hipDestroyExternalMemory");
    p_hipEventCreate = (hipEventCreate_t)dlsym(hip_handle, "hipEventCreate");
    p_hipEventCreateWithFlags = (hipEventCreateWithFlags_t)dlsym(hip_handle, "hipEventCreateWithFlags");
    p_hipEventRecord = (hipEventRecord_t)dlsym(hip_handle, "hipEventRecord");
    p_hipEventSynchronize = (hipEventSynchronize_t)dlsym(hip_handle, "hipEventSynchronize");
    p_hipEventQuery = (hipEventQuery_t)dlsym(hip_handle, "hipEventQuery");
    p_hipEventElapsedTime = (hipEventElapsedTime_t)dlsym(hip_handle, "hipEventElapsedTime");
    p_hipStreamCreateWithFlags = (hipStreamCreateWithFlags_t)dlsym(hip_handle, "hipStreamCreateWithFlags");
    p_hipStreamSynchronize = (hipStreamSynchronize_t)dlsym(hip_handle, "hipStreamSynchronize");
    p_hipDeviceSynchronize = (hipDeviceSynchronize_t)dlsym(hip_handle, "hipDeviceSynchronize");
    p_hipGetDevicePropertiesR0600 =
        (hipGetDevicePropertiesR0600_t)dlsym(hip_handle, "hipGetDevicePropertiesR0600");
    p_hipModuleLoadData = (hipModuleLoadData_t)dlsym(hip_handle, "hipModuleLoadData");
    p_hipModuleGetFunction = (hipModuleGetFunction_t)dlsym(hip_handle, "hipModuleGetFunction");
    p_hipModuleGetGlobal = (hipModuleGetGlobal_t)dlsym(hip_handle, "hipModuleGetGlobal");
    p_hipModuleLaunchKernel = (hipModuleLaunchKernel_t)dlsym(hip_handle, "hipModuleLaunchKernel");

    pthread_mutex_unlock(&init_mutex);
    return 1;
}

/* Fat-binary / kernel registration bookkeeping (registry.c/.h) - plain
 * in-process state, since (unlike the socket-IPC design) there's no
 * cross-process boundary needing a handle table at all. Factored out
 * into its own pure, Wine/HIP-independent file so it has real unit test
 * coverage (see test_registry.c, run via `make test`) rather than being
 * only exercisable through the full Proton/game integration test. */
static pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *find_module(const void *token)
{
    pthread_mutex_lock(&registry_mutex);
    void *result = registry_find_module(token);
    pthread_mutex_unlock(&registry_mutex);
    return result;
}

static void *find_function(const void *host_fn)
{
    pthread_mutex_lock(&registry_mutex);
    void *result = registry_find_function(host_fn);
    pthread_mutex_unlock(&registry_mutex);
    return result;
}

static const char *find_function_name(const void *host_fn)
{
    pthread_mutex_lock(&registry_mutex);
    const char *result = registry_find_function_name(host_fn);
    pthread_mutex_unlock(&registry_mutex);
    return result;
}

static void *find_var(const void *host_var)
{
    pthread_mutex_lock(&registry_mutex);
    void *result = registry_find_var(host_var);
    pthread_mutex_unlock(&registry_mutex);
    return result;
}
/* ---------------------------------------------------------------------
 * Dispatch table
 * ------------------------------------------------------------------- */

static NTSTATUS unix_get_device_count(void *args)
{
    struct args_get_device_count *a = args;
    if (!ensure_loaded() || !p_hipGetDeviceCount) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipGetDeviceCount(&a->count);
    log_msg("hipGetDeviceCount() -> %d device(s), ret=%d", a->count, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_set_device(void *args)
{
    struct args_set_device *a = args;
    if (!ensure_loaded() || !p_hipSetDevice) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipSetDevice(a->device);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_driver_get_version(void *args)
{
    struct args_driver_get_version *a = args;
    int have_symbol = ensure_loaded() && p_hipDriverGetVersion;
    int real_version = 0, real_ret = -1;
    if (have_symbol)
        real_ret = p_hipDriverGetVersion(&real_version);
    hip_version_query_apply(&a->version, &a->ret, have_symbol, real_version, real_ret);
    log_msg("hipDriverGetVersion() -> version=%d, ret=%d", a->version, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_runtime_get_version(void *args)
{
    struct args_runtime_get_version *a = args;
    int have_symbol = ensure_loaded() && p_hipRuntimeGetVersion;
    int real_version = 0, real_ret = -1;
    if (have_symbol)
        real_ret = p_hipRuntimeGetVersion(&real_version);
    hip_version_query_apply(&a->version, &a->ret, have_symbol, real_version, real_ret);
    log_msg("hipRuntimeGetVersion() -> version=%d, ret=%d", a->version, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_get_error_string(void *args)
{
    struct args_get_error_string *a = args;
    static const char *fallback = "amdhip64_7 unixlib: HIP not available";
    if (!ensure_loaded() || !p_hipGetErrorString) { a->ret_str = fallback; return STATUS_SUCCESS; }
    /* Real HIP's error strings live in libamdhip64.so's own mapped
     * memory - directly readable from PE-side code too, since it's the
     * same process. No copying needed. */
    a->ret_str = p_hipGetErrorString(a->error);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_get_device_properties(void *args)
{
    struct args_get_device_properties *a = args;
    if (!ensure_loaded() || !p_hipGetDevicePropertiesR0600) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipGetDevicePropertiesR0600(a->props, a->device);
    return STATUS_SUCCESS;
}

/* HIP_ERROR_OUT_OF_MEMORY matches real HIP's hipErrorOutOfMemory (2) -
 * see unixlib.h for the rest of this shim's error-code convention. */
#define HIP_ERROR_OUT_OF_MEMORY 2

/* docs/linux-support-spec.md §38/§39: the real, hardware-confirmed
 * crash cause is a GPU page fault at exactly 0x100000000 (2^32/4GB) -
 * a 32-bit address/offset overflow somewhere in danielblnc's own
 * kernels. This env var is an opt-in diagnostic knob to test whether
 * keeping this pipeline's total outstanding real GPU allocation
 * comfortably under 4GB avoids whatever buffer layout triggers it.
 * Unset (0) by default - no cap, no behavior change from before this
 * was added. Read once, lazily, the first time unix_malloc runs. */
static unsigned long long vram_cap_bytes = 0;
static int vram_cap_read;

static void ensure_vram_cap_read(void)
{
    if (vram_cap_read)
        return;
    vram_cap_read = 1;
    const char *env = getenv("DLSSNR_VRAM_CAP_BYTES");
    if (env && *env)
    {
        vram_cap_bytes = strtoull(env, NULL, 10);
        log_msg("DLSSNR_VRAM_CAP_BYTES=%llu (real GPU alloc cap active)", vram_cap_bytes);
    }
}

static NTSTATUS unix_malloc(void *args)
{
    struct args_malloc *a = args;
    if (!ensure_loaded() || !p_hipMalloc) { a->ptr = NULL; a->ret = -1; return STATUS_SUCCESS; }
    ensure_vram_cap_read();
    if (alloc_cap_would_exceed(alloc_cap_total(), a->size, vram_cap_bytes))
    {
        a->ptr = NULL;
        a->ret = HIP_ERROR_OUT_OF_MEMORY;
        log_msg("hipMalloc(size=%zu) -> REJECTED (would exceed DLSSNR_VRAM_CAP_BYTES=%llu, "
                "current outstanding=%llu)", a->size, vram_cap_bytes, alloc_cap_total());
        return STATUS_SUCCESS;
    }
    void *ptr = NULL;
    a->ret = p_hipMalloc(&ptr, a->size);
    a->ptr = ptr;
    if (a->ret == 0 && ptr)
        alloc_cap_record(ptr, a->size);
    log_msg("hipMalloc(size=%zu) -> %p, ret=%d (outstanding=%llu)", a->size, ptr, a->ret, alloc_cap_total());
    return STATUS_SUCCESS;
}

static NTSTATUS unix_free(void *args)
{
    struct args_free *a = args;
    if (!ensure_loaded() || !p_hipFree) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipFree(a->ptr);
    if (a->ret == 0)
        alloc_cap_release(a->ptr);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_memcpy(void *args)
{
    struct args_memcpy *a = args;
    if (!ensure_loaded() || !p_hipMemcpy) { a->ret = -1; return STATUS_SUCCESS; }
    /* hipMemcpy is synchronous by default - it waits for prior queued
     * work to complete, so it can block indefinitely the same way
     * hipDeviceSynchronize/hipEventSynchronize/hipStreamSynchronize
     * can if the GPU command queue is stuck. Same entry+exit logging
     * fix as those, closing the same blind spot here too. */
    log_msg("hipMemcpy(size=%zu, kind=%d): entering (blocking call)", a->size, a->kind);
    a->ret = p_hipMemcpy(a->dst, a->src, a->size, a->kind);
    log_msg("hipMemcpy(size=%zu, kind=%d) -> ret=%d (real)", a->size, a->kind, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_memset(void *args)
{
    struct args_memset *a = args;
    if (!ensure_loaded() || !p_hipMemset) { a->ret = -1; return STATUS_SUCCESS; }
    log_msg("hipMemset(ptr=%p, value=%d, size=%zu): entering (blocking call)", a->ptr, a->value, a->size);
    a->ret = p_hipMemset(a->ptr, a->value, a->size);
    log_msg("hipMemset(ptr=%p, value=%d, size=%zu) -> ret=%d (real)", a->ptr, a->value, a->size, a->ret);
    return STATUS_SUCCESS;
}

/* docs/linux-support-spec.md: hipMemcpyAsync/hipMemsetAsync used to be
 * executed synchronously on the real HIP library's default/null stream,
 * discarding the caller's actual stream argument - correction, found
 * live in a real, long interactive-play session: danielblnc's runtime
 * records HIP events on its own real stream around the async copies/
 * sets/kernel launches that feed the network, then measures elapsed
 * time between them to report "network on the GPU" - every single job
 * measured 0.0 ms this way, despite dozens of real kernel launches per
 * job. If the actual work always ran on the null stream instead of the
 * caller's stream, an event recorded on the caller's (empty) stream
 * completes immediately, before the real work is even queued - a
 * plausible, unifying explanation for the 0.0 ms measurement, the ~96%
 * capture-check timeout rate (the check waits for a flag write that,
 * on this now-corrected theory, could land at a wildly different time
 * than the runtime's own timing model expects), and conceivably the
 * GPU ring hang itself (a spin-wait budgeted around a ~20 ms real
 * completion time, if repeatedly primed on the wrong ordering
 * information). Real HIP calls now genuinely run on the caller's real
 * stream instead of the null stream - unix_launch_kernel below does
 * the same for kernel launches. Not yet re-tested live; this is the
 * fix these unix_*_async functions previously argued was unnecessary. */
static NTSTATUS unix_memcpy_async(void *args)
{
    struct args_memcpy *a = args;
    if (!ensure_loaded() || !p_hipMemcpyAsync) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipMemcpyAsync(a->dst, a->src, a->size, a->kind, a->stream);
    log_msg("hipMemcpyAsync(size=%zu, kind=%d, stream=%p) -> ret=%d (real, async)",
            a->size, a->kind, a->stream, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_memset_async(void *args)
{
    struct args_memset *a = args;
    if (!ensure_loaded() || !p_hipMemsetAsync) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipMemsetAsync(a->ptr, a->value, a->size, a->stream);
    log_msg("hipMemsetAsync(ptr=%p, value=%d, size=%zu, stream=%p) -> ret=%d (real, async)",
            a->ptr, a->value, a->size, a->stream, a->ret);
    return STATUS_SUCCESS;
}

/* `symbol` is the host-side address hipMemcpyToSymbol's caller passes -
 * the exact same pointer __hipRegisterVar's host_var argument was, so
 * find_var() (populated by unix_register_var) resolves it to the real
 * device global unix_register_var already looked up via
 * hipModuleGetGlobal. Forwards the real HIP call at device_ptr+offset,
 * same as unix_memcpy. If the symbol was never registered (e.g. a
 * genuinely unknown/unsupported one), fails safely rather than writing
 * to an unresolved address. */
static NTSTATUS unix_memcpy_to_symbol(void *args)
{
    struct args_memcpy_to_symbol *a = args;
    void *device_ptr = find_var(a->symbol);
    if (!device_ptr || !ensure_loaded() || !p_hipMemcpy) { a->ret = -1; return STATUS_SUCCESS; }
    log_msg("hipMemcpyToSymbol(device_ptr=%p, offset=%zu, size=%zu): entering (blocking call)",
            device_ptr, a->offset, a->size);
    a->ret = p_hipMemcpy((char *)device_ptr + a->offset, a->src, a->size, a->kind);
    log_msg("hipMemcpyToSymbol(device_ptr=%p, offset=%zu, size=%zu) -> ret=%d (real)",
            device_ptr, a->offset, a->size, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_device_synchronize(void *args)
{
    struct args_device_synchronize *a = args;
    if (!ensure_loaded() || !p_hipDeviceSynchronize) { a->ret = -1; return STATUS_SUCCESS; }
    /* This call previously had zero logging on either side of the shim -
     * a real blind spot found live (docs/linux-support-spec.md's
     * "picture froze" session): the game hung with the GPU itself
     * confirmed idle (rocm-smi responsive, low utilization), most
     * likely blocked inside this exact call, and neither log showed
     * anything at all because nothing here was ever logged, entry or
     * exit. Logging entry separately from the ret= line means a real
     * hang now shows up unambiguously as a lone "entering" line with no
     * matching "-> ret=" ever following it, instead of total silence
     * that could just as easily mean "no synchronize call happened". */
    log_msg("hipDeviceSynchronize: entering (blocking call)");
    a->ret = p_hipDeviceSynchronize();
    log_msg("hipDeviceSynchronize -> ret=%d (real)", a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_register_fat_binary(void *args)
{
    struct args_register_fat_binary *a = args;
    a->fatbin_token = NULL;
    if (!ensure_loaded() || !p_hipModuleLoadData) return STATUS_SUCCESS;

    const void *bundle = resolve_bundle_ptr(a->data);
    if (!bundle)
    {
        log_msg("__hipRegisterFatBinary: not a recognizable __CLANG_OFFLOAD_BUNDLE__");
        return STATUS_SUCCESS;
    }

    void *module = NULL;
    int rc = p_hipModuleLoadData(&module, bundle);
    if (rc != HIP_SUCCESS)
    {
        log_msg("hipModuleLoadData failed, rc=%d", rc);
        return STATUS_SUCCESS;
    }

    pthread_mutex_lock(&registry_mutex);
    registry_add_fatbin(a->data, module);
    pthread_mutex_unlock(&registry_mutex);

    a->fatbin_token = (void *)a->data;
    log_msg("__hipRegisterFatBinary: loaded module %p", module);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_register_function(void *args)
{
    struct args_register_function *a = args;
    void *module = find_module(a->fatbin_token);
    if (!module || !p_hipModuleGetFunction) return STATUS_SUCCESS;

    void *function = NULL;
    int rc = p_hipModuleGetFunction(&function, module, a->device_name);
    if (rc != HIP_SUCCESS)
    {
        log_msg("hipModuleGetFunction(%s) failed, rc=%d", a->device_name, rc);
        return STATUS_SUCCESS;
    }

    pthread_mutex_lock(&registry_mutex);
    registry_add_function(a->host_fn, function, a->device_name);
    pthread_mutex_unlock(&registry_mutex);
    log_msg("resolved kernel \"%s\" -> %p", a->device_name, function);
    return STATUS_SUCCESS;
}

/* docs/linux-support-spec.md's g_e4m3_lut finding: __hipRegisterVar used
 * to be a pure no-op (see pe_shim.c's old comment on it) - a real
 * device global (confirmed live: __hipRegisterVar(device_name=g_e4m3_lut)
 * and a real 512-byte hipMemcpyToSymbol call both happen during warmup)
 * had no way to be found again later, so that write silently failed
 * every time with zero trace anywhere. This mirrors
 * unix_register_function exactly, just resolving a device global via
 * hipModuleGetGlobal instead of a kernel via hipModuleGetFunction. */
static NTSTATUS unix_register_var(void *args)
{
    struct args_register_var *a = args;
    void *module = find_module(a->fatbin_token);
    if (!module || !p_hipModuleGetGlobal) return STATUS_SUCCESS;

    void *device_ptr = NULL;
    size_t device_size = 0;
    int rc = p_hipModuleGetGlobal(&device_ptr, &device_size, module, a->device_name);
    if (rc != HIP_SUCCESS)
    {
        log_msg("hipModuleGetGlobal(%s) failed, rc=%d", a->device_name, rc);
        return STATUS_SUCCESS;
    }

    pthread_mutex_lock(&registry_mutex);
    registry_add_var(a->host_var, device_ptr);
    pthread_mutex_unlock(&registry_mutex);
    log_msg("resolved var \"%s\" -> %p (%zu bytes)", a->device_name, device_ptr, device_size);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_unregister_fat_binary(void *args)
{
    (void)args;
    return STATUS_SUCCESS;
}

/* Throwaway spike (docs/linux-support-spec.md \xc2\xa715h.2): does a real
 * Linux fd, obtained by the PE side from Wine's own Vulkan thunk, work
 * as-is when handed straight to hipImportExternalMemory from THIS
 * native side, in the same process? Not wired into any real HIP call
 * path - only reachable via the DlssnrVulkanFdSpike rundll32 entry
 * point. Safe to delete once the question is answered either way. */
static NTSTATUS unix_launch_kernel(void *args)
{
    struct args_launch_kernel *a = args;
    void *function = find_function(a->host_fn);
    if (!function || !p_hipModuleLaunchKernel)
    {
        log_msg("hipLaunchKernel: function %p never registered", a->host_fn);
        a->ret = -1;
        return STATUS_SUCCESS;
    }

    /* a->args is args[0] from the PE side's hipLaunchKernel call - a
     * real pointer into this same process's memory, holding the
     * kernel's one struct argument (every kernel observed takes exactly
     * one, confirmed by demangling names like
     * _Z16k_swin_1h_32_fp810SwinParams -> k_swin_1h_32_fp8(SwinParams)).
     * No copying, no fixups: pass it straight through. */
    void *kernel_params[1] = { a->args };
    /* docs/linux-support-spec.md: this used to always pass NULL here,
     * discarding the caller's real stream - see unix_memcpy_async's
     * comment for the real, live evidence this was a genuine bug, not
     * a harmless simplification. */
    a->ret = p_hipModuleLaunchKernel(
        function,
        a->grid.x, a->grid.y, a->grid.z,
        a->block.x, a->block.y, a->block.z,
        a->shared_mem,
        a->stream,
        kernel_params,
        NULL);
    /* docs/linux-support-spec.md \xc2\xa736c: the previous log only recorded
     * the function pointer, not the launch configuration itself - added
     * to check whether a specific kernel's grid/block/shared-mem
     * request is the real trigger for the "operation not supported"
     * failures seen live (a real launch-config rejection would often
     * still show up as ret!=0 right here, since that class of error is
     * typically synchronous, unlike a mid-kernel execution fault which
     * only surfaces later at hipGetLastError/hipDeviceSynchronize).
     *
     * docs/linux-support-spec.md \xc2\xa749: the log still only had the bare
     * function pointer, not which kernel it actually is - every real
     * crash-log reading session this project has done needed a manual
     * c++filt cross-reference against an earlier "resolved kernel" log
     * line to find out (e.g. \xc2\xa735b's identification of
     * k_swin_var<32,true>). The registry already learns each kernel's
     * real mangled name at registration time (unix_register_function)
     * and, since \xc2\xa749, keeps it - looked up and logged here so every
     * single launch is self-identifying, not just the first one. */
    const char *name = find_function_name(a->host_fn);
    log_msg("hipLaunchKernel(function=%p, kernel=%s, stream=%p) grid=(%u,%u,%u) block=(%u,%u,%u) "
            "sharedMem=%u threads/block=%u -> ret=%d (real)",
            function, name && *name ? name : "?", a->stream, a->grid.x, a->grid.y, a->grid.z,
            a->block.x, a->block.y, a->block.z, a->shared_mem,
            a->block.x * a->block.y * a->block.z, a->ret);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------
 * External memory (interop) - real
 * ------------------------------------------------------------------- */

static NTSTATUS unix_import_external_memory(void *args)
{
    struct args_import_external_memory *a = args;
    int have_symbol = ensure_loaded() && p_hipImportExternalMemory;
    void *real_ptr = NULL;
    int real_ret = -1;

    if (have_symbol)
    {
        struct hip_ext_mem_handle_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.type = HIP_EXT_MEM_TYPE_OPAQUE_FD;
        desc.handle.fd = a->fd;
        desc.size = a->size;
        desc.flags = a->flags;
        real_ret = p_hipImportExternalMemory(&real_ptr, &desc);
    }
    hip_forward_ptr_result(&a->ext_mem, &a->ret, have_symbol, real_ptr, real_ret);
    log_msg("hipImportExternalMemory(fd=%d, size=%llu) -> ext_mem=%p, ret=%d",
            a->fd, a->size, a->ext_mem, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_external_memory_get_mapped_buffer(void *args)
{
    struct args_external_memory_get_mapped_buffer *a = args;
    int have_symbol = ensure_loaded() && p_hipExternalMemoryGetMappedBuffer;
    void *real_ptr = NULL;
    int real_ret = -1;

    if (have_symbol)
    {
        struct hip_ext_mem_buffer_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.offset = a->offset;
        desc.size = a->size;
        desc.flags = a->flags;
        real_ret = p_hipExternalMemoryGetMappedBuffer(&real_ptr, a->ext_mem, &desc);
    }
    hip_forward_ptr_result(&a->dev_ptr, &a->ret, have_symbol, real_ptr, real_ret);
    log_msg("hipExternalMemoryGetMappedBuffer(ext_mem=%p, offset=%llu, size=%llu) -> dev_ptr=%p, ret=%d",
            a->ext_mem, a->offset, a->size, a->dev_ptr, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_destroy_external_memory(void *args)
{
    struct args_destroy_external_memory *a = args;
    if (!ensure_loaded() || !p_hipDestroyExternalMemory) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipDestroyExternalMemory(a->ext_mem);
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------
 * Events / streams - real
 * ------------------------------------------------------------------- */

static NTSTATUS unix_event_create(void *args)
{
    struct args_event_create *a = args;
    int have_symbol = ensure_loaded() && (a->flags ? !!p_hipEventCreateWithFlags : !!p_hipEventCreate);
    void *real_ptr = NULL;
    int real_ret = -1;

    if (have_symbol)
        real_ret = a->flags ? p_hipEventCreateWithFlags(&real_ptr, a->flags) : p_hipEventCreate(&real_ptr);
    hip_forward_ptr_result(&a->event, &a->ret, have_symbol, real_ptr, real_ret);
    log_msg("hipEventCreate(flags=%u) -> event=%p, ret=%d", a->flags, a->event, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_event_record(void *args)
{
    struct args_event_record *a = args;
    if (!ensure_loaded() || !p_hipEventRecord) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipEventRecord(a->event, a->stream);
    log_msg("hipEventRecord(event=%p, stream=%p) -> ret=%d", a->event, a->stream, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_event_synchronize(void *args)
{
    struct args_event_synchronize *a = args;
    if (!ensure_loaded() || !p_hipEventSynchronize) { a->ret = -1; return STATUS_SUCCESS; }
    /* Same blind spot hipDeviceSynchronize used to have, and the same
     * fix: log entry separately from the ret= line, so a real hang
     * inside this specific blocking call shows up as an unmatched
     * "entering" line instead of silence indistinguishable from "no
     * call happened". */
    log_msg("hipEventSynchronize(event=%p): entering (blocking call)", a->event);
    a->ret = p_hipEventSynchronize(a->event);
    log_msg("hipEventSynchronize(event=%p) -> ret=%d", a->event, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_event_query(void *args)
{
    struct args_event_query *a = args;
    if (!ensure_loaded() || !p_hipEventQuery) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipEventQuery(a->event);
    log_msg("hipEventQuery(event=%p) -> ret=%d", a->event, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_event_elapsed_time(void *args)
{
    struct args_event_elapsed_time *a = args;
    a->ms = 0.0f;
    if (!ensure_loaded() || !p_hipEventElapsedTime) { a->ret = -1; return STATUS_SUCCESS; }
    a->ret = p_hipEventElapsedTime(&a->ms, a->start, a->stop);
    log_msg("hipEventElapsedTime(start=%p, stop=%p) -> ms=%f, ret=%d", a->start, a->stop, a->ms, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_stream_create(void *args)
{
    struct args_stream_create *a = args;
    int have_symbol = ensure_loaded() && p_hipStreamCreateWithFlags;
    void *real_ptr = NULL;
    int real_ret = -1;

    if (have_symbol)
        real_ret = p_hipStreamCreateWithFlags(&real_ptr, a->flags);
    hip_forward_ptr_result(&a->stream, &a->ret, have_symbol, real_ptr, real_ret);
    log_msg("hipStreamCreateWithFlags(flags=%u) -> stream=%p, ret=%d", a->flags, a->stream, a->ret);
    return STATUS_SUCCESS;
}

static NTSTATUS unix_stream_synchronize(void *args)
{
    struct args_stream_synchronize *a = args;
    if (!ensure_loaded() || !p_hipStreamSynchronize) { a->ret = -1; return STATUS_SUCCESS; }
    log_msg("hipStreamSynchronize(stream=%p): entering (blocking call)", a->stream);
    a->ret = p_hipStreamSynchronize(a->stream);
    log_msg("hipStreamSynchronize(stream=%p) -> ret=%d", a->stream, a->ret);
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    unix_get_device_count,
    unix_set_device,
    unix_driver_get_version,
    unix_runtime_get_version,
    unix_get_error_string,
    unix_get_device_properties,
    unix_malloc,
    unix_free,
    unix_memcpy,
    unix_device_synchronize,
    unix_register_fat_binary,
    unix_register_function,
    unix_unregister_fat_binary,
    unix_launch_kernel,
    unix_import_external_memory,
    unix_external_memory_get_mapped_buffer,
    unix_destroy_external_memory,
    unix_event_create,
    unix_event_record,
    unix_event_synchronize,
    unix_event_query,
    unix_event_elapsed_time,
    unix_stream_create,
    unix_stream_synchronize,
    unix_memset,
    unix_memset_async, /* HIP_CALL_MEMSET_ASYNC - now genuinely async, see unix_memcpy_async's comment */
    unix_register_var,
    unix_memcpy_to_symbol,
    unix_memcpy_async,
};
