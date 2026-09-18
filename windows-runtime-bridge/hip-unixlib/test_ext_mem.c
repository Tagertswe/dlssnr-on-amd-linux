/* Unit tests for ext_mem.c - plain host gcc, no Wine/HIP dependency.
 * Run via `make test`. */
#include "ext_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_opaque_fd_uses_handle_fd_directly(void)
{
    struct hip_ext_mem_handle_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = HIP_EXT_MEM_TYPE_OPAQUE_FD;
    desc.handle.fd = 42;
    assert(hip_ext_mem_extract_fd(&desc) == 42);
}

static void test_d3d12_resource_reinterprets_win32_handle_as_fd(void)
{
    /* This is danielblnc's actual, real usage: vkd3d-proton's
     * CreateSharedHandle patch smuggles a real fd through the
     * win32.handle field as a plain integer value (confirmed live,
     * real session values - see docs/linux-support-spec.md \xc2\xa727d:
     * a handle of 0x181 was observed alongside fd=385, the same
     * number read two ways). */
    struct hip_ext_mem_handle_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = HIP_EXT_MEM_TYPE_D3D12_RESOURCE;
    desc.handle.win32.handle = (void *)(long)385;
    assert(hip_ext_mem_extract_fd(&desc) == 385);
}

static void test_unsupported_type_returns_invalid(void)
{
    struct hip_ext_mem_handle_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = HIP_EXT_MEM_TYPE_D3D12_HEAP;
    desc.handle.fd = 7; /* must be ignored - this type is never real */
    assert(hip_ext_mem_extract_fd(&desc) == -1);
}

static void test_unknown_type_returns_invalid(void)
{
    struct hip_ext_mem_handle_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = 99;
    assert(hip_ext_mem_extract_fd(&desc) == -1);
}

static void test_null_desc_returns_invalid(void)
{
    assert(hip_ext_mem_extract_fd(NULL) == -1);
}

int main(void)
{
    test_opaque_fd_uses_handle_fd_directly();
    test_d3d12_resource_reinterprets_win32_handle_as_fd();
    test_unsupported_type_returns_invalid();
    test_unknown_type_returns_invalid();
    test_null_desc_returns_invalid();
    printf("all tests passed\n");
    return 0;
}
