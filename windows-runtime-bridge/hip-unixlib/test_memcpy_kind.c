/* Unit tests for memcpy_kind.c - plain host gcc, no Wine/HIP
 * dependency. Run via `make test`.
 *
 * Regression test for the real bug found live: hipMemcpy(...,
 * hipMemcpyDeviceToDevice) was rejected by pe_shim.c before ever
 * reaching native.c/real HIP, which does support it - see
 * memcpy_kind.h's own comment and docs/linux-support-spec.md. */
#include "memcpy_kind.h"

#include <assert.h>
#include <stdio.h>

static void test_host_to_host_supported(void)
{
    assert(hip_memcpy_kind_supported(HIP_MEMCPY_HOST_TO_HOST));
}

static void test_host_to_device_supported(void)
{
    assert(hip_memcpy_kind_supported(HIP_MEMCPY_HOST_TO_DEVICE));
}

static void test_device_to_host_supported(void)
{
    assert(hip_memcpy_kind_supported(HIP_MEMCPY_DEVICE_TO_HOST));
}

static void test_device_to_device_supported(void)
{
    /* This is the exact case that was wrongly rejected and caused the
     * observed "operation not supported" cascade. */
    assert(hip_memcpy_kind_supported(HIP_MEMCPY_DEVICE_TO_DEVICE));
}

static void test_default_supported(void)
{
    assert(hip_memcpy_kind_supported(HIP_MEMCPY_DEFAULT));
}

static void test_out_of_range_rejected(void)
{
    assert(!hip_memcpy_kind_supported(-1));
    assert(!hip_memcpy_kind_supported(5));
    assert(!hip_memcpy_kind_supported(999));
}

int main(void)
{
    test_host_to_host_supported();
    test_host_to_device_supported();
    test_device_to_host_supported();
    test_device_to_device_supported();
    test_default_supported();
    test_out_of_range_rejected();
    printf("all tests passed\n");
    return 0;
}
