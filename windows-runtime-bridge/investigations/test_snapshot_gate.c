/* Unit tests for snapshot_gate.c - plain host gcc, no Wine/D3D12
 * dependency. Run directly: gcc -Wall -o test_snapshot_gate
 * snapshot_gate.c test_snapshot_gate.c && ./test_snapshot_gate */
#include "snapshot_gate.h"

#include <assert.h>
#include <stdio.h>

static void test_producer_last_in_batch_matches(void)
{
    uintptr_t lists[] = {0x100, 0x200, 0x300};
    assert(snapshot_batch_match(1, 1, 0x300, lists, 3) == 1);
}

static void test_producer_not_last_fails(void)
{
    /* The real failure case this whole check exists for: a producer
     * list appears in the batch, but something was submitted after it -
     * its result is not yet safe to trust from a fence wait alone. */
    uintptr_t lists[] = {0x100, 0x300, 0x200};
    assert(snapshot_batch_match(1, 1, 0x300, lists, 3) == 0);
}

static void test_producer_absent_fails(void)
{
    uintptr_t lists[] = {0x100, 0x200, 0x300};
    assert(snapshot_batch_match(1, 1, 0x999, lists, 3) == 0);
}

static void test_thread_mismatch_fails(void)
{
    /* guentra's own comment: a producer recorded on one thread and
     * checked from another is a real, observed failure mode, not
     * theoretical (the XeSS/Rise of the Ronin cross-thread case). */
    uintptr_t lists[] = {0x100, 0x200, 0x300};
    assert(snapshot_batch_match(1, 2, 0x300, lists, 3) == 0);
}

static void test_producer_appears_twice_fails(void)
{
    /* Present, and last - but also appears earlier. A resubmission of
     * the same list pointer within one batch is not the same as "this
     * is definitely the result we recorded," so this must fail safe. */
    uintptr_t lists[] = {0x300, 0x200, 0x300};
    assert(snapshot_batch_match(1, 1, 0x300, lists, 3) == 0);
}

static void test_single_entry_batch_matches(void)
{
    uintptr_t lists[] = {0x300};
    assert(snapshot_batch_match(1, 1, 0x300, lists, 1) == 1);
}

static void test_zero_producer_fails(void)
{
    uintptr_t lists[] = {0x100, 0x200};
    assert(snapshot_batch_match(1, 1, 0, lists, 2) == 0);
}

static void test_null_lists_fails(void)
{
    assert(snapshot_batch_match(1, 1, 0x300, NULL, 3) == 0);
}

static void test_zero_count_fails(void)
{
    uintptr_t lists[] = {0x300};
    assert(snapshot_batch_match(1, 1, 0x300, lists, 0) == 0);
}

static void test_oversized_batch_fails(void)
{
    /* Real fixed limit from the source this is ported from - a sanity
     * cap, not a real observed batch size. */
    uintptr_t lists[65];
    size_t i;
    for (i = 0; i < 65; i++)
        lists[i] = 0x100 + i;
    lists[64] = 0x300;
    assert(snapshot_batch_match(1, 1, 0x300, lists, 65) == 0);
}

static void test_max_size_batch_matches(void)
{
    uintptr_t lists[64];
    size_t i;
    for (i = 0; i < 64; i++)
        lists[i] = 0x100 + i;
    lists[63] = 0x300;
    assert(snapshot_batch_match(1, 1, 0x300, lists, 64) == 1);
}

int main(void)
{
    test_producer_last_in_batch_matches();
    test_producer_not_last_fails();
    test_producer_absent_fails();
    test_thread_mismatch_fails();
    test_producer_appears_twice_fails();
    test_single_entry_batch_matches();
    test_zero_producer_fails();
    test_null_lists_fails();
    test_zero_count_fails();
    test_oversized_batch_fails();
    test_max_size_batch_matches();
    printf("all tests passed\n");
    return 0;
}
