/* Unit tests for version_query.c - plain host gcc, no Wine/HIP dependency.
 * Run via `make test`. */
#include "version_query.h"

#include <assert.h>
#include <stdio.h>

static void test_symbol_missing_reports_zero_and_failure(void)
{
    /* Neither the native library nor the specific symbol resolved -
     * must never fabricate a version number, and must signal failure
     * rather than a false HIP_SUCCESS. */
    int version = -1, ret = 0;
    hip_version_query_apply(&version, &ret, /*have_symbol=*/0, /*real_version=*/12345, /*real_ret=*/0);
    assert(version == 0);
    assert(ret == -1);
}

static void test_symbol_missing_ignores_leftover_real_args(void)
{
    /* Even if real_version/real_ret happen to hold something (they
     * shouldn't be read at all when have_symbol is 0, since native.c
     * never calls through a null function pointer to produce them) the
     * result must still be the same safe fallback. */
    int version = -1, ret = 0;
    hip_version_query_apply(&version, &ret, /*have_symbol=*/0, /*real_version=*/0, /*real_ret=*/-1);
    assert(version == 0);
    assert(ret == -1);
}

static void test_symbol_present_forwards_real_success(void)
{
    /* This is the actual bug being fixed: a real, resolved HIP driver
     * reporting a real version number must reach the caller unchanged -
     * danielblnc's own runtime makes compatibility decisions based on
     * this exact value (docs/linux-support-spec.md \xc2\xa727e). */
    int version = -1, ret = -1;
    hip_version_query_apply(&version, &ret, /*have_symbol=*/1, /*real_version=*/70260201, /*real_ret=*/0);
    assert(version == 70260201);
    assert(ret == 0);
}

static void test_symbol_present_forwards_real_failure(void)
{
    /* The real function resolved but itself reported an error - that
     * error must also be forwarded verbatim, not silently swallowed
     * into a fabricated success the way the original stub always did. */
    int version = -1, ret = -1;
    hip_version_query_apply(&version, &ret, /*have_symbol=*/1, /*real_version=*/0, /*real_ret=*/801);
    assert(version == 0);
    assert(ret == 801);
}

int main(void)
{
    test_symbol_missing_reports_zero_and_failure();
    test_symbol_missing_ignores_leftover_real_args();
    test_symbol_present_forwards_real_success();
    test_symbol_present_forwards_real_failure();
    printf("all tests passed\n");
    return 0;
}
