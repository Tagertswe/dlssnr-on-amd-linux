# DLSS-NR-on-AMD / Linux-Proton project

## What this project is

Getting NVIDIA's DLSS 5 Neural Rendering ("DLSS-NR") working on Linux
via Proton, on AMD GPUs, using danielblnc/DLSS-NR-on-AMD's proprietary
standalone Windows runtime (a `version.dll` that DLL-hijacks the real
game). Tested against Cyberpunk 2077 on an AMD Radeon RX 9070 XT
(RDNA4, gfx1201).

Daniel's runtime expects NVIDIA's real `amdhip64_7.dll` (AMD's own HIP
compute runtime) to be present and calls into it for GPU compute and
D3D12↔HIP memory interop. This project builds and patches everything
needed to satisfy that under Wine/Proton, using real ROCm on the host
- no proprietary code, weights, or logic from NVIDIA or danielblnc is
included anywhere in this repo. Never bundle danielblnc's or NVIDIA's
proprietary binaries/weights into the repo, and **never byte-patch or
otherwise produce a modified/derivative copy of danielblnc's actual
compiled binary**, even locally, even un-committed. This shim only
ever loads an unmodified copy of his runtime (via the DLL-hijack
mechanism his own installer sets up) and forwards calls through to
real ROCm - it never opens, writes to, or alters his binary's bytes in
any way. See `docs/linux-support-spec.md`'s research into
guentra/DLSS-NR-on-AMD-Linux (a different, unrelated Linux port) for
why this distinction matters: their tool patches an embedded shader
inside danielblnc's actual compiled DLL and produces a modified
derivative of it as its real output - a materially different (and
apparently license-relevant - guentra cited exactly this as why he
stopped development) act than pure interop with an unmodified binary.

**The authoritative, detailed, session-by-session log of this entire
investigation is `docs/linux-support-spec.md`.** It is long (28+
numbered sections as of this writing) and is the real source of truth
for exact findings, exact error messages, and exact reasoning - this
file is a map to it, not a replacement for it. Read the most recent
sections first when picking this project back up.

## Repo layout

- **`windows-runtime-bridge/hip-unixlib/`** - the active, current implementation. A
  Wine [unixlib](https://gitlab.winehq.org/wine/wine/-/wikis/Unix-Library-Guide)
  module: a PE-side shim (`pe_shim.c`, cross-compiled to
  `amdhip64_7.dll`) paired with native Linux code (`native.c`, compiled
  to `amdhip64_7.so`) that runs in the *same process* as the Windows
  side under Wine - no serialization, no IPC, just real pointers shared
  directly. `native.c` `dlopen`s the host's real ROCm/HIP runtime and
  forwards real calls through it. See "Current status" below for what's
  real vs. still stubbed.
- **`windows-runtime-bridge/hip-bridge/`** and **`windows-runtime-bridge/hip-stub/`** - superseded
  earlier designs (a socket-IPC daemon, and a Rust port), kept for
  reference. Not where new work happens.
- **`windows/`** - legacy OptiScaler-based Windows approach, explored
  and set aside (a real conflict risk when combined with danielblnc's
  runtime - both hook the same D3D12 surface). Not where new work
  happens.
- **`windows-runtime-bridge/investigations/`** - standalone Linux diagnostics that talk to
  the real ROCm runtime directly (`dlopen`, no Wine/Proton/game
  involved) - `rocm_probe.c` (no compiler needed, run any time) and
  `fp8_kernel_probe.c` (needs `libamd-comgr-dev` installed once, then
  compiles and runs a real fp8 test kernel at runtime). See its own
  README for exact commands.
- **`windows-runtime-bridge/backends/`** - scaffolding (docs + `switch_backend.sh`) for
  switching between this project's own injection mechanism and
  `guentra/dlss5-amd-hip-linux`'s (a materially different, MIT-licensed,
  self-contained alternative - see `docs/linux-support-spec.md` §61-§62).
  Documentation and a dry-run-tested switch script only; `guentra`'s
  actual release binaries have not been fetched or deployed anywhere.
- **`docs/linux-support-spec.md`** - the full investigation log. Every
  finding, every patch, every dead end, every live test result, in
  order. This is where you look for "why" and "exactly what happened."

## Current status (as of the session that added this file)

**Latest update (2026-09-16, see `docs/linux-support-spec.md` §50):**
the "operation not supported" / broken-pipeline-output problem below
is now fixed, via a chain of four real bugs found and fixed live in
one session - a stale `.dll` (out of sync with the `.so`), silent
`hipMemset`/`hipMemsetAsync`/`hipMemcpyToSymbol` stubs, a missing
`hipGetLastError()` reset-on-read (the real root cause of "one failure
poisons every later check"), and a missing `__hipRegisterVar`/
`hipMemcpyToSymbol` device-symbol table (`g_e4m3_lut`). Confirmed live:
zero HIP/GPU errors, real computed output (`encoded mean 0.997` vs.
`0.000` before). **This is unrelated to the raw-HIP-pointer GPU fault
in §37-§42 below** - that's a different code path, not touched or
disproven by tonight's fixes; no crash happened in tonight's testing,
but every session was short (~15-40s), nowhere near enough to treat
that as evidence either way. A new, separate, deterministic D3D12
capture-drop issue was found instead (§50e) - not a HIP-path bug.

**Further update, same session (§53-§56):** `dmesg` revealed every
single test run that night had silently triggered a real kernel-level
AMDGPU `gfx_0.0.0` ring timeout + hardware ring reset - completely
invisible to userspace/our own logs, caused by danielblnc's GPU-side
spin-wait shader. **Real, verified fix: `CpuWait=1` in
`dlssnr_on_amd.ini`** (a real ini key in danielblnc's own runtime,
found via `strings` on the extracted DLL) eliminates the ring hang
entirely, confirmed via `dmesg` showing zero timeouts under it versus
every other run. This is a genuine, zero-code, keep-it-on mitigation.
It does **not** fix §50e's capture-drop pattern, though - `dmesg`
proved that's a real, separate issue that only ever coincided with the
ring hang, not the same mechanism. Capture-drop remains open; ranked
candidates and next steps in §52/§56.

**Confirmed working, live, in the real game:**
- The root cause of `hipImportExternalMemory` always receiving a
  null/zero handle - a structurally broken D3D12 shared-handle path
  under Wine - is diagnosed and fixed. Real, non-zero Linux file
  descriptors now flow from the game's D3D12 layer into Daniel's
  runtime, confirmed via live log output during real gameplay.
- The fix lives in two places **outside this repo** (see "External
  patches" below): a `vkd3d-proton` patch (opt-in
  `VKD3D_CONFIG=external_memory_fd`) and a one-line Wine `winevulkan`
  patch. Both are necessary together.
- `windows-runtime-bridge/hip-unixlib`'s own bridge for consuming that fd was
  completed and confirmed live this session: `hipImportExternalMemory`,
  `hipExternalMemoryGetMappedBuffer`, `hipDestroyExternalMemory`, and
  the HIP event/stream functions (`hipEventCreate(WithFlags)`,
  `hipEventRecord/Synchronize/Query/ElapsedTime`,
  `hipStreamCreateWithFlags/Synchronize`) now all forward to the real
  HIP library instead of being stubs that always reported "not
  supported." Before this fix, `hipImportExternalMemory` logged
  every call and then *always* returned failure regardless of input -
  the real fd from vkd3d-proton's export fix was being thrown away
  every time.
- Fixing that also resolved §28's "predication buffer unavailable"
  gap outright: the missing real event/stream bridge *was* that
  blocker. Confirmed live - `"inline flag check: ... reached the GPU
  wait after 0 iterations"` and real, sustained dispatch counts
  (`dispatches 975, 1.00/frame`) - the first real neural-pass dispatch
  in this project's history. See `docs/linux-support-spec.md` §29b.

**A real shim bug was found and fixed along the way, but it was NOT
the cause of the crash - corrected after re-testing live.**
`pe_shim.c`'s `hipMemcpy` forwarding only ever allowed
`HIP_MEMCPY_HOST_TO_DEVICE`/`HIP_MEMCPY_DEVICE_TO_HOST` through - a
real `hipMemcpyDeviceToDevice` call (kind `3`) was rejected locally
before ever reaching `native.c`/real HIP. This is a genuine bug and
stays fixed (`windows-runtime-bridge/hip-unixlib/memcpy_kind.h`/`.c`,
`hip_memcpy_kind_supported()`, covered by `test_memcpy_kind.c`;
`pe_shim.c`'s gate now accepts the full real 0-4 `hipMemcpyKind`
range). But two live re-tests after deploying it - the second with
lingering `wine`/`steam`/`cyberpunk` processes explicitly confirmed
absent first, ruling out a stale-DLL-cache explanation - still
crashed the same way. Isolating exactly the new log lines from that
clean run shows the real first `801` has nothing logged between a
successful `hipLaunchKernel` and `hipGetLastError()` - no memcpy call
anywhere nearby. The memcpy fix was a red herring for this specific
crash, found by proximity in the log rather than strict adjacency
checking. See `docs/linux-support-spec.md` §34 (the original,
incorrect attribution) and §35 (the correction).

**The real failing kernel is now identified.** Demangling its symbol
from the log's `__hipRegisterFunction` call:
`_Z10k_swin_varILi32ELb1EEv9VarParams` -> `void k_swin_var<32,
true>(VarParams)` - a Swin-transformer variance kernel. It fails
(`hipGetLastError() -> 801` / `"operation not supported"`) on every
single launch in the crash log, immediately after `hipLaunchKernel`
itself reports success (launches are async - the real GPU-side
failure surfaces later, at the error check). This is a genuine
deferred/runtime execution failure inside the kernel, not a shim gate
rejecting something before it reaches real HIP.

**The earlier leading theory (basic fp8 support) was ruled out first,
by direct test, not inference**, before any of the above.
`windows-runtime-bridge/investigations/fp8_kernel_probe.c` compiles a real fp8 kernel
from source at runtime (via `libamd_comgr`) and runs it through the
exact same `hipModule*` calls this shim uses - it executed correctly,
producing a byte-exact correct result from a real
`__builtin_amdgcn_cvt_f32_fp8` decode instruction on this exact
GPU/ROCm/OS combination. See `docs/linux-support-spec.md` §33. This
rules out "fp8 doesn't work on this hardware at all," but not a more
complex fp8 *matrix* (WMMA) usage specifically - see below.

**The WMMA fp8 matrix theory is now also ruled out.**
`windows-runtime-bridge/investigations/wmma_fp8_probe.c` (new) compiles and runs a real
`__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12` matrix-
multiply-accumulate instruction (the real Clang builtin name, found
by grepping strings out of the installed `libamd_comgr.so` itself -
the LLVM-IR-intrinsic-derived guess didn't exist as a builtin) -
`hipDeviceSynchronize` reports clean success, same as the fp32
control. Both fp8 code paths this hardware plausibly uses - scalar
decode (§33) and matrix WMMA (§36) - are now confirmed to execute
without error. fp8/WMMA support is no longer a credible explanation
in any form.

**The real root cause is now found and confirmed at the kernel level -
this is the investigation's terminus.** Every crash/freeze this
project chased through §29-§36 was never a HIP error, shim bug, or
fp8/WMMA instruction-support gap. It is a real, repeatable AMD GPU
hardware/driver-level ring hang:

```
amdgpu 0000:28:00.0: ring gfx_0.0.0 timeout, signaled seq=N, emitted seq=N+3
amdgpu 0000:28:00.0: Starting gfx_0.0.0 ring reset
amdgpu 0000:28:00.0: Ring gfx_0.0.0 reset succeeded
amdgpu 0000:28:00.0: [drm] device wedged, but recovered through reset
```

Confirmed directly in `journalctl -k` - four occurrences in one test
session, every one showing an identical gap of exactly 3 between the
ring's last-signaled and last-emitted sequence numbers. The kernel's
own timeout-detection-and-recovery (TDR) fires, resets the `gfx_0.0.0`
ring (successfully, every time - not a catastrophic crash), and that
reset kills every Vulkan/D3D12 context sharing the GPU, which is what
actually produces every downstream symptom: `VK_ERROR_DEVICE_LOST` and
RADV's "context is lost, this context is innocent" (meaning something
*else* wedged it), the D3D12 device-removed state, HIP's honest
`hipGetLastError() -> 801` reports once the device is gone, danielblnc's
own `"its capture never landed"` diagnostic, and finally the frozen
picture itself (no app in this stack has real device-lost recovery).

Found by extending `hipDeviceSynchronize` (previously unlogged on
either side of the shim - a real blind spot) plus
`hipEventSynchronize`/`hipStreamSynchronize`/`hipMemcpy` with entry+exit
logging, ruling out all four as the hang site, then setting
`VKD3D_DEBUG=trace` (vkd3d-proton's own real trace env var) in the
Steam launch options to see the Vulkan/D3D12 layer directly.

§34's memcpy-kind fix and §36's WMMA fp8 test were both real, correct
findings in their own right (and stay fixed/kept) - neither was ever
the actual cause of any crash. §36's isolated single-work-item WMMA
test proved the instruction *family* works; it says nothing about
whether the same instruction at real production scale, with real data
dependencies, can wedge the ring - which is now confirmed to happen.

**§38 answers *what* wedges the ring, from a real captured kernel
devcoredump - not inference.** A `sudo`-run polling script (watching
`/sys/class/drm/card*/device/devcoredump/data`, started *before* the
next launch so it caught the file within its ~2-minute window)
grabbed the coredump from a live ring-timeout event. AMD's
devcoredump format turned out to be plain, human-readable ASCII (not
opaque binary needing vendor tools) and states the real cause
directly:

```
[gfxhub] Page fault observed
Faulty page starting at address: 0x0000000100000000
Protection fault status register: 0x841050
```

`0x100000000` is exactly 2^32 (4GB), to the byte - the textbook
signature of a **32-bit integer overflow/pointer-truncation bug**:
some GPU-side address or byte-offset computation used 32-bit
arithmetic where a real 64-bit value was needed, and wrapped exactly
at the 4GB boundary. This is the actual, hardware-confirmed root
cause; everything in the paragraph above (ring reset, lost Vulkan
context, device removal, frozen picture) is a real, correct
*consequence* of this one real page fault, not a separate mystery.
A concrete supporting data point: summed real `hipMalloc` traffic this
session totals ~8.2GB across 540 calls against this GPU's real 16GB
VRAM - confirming the working set genuinely operates at a scale where
a buffer's GPU virtual address plausibly lands near/past 4GB, exactly
where this bug class first manifests.

**Conclusion:** this is very likely a genuine bug in danielblnc's own
compute kernels (a 32-bit address/offset computation that doesn't
hold up for some real GPU virtual address) - not a Linux/Wine/Proton/
ROCm-driver problem, and not anything this project's own shim causes
or can fix. It may be latent on Windows too, simply harder to trigger
there depending on allocation-layout/timing differences under the
native Windows HIP stack.

**A real mitigation was tried from this project's own side and ruled
out (§40).** Built and live-tested `DLSSNR_VRAM_CAP_BYTES` (an opt-in
cap on this pipeline's total outstanding real GPU allocation, rejecting
over-cap `hipMalloc` calls with a real `hipErrorOutOfMemory` -
`windows-runtime-bridge/hip-unixlib/alloc_cap.h`/`.c`, unit tested). Live-tested at a
3GiB cap: **the ring hang happened again anyway**, with real
outstanding allocation at fault time only ~720MB - nowhere near the
cap or the 4GB boundary. This rules out "total live GPU memory
approaching 4GB" as the trigger; the bug is more likely about *which
GPU virtual address* a specific buffer happens to land at (VM-manager
placement, not data volume), which this project can't reliably control
from userspace. A userspace-side mitigation is therefore not a
reliable path.

**§41-§42: definitively confirmed, standalone, not just inferred.**
Two real, buildable, no-game-needed tests settle this:

- §41: a real standalone `vkd3d-proton` test
  (`tests/d3d12_external_memory_fd.c`,
  `test_external_memory_fd_shared_buffer_far_offset_access`) proved a
  genuinely out-of-bounds *D3D12 UAV* access at exactly this fault
  address is safely clamped by the driver (`GetDeviceRemovedReason()
  -> S_OK`) - ruling out "any access at this address" as sufficient on
  its own, and ruling out this project's own vkd3d-proton patch as the
  cause (its diff has zero address/offset arithmetic at all).
- §42: `windows-runtime-bridge/investigations/raw_pointer_fault_probe.c` - a standalone
  HIP probe compiling a real kernel that writes through a *raw
  pointer* (no descriptor bounds-checking at all, exactly how HIP
  kernels normally address memory) set to the literal address
  `0x100000000`. **This reproduces the exact real crash on demand**:
  `HSA_STATUS_ERROR_MEMORY_FAULT`, and the kernel journal's own
  `GCVM_L2_PROTECTION_FAULT_STATUS: 0x00841050` is byte-for-byte
  identical to the real game crash's fault register value from §38c.
  Confirmed, not inferred.

**Conclusion, final:** a raw HIP pointer write to an invalid GPU
virtual address is the real, hardware-confirmed mechanism behind
every crash this whole investigation chased. This project's own shim,
its vkd3d-proton patch, and the ROCm/Wine/Proton stack are all cleared
- conclusively, by direct standalone reproduction, not by elimination.
The remaining unknown - *which* computation inside danielblnc's own
closed-source kernels produces the bad `0x100000000` value - needs his
own kernel source to find; this project has reverse-engineered nothing
of his to get this far (see §7's licensing stance).

**§43/§44: the ROCm version-gap theory - live-tested against the real
game, and also ruled out.** Actually upgraded this system to
`hip-runtime-amd 7.2.53211.70204-93~24.04` (matching danielblnc's own
documented version requirement exactly, the first time this project
has run his exact expected version) and confirmed it was genuinely
active in the real game process (`hipDriverGetVersion() ->
version=70253211`). Getting the shim to actually pick up the new
library needed a real fix of its own - AMD's installer keeps versioned
ROCm releases under `/opt/rocm-<version>/lib`, and while `/opt/rocm`
is normally a stable symlink to the current one, that symlink chain
(`/opt/rocm -> /etc/alternatives/rocm -> /opt/rocm-7.2.4`) doesn't
resolve inside the game's own sandboxed process at all - pressure-
vessel's `PRESSURE_VESSEL_FILESYSTEMS_RO`/`RW` cannot bind anything
under `/etc`, confirmed live. Fixed by having `native.c` target the
real versioned path directly.

**The crash happened anyway, with the identical signature** - same
`ring gfx_0.0.0 timeout`, same exact "gap of 3" between signaled/
emitted sequence numbers seen in every prior occurrence, same clean
auto-recovery. The ROCm version gap is not a contributing factor;
matching danielblnc's own documented requirement exactly changes
nothing. Combined with §42's standalone reproduction, every
environmental theory this investigation raised is now closed out -
what's left is purely a bug inside danielblnc's own kernel logic.

This also answers "why does this work on Windows for other people":
almost certainly not a ROCm-version explanation specifically, since
matching his exact version made no difference here. More likely
candidates remain environment-independent-of-ROCm-version ones -
Windows' WDDM handling a lost device very differently than Linux's
`amdgpu` driver (masking the same bug as a corrupted frame or brief
hitch instead of a hard crash), a hardware-generation-specific
trigger (this is an RDNA4 card), or the D3D12↔HIP interop path itself
(Linux-only regardless of ROCm version - it doesn't exist on Windows
at all).

**Way forward:** hand danielblnc the complete package - the
devcoredump (§38), the exact fault register value confirmed
reproducible in isolation (§42a), the standalone repro itself
(`windows-runtime-bridge/investigations/raw_pointer_fault_probe.c`, open source, no
proprietary code, a five-minute run against his own source to find the
actual bad address computation), and confirmation that his own
documented ROCm version requirement doesn't change the outcome (§44).
No further blind mitigation from this project's side is likely to be
productive from here.

See `docs/linux-support-spec.md` §29c, §30, §33, §34, §35, §36, §37,
§38, §39, §40, §41, §42, §43, and **§44 (the version-gap theory,
live-tested and ruled out)** for full detail.

**Known-real usage pattern** (do not treat as a bug if rediscovered):
Daniel's mod does not appear in Cyberpunk's native settings menu.
Leave the game's own upscaler set to FSR; DLSS-NR is controlled
entirely through the mod's own in-game overlay (press **End** in
game). FSR must be set to an actual scaling preset (Quality/Balanced/
Performance), not "Native AA" - Native AA renders at full resolution
and gives the neural pass no upscaling work to do at all.

## External patches (not in this repo)

Two patches live in separate local clones, not committed here (they
patch other open-source projects, not this one):

- **`~/repos/vkd3d-proton`** - `HansKristian-Work/vkd3d-proton`, opt-in
  `VKD3D_CONFIG=external_memory_fd` patch across `device.c`,
  `resource.c`, `heap.c`, `memory.c` + a real test suite
  (`tests/d3d12_external_memory_fd.c`, 6 tests, run via
  `VKD3D_TEST_MATCH=<name> wine tests/d3d12.exe` after building with
  `-Denable_tests=true`).
- **`~/repos/wine-proton`** - `ValveSoftware/wine`, one-line
  `dlls/winevulkan/make_vulkan` fix exposing `VK_KHR_external_memory_fd`
  through Wine's own Vulkan thunks (Wine's generator had it scaffolded
  but deliberately hidden).
- Both patches are also applied inside a full clone of
  `ValveSoftware/Proton` at `/mnt/gaming-linux2/proton-build/Proton`,
  built through Valve's own official Docker/Podman pipeline (`make
  redist`) rather than a hand-reconstructed toolchain - this is the
  byte-compatible build that was actually validated live. The built
  result is installed as a separate, clearly-named Steam compatibility
  tool (`fdtest-11.0-2c` in `~/.steam/root/compatibilitytools.d/`), independent
  of the user's real Proton 11.0 install.
- Porting either patch to a different vkd3d-proton/wine commit than
  what's currently checked out may need real adaptation, not a blind
  `git apply` - see `docs/linux-support-spec.md` §27b for a worked
  example (vkd3d-proton's config-flag system was refactored between
  commits).

## How this has been tested

**Unit tests (`windows-runtime-bridge/hip-unixlib`, run via `make test`):** pure,
Wine/HIP-independent logic is factored out of `native.c`/`pe_shim.c`
into small standalone modules specifically so it's testable with a
plain host compiler - no Wine, no ROCm, no cross-compilation:
- `registry.c` - fat-binary/kernel-function token registry.
- `version_query.c` - the "did the real symbol resolve, forward or
  fail safely" decision for scalar-returning HIP calls.
- `ext_mem.c` - extracting a real fd from danielblnc's
  `hipExternalMemoryHandleDesc`, including the real ABI struct shared
  between the PE and native sides.
- `hip_forward.c` - the same forward-or-fail-safely decision as
  `version_query.c`, generalized for HIP calls that return an opaque
  pointer (events, streams, external memory) instead of a scalar.

Everything that actually touches Wine or the real HIP library (the
bulk of `native.c`/`pe_shim.c`) is **not** unit tested - it can't be,
without real hardware and a real Wine environment - and is instead
validated live, the way below.

**Live validation, in increasing order of realism:**
1. `windows-runtime-bridge/hip-unixlib`'s own test target (`make test`) - fast, no
   Wine/game needed, catches regressions in the pure logic.
2. The vkd3d-proton test suite (`tests/d3d12_external_memory_fd.c`)
   run standalone against a built `d3d12.dll`/`d3d12core.dll`, via
   `wine tests/d3d12.exe` with `VKD3D_TEST_MATCH` - fast, isolated,
   no game required, run **memory-capped**
   (`systemd-run --scope --user -p MemoryMax=2G -p MemoryHigh=1536M`)
   given this project's history of real system-memory pressure during
   live testing (see `docs/linux-support-spec.md` for the incident).
3. The real game, launched through Steam with the `fdtest-11.0-2c`
   compatibility tool selected (Cyberpunk → Properties → Compatibility)
   and `VKD3D_CONFIG=external_memory_fd` added to Launch Options. Real
   game state is inspected via:
   - `~/steam-<AppID>.log` (set `PROTON_LOG=1` and, for a manual
     `proton run` invocation, `SteamGameId=<AppID>` as an env var -
     Steam sets this automatically for a normal launch) - Wine's own
     trace log, useful for confirming exactly which DLL/path loaded
     and for full crash backtraces.
   - `<game>/bin/x64/amdhip64_7_unixlib_pe.log` - this shim's own log,
     one line per real HIP call, including the exact fd/size/flags
     values for every `hipImportExternalMemory` call.
   - `<game>/bin/x64/dlssnr_on_amd.log` - danielblnc's own runtime log
     (frame counts, dispatch counts, route, and rich free-text
     diagnostics that are often the most direct source of truth about
     what's actually blocking - read them literally before
     theorizing).
4. Always cap memory and prefer a hard wall-clock timeout
   (`timeout --kill-after=Ns`) when testing anything that creates a
   real Vulkan device - this project has a real, confirmed history of
   a crash there causing genuine system-wide memory pressure.

## Standing constraints worth knowing before touching this project

- Never bundle danielblnc's or NVIDIA's proprietary binaries/weights
  into this repo (see "What this project is" above).
- Hold off on any public release or new repo creation until danielblnc
  responds to prior outreach about this project.
- The user's real Proton 11.0 install and real Cyberpunk installation
  are live, in-use systems - prefer disposable copies for anything
  destructive or exploratory; when testing directly against the real
  install is genuinely necessary, get explicit confirmation first.
