#include "last_error.h"

int hip_get_last_error_and_reset(int current, int success_code, int *out_new_state)
{
    if (out_new_state)
        *out_new_state = success_code;
    return current;
}
