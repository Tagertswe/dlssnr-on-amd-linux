/* Unit tests for hip_forward.c - plain host gcc, no Wine/HIP
 * dependency. Run via `make test`. */
#include "hip_forward.h"

#include <assert.h>
#include <stdio.h>

static int dummy_target;

static void test_symbol_missing_reports_null_and_failure(void)
{
    void *ptr = &dummy_target;
    int ret = 0;
    hip_forward_ptr_result(&ptr, &ret, /*have_symbol=*/0, /*real_ptr=*/&dummy_target, /*real_ret=*/0);
    assert(ptr == NULL);
    assert(ret == -1);
}

static void test_symbol_present_forwards_real_success(void)
{
    void *ptr = NULL;
    int ret = -1;
    hip_forward_ptr_result(&ptr, &ret, /*have_symbol=*/1, /*real_ptr=*/&dummy_target, /*real_ret=*/0);
    assert(ptr == &dummy_target);
    assert(ret == 0);
}

static void test_symbol_present_forwards_real_failure(void)
{
    /* The real call resolved and ran, but the real HIP library itself
     * reported an error - that must reach the caller too, not be
     * silently turned into a fabricated success. */
    void *ptr = &dummy_target;
    int ret = -1;
    hip_forward_ptr_result(&ptr, &ret, /*have_symbol=*/1, /*real_ptr=*/NULL, /*real_ret=*/801);
    assert(ptr == NULL);
    assert(ret == 801);
}

int main(void)
{
    test_symbol_missing_reports_null_and_failure();
    test_symbol_present_forwards_real_success();
    test_symbol_present_forwards_real_failure();
    printf("all tests passed\n");
    return 0;
}
