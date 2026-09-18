/* Adapted (C port, restructured) from guentra/dlss5-amd-hip-linux's
 * src/native_snapshot_gate.h, MIT licensed - see snapshot_gate.h's own
 * header comment for the real source and rationale. */
#include "snapshot_gate.h"

int snapshot_batch_match(uint32_t producer_thread, uint32_t current_thread,
        uintptr_t producer_list, const uintptr_t *lists, size_t count)
{
    size_t i;

    if (!producer_list || !lists || !count || count > 64 || producer_thread != current_thread)
        return 0;
    if (lists[count - 1] != producer_list)
        return 0;
    for (i = 0; i + 1 < count; i++)
        if (lists[i] == producer_list)
            return 0;
    return 1;
}
