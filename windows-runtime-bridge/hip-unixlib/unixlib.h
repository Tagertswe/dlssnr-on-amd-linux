/* Shared opcode enum and argument structs between the PE-side shim
 * (amdhip64_7.dll) and the native side (libamdhip64_7.so).
 *
 * Since both sides run in the same real process address space under
 * Wine (confirmed empirically - see docs/linux-support-spec.md's
 * "hip-unixlib" section), argument structs pass plain pointers directly
 * - no serialization, no fake-address translation, no fixups. This is
 * the whole reason this port is architecturally simpler than the
 * socket-IPC design it replaces, not just a different transport for the
 * same logic.
 */
#ifndef HIP_UNIXLIB_H
#define HIP_UNIXLIB_H

#include <stddef.h>

enum hip_unix_call
{
    HIP_CALL_GET_DEVICE_COUNT,
    HIP_CALL_SET_DEVICE,
    HIP_CALL_DRIVER_GET_VERSION,
    HIP_CALL_RUNTIME_GET_VERSION,
    HIP_CALL_GET_ERROR_STRING,
    HIP_CALL_GET_DEVICE_PROPERTIES,
    HIP_CALL_MALLOC,
    HIP_CALL_FREE,
    HIP_CALL_MEMCPY,
    HIP_CALL_DEVICE_SYNCHRONIZE,
    HIP_CALL_REGISTER_FAT_BINARY,
    HIP_CALL_REGISTER_FUNCTION,
    HIP_CALL_UNREGISTER_FAT_BINARY,
    HIP_CALL_LAUNCH_KERNEL,
    HIP_CALL_IMPORT_EXTERNAL_MEMORY,
    HIP_CALL_EXTERNAL_MEMORY_GET_MAPPED_BUFFER,
    HIP_CALL_DESTROY_EXTERNAL_MEMORY,
    HIP_CALL_EVENT_CREATE,
    HIP_CALL_EVENT_RECORD,
    HIP_CALL_EVENT_SYNCHRONIZE,
    HIP_CALL_EVENT_QUERY,
    HIP_CALL_EVENT_ELAPSED_TIME,
    HIP_CALL_STREAM_CREATE,
    HIP_CALL_STREAM_SYNCHRONIZE,
    HIP_CALL_MEMSET,
    HIP_CALL_MEMSET_ASYNC,
    HIP_CALL_REGISTER_VAR,
    HIP_CALL_MEMCPY_TO_SYMBOL,
    HIP_CALL_MEMCPY_ASYNC,
};

struct args_get_device_count { int count; int ret; };
struct args_set_device { int device; int ret; };
struct args_driver_get_version { int version; int ret; };
struct args_runtime_get_version { int version; int ret; };
struct args_get_error_string { int error; const char *ret_str; };
struct args_get_device_properties { void *props; int device; int ret; };
struct args_malloc { void *ptr; size_t size; int ret; };
struct args_free { void *ptr; int ret; };
/* `stream` is only meaningful for the *_ASYNC opcodes - HIP_CALL_MEMCPY/
 * HIP_CALL_MEMSET (the synchronous calls) ignore it and always run on
 * the real, blocking, stream-independent HIP call, unchanged. See
 * docs/linux-support-spec.md's real-play-session findings: kernels and
 * async copies/sets used to always run on the null stream regardless of
 * what stream danielblnc's runtime actually passed, meaning his own
 * `hipEventRecord`-based timing measured nothing in front of the work
 * it was supposed to be timing - a real, plausible driver of both the
 * ~96% capture-check timeout rate and the GPU ring hang. */
struct args_memcpy { void *dst; const void *src; size_t size; int kind; void *stream; int ret; };
struct args_memset { void *ptr; int value; size_t size; void *stream; int ret; };
struct args_device_synchronize { int ret; };

struct args_register_fat_binary { const void *data; void *fatbin_token; };
struct args_register_function { void *fatbin_token; const void *host_fn; const char *device_name; };
struct args_unregister_fat_binary { void *fatbin_token; };

/* __hipRegisterVar's host_var is the compiler-generated host-side
 * address that later shows up as hipMemcpyToSymbol's `symbol` argument
 * - the real runtime's whole job is mapping that host-side address to
 * the actual resolved device global (via hipModuleGetGlobal on the
 * module __hipRegisterFatBinary already loaded), the same shape as
 * host_fn -> hipFunction_t for kernels above. */
struct args_register_var { void *fatbin_token; const void *host_var; const char *device_name; };
struct args_memcpy_to_symbol { const void *symbol; const void *src; size_t size; size_t offset; int kind; int ret; };

struct dim3_value { unsigned int x, y, z; };

struct args_launch_kernel
{
    const void *host_fn;
    struct dim3_value grid;
    struct dim3_value block;
    void *args;      /* args[0], the caller's single param-struct pointer - real memory, dereferenced directly */
    unsigned int shared_mem;
    void *stream;    /* the real stream the caller passed - previously discarded, see args_memcpy's comment */
    int ret;
};

/* External memory (interop) - the fd is a plain Linux file descriptor,
 * already valid in this same process (see the file header comment):
 * either the caller passed hipExternalMemoryHandleTypeOpaqueFd
 * directly, or (danielblnc's actual usage - see ext_mem.c) it arrives
 * smuggled through a D3D12Resource-typed handle field, exactly as
 * vkd3d-proton's own CreateSharedHandle patch produces it
 * (docs/linux-support-spec.md \xc2\xa727a). */
struct args_import_external_memory { int fd; unsigned long long size; unsigned int flags; void *ext_mem; int ret; };
struct args_external_memory_get_mapped_buffer { void *ext_mem; unsigned long long offset; unsigned long long size; unsigned int flags; void *dev_ptr; int ret; };
struct args_destroy_external_memory { void *ext_mem; int ret; };

struct args_event_create { unsigned int flags; void *event; int ret; };
struct args_event_record { void *event; void *stream; int ret; };
struct args_event_synchronize { void *event; int ret; };
struct args_event_query { void *event; int ret; };
struct args_event_elapsed_time { void *start; void *stop; float ms; int ret; };
struct args_stream_create { unsigned int flags; void *stream; int ret; };
struct args_stream_synchronize { void *stream; int ret; };

#endif
