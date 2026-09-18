/* Pure, Wine/HIP-independent logic factored out of pe_shim.c/native.c
 * so it can be unit tested with a plain host compiler - no Wine
 * headers, no ROCm runtime, no cross-compilation needed. Also holds
 * the real HIP external-memory ABI structs, shared verbatim between
 * the PE side (which receives them from danielblnc's runtime) and
 * the native side (which passes them straight to the real
 * dlopen'd HIP library) - both sides need byte-identical layout, and
 * neither needs any Windows- or Wine-specific type to describe it. */
#ifndef HIP_UNIXLIB_EXT_MEM_H
#define HIP_UNIXLIB_EXT_MEM_H

/* Real public HIP struct layout (from hip_runtime_api.h - matched
 * exactly, this is the actual ABI danielblnc's binary was compiled
 * against, confirmed via static analysis - see
 * docs/linux-support-spec.md). */
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

/* hipExternalMemoryBufferDesc, same sourcing as above. */
struct hip_ext_mem_buffer_desc
{
    unsigned long long offset;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};

enum
{
    HIP_EXT_MEM_TYPE_OPAQUE_FD = 1,
    HIP_EXT_MEM_TYPE_OPAQUE_WIN32 = 2,
    HIP_EXT_MEM_TYPE_OPAQUE_WIN32_KMT = 3,
    HIP_EXT_MEM_TYPE_D3D12_HEAP = 4,
    HIP_EXT_MEM_TYPE_D3D12_RESOURCE = 5,
    HIP_EXT_MEM_TYPE_D3D11_RESOURCE = 6,
    HIP_EXT_MEM_TYPE_D3D11_RESOURCE_KMT = 7,
    HIP_EXT_MEM_TYPE_NV_SCI_BUF = 8,
};

/* Extracts a real, already-valid-in-this-process Linux fd from
 * whatever handle shape the caller's hipExternalMemoryHandleDesc
 * actually holds. Two shapes are handled for real:
 *
 *   - OpaqueFd: the fd is exactly where the real HIP ABI says it is
 *     (handle.fd).
 *
 *   - D3D12Resource: danielblnc's actual usage. vkd3d-proton's own
 *     CreateSharedHandle patch (docs/linux-support-spec.md \xc2\xa727a)
 *     smuggles a real fd through the Win32-shaped `handle.win32.handle`
 *     field as a plain integer value, since this whole bridge only
 *     ever runs under Wine, where no real Win32 handle table exists
 *     to satisfy anyway.
 *
 * Every other type - or a null desc - returns -1 (not a real fd).
 * Callers must treat that the same as this bridge treated every call
 * before this fix existed: "not supported", never a fabricated fd. */
int hip_ext_mem_extract_fd(const struct hip_ext_mem_handle_desc *desc);

#endif
