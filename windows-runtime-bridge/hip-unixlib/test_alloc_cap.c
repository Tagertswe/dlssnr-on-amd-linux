/* Unit tests for alloc_cap.c - plain host gcc, no Wine/HIP dependency.
 * Run via `make test`. */
#include "alloc_cap.h"

#include <assert.h>
#include <stdio.h>

static int dummy_a, dummy_b, dummy_c;

static void test_no_cap_never_rejects(void)
{
    assert(!alloc_cap_would_exceed(0, 1000000000ULL, 0));
    assert(!alloc_cap_would_exceed(999999999999ULL, 1000000000ULL, 0));
}

static void test_cap_rejects_when_would_exceed(void)
{
    /* 3GB cap, 2GB already outstanding, requesting 2GB more -> exceeds. */
    unsigned long long cap = 3ULL * 1024 * 1024 * 1024;
    unsigned long long current = 2ULL * 1024 * 1024 * 1024;
    unsigned long long request = 2ULL * 1024 * 1024 * 1024;
    assert(alloc_cap_would_exceed(current, request, cap));
}

static void test_cap_allows_when_under(void)
{
    unsigned long long cap = 3ULL * 1024 * 1024 * 1024;
    unsigned long long current = 1ULL * 1024 * 1024 * 1024;
    unsigned long long request = 1ULL * 1024 * 1024 * 1024;
    assert(!alloc_cap_would_exceed(current, request, cap));
}

static void test_cap_allows_exact_fit(void)
{
    /* Landing exactly on the cap is allowed - only strictly over rejects. */
    assert(!alloc_cap_would_exceed(2000, 1000, 3000));
}

static void test_record_and_release_track_total(void)
{
    alloc_cap_reset();
    assert(alloc_cap_total() == 0);

    assert(alloc_cap_record(&dummy_a, 100));
    assert(alloc_cap_record(&dummy_b, 200));
    assert(alloc_cap_total() == 300);

    assert(alloc_cap_release(&dummy_a) == 100);
    assert(alloc_cap_total() == 200);

    /* Releasing something never tracked is a harmless no-op. */
    assert(alloc_cap_release(&dummy_c) == 0);
    assert(alloc_cap_total() == 200);

    assert(alloc_cap_release(&dummy_b) == 200);
    assert(alloc_cap_total() == 0);
}

static void test_null_ptr_ignored(void)
{
    alloc_cap_reset();
    assert(!alloc_cap_record(NULL, 100));
    assert(alloc_cap_total() == 0);
    assert(alloc_cap_release(NULL) == 0);
}

int main(void)
{
    test_no_cap_never_rejects();
    test_cap_rejects_when_would_exceed();
    test_cap_allows_when_under();
    test_cap_allows_exact_fit();
    test_record_and_release_track_total();
    test_null_ptr_ignored();
    printf("all tests passed\n");
    return 0;
}
