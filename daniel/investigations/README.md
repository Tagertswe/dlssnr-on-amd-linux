# investigations

**Category: experiments/investigations** - standalone, one-off
diagnostic tools that each answer a specific question during the root-
cause chase, run by hand as needed. Not meant to run regularly and not
part of `make test`. For the project's other category - ongoing
regression/correctness tests for the shim's own production code, run
via `make test` on every change - see `daniel/hip-unixlib/`'s
`test_*.c` files (and, once runtime-verified,
`interop_roundtrip_test.c`) instead.

A standalone ROCm runtime capability probe. Not part of the Wine/Proton
shim - a plain Linux diagnostic tool that `dlopen`s the real
`libamdhip64.so` directly (same technique as `daniel/hip-unixlib`) and
exercises real device/memory/stream/event calls, independent of Wine,
Proton, and danielblnc's proprietary runtime entirely.

Needs no HIP compiler, HIP headers, or ROCm dev packages - only the
ROCm runtime itself (`libamdhip64.so.7`) has to be installed, matching
this whole project's "no ROCm dev package" constraint.

## Build and run

```
gcc -Wall -o rocm_probe rocm_probe.c -ldl
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./rocm_probe
```

(Adjust `LD_LIBRARY_PATH` to wherever `libamdhip64.so.7` actually lives
on your system if it's not on the default loader path.)

## What it checks

Real device count/selection, memory allocation, and - the actual point
of this tool - the full stream + event completion-signaling pipeline
(`hipStreamCreateWithFlags`, `hipEventCreateWithFlags`,
`hipEventRecord`, `hipMemcpyAsync` as real GPU DMA-engine work between
two events, `hipStreamSynchronize`, `hipEventSynchronize`,
`hipEventQuery`, `hipEventElapsedTime`), plus the real
`hipDriverGetVersion`/`hipRuntimeGetVersion` numbers.

This isolates whether the runtime's basic completion-signaling
machinery - the exact thing danielblnc's "inline mode" depends on -
works at all on the current GPU/ROCm/kernel/OS combination, without
needing to compile any kernel. See `docs/linux-support-spec.md` §31
for the result this produced and how it was used.

## `fp8_kernel_probe.c` - the fp8 question, settled empirically (ruled out)

`rocm_probe.c` above deliberately avoids needing a compiler. This
second tool goes one step further and answers the fp8 question
directly rather than by inference: it compiles small, original,
clean-room OpenCL C kernels *from source, at runtime*, using AMD's own
`libamd_comgr` code-object-manager library (the same compiler
machinery HIP's `hiprtc` convenience API sits on top of - `hiprtc`
itself isn't packaged on this system, but `libamd-comgr3` is), then
loads and launches the real compiled result via the exact same
`hipModuleLoadData`/`hipModuleGetFunction`/`hipModuleLaunchKernel`
calls `daniel/hip-unixlib/native.c` already uses for danielblnc's own
kernels.

**Result: fp8 genuinely works correctly on this GPU/ROCm/OS
combination** - see `docs/linux-support-spec.md` §33 for the full
account. `kernel_touch` (a trivial fp32-only control) and
`kernel_fp8_decode` (a real `__builtin_amdgcn_cvt_f32_fp8` call,
decoding the well-known e4m3 bit pattern for `1.0`) both compiled,
loaded, and executed correctly - the fp8 kernel producing an exact
`1.0` result. This rules out "this GPU/ROCm doesn't support fp8" as
the explanation for danielblnc's runtime's `"operation not
supported"` error; the real cause is something narrower (see §33d).

### Requires `libamd-comgr-dev` (headers only, exact version match to
### the already-installed `libamd-comgr3` runtime - no driver/runtime
### risk, trivially removable afterward):

```
sudo apt install libamd-comgr-dev
gcc -Wall -o fp8_kernel_probe fp8_kernel_probe.c -ldl -lamd_comgr
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./fp8_kernel_probe
```

**No-install alternative** (used to actually produce the §33 result,
during a remote session with no `sudo` available): `apt-get download
libamd-comgr-dev` fetches the real `.deb` without root; `dpkg-deb -x
<deb> <dir>` extracts it to a plain directory. Point `-I`/`-L` at the
extracted `usr/include`/`usr/lib/x86_64-linux-gnu`, and re-point the
extracted `libamd_comgr.so` symlink at the real, already-installed
`libamd_comgr.so.3` (the extracted one is dangling, since the actual
runtime `.so` ships in the separate `libamd-comgr3` package, already
installed) before linking.

Two real mistakes were found and fixed by actually compiling against
the real header, not by guessing further - see the code's own
comments and `docs/linux-support-spec.md` §33b for both: a
3-vs-2-argument builtin signature error, and (more significantly)
`AMD_COMGR_LANGUAGE_HIP`'s two-target host/device split silently
dropping the raw kernel attribute, fixed by switching to
`AMD_COMGR_LANGUAGE_OPENCL_1_2`'s simpler single-target path.

## `wmma_fp8_probe.c` - the WMMA matrix fp8 follow-up (also ruled out)

`fp8_kernel_probe.c` only tests a scalar fp8 decode instruction.
danielblnc's real, transformer-shaped kernels (identified live -
`docs/linux-support-spec.md` §35b - as `k_swin_var<32, true>`, the
actual kernel observed failing with `"operation not supported"` in
the real game) very likely use a real matrix-multiply-accumulate
(WMMA) fp8 instruction instead. This probe tests that directly, same
technique (comgr, `hipModule*`).

**Result: also rules out fp8/WMMA.** `hipDeviceSynchronize` reports
clean success for a real
`__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12` call - see
`docs/linux-support-spec.md` §36 for the full account, including two
real build obstacles found and fixed by reading the actual compiler
error and, for the builtin's real name (the LLVM-IR-intrinsic-derived
guess doesn't exist as a Clang builtin), by grepping builtin-name
strings directly out of the installed `libamd_comgr.so.3.0.0` binary.
Both fp8 code paths this hardware plausibly uses now confirmed
working - the real cause of the crash is something else entirely, not
yet found.

```
sudo apt install libamd-comgr-dev
gcc -Wall -o wmma_fp8_probe wmma_fp8_probe.c -ldl -lamd_comgr
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./wmma_fp8_probe
```

## `raw_pointer_fault_probe.c` - the real root cause, confirmed

Compiles an original OpenCL C kernel (`kernel_write_raw_ptr`) via
comgr and launches it with a literal invalid GPU address
(`(void*)(uintptr_t)0x100000000ULL`). Reproduces
`HSA_STATUS_ERROR_MEMORY_FAULT` matching the real in-game crash's
fault register value (`0x841050`) exactly. This is the investigation's
decisive result - see `docs/linux-support-spec.md` §42 for the full
account and everything it rules out.

```
sudo apt install libamd-comgr-dev
gcc -Wall -o raw_pointer_fault_probe raw_pointer_fault_probe.c -ldl -lamd_comgr
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./raw_pointer_fault_probe
```

`windows-repro/raw_pointer_fault_repro.cpp` is a portable twin of this
same probe for danielblnc, using plain `hip_runtime.h`/`hipcc` instead
of comgr/dlopen, so it builds directly on his real Windows toolchain.

## `index_overflow_probe.c` - int32 index arithmetic, inconclusive

Attempted to characterize exactly where ordinary int32 flattened-index
arithmetic (`idx = ((z*H+y)*W+x)*C+c`) starts producing invalid
addresses, as a narrower theory of what in danielblnc's real kernels
produces the invalid pointer `raw_pointer_fault_probe.c` shows crashes
unconditionally. Result was inconclusive by design flaw (buffer too
small to isolate "overflow" from "merely far out of bounds") - reported
honestly as such in `docs/linux-support-spec.md` §48 rather than
overclaiming a clean answer.

## `readback_coherency_probe.c` - D3D12 READBACK-heap memory-visibility test (ruled out)

A standalone D3D12 program (no HIP/shim dependency at all) built to
test one specific theory behind `docs/linux-support-spec.md` §50e's
deterministic capture-drop pattern: is a CPU-visible read of a
GPU-written READBACK-heap buffer ever stale under this system's real
vkd3d-proton/Wine/RADV stack, even after a real, blocking
`ID3D12Fence` wait confirms the GPU write is complete? 200 real
write/copy/fence-wait/read cycles, run against the real patched Proton
prefix.

**Result: ruled out.** 200/200 passed, zero mismatches - every
fence-confirmed GPU write was immediately visible to the CPU. See
`docs/linux-support-spec.md` §51 for the full account, including why
this theory looked at least as plausible as command-list reordering
before being tested.

```
winegcc -o readback_coherency_probe.exe readback_coherency_probe.c \
    -b x86_64-windows --no-default-config \
    -L/usr/lib/x86_64-linux-gnu/wine/x86_64-windows -ld3d12
```

Run it the same way `interop_roundtrip_test.c` documents (drop into
the real game's `bin/x64/` and run via the real, already-working
Proton prefix - memory-capped and wall-clock-limited, per this
project's standing safety convention for anything that creates a real
D3D12 device).

## `submission_timing_probe.c` - back-to-back submission order/timing (inconclusive, not a clear rule-out)

With memory-coherency ruled out, tests the remaining candidate for
§50e's capture-drop pattern directly: 8 independent command lists
submitted back-to-back with no wait between them, spin-polled
immediately after (no blocking fence wait) to check both completion
order and per-slot latency against a 1-second budget.

**Result: all 8 resolved in order, near-instantly (~0.4ms).** Neither
reordering nor a timeout reproduced - but only for this simplified,
isolated, low-contention pattern (a plain buffer copy, no concurrent
GPU load, single queue, single thread). Real differences from
danielblnc's actual usage (a real compute/pixel shader wait dispatch,
real concurrent rendering load, possible multi-queue interaction,
possible CPU-thread-side polling delay) mean this is a real negative
result for the idealized case, not a clearance of vkd3d-proton
generally. See `docs/linux-support-spec.md` §52 for the full account
and what a more faithful next probe would need.

```
winegcc -o submission_timing_probe.exe submission_timing_probe.c \
    -b x86_64-windows --no-default-config \
    -L/usr/lib/x86_64-linux-gnu/wine/x86_64-windows -ld3d12
```

## `snapshot_gate.c` / `faithful_submission_probe.c` - a more realistic submission model (PASS)

`submission_timing_probe.c` above tested two idealized extremes: always
blocking after every submission, or never blocking at all. Real games do
neither - they use a ring buffer of command-list slots, reused round-
robin, waiting on a slot's fence only when it's about to be reused. This
probe tests that actual pattern (an 8-slot ring, 40 total submissions,
5 full wraps), using real ring-buffer/fence logic and a real producer-
ordering check adapted from
[guentra/dlss5-amd-hip-linux](https://github.com/guentra/dlss5-amd-hip-linux)
(MIT licensed - see `THIRD-PARTY.md`), rather than this project's own
earlier, more naive synchronization code.

**Result: PASS.** All 40 submissions, all 40 readbacks correct, zero
gate-check failures, after a real final `Flush()`. See
`docs/linux-support-spec.md` §61 for the full account.

```
gcc -Wall -o test_snapshot_gate snapshot_gate.c test_snapshot_gate.c && ./test_snapshot_gate

winegcc -o faithful_submission_probe.exe faithful_submission_probe.c \
    -b x86_64-windows --no-default-config \
    -L/usr/lib/x86_64-linux-gnu/wine/x86_64-windows -ld3d12
```
