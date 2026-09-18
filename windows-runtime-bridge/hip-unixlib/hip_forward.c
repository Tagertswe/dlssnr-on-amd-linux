#include "hip_forward.h"
#include <stddef.h>

void hip_forward_ptr_result(void **out_ptr, int *out_ret,
        int have_symbol, void *real_ptr, int real_ret)
{
    if (!have_symbol)
    {
        *out_ptr = NULL;
        *out_ret = -1;
        return;
    }
    *out_ptr = real_ptr;
    *out_ret = real_ret;
}
