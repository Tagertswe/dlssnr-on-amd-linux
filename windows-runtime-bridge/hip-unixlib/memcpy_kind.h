/* Pure, Wine/HIP-independent logic factored out of pe_shim.c so it can
 * be unit tested with a plain host compiler - no Wine headers, no
 * ROCm runtime, no cross-compilation needed. */
#ifndef HIP_UNIXLIB_MEMCPY_KIND_H
#define HIP_UNIXLIB_MEMCPY_KIND_H

#define HIP_MEMCPY_HOST_TO_HOST 0
#define HIP_MEMCPY_HOST_TO_DEVICE 1
#define HIP_MEMCPY_DEVICE_TO_HOST 2
#define HIP_MEMCPY_DEVICE_TO_DEVICE 3
#define HIP_MEMCPY_DEFAULT 4

/* Whether pe_shim.c's memcpy_forward() should let this hipMemcpyKind
 * value through to the real hipMemcpy/hipMemcpyAsync, rather than
 * rejecting it locally with HIP_ERROR_NOT_SUPPORTED before it ever
 * reaches native.c.
 *
 * Found via docs/linux-support-spec.md's live crash-log investigation:
 * this gate used to accept only HOST_TO_DEVICE/DEVICE_TO_HOST, so any
 * real hipMemcpyDeviceToDevice (kind 3) call - needed for GPU-side
 * buffer-to-buffer copies within the pipeline - was rejected here,
 * never reaching native.c's unix_memcpy (which already forwards
 * `kind` to the real HIP runtime unmodified and needs no change).
 * That single rejection set the shim's sticky last_error, which every
 * later hipGetLastError() check in the same frame's job then echoed
 * back as "operation not supported" for every named pipeline stage -
 * this, not any GPU/ROCm/fp8 limitation, was the real cause. */
int hip_memcpy_kind_supported(int kind);

#endif
