# Draft message to danielblnc

*Not sent yet - review and edit before sending. Written for a GitHub
issue on `danielblnc/DLSS-NR-on-AMD` or a direct message, whichever
you'd rather use.*

---

Hey Daniel,

I've been getting DLSS-NR-on-AMD running under Linux/Proton (a HIP
compatibility shim - no proprietary code or weights of yours involved,
just glue code and patches to Wine/vkd3d-proton). Found three separate
real issues along the way - one I have a full standalone repro for,
one I think you may already partly know about, and one I'm still
chasing myself. Writing all three up properly rather than sending
separate half-finished reports.

## tl;dr

**Main finding**: one of your compute kernels writes through a pointer
that resolves to an invalid GPU address - exactly `0x100000000` (2^32
/ 4GB), which strongly suggests a 32-bit truncation or overflow
somewhere in an address/offset/index computation. I've ruled out
everything on my own side that could plausibly cause this. Attached is
a small, standalone HIP program that reproduces the exact same
hardware fault on demand, independent of any of your code - you can
run it on your own Windows machine with your existing HIP SDK, no
Linux needed. Two smaller, separate findings follow further down -
a real GPU ring-hang mechanism (Linux-specific, and I think your own
`SpinDraw`/`CpuWait` settings already address it) and a still-open
pattern in your capture-check I don't have a root cause for yet.

## Context: when this happens

Real gameplay, not a synthetic benchmark - Cyberpunk 2077, AMD Radeon
RX 9070 XT (RDNA4), DLSS-NR active through your mod's own overlay,
FSR set to an actual scaling preset (so the neural pass has real
upscaling work to do). Happens under normal play, not tied to any one
specific scene or action I've been able to pin down - it's
intermittent but frequent enough to make a session unplayable within
a few minutes most of the time.

## The chain: what actually happens, step by step

1. Your runtime dispatches a real compute kernel (identified by
   demangling its registered symbol:
   `_Z10k_swin_varILi32ELb1EEv9VarParams` ->
   `void k_swin_var<32, true>(VarParams)`, a Swin-transformer variance
   kernel - one of several kernels in the pipeline, not necessarily
   the only one affected).
2. The launch itself reports success immediately (`hipLaunchKernel`
   returns 0) - this is expected, kernel launches are asynchronous.
3. Somewhere during actual GPU-side execution, a raw pointer write
   dereferences an invalid address. The GPU's memory-management
   hardware (`gfxhub`) detects this as a real page fault.
4. The AMD kernel driver's timeout-detection-and-recovery (TDR) fires
   because the GPU command ring stops making progress, forces a ring
   reset (which succeeds cleanly every time - not a catastrophic GPU
   crash), and the reset takes down every Vulkan/D3D12 context sharing
   that physical GPU as collateral damage.
5. That's what actually produces every downstream symptom: your own
   runtime's `hipGetLastError()` starts reporting `801`
   (`hipErrorNotSupported`) - an honest report that the device is gone,
   not a rejection of anything specific - your `"its capture never
   landed"` diagnostic (the D3D12 command list your capture expected
   to execute never did, because the device it was queued on had just
   been reset out from under it), and finally the frozen picture
   itself, since nothing in this stack (your runtime, the game, Proton)
   has real recovery logic for a lost D3D12 device.

## What's been ruled out

Before concluding this is a real bug rather than an environment
problem, I tested and eliminated every plausible alternative I could
think of:

- **My own HIP shim** - the logic that forwards your runtime's real
  HIP calls to the actual driver. Reviewed and live-tested; clean.
- **My vkd3d-proton patch** (the D3D12↔HIP memory-sharing bridge your
  runtime needs under Wine) - reviewed line by line; it only attaches
  Vulkan export-memory flags, no address arithmetic anywhere in it at
  all.
- **Total GPU memory usage** - built and live-tested a hard cap on
  total outstanding allocation, well under 4GB. Crash happened anyway,
  with real usage at the time nowhere near that cap.
- **ROCm version** - upgraded to 7.2.4, matching your own documented
  `"every working system runs 70260201"` requirement exactly, and
  re-tested live. Identical crash, identical signature.
- **Descriptor-bounds-checking generally** - built a standalone test
  that deliberately writes out of bounds through a real, legitimate
  D3D12 UAV descriptor at this exact same address. The driver safely
  clamps it - no crash. This is what points specifically at HIP
  kernels rather than the D3D12/rendering side: D3D12's buffer views
  are bounds-checked by the driver; raw HIP pointers aren't.

## Evidence: the real fault, captured directly

Captured a live kernel-driver devcoredump at the moment of an actual
in-game crash (`amdgpu`'s own diagnostic, human-readable):

```
Ring timed out details
IP Type: 0 Ring Name: gfx_0.0.0

[gfxhub] Page fault observed
Faulty page starting at address: 0x0000000100000000
Protection fault status register: 0x841050
```

And the kernel journal for the same event:

```
amdgpu 0000:28:00.0: ring gfx_0.0.0 timeout, signaled seq=N, emitted seq=N+3
amdgpu 0000:28:00.0:  Process GameThread pid ... thread vkd3d_queue pid ...
amdgpu 0000:28:00.0: Starting gfx_0.0.0 ring reset
amdgpu 0000:28:00.0: Ring gfx_0.0.0 reset succeeded
amdgpu 0000:28:00.0: [drm] device wedged, but recovered through reset
```

That "gap of 3" between the ring's last-signaled and last-emitted
sequence numbers showed up identically across every occurrence I
captured - a consistent, repeatable pattern rather than random GPU
instability.

I also have the full raw devcoredump (kernel-level GPU register state
at the moment of the fault) - happy to send it directly rather than
post it anywhere public, since I don't know what it might reveal about
resident VRAM contents at crash time.

## How to reproduce it yourself (Windows, ~1 minute, your existing HIP SDK)

Attached: `raw_pointer_fault_repro.cpp`. Self-contained, no Linux
dependency, no special tools - the same `hipcc` toolchain you already
use to build DLSS-NR-on-AMD. Confirmed working (compiled and run on
my end with real HIP 7.2.4):

```
hipcc raw_pointer_fault_repro.cpp -o raw_pointer_fault_repro.exe
raw_pointer_fault_repro.exe
```

It launches a trivial control kernel first (writes through a real
`hipMalloc`'d pointer - succeeds every time), then launches the exact
same kernel again with the pointer argument replaced by the literal
address `0x100000000`. On my system, that second call reliably
produces:

```
Memory access fault by GPU node-1 on address 0x100000000. Reason: Page not present or supervisor privilege.
```

Whatever your own run reports - the same hard fault, a clean HIP
error instead, or (if Windows' driver stack handles this differently
than Linux's) silent success - is useful data either way. If it
doesn't fault the same way for you, that alone would explain why this
hasn't shown up as a hard crash for other Windows users, without
meaning the underlying bug isn't there.

## What I can't tell you

This repro proves the *mechanism* - a raw pointer write to this exact
address faults the GPU, reliably, independent of anything on my side.
It doesn't (and can't, without your source) show *which* kernel or
which specific line of address/offset/index arithmetic actually
produces that value during real execution. That's the one piece only
you can find quickly, from the actual source - my best guess, for
what it's worth, is a 32-bit index or offset computation (e.g.
flattening a multi-dimensional tensor access) that overflows once
addressed data crosses a certain size, but that's inference from the
outside, not something I can confirm.

No pressure on timeline - I know this is probably a small corner of a
much bigger project for you. Just wanted to hand you something
concrete rather than a vague bug report, in case it saves you time
whenever you do get to it.

## A second, separate issue: the GPU-side spin-wait can hang the ring too - even with no page fault at all

This is a different mechanism from the one above, not another symptom
of it - important to keep separate. I found it by checking `dmesg`
after noticing a "clean" test session (zero HIP/GPU errors reported
anywhere) still had a real kernel-level event I'd missed:

```
amdgpu 0000:28:00.0: ring gfx_0.0.0 timeout, signaled seq=N, emitted seq=N+3
amdgpu 0000:28:00.0:  Process GameThread pid ... thread vkd3d_queue pid ...
amdgpu 0000:28:00.0: Starting gfx_0.0.0 ring reset
amdgpu 0000:28:00.0: Ring gfx_0.0.0 reset succeeded
```

No page fault this time - just a ring that stopped making progress
long enough to hit AMDGPU's own timeout. Every single test session
that used the default GPU-side spin wait hit this, silently
(completely invisible to your own runtime's log and to Vulkan - no
`VK_ERROR_DEVICE_LOST`, nothing - the kernel just resets the ring and
everything carries on like nothing happened).

I think you already know about this one, actually - your own compiled
binary has this string, which describes the exact mechanism I
captured:

> "the compute-dispatch spin is not preempted by the OS scheduler: GPU
> watchdog resets every few minutes; SpinDraw=0 restores it"

Setting `CpuWait=1` in `dlssnr_on_amd.ini` reliably avoids it for me -
confirmed via `dmesg` showing zero ring timeouts across a real test
session with it set, versus every session without it. If that's a
known, intentional tradeoff on your end already, ignore this section
entirely - just flagging that I have real, independent confirmation
it's a genuine hardware-level ring hang under Linux specifically
(whatever the equivalent Windows/WDDM behavior is, it's clearly
different enough that this doesn't show up as a hard crash there).

## A third thing, still unsolved on my end - and I think it's your real performance bottleneck under Proton

Separately from both bugs above, I see a real, deterministic pattern in
your own `"its capture never landed"` diagnostic - the same job indices
fail the same way, run after run (jobs 2 and 3 consistently report
`captured word 1, submitted 3`, identically, every single time, across
every test session I've run). It happens on both wait methods, even in
sessions with zero GPU/ring errors of any kind.

I let a real, several-minutes-long interactive play session run instead
of my usual short automated tests, and the picture is more specific
than I expected. Over ~1100 real network jobs: **your own capture-check
times out on 96% of them** (`timeouts 1056` of `dispatches 1099`) before
eventually succeeding via whatever retry/residual-fallback path you
have. It's not usually a hard failure - it delivers a result almost
every time (`submitted 1060 ready 1059`) - but each timeout costs a
real, measured chunk of wall-clock time: your own `worker wall` time
for the actual network computation is ~20 ms, but the *game* waits
~156 ms for it (`spin waiting on us 156.1 ms`). That ~130 ms of pure
waste per frame is showing up as real, felt low frame rate on my end
(roughly 1-6 fps depending on the moment).

I also noticed your own `network on the GPU` timing reads `0.0 ms` on
every single job, in every session, regardless of anything above. I
initially thought this was my own shim's fault - I'd found a real bug
where `hipLaunchKernel`/`hipMemcpyAsync`/`hipMemsetAsync` were silently
discarding the stream argument and running everything on HIP's default
stream instead, which would explain an event recorded on your own real
stream completing before the real work. I fixed that properly (real
stream forwarding, verified via logging exactly which stream every
kernel launches on) and re-tested - no change at all. Turned out your
own runtime passes `NULL` as the kernel-launch stream itself, by
design - not something on my end. So the 0.0 ms measurement and the
96% timeout rate are still unexplained, but I'm now confident they're
not caused by anything in my compatibility shim - I've ruled out
memory-coherency issues, an idealized and a more realistic (ring-
buffer/deferred-fence) command-list submission-ordering test, and now
stream-ordering, all clean on my side. That leaves something inside
your own capture-check's timing/budget logic itself as the most likely
place left to look - possibly tuned against native Windows/WDDM timing
that this Proton/Wine/RADV stack doesn't match, though that's my best
guess from the outside, not something I can confirm without your
source.

Thanks for the work on DLSS-NR-on-AMD in general - it's the reason any
of this Linux port is possible at all.

---

*Repro file: `daniel/investigations/windows-repro/raw_pointer_fault_repro.cpp`*
