/* Pure, Wine/HIP-independent logic factored out of native.c so it can
 * be unit tested with a plain host compiler - no Wine headers, no
 * ROCm runtime, no cross-compilation needed.
 *
 * An opt-in diagnostic knob (docs/linux-support-spec.md §38/§39): the
 * real, hardware-confirmed crash cause is a GPU page fault at exactly
 * 0x100000000 (2^32 / 4GB) - the textbook signature of a 32-bit
 * address/offset overflow somewhere in danielblnc's own kernels. This
 * tests whether keeping this pipeline's total *outstanding* (not yet
 * freed) real GPU allocation comfortably under 4GB avoids whatever
 * buffer layout triggers it, without needing danielblnc's source.
 *
 * Disabled (no cap, no tracking overhead beyond the table itself) by
 * default - only active when native.c reads a real cap value from the
 * DLSSNR_VRAM_CAP_BYTES environment variable at startup. */
#ifndef HIP_UNIXLIB_ALLOC_CAP_H
#define HIP_UNIXLIB_ALLOC_CAP_H

void alloc_cap_reset(void);

/* Would allocating `requested_size` more bytes push total outstanding
 * tracked allocation strictly above `cap_bytes`? cap_bytes == 0 means
 * "no cap" - always returns 0 (never rejects). Pure: takes the
 * current running total as an explicit argument rather than reading
 * global state, so it's testable without going through
 * alloc_cap_record/alloc_cap_release at all. */
int alloc_cap_would_exceed(unsigned long long current_total,
        unsigned long long requested_size, unsigned long long cap_bytes);

/* Record a real allocation `ptr` of `size` bytes that already
 * succeeded, so alloc_cap_total() reflects it. Returns 1 on success,
 * 0 if the fixed-size tracking table is full (the real allocation
 * itself is NOT undone by this - a full table only means this one
 * allocation's size won't be subtracted back out on free, so the cap
 * becomes conservative rather than wrong in the unsafe direction). */
int alloc_cap_record(void *ptr, unsigned long long size);

/* Release a tracked allocation. Returns the size that was subtracted
 * (0 if `ptr` was never tracked, e.g. NULL or a table-full miss from
 * alloc_cap_record). */
unsigned long long alloc_cap_release(void *ptr);

unsigned long long alloc_cap_total(void);

#endif
