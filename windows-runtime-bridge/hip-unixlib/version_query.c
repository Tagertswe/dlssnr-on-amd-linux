#include "version_query.h"

void hip_version_query_apply(int *out_version, int *out_ret,
        int have_symbol, int real_version, int real_ret)
{
    if (!have_symbol)
    {
        *out_version = 0;
        *out_ret = -1;
        return;
    }
    *out_version = real_version;
    *out_ret = real_ret;
}
