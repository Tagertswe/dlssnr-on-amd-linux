/* Pure, Wine/HIP-independent logic factored out of pe_shim.c so it can
 * be unit tested with a plain host compiler - no Wine headers, no ROCm
 * runtime, no cross-compilation needed. */
#ifndef HIP_UNIXLIB_LAST_ERROR_H
#define HIP_UNIXLIB_LAST_ERROR_H

/* Real hipGetLastError()/cudaGetLastError() semantics: return the
 * sticky last-error code, and reset the stored state to success -
 * "get and clear", not just "get".
 *
 * Found via docs/linux-support-spec.md's live crash-log investigation:
 * this shim's hipGetLastError() used to just read `last_error` and
 * return it, never clearing it - so a single real failure anywhere in
 * the process's lifetime (even a one-off, early-warmup call to a
 * still-stubbed function like hipMemcpyToSymbol) poisoned every later
 * hipGetLastError() check for the rest of the session, regardless of
 * whether the call actually being checked succeeded. A live run showed
 * exactly this: one real failure, 389 identical stale-error reads.
 *
 * success_code is HIP_SUCCESS (0) - passed in rather than hardcoded so
 * this stays testable without pulling in pe_shim.c's Wine-dependent
 * constants. */
int hip_get_last_error_and_reset(int current, int success_code, int *out_new_state);

#endif
