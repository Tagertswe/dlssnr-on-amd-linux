/* Pure, Wine/HIP-independent logic factored out of native.c so it can
 * be unit tested with a plain host compiler - no Wine headers, no
 * ROCm runtime, no cross-compilation needed. */
#ifndef HIP_UNIXLIB_FORWARD_H
#define HIP_UNIXLIB_FORWARD_H

/* Shared "did the real native symbol resolve, and if so what did it
 * return" fallback pattern, for every unix_* dispatch function in
 * native.c whose real HIP call produces an opaque pointer result
 * (event/stream creation, external-memory import/get-mapped-buffer)
 * rather than a plain int - see version_query.c's
 * hip_version_query_apply() for the plain-int equivalent this
 * mirrors.
 *
 * have_symbol is 0 when either the native ROCm/HIP library itself
 * failed to load, or that specific symbol wasn't found in it -
 * native.c's ensure_loaded()/dlsym already collapse those into one
 * "no usable function pointer" case before calling this. Never
 * fabricates a result: when the symbol never resolved, out_ptr is
 * NULL and out_ret is a failure code, matching every other function
 * in this shim's own fallback convention. When the symbol did
 * resolve, forwards its real return values exactly, unmodified. */
void hip_forward_ptr_result(void **out_ptr, int *out_ret,
        int have_symbol, void *real_ptr, int real_ret);

#endif
