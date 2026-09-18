/* Pure, Wine/HIP-independent logic factored out of native.c so it can be
 * unit tested with a plain host compiler - no Wine headers, no ROCm
 * runtime, no cross-compilation needed. */
#ifndef HIP_UNIXLIB_VERSION_QUERY_H
#define HIP_UNIXLIB_VERSION_QUERY_H

/* Decides what unix_driver_get_version()/unix_runtime_get_version()
 * report back to the PE side, given whether the real
 * hipDriverGetVersion/hipRuntimeGetVersion symbol resolved via dlsym
 * and, if it did, what it returned.
 *
 * have_symbol is 0 when either the native ROCm/HIP library itself
 * failed to load, or that specific symbol wasn't found in it -
 * native.c's ensure_loaded()/dlsym already collapse those into one
 * "no usable function pointer" case before calling this. In that case
 * report version 0 with a failure code, matching every other function
 * in this shim's own fallback convention (see e.g.
 * unix_get_device_count()) - never silently claim a version we don't
 * actually know. When the symbol did resolve, forward its real return
 * values exactly, unmodified: this shim must never fabricate a HIP
 * version number, since callers (confirmed via danielblnc's own
 * runtime - see docs/linux-support-spec.md \xc2\xa727e) can and do make
 * real compatibility decisions based on the reported value. */
void hip_version_query_apply(int *out_version, int *out_ret,
        int have_symbol, int real_version, int real_ret);

#endif
