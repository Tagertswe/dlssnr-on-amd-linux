/* Unit tests for last_error.c - plain host gcc, no Wine/HIP dependency.
 * Run via `make test`. */
#include "last_error.h"

#include <assert.h>
#include <stdio.h>

#define HIP_SUCCESS 0
#define HIP_ERROR_NOT_SUPPORTED 801

static void test_returns_the_current_error(void)
{
    int new_state;
    assert(hip_get_last_error_and_reset(HIP_ERROR_NOT_SUPPORTED, HIP_SUCCESS, &new_state)
           == HIP_ERROR_NOT_SUPPORTED);
}

static void test_resets_state_to_success(void)
{
    int new_state = -1;
    hip_get_last_error_and_reset(HIP_ERROR_NOT_SUPPORTED, HIP_SUCCESS, &new_state);
    assert(new_state == HIP_SUCCESS);
}

/* The real bug this fixes: a single stale error must not keep reporting
 * forever - a second read, after the first one reset the state, must
 * come back clean. */
static void test_second_read_after_reset_is_clean(void)
{
    int state = HIP_ERROR_NOT_SUPPORTED;
    int first = hip_get_last_error_and_reset(state, HIP_SUCCESS, &state);
    int second = hip_get_last_error_and_reset(state, HIP_SUCCESS, &state);
    assert(first == HIP_ERROR_NOT_SUPPORTED);
    assert(second == HIP_SUCCESS);
}

static void test_already_success_stays_success(void)
{
    int new_state;
    assert(hip_get_last_error_and_reset(HIP_SUCCESS, HIP_SUCCESS, &new_state) == HIP_SUCCESS);
    assert(new_state == HIP_SUCCESS);
}

static void test_null_out_state_does_not_crash(void)
{
    assert(hip_get_last_error_and_reset(HIP_ERROR_NOT_SUPPORTED, HIP_SUCCESS, NULL)
           == HIP_ERROR_NOT_SUPPORTED);
}

int main(void)
{
    test_returns_the_current_error();
    test_resets_state_to_success();
    test_second_read_after_reset_is_clean();
    test_already_success_stays_success();
    test_null_out_state_does_not_crash();
    printf("all tests passed\n");
    return 0;
}
