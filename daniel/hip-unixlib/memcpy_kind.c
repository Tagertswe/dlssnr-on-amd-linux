#include "memcpy_kind.h"

int hip_memcpy_kind_supported(int kind)
{
    return kind >= HIP_MEMCPY_HOST_TO_HOST && kind <= HIP_MEMCPY_DEFAULT;
}
