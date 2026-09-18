#include "ext_mem.h"
#include <stdint.h>
#include <stddef.h>

int hip_ext_mem_extract_fd(const struct hip_ext_mem_handle_desc *desc)
{
    if (!desc)
        return -1;

    switch (desc->type)
    {
        case HIP_EXT_MEM_TYPE_OPAQUE_FD:
            return desc->handle.fd;
        case HIP_EXT_MEM_TYPE_D3D12_RESOURCE:
            return (int)(intptr_t)desc->handle.win32.handle;
        default:
            return -1;
    }
}
