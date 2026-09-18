#include "alloc_cap.h"

#include <stddef.h>

#define MAX_TRACKED_ALLOCS 4096

struct alloc_entry { void *ptr; unsigned long long size; };

static struct alloc_entry allocs[MAX_TRACKED_ALLOCS];
static int alloc_count;
static unsigned long long total_bytes;

void alloc_cap_reset(void)
{
    alloc_count = 0;
    total_bytes = 0;
}

int alloc_cap_would_exceed(unsigned long long current_total,
        unsigned long long requested_size, unsigned long long cap_bytes)
{
    if (cap_bytes == 0)
        return 0;
    return current_total + requested_size > cap_bytes;
}

int alloc_cap_record(void *ptr, unsigned long long size)
{
    if (!ptr || alloc_count >= MAX_TRACKED_ALLOCS)
        return 0;
    allocs[alloc_count].ptr = ptr;
    allocs[alloc_count].size = size;
    alloc_count++;
    total_bytes += size;
    return 1;
}

unsigned long long alloc_cap_release(void *ptr)
{
    if (!ptr)
        return 0;
    for (int i = 0; i < alloc_count; i++)
    {
        if (allocs[i].ptr == ptr)
        {
            unsigned long long size = allocs[i].size;
            allocs[i] = allocs[alloc_count - 1];
            alloc_count--;
            total_bytes -= size;
            return size;
        }
    }
    return 0;
}

unsigned long long alloc_cap_total(void)
{
    return total_bytes;
}
