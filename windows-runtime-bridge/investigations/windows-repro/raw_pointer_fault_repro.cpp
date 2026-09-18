// Raw HIP pointer fault - portable repro, Windows/HIP SDK compatible.
//
// Standalone HIP program (no Linux dependency at all: no dlopen, no
// comgr, no OpenCL C - just the same HIP runtime API and hipcc
// toolchain a real HIP project already uses) that reproduces a real
// GPU page fault confirmed on Linux/ROCm - see
// docs/linux-support-spec.md for the full investigation
// (dlssnr-on-amd-linux, sections §37-§44) this repro is the tail end
// of.
//
// What it does: launches one trivial control kernel (writes through a
// real hipMalloc'd pointer - should always succeed), then launches
// the exact same kernel again with the pointer replaced by the
// literal address 0x100000000 (2^32/4GB) - the real fault address
// captured from a live crash on Linux (a genuine AMD kernel-driver
// GPU page fault, GCVM_L2_PROTECTION_FAULT_STATUS 0x841050,
// reproduced standalone and on demand).
//
// No proprietary code: this is original, minimal, clean-room test
// code written for this diagnostic alone. It does not contain,
// reference, or derive from any DLSS-NR-on-AMD kernel source.
//
// Build (Windows, HIP SDK installed - same toolchain used to build
// DLSS-NR-on-AMD itself):
//
//   hipcc raw_pointer_fault_repro.cpp -o raw_pointer_fault_repro.exe
//   raw_pointer_fault_repro.exe
//
// Build (Linux, ROCm installed):
//
//   hipcc raw_pointer_fault_repro.cpp -o raw_pointer_fault_repro
//   ./raw_pointer_fault_repro

#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstdint>

__global__ void kernel_write_raw_ptr(float *raw_ptr)
{
    *raw_ptr = 42.0f;
}

static void check(const char *step, hipError_t err)
{
    printf("  %-45s -> %d (%s)\n", step, (int)err, hipGetErrorString(err));
    fflush(stdout);
}

int main()
{
    printf("=== raw HIP pointer fault repro ===\n\n");
    printf("Target address: 0x100000000 - the real fault address captured\n");
    printf("live on Linux/ROCm (see docs/linux-support-spec.md). This is\n");
    printf("deliberately NOT a real allocation - the point is to see what a\n");
    printf("raw, unchecked HIP pointer write to exactly this address does\n");
    printf("on your own Windows/AMD driver stack.\n\n");

    int count = 0;
    check("hipGetDeviceCount", hipGetDeviceCount(&count));
    if (count < 1) { printf("No device, aborting.\n"); return 1; }
    check("hipSetDevice(0)", hipSetDevice(0));

    printf("\n--- control: real allocated buffer ---\n");
    float *dev_out = nullptr;
    check("hipMalloc(dev_out, 4 bytes)", hipMalloc(&dev_out, sizeof(float)));
    kernel_write_raw_ptr<<<1, 1>>>(dev_out);
    check("hipDeviceSynchronize (control)", hipDeviceSynchronize());
    float result = -1.0f;
    check("hipMemcpy(result back)", hipMemcpy(&result, dev_out, sizeof(float), hipMemcpyDeviceToHost));
    printf("  result: %f (expect 42.0)\n", result);

    printf("\n--- kernel_write_raw_ptr(0x100000000) - THE ACTUAL QUESTION ---\n");
    float *bad_ptr = reinterpret_cast<float *>(static_cast<uintptr_t>(0x100000000ULL));
    kernel_write_raw_ptr<<<1, 1>>>(bad_ptr);
    printf("  calling hipDeviceSynchronize now - this is the real test moment...\n");
    fflush(stdout);
    check("hipDeviceSynchronize [THE ANSWER IS HERE]", hipDeviceSynchronize());
    printf("  (if you see this line, the process did not crash/hang)\n");

    check("hipFree(dev_out)", hipFree(dev_out));

    printf("\n=== repro complete ===\n");
    printf("On Linux/ROCm this reliably reproduces the exact real crash: a\n");
    printf("hardware page fault (HSA_STATUS_ERROR_MEMORY_FAULT / a real\n");
    printf("amdgpu kernel-driver GPU page fault, GCVM_L2_PROTECTION_FAULT_\n");
    printf("STATUS 0x841050) matching what live gameplay hits. Whatever this\n");
    printf("run reports (a clean error, a crash, a hang, or silent success)\n");
    printf("is real data about whether the same raw-pointer access pattern\n");
    printf("faults on your own Windows/driver stack the same way.\n");
    return 0;
}
