/* Pure, Wine/D3D12-independent logic, adapted (not copied verbatim - a
 * C port of the same check, restructured to this project's C style) from
 * guentra/dlss5-amd-hip-linux's src/native_snapshot_gate.h, MIT licensed:
 *   https://github.com/guentra/dlss5-amd-hip-linux
 *   Copyright (c) guentra - MIT License (see that repo's LICENSE)
 *
 * The real question this answers: given a batch of command lists just
 * submitted via ExecuteCommandLists, is a specific "producer" list (the
 * one whose result we're about to trust) genuinely the *last* occurrence
 * of that list in the batch, recorded on the same thread that's now
 * checking it? A producer list that appears earlier in the batch (and
 * is followed by something else), or that got recorded on a different
 * thread than the one now reading its result, is not something a real
 * fence wait alone protects you from - the fence proves the GPU work
 * finished, not that you're looking at the right submission's result.
 *
 * docs/linux-support-spec.md \xc2\xa761: built to test whether a *properly*
 * gated, fence-guaranteed submission (unlike this project's own earlier,
 * more naive probes in \xc2\xa751/\xc2\xa752) can still ever produce a dropped/
 * stale result under this system's real vkd3d-proton/Wine/RADV stack. */
#ifndef HIP_INVESTIGATIONS_SNAPSHOT_GATE_H
#define HIP_INVESTIGATIONS_SNAPSHOT_GATE_H

#include <stddef.h>
#include <stdint.h>

/* producer_thread/current_thread: the thread that recorded the producer
 * list vs. the thread now checking it - must match (guentra's own code
 * comments this as the real, observed requirement: a producer recorded
 * on one thread and checked from another is a real failure mode seen in
 * at least one title in that project, not a theoretical concern).
 * producer_list: the specific command list whose result we're about to
 * trust. lists/count: the full batch just passed to ExecuteCommandLists,
 * in submission order. Returns true only if producer_list is present,
 * appears exactly once, and is the *last* entry in the batch. */
int snapshot_batch_match(uint32_t producer_thread, uint32_t current_thread,
        uintptr_t producer_list, const uintptr_t *lists, size_t count);

#endif
