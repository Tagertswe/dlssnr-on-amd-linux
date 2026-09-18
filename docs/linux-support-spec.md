# Spec: DLSS-NR-on-AMD (danielblnc standalone path) on Linux/Proton

Status: **research spec — not an implementation plan yet**. Purpose is to
capture everything currently known/unknown before any code is written.

**Scope of intent, per user (2026-09-13, refined later the same day):**
originally framed as private personal learning only, not to be shared —
that framing has since been refined, not reversed. The user wants the
**original code this project produces** (the HIP shim, any Vulkan-interop
bridge, tooling, docs) **released as open source**, given back to the
community, rather than kept private indefinitely. The user has separately
asked danielblnc directly for permission to contribute to his project;
those two things aren't in tension — this project's own code can be open
source on its own terms (following the `zmodelerlover`-style pattern from
§1c of shipping original tooling/code openly while never shipping the
third-party proprietary material it operates on) independent of whatever
danielblnc decides about his own project. §7's calculus is unaffected by
this refinement: personal use of danielblnc's binary is one thing, but
publishing *our own* code was always going to raise the redistribution/
reverse-engineering questions in §8 item 7 the moment it's not purely
private — this refinement just makes that the explicit, current plan
rather than a hypothetical later step.

**Hard rule that follows from this, effective immediately: nothing whose
licensing works against open distribution may ever be committed to this
repository.** Concretely — danielblnc's proxy/runtime binaries in any
form, the weights file (his or NVIDIA's), any extracted/patched build of
either, and any other third-party material with restrictive or unclear
terms — must be kept **outside** version control entirely (local-only,
`.gitignore`d, never `git add`ed), the same way `zmodelerlover`'s own
repo never ships danielblnc's runtime and instead ships only the tooling
to obtain it from the user's own separately-downloaded copy (§1c). This
is a stricter, permanent version of the "private for now" framing above —
it doesn't lapse if danielblnc later agrees to something, unless and until
he explicitly clears specific files for redistribution.

**This repo, right now, violates that rule and needs cleanup before
anything gets published.** As catalogued in §2 and §8 item 7: the root of
this repo has `dxgi.dll` and `dlssnr_amd_pass1.dll` sitting untracked but
present, and `windows/Arquivos necessarios/` (also currently untracked
per `git status`, but sitting in the working tree of a public GitHub
repo) contains the full OptiScaler-AMD package including
`dlssnr_amd_pass{1,2,3}.dll`, `dlssnr_on_amd_weights.bin` (the real
~140.8 MB file, per §2), and `OptiScaler.dll`. None of this has been
staged or committed in this session, but it needs to be gitignored (or
removed from the working tree entirely) before this repo can be made
public in a way consistent with the new policy — flagged here as a
concrete to-do, not yet acted on without the user's go-ahead.

**2026-09-13 pivot — OptiScaler is out of scope going forward.** After
comparing danielblnc's own current release (§1b) against the
Vodkaman23/OptiScaler integration this repo started from, the user
decided to target **danielblnc's own standalone runtime directly**
instead: it's simpler (one self-contained proxy DLL vs. OptiScaler
orchestrating three separate pass DLLs), it's the actively-maintained
version (OptiScaler integration is frozen on the 6-releases-old v0.2.14),
and it's the natural artifact to eventually hand back to danielblnc given
the contribution intent above. §2–§5 below document the OptiScaler-based
investigation that led to this decision and are kept for the historical
record and because some of it (the licensing findings especially) still
applies; §6 onward is written for whichever proxy mechanism is in play
and mostly carries over unchanged, since the underlying technical
questions (HIP-on-Linux, D3D12↔HIP interop) don't depend on which proxy
DLL drives the runtime.

## 1f. Validation targets (per user, 2026-09-13)

Two concrete milestones, in order:

1. **Cyberpunk 2077** — already this spec's working validation target
   throughout (§1). Native DX12, native FSR, matches danielblnc's own
   validation target exactly. No additional complications beyond
   everything already discussed in §4–§8.
2. **Skyrim (Special Edition)** — second goal.

**Important caveat on Skyrim, found by checking OptiScaler's own
compatibility wiki (`Skyrim-SE.asciidoc`) before assuming this would work
the same way as Cyberpunk:** Skyrim SE has **no native DX12 renderer and
no native FSR** — it's DX11-only out of the box. The wiki entry confirms
the only way OptiScaler (and by extension any FSR-dispatch-hooking
runtime like danielblnc's) gets involved at all is through **community
upscaler-injection mods** — PureDark's "Skyrim Upscaler" SKSE plugin or
"Community Shaders" — which are themselves responsible for building a
DX11-on-DX12 bridge and a synthetic FSR context inside the game before
anything downstream can hook it. The wiki notes explicitly call out that
only the `dx11on12` upscaler path is stable ("native dx11 ones crash
game"), and that a specific `nvngx_dlss.dll` from one of those mods has to
be manually placed and renamed first just to avoid crashes on Windows.

**What this means for our plan:** danielblnc's standalone runtime hooks
`ffxFsr3ContextDispatchUpscale`/`ffxDispatch`-style FidelityFX entry
points (§1b) — it assumes an FSR context already exists to hook. For
Cyberpunk that's true natively. For Skyrim, **something has to create
that FSR/DX12 context first**, and today that something is either
PureDark's SKSE plugin, Community Shaders, or OptiScaler's own
`dx11on12` bridge — i.e. Skyrim isn't just "danielblnc's runtime on a
different game," it likely needs one of those DX11-on-DX12 bridging mods
installed *underneath* it, each with its own Windows-specific packaging
(SKSE plugin architecture, its own DLL placement rules) that hasn't been
investigated for Linux/Proton compatibility at all yet. This is new
scope, not already covered by §4–§8's Cyberpunk-focused research, and
should get its own investigation pass (SKSE-under-Proton track record,
whether PureDark's or Community Shaders' DX11-on-DX12 bridge behaves the
same way under Proton as OptiScaler's own does) before assuming the
Cyberpunk work simply carries over.

## 1g. Recommendation: which approach for which goal (2026-09-13)

Extrapolating from everything gathered so far, in response to the
"OptiScaler or danielblnc's path" question directly:

**For goal 1 (Cyberpunk) — danielblnc's standalone path, and it's the
more clean-room choice, not just the simpler one.** His DLL is designed
to fully self-drive (its own Detours-based hooks on
`Present`/`ExecuteCommandLists`/`CreateSwapChain`). Used as intended, on a
game with native DX12+FSR, that requires **no patching of his binary at
all** — just confirming it loads/hooks under Proton (§8 item 5) and
providing the HIP shim underneath (§8 item 6). Compare that to
OptiScaler-integrating him (what Vodkaman23's package did): OptiScaler
has its own hooks competing for the same entry points, which is almost
certainly why that specific integration needed exactly the kind of
low-level binary patching `zmodelerlover`'s `runtime-patches.json` (§1c)
documents — disabling his internal detour thread so it doesn't fight
OptiScaler's. That's real reverse-engineering exposure under his
license's §3, for no technical benefit on a game that already has
everything his runtime needs natively. It's also the better fit given
§1's contribution intent — it's literally his own codebase, run as he
ships it, on a different OS underneath — and it's the option this spec
has already de-risked the most (§4 resolved on real hardware, §6/§7
traced to source-level confirmation).

**For goal 2 (Skyrim) — not really an either/or; likely both, in
different roles.** Skyrim has no native DX12/FSR at all (§1f), so
something has to synthesize that context before danielblnc's runtime
(which only hooks an *existing* FSR dispatch) has anything to attach to.
The realistic shape: a DX11-on-DX12/FSR-context bridge (OptiScaler's own
`dx11on12` path, or an SKSE mod doing the same job) creates the context →
danielblnc's runtime hooks that synthetic dispatch exactly as it would in
a native DX12 game → the HIP shim sits underneath both. If OptiScaler
ends up being that bridge, it should be built fresh against **vanilla
upstream OptiScaler** (real GPLv3 source, legitimately readable and
extensible) rather than reviving Vodkaman23's abandoned,
unlicensed, version-frozen fork — consistent with §1d/§1e's engineering
approach and the open-source commitment in the scope-of-intent note.

**Suggested sequencing:** prove out danielblnc's path end-to-end on
Cyberpunk first (most de-risked, least legally fraught, no bridge-design
decision needed yet). Treat Skyrim's DX11-on-DX12 bridging as its own
follow-on investigation once Cyberpunk works, rather than designing one
unified architecture for both games up front.

## 1. Goal

Get danielblnc's DLSS 5 Neural Rendering-on-AMD mod — his own standalone
build (currently v0.3.0, a single self-contained proxy DLL that hooks the
game itself via Microsoft Detours and drives the AMD HIP compute runtime)
— working for an AMD RDNA3/4 card under Linux, via Proton, without
reimplementing the neural network itself. OptiScaler is no longer part of
the plan (see pivot note above).

Target validation environment (per user, 2026-09-12):
- GPU: **AMD Radeon RX 9070 XT** (RDNA4, "Navi 48", ROCm target `gfx1201`).
  ROCm not yet installed on the host.
- Runtime: **Valve's official Proton** via Steam (not Proton-GE, not plain
  Wine).
- Validation game: **Cyberpunk 2077** (same title danielblnc validates on
  Windows).

## 1a. A third project: `zmodelerlover/dlss5-neural-amd`

Investigated per user request (2026-09-12). This is **not** the origin of
danielblnc's or Vodkaman23's work — it's a sibling/downstream project,
created 2026-09-09 (danielblnc's repo dates to 2026-09-03, six days
earlier). Relationship, confirmed from its own README:

- It's a **ReShade add-on** (`dlss5-neural.addon64`) — a completely
  different front-end/injection mechanism from OptiScaler's `dxgi.dll`
  hijack, built by a different author ("cLohan", per its MIT `LICENSE`).
  Fully open source (`src/`, `tools/`, `build.ps1`), unlike either of the
  other two projects.
- It drives **the exact same closed danielblnc runtime** — "The runtime is
  DLSS-NR-on-AMD v0.2.17" — rather than reimplementing anything. It
  requires `amdhip64_7.dll` specifically ("HIP 7... HIP 6 will not do"),
  matching our own import-table finding exactly.
- **It deliberately does not redistribute danielblnc's binaries.** Its own
  words: "They are not in this repo and never will be: the weights are
  NVIDIA-derived and the runtime comes from a third-party project with its
  own distribution terms." Users get `dlssnr_amd_pass1.dll` and the
  weights either from a Discord channel, or — the cleaner path it
  documents — by running `tools/extract_runtime.py` against **your own**
  copy of danielblnc's official `dlssnr_on_amd_setup.exe`, which it states
  is "never executed" (the installer is a self-extracting archive with the
  payload appended raw; the tool just carves it out).
- The added claim "**the weights are NVIDIA-derived**" is worth flagging
  for §7/licensing: it implies the weights file itself likely traces back
  to NVIDIA's own `nvngx_dlssnr.dll`, which brings NVIDIA's DLSS EULA into
  the picture as a *second*, separate licensing concern beyond
  danielblnc's own license — not something resolved by this pass, just
  newly identified.
- `tools/runtime-patches.json` documents byte-level patches this project
  applies to danielblnc's binary (disassembled bytes, offsets, and
  reasoning about what each patched instruction does — disabling the
  runtime's internal self-installed detours on
  `ExecuteCommandLists`/`CreateSwapChain`/`Present` so the ReShade add-on
  can drive the runtime instead of letting it drive itself). This is a
  real, working, published reverse-engineering artifact of danielblnc's
  binary — useful engineering reference, but itself exactly the kind of
  "modify/patch/reverse engineer" activity danielblnc's license (§7)
  prohibits; it hasn't stopped this project from publishing it under MIT.
- **Confirms `gfx1201`/RDNA4 works in practice, on Windows**: its own
  results table lists a 9070 XT among tested cards, matching our earlier
  static analysis (§3) that `gfx1201` code objects are genuinely embedded
  and functional, not just present-but-broken.
- **Relevant to our hardest open problem (§7, D3D12↔compute interop):**
  it ships `src/vkbridge/vkbridge.cpp` and a Vulkan route
  (`src/neural/vk_route.inc`) that already bridges D3D12-shaped resources
  into a Vulkan host (RPCS3, Detroit: Become Human, DOOM Eternal all run
  via its Vulkan path) — i.e. someone has already solved an analogous
  cross-API resource-sharing problem for this exact runtime, on Windows.
  Reading that code (MIT-licensed, so freely readable/reusable for *our
  own* new code) before attempting the Linux dmabuf bridge from scratch
  would likely save significant research time, even though it doesn't
  itself target Linux.
- **No Linux/Proton/Wine mentions anywhere in its README** — confirmed via
  direct search. This project hasn't attempted Linux either; it's Windows
  + ReShade only today.
- **Practical takeaway for this repo's own §7 problem:** this project's
  policy of never committing danielblnc's binaries, and instead shipping a
  verified extraction tool that operates on the user's own separately-
  obtained installer, is a materially cleaner pattern than what this
  repo currently does (committing the actual `dlssnr_amd_pass*.dll` and
  weights files). Worth considering adopting the same approach here
  regardless of what happens with the Linux/HIP-shim work.

## 1b. danielblnc's v0.3.0 release (2026-09-12) — extracted and analyzed

danielblnc shipped `v0.3.0` the day before this update. Using
zmodelerlover's `tools/extract_runtime.py` (read-only PE parsing — it
carves the appended payload out of the installer by walking both PEs'
section tables; **it does not execute anything**, verified by reading its
source before running it) against the official
`dlssnr_on_amd_setup.exe` asset from
`github.com/danielblnc/DLSS-NR-on-AMD/releases/tag/v0.3.0`, downloaded
directly from GitHub:

```
runtime: 7,290,880 bytes
sha256 : 8321cae728d28cb7632d0d58d3d913e91132bf7645c126505698fbe4cd5a0138
```

This was done for private research/comparison only (per the user's stated
intent above) — the extracted file was written to the session scratch
directory, not committed to this repo, and is not being redistributed.

**What's the same as the v0.2.14 build already in this repo:**
- Identical import set: `d3d12.dll`, `dxgi.dll`, `amdhip64_7.dll`,
  `bcrypt.dll`, plus ordinary Win32 (no new HIP interop functions added).
- Identical toolchain: `clang version 21.0.0git
  (AMD-Lightning-Internal/llvm-project ...)` — same internal fork,
  consistent with §8's residual-risk note still applying unchanged.
- Identical `gfx*` target list in the fat binary (`gfx1100/1101/1102`,
  `gfx1200/1201`, plus the three generic targets) — kernel sizes moved by
  only a few hundred bytes per target (e.g. `gfx1201`: 424,840 →
  425,120 bytes), consistent with a small code change rather than a
  rewrite.

**What's different / new in v0.3.0**, per the release notes and confirmed
in the binary's own strings: **the "pre-upscale" (run-before-FSR) mode is
now native to danielblnc's own standalone build**, on by default, driven
by hooking FidelityFX's own dispatch entry points directly
(`ffxFsr3ContextDispatchUpscale`, `ffxFsr3UpscalerContextDispatch`,
`ffxDispatch`) rather than needing an external orchestrator. Representative
strings:
```
pre-upscale mode: the network runs on the %ux%u render-resolution colour
(render size %dx%d) and FSR receives the corrected copy
Pre-upscale runs the network before FSR on fewer pixels; FSR's accumulator
then provides the temporal filtering. Post-upscale runs it on the final
frame.
```
Also notable: v0.3.0's strings no longer mention "standalone version.dll
proxy" or `dlssnr_amd_pass{1,2,3}` naming at all — this looks like a
single self-contained proxy DLL, not the pass1/2/3-plus-orchestrator shape
that the `windows/.../OptiScaler-AMD-PreSR-Multipass-v1.2` package in
this repo uses.

**Why this matters strategically, not just technically:** the
`OptiScaler-AMD-PreSR-Multipass-v1.2` package this repo is built around
achieves "run the network before FSR" by bolting an external `[DlssNr]`
orchestration layer onto OptiScaler, on top of a runtime pinned at
**v0.2.14** — a build danielblnc has already superseded twice over
(v0.2.17, now v0.3.0) with the same behavior built in natively and
apparently no multi-pass DLL juggling required. Given the user's stated
plan — private learning, plus having asked danielblnc directly about
contributing — the more future-proof reference point is probably
danielblnc's own current mainline release, not the Vodkaman23/OptiScaler
fork frozen on a 6-versions-old runtime. Any Linux feasibility research
(§4, §6–§8) should probably target what v0.3.0 actually needs (its own
detour points, its own resource formats) rather than assuming the older
multi-pass architecture's specifics carry over unchanged.

## 1c. The wider ecosystem, surveyed 2026-09-13

Searched GitHub and the web broadly for "is anyone else doing this" rather
than assuming this repo's three known projects are the whole picture. They
are not — there's a surprisingly large, fast-moving ecosystem around
DLSS 5 / DLSS-NR on both vendors, on both OSes. Not exhaustively
catalogued here, but the load-bearing findings:

**A closed PR directly on danielblnc's own repo.** PR #141,
"docs: add Linux and Proton investigation notes" by a third-party
contributor (`pnhearer`), was opened and then **closed by the maintainer**
with the note: *"Closing this PR because it does not contain the intended
demonstration implementation. It was opened with the wrong scope and will
be replaced only after the complete source implementation is reviewed."*
Two takeaways: (1) danielblnc is clearly open to reviewing Linux-related
contributions in principle — relevant context for the user's own outreach
to him — but (2) nothing Linux-related has actually landed on his repo
yet, and he has a bar for what counts as an acceptable submission
(a real implementation, not just investigation notes). Worth reading this
as a signal about what he'd want to see before anything gets merged.

**A parallel, much more ambitious AMD effort that takes a completely
different approach:** `skchen17/dlssnr-amd-lab` — "DLSSNR-on-AMD interop
lab (research tooling, no proprietary binaries)." Unlike everything else
surveyed, this project is attempting a **from-scratch reimplementation**
of the neural network using **original (not NVIDIA-extracted) weights**
on native ROCm/PyTorch — explicitly the approach this spec's non-goals
(§9) rule out for our purposes. Extremely detailed, honest engineering
logs (error metrics, kernel counts, explicit "not deployment ready"
statuses) suggest serious, careful work, but as of its latest checkpoint
it's explicitly **not functional** (partial network coverage, 6–48% NRMSE
error against NVIDIA reference outputs depending on component, no
real-time game acceptance yet). Not a shortcut for us, but worth knowing
it exists as a parallel, harder-mode effort at the same underlying goal.

**Directly relevant prior art from the NVIDIA side —
`NapXDD/addon-dlssnr-linux`.** This is the single most encouraging find of
this pass: a **working, tested, released, CI-built** ReShade add-on that
runs NVIDIA's own DLSS 5 Neural Rendering (`nvngx_dlssnr.dll`) on
**Linux/Proton today** (tested config documented: RTX 5070, Linux driver
610.57.04, Fedora, KDE Plasma 6, Proton). It's the NVIDIA-side structural
twin of what this spec is trying to do for AMD: same category of problem
(closed proprietary neural-rendering DLL, needs driver-level orchestration
that doesn't work under Proton, solved with a ReShade add-on driving the
DLL directly instead). Its README documents the specific Linux bug it had
to route around: the driver-dispatched path fails with `FAIL_OutOfDate`
under Proton because "the NGX OTA updater it relies on is unavailable
under Proton" — so it bypasses driver dispatch entirely and drives the
model DLL directly through a small forwarder DLL. That's a good
illustration of the *class* of Linux-specific problem to expect (not the
HIP one specifically, but "some Windows-side orchestration/update
mechanism silently doesn't work under Proton, route around it directly"
is a pattern likely to recur).

It also demonstrates a genuinely useful **build technique**: it's "built
on Linux with clang targeting the MSVC ABI (`-target
x86_64-pc-windows-msvc`) so the vtables and by-value aggregate returns
match MSVC-built ReShade" — i.e. producing a Windows-ABI-correct add-on
DLL entirely from a Linux toolchain, no Windows/MSVC machine involved.
Directly relevant to the user's stated Rust preference (§1d): the same
class of technique (cross-compiling to a Windows target from Linux,
matching whatever ABI the host — ReShade or otherwise — expects) is how
Rust would produce a compatible DLL too, via `rustup target add
x86_64-pc-windows-gnu` (or `-msvc` with the right linker) rather than
needing an actual Windows build environment.

**One important asymmetry this surfaced, worth stating plainly:** NVIDIA's
neural-rendering DLL apparently does *not* need a cross-API compute bridge
at all under Proton — it just runs, once driver dispatch is bypassed.
That's consistent with it doing its compute through the D3D12 command
stream itself (compute shaders / a DirectML-style path) rather than
calling out to a separate driver-level compute API the way danielblnc's
AMD runtime explicitly does via `amdhip64_7.dll`. In other words: the
NVIDIA-side prior art validates that "ReShade/Detours-style hooking under
Proton" works fine as an approach, but it doesn't touch — and so doesn't
de-risk — the specific problem this spec's §7 is about, which is unique
to the AMD runtime's choice to use HIP as a distinct, separately-invoked
compute API rather than staying inside the D3D12 command stream. That
remains this project's actual hard problem; the NVIDIA precedent is
encouraging about everything *around* it.

A number of other adjacent repos exist (`DLSS5VKLayer` — a native Vulkan
layer + IPC-to-a-Wine-helper architecture, covered by Phoronix;
`LQCCS/ComfyUI-DLSS5NR-Wine` — Wine + vkd3d-proton for a non-game host;
`ccoredesenvolvimento/dlss5-linux-bridge`, `pantsoftime/dlss5-linux`,
`24high/DLSS5-Reshade-Linux-Steam`, `paulkoan/NeuralScreen-Linux`, and
others) — not individually investigated in this pass, flagged here in
case any becomes worth a closer look later.

## 1d. Engineering approach (per user, 2026-09-13)

- **This should be a well-tested effort.** Whatever gets built — the HIP
  shim, any interop bridge code, tooling — should carry real test
  coverage as it's developed, not be validated by hand-waving or one-off
  manual runs alone. Concretely, once implementation starts: unit tests
  for logic that can be isolated from real hardware (e.g. HIP call
  forwarding/argument marshalling against a fake/mock `libamdhip64`, fat
  binary parsing, handle bookkeeping), plus documented, repeatable
  integration/smoke tests for anything that genuinely needs the real GPU
  or a real Proton prefix (the §8 items 4–6 spikes should each become a
  reproducible script/test, not a one-off manual session).
- **Rust preferred where practical.** Default to Rust for new code (the
  HIP shim itself, any Vulkan-interop bridge code, supporting tooling).
  Cross-compiling a Windows-ABI-correct DLL from Linux with Rust
  (`x86_64-pc-windows-gnu`/`-msvc` targets) is well-trodden — directly
  analogous to the clang `-target x86_64-pc-windows-msvc` technique
  `NapXDD/addon-dlssnr-linux` already uses successfully for this same
  category of problem (§1c).
- **Adapt to existing codebases where Rust doesn't fit, rather than
  forcing it.** If a piece of work is best done by extending an existing
  C/C++ codebase directly — e.g. matching ReShade's native addon ABI, or
  working inside vkd3d-proton/Mesa/ROCm's own build systems if that ever
  becomes necessary — do that in its native language, and keep Rust for
  the parts that don't require living inside someone else's C/C++ ABI
  surface. FFI boundaries (Rust calling into `libamdhip64.so`, a Vulkan
  binding crate like `ash`, etc.) are the expected normal case, not a
  fallback to avoid.

## 1e. Logging and diagnostics (per user, 2026-09-13)

**Rationale:** the eventual goal includes upstreaming to danielblnc (§1
scope note), and separately, "it works on my one game on my one machine"
is not going to hold across the wildly varied Linux/Proton/game
combinations this would actually see in the wild — different distros,
Mesa/RADV versions, ROCm versions, Proton versions, and games. Both of
those mean debugging capability has to be built in from the start, not
retrofitted after something breaks in someone else's environment we can't
reproduce.

**What this means concretely, once implementation starts:**

- **Structured logging throughout**, not ad hoc `printf`/`eprintln!`
  scattered around. In Rust, that means the `tracing` ecosystem
  (`tracing` + `tracing-subscriber`) rather than a bespoke logger: spans
  for logical units of work (a frame, a HIP call sequence, a handle
  import), structured fields (GPU UUID, resource format, handle values,
  gfx target, ROCm/HIP version, Proton version) rather than
  string-interpolated messages, and a level scheme (`error`/`warn`/`info`/
  `debug`/`trace`) a user or reviewer can dial without rebuilding.
- **Capture the environment, every run, unconditionally at a low level**:
  distro/kernel version, `amdgpu`/Mesa/RADV version, ROCm/HIP runtime
  version (`hipRuntimeGetVersion`), the GPU's `gfx` target as HIP reports
  it, Proton/Wine version, and which game/executable. This is exactly the
  kind of thing that turns a vague "it doesn't work" bug report into
  something actionable, and it's cheap to always log at startup.
- **Log the interop bridge in detail** (§7/§8 item 6 especially) — handle
  values at each hop (D3D12 handle → Vulkan import → re-exported fd → HIP
  external memory handle), sizes/formats/offsets, and success/failure of
  each step individually. That chain is this project's single riskiest
  piece and the one most likely to fail differently on different
  driver/ROCm/Proton combinations — when it does, the log needs to show
  *which hop* failed, not just "it crashed" or "it produced garbage."
- **Match existing ecosystem conventions where sensible**, both because
  it eases correlating with logs the user or other testers already
  produce, and because it makes any eventual contribution to danielblnc's
  project easier to review: his own runtime already writes
  `_dlssnr_on_amd.log` (per the strings dumped in §3), ReShade add-ons in
  this space write `ReShade.log`, and OptiScaler has its own
  `LogToFile`/`LogLevel`/`LogAsync` ini-driven logging (§4's `OptiScaler.ini`
  reference). Whatever this project produces should sit next to those
  conventions rather than inventing an incompatible fourth logging story
  — e.g. a similarly-named log file next to the proxy DLL, and a log level
  that can be bumped via an ini/env-var flag the way all three of those
  projects already do.
- **Treat log volume as a feature, not noise, for this specific project.**
  Given the explicit intent to eventually hand this to danielblnc and to
  debug across Linux configurations we can't all test locally, err on the
  side of `debug`/`trace`-level detail being available (even if `info` or
  above is the default), rather than trimming logging down to "just what
  seemed necessary while it worked on my machine."

## 2. Current repo state (as of this audit)

The repo has two, **not identical**, copies of the mod's DLLs:

| File | Root of repo | `windows/Arquivos necessarios/OptiScaler-AMD-PreSR-Multipass-v1.2/` |
|---|---|---|
| `dlssnr_amd_pass1.dll` | sha256 `fe96f5...1f75` | sha256 `e145ff...fa9ad` |
| proxy dll | `dxgi.dll`, sha256 `e260e3...1ef17` | `OptiScaler.dll`, sha256 `07a1e2...b2ca8f18`* |

*(installer script copies `OptiScaler.dll` → `dxgi.dll` in the game folder,
so these are meant to be the same role, different build.)*

These are two different mod builds/versions dropped in at different times.
**Open item:** confirm which is authoritative before building anything
against either one — ideally get both from their original sources
(Vodkaman23 fork vs. the "v1.2" AMD multipass package) and diff behavior,
or just standardize on the newer `v1.2` package since it has the explicit
`[DlssNr]` HIP backend config section the older root-level `dxgi.dll` may
or may not have.

**Weights file — RESOLVED.** The copy inside
`OptiScaler-AMD-PreSR-Multipass-v1.2/dlssnr_on_amd_weights.bin` (134 bytes)
is a stale **Git LFS pointer**, not the real binary:

```
version https://git-lfs.github.com/spec/v1
oid sha256:6bf8dc931ef3ccffe18c82de26ab374156e7f19539ffcf8eabaa25dca5cf15ab
size 147689451
```

But the real ~140.8 MB file already exists one directory up, at
`windows/Arquivos necessarios/dlssnr_on_amd_weights.bin`
(147,689,451 bytes) — its sha256 (`6bf8dc93...cf15ab`) matches the pointer
exactly. Source: the user downloaded a `Arquivos necessarios.zip`
(245.9 MB) from a Google Drive folder linked in a danielblnc GitHub issue
(alongside three demo/tutorial videos, no other docs); the zip did contain
the real weights, just alongside a stray stale-pointer copy in the nested
v1.2 subfolder. Fix before testing: copy/overwrite the stub with the real
file —
```
cp "windows/Arquivos necessarios/dlssnr_on_amd_weights.bin" \
   "windows/Arquivos necessarios/OptiScaler-AMD-PreSR-Multipass-v1.2/dlssnr_on_amd_weights.bin"
```

## 3. How the existing Windows mod works (confirmed by binary inspection)

Install mechanism (`INSTALAR_AMD.ps1`):
1. Copies `OptiScaler.dll` → `<game>/dxgi.dll` (classic DLL search-order
   hijack; the game loads system DXGI functions from this file instead of
   the real `C:\Windows\System32\dxgi.dll`, OptiScaler forwards unhandled
   exports through).
2. Drops `OptiScaler.ini`, the three pass DLLs, and the weights file next
   to it.
3. Backs up anything it overwrites, refuses to run if a foreign
   `version.dll` proxy is already present (conflict guard).

`OptiScaler.ini` has a `[DlssNr]` section:
```ini
[DlssNr]
; AMD HIP backend: active-resolution pre-SR, independent runtime per pass.
Enabled=true
RunBeforeSR=true
Passes=1
LocalTone=0
LocalStructure=1
SkinStructure=1
ApplyAfterRR=false
```
So OptiScaler itself drives when/how many passes run; the actual neural
network execution is delegated to the pass DLLs, invoked **before** FSR's
super-resolution step (`RunBeforeSR=true`).

**Binary composition:**
- `OptiScaler.dll`: imports are entirely standard Win32/CRT/DXGI/D3D12 —
  nothing unusual. This is the same general-purpose upscaler-proxy
  distributed at `github.com/optiscaler/OptiScaler`, which the broader
  OptiScaler project already documents running under Proton (it's a
  well-known DXVK/vkd3d-proton-compatible hooking pattern). **Open item:**
  explicitly verify Proton compatibility for *this exact* fork/version,
  since the AMD DLSS-NR fork may have diverged from upstream OptiScaler.
- `dlssnr_amd_pass1.dll`, `pass2.dll`, `pass3.dll`: **byte-identical**
  (same sha256) — one binary, invoked up to 3 times per `Passes=` config.
  Each imports `amdhip64_7.dll` (AMD's Windows HIP/ROCm runtime) plus
  ordinary `d3d12.dll`/`dxgi.dll`/`D3DCOMPILER_47.dll`/`bcrypt.dll`. It
  also contains a genuine embedded HIP fat binary:
  - `.hipFatB` section: 24-byte fat-binary-wrapper header.
  - `.hip_fat` section: **~6.5 MB** of embedded, offline-compiled GPU code
    objects (this is a real compute payload, not a stub — consistent size
    for actual GPU kernels across a few gfx targets).

**Exact HIP ABI surface used** (28 functions total, all imported by name,
not ordinal):
- Fat-binary bootstrap: `__hipRegisterFatBinary`, `__hipRegisterFunction`,
  `__hipRegisterVar`, `__hipUnregisterFatBinary`,
  `__hipPushCallConfiguration`, `__hipPopCallConfiguration`.
- Device/runtime: `hipGetDeviceCount`, `hipGetDevicePropertiesR0600`,
  `hipSetDevice`, `hipDeviceSynchronize`, `hipDriverGetVersion`,
  `hipRuntimeGetVersion`, `hipGetLastError`, `hipGetErrorString`.
- Memory: `hipMalloc`, `hipFree`, `hipMemcpy`, `hipMemcpyAsync`,
  `hipMemcpyToSymbol`, `hipMemset`, `hipMemsetAsync`.
- Execution: `hipLaunchKernel`.
- Events: `hipEventCreate`, `hipEventRecord`, `hipEventSynchronize`,
  `hipEventElapsedTime`.
- **D3D12 interop (the hard part):** `hipImportExternalMemory`,
  `hipExternalMemoryGetMappedBuffer`, `hipDestroyExternalMemory` — used to
  hand HIP a D3D12 shared-resource handle so it can read/write the game's
  GPU buffers directly without a copy through system memory.

`bcrypt.dll` is also imported (hashing, likely for weight/model integrity
checks) — unremarkable, works fine under Wine already.

`dinput8.dll` and `dlss-enabler-headless.dll` in the package have plain
Win32 imports only, not part of the compute path — standard input-hook and
DLSS-enabler-style proxy helpers, no special Linux concerns expected.

## 4. Confirmed: OptiScaler itself is already, officially, Linux-supported

Checked the upstream `optiscaler/OptiScaler` repo directly (not just the
README):

- It ships a first-party **`setup_linux.sh`** installer (379 lines) whose
  entire job is: rename `OptiScaler.dll` to the target hook filename, then
  print `WINEDLLOVERRIDES=<filename>=n,b %COMMAND%` for the user to add as
  a Steam launch option. This is the exact same Windows PE `OptiScaler.dll`
  running unmodified under Wine's PE loader (`n,b` = try native DLL first,
  fall back to Wine's builtin) — no separate Linux build exists or is
  needed for the proxy layer.
- The wiki's `Known-Issues.md` has a dedicated **"Shader Compilation error
  on Linux"** section, instructing users to install `d3dcompiler_47` via
  WineTricks/ProtonTricks if OptiScaler's runtime-compiled shaders (RCAS,
  Reactive Mask Bias, Output Scaling) fail to build. This is real,
  maintained documentation for real Linux users — i.e. OptiScaler-on-Proton
  is a known-working, supported configuration today, not a hopeful guess.

This resolves open item §8.5 (verify `OptiScaler.dll` launches under
Proton) down to just "confirm the AMD-DLSS-NR fork didn't diverge from
upstream in a Linux-breaking way" — the general mechanism itself is
already proven at the OptiScaler-project level.

**Provenance, per user (2026-09-12):** Vodkaman23 (this repo's upstream)
took danielblnc's original DLSS-NR-on-AMD solution and integrated it into
OptiScaler rather than shipping it as a standalone Windows mod — i.e. the
`windows/Arquivos necessarios/OptiScaler-AMD-PreSR-Multipass-v1.2/` folder
in this repo *is* that integration: OptiScaler's `[DlssNr]` ini section
plus the `dlssnr_amd_pass*.dll`/weights from danielblnc's project, wired
together. Vodkaman23's own GitHub repo has no source, no releases, and no
license file — only compiled binaries — so this appears to be a private
source patch of OptiScaler (not published) distributed as a pre-built
package. This confirms the "OptiScaler path" *is* the right target: it
inherits OptiScaler's proven Linux/Proton track record for the hooking
layer, while the HIP compute layer remains danielblnc's original,
Windows-only, un-precedented-on-Linux code (§7 below is still the
real gap).

**Provenance — now directly confirmed, not just inferred.** Both
`dlssnr_amd_pass1.dll` and the official `dlssnr_on_amd_setup.exe` v0.2.14
release asset from `danielblnc/DLSS-NR-on-AMD`'s own GitHub releases page
(downloaded directly from
`github.com/danielblnc/DLSS-NR-on-AMD/releases/download/v0.2.18/...`
— using the v0.2.14 tag specifically, published 2026-09-06) contain the
**identical self-identifying string**:
```
Danielblnc's DLSS-NR on AMD v0.2.14   (End to close)
```
along with matching internal strings (`dlssnr_on_amd_weights.bin`,
`_dlssnr_on_amd.ini`, `_dlssnr_on_amd.log`, the same error/status message
text verbatim). This is conclusive: the pass DLLs in this repo are
danielblnc's own compiled output, not a lookalike or independent
reimplementation, and can be pinned to his v0.2.14 alpha release
specifically.

**Additional finding — the DLSS-NR integration isn't confined to the pass
DLLs.** `OptiScaler.dll` itself (the proxy, `windows/.../v1.2/OptiScaler.dll`)
also contains DLSS-NR-specific strings not present in stock OptiScaler —
e.g. `DlssNrEngine`, a "DLSS-NR proxy probe" for NVNGX feature 18,
discovery of a "float setter" vtable slot for intensity/structure/tone
parameters, and a forwarder that loads `nvngx.dll_dlssnr.dll`. So the
`OptiScaler.dll` shipped in this package is **not vanilla upstream
OptiScaler** — it's a patched/forked build combining OptiScaler's GPLv3
source with DLSS-NR-specific glue code. That's relevant to §7: it means
even the "proxy" binary is a derivative work touching both codebases, not
a clean separation of "GPLv3 OptiScaler, unmodified" + "separate
proprietary pass DLLs."

**Important negative finding:** I diffed the fork's `OptiScaler.ini`
against upstream's current `OptiScaler.ini` — **the `[DlssNr]` section
does not exist upstream at all.** The HIP-backend neural-rendering feature
is entirely specific to danielblnc's private AMD fork of OptiScaler, not
something the OptiScaler project documents, tests, or supports. So while
the *proxy/hooking* layer inherits OptiScaler's proven Linux track record,
the *`[DlssNr]`/HIP compute* layer has **zero existing Linux precedent** —
it's a bespoke addition riding on top of a Linux-proven host, but the new
part itself is exactly the untested part described in §5 below.

## 6. Why this is plausible on Linux at all

HIP is not Windows-proprietary — `libamdhip64.so` is ROCm's native Linux
runtime for the same API, and is generally the more mature/first-class
implementation (ROCm is Linux-first). The ~28 functions above are a small,
stable, publicly documented subset of the HIP runtime API. None of them are
Windows-specific *by name* — the ABI calling convention differs (Win64
`__stdcall`-ish MS ABI vs. System V), but the semantics are portable.

The embedded `.hip_fat` payload is HSACO (HIP fat binary format wrapping
offline-compiled AMDGPU ISA) — this format is not OS-specific; the same
compiled GPU code objects should be loadable by Linux's HIP runtime,
*provided* the fat binary wrapper struct layout the shim parses matches
what `__hipRegisterFatBinary` expects and provided the embedded kernels
were compiled for a target that includes `gfx1201` (RDNA4) — **unconfirmed,
needs to be checked** by dumping the individual code-object ELF headers out
of `.hip_fat` (each embedded HSACO records its own `e_flags`/target arch;
this can be inspected without running anything, just parsing the fat
binary — a useful next research step, not full "implementation").

**Therefore the two-layer picture (updated post-pivot, §1 above):**
1. **Proxy/hook layer** — now danielblnc's own standalone DLL (Detours-
   based self-hooking of `Present`/`ExecuteCommandLists`/`CreateSwapChain`
   directly), not OptiScaler's `dxgi.dll`. This is the *same class* of
   technique (an ordinary DLL-search-order hijack plus IAT/vtable
   detouring) that OptiScaler's proven Proton track record (§4) relies on,
   but it is danielblnc's own code, not OptiScaler's, so §4's specific
   evidence doesn't transfer automatically — it needs its own from-scratch
   verification under Proton (folded into open question 5, §8).
2. `dlssnr_amd_pass*.dll` / the v0.3.0 unified runtime's compute layer:
   blocked only by the Windows-only `amdhip64_7.dll`. A **compatibility
   shim** — a fake `amdhip64_7.dll` loaded by Wine that forwards these 28
   calls to the real `libamdhip64.so` on the Linux host — could let the
   existing closed binary run unmodified, with real ROCm doing the
   compute. This part of the analysis is unaffected by the pivot away
   from OptiScaler.

## 7. The genuinely hard sub-problem: D3D12 ↔ HIP memory interop

`hipImportExternalMemory` on Windows expects a Win32 NT handle to a shared
D3D12 resource (`HANDLE`, `IDXGIResource1::CreateSharedHandle` style).
Under Proton, that D3D12 resource is not real D3D12 — it's a vkd3d-proton
object backed by an actual Vulkan allocation. To bridge:

1. The shim's `hipImportExternalMemory` receives an NT handle (as seen
   inside the Wine process — Wine/vkd3d-proton has *some* internal
   representation of it).
2. It needs to resolve that to the underlying vkd3d-proton Vulkan
   `VkDeviceMemory`, export it as a Linux dmabuf fd via
   `VK_KHR_external_memory_fd` (vkd3d-proton already uses this class of
   extension internally for its own Wine↔host memory sharing, so the
   capability likely exists at the Vulkan layer — needs confirming it's
   reachable from outside vkd3d-proton's own code, i.e. whether there's
   a supported/hooked path to get the fd, or whether this requires a
   vkd3d-proton patch).
3. Import that dmabuf fd into HIP using the **POSIX fd** external-memory
   path (`hipExternalMemoryHandleTypeOpaqueFd` or similar), not the Win32
   handle path.

**Update (2026-09-13) — this is meaningfully de-risked now, with a
concrete architecture to prototype.** Investigated both sides directly:

**D3D12 side — vkd3d-proton's `CreateSharedHandle`/`OpenSharedHandle` are
genuinely implemented, not stubs, for the actual Proton deployment.**
Cloned and read `libs/vkd3d/device.c` directly. The implementation is
guarded by `#ifdef _WIN32` — the `#else` branch (`FIXME("...can only be
implemented in native Win32.\n"); return E_NOTIMPL;`) looked alarming on
first grep, but it's dead code for the real deployment: vkd3d-proton's
`d3d12.dll` is built via MinGW cross-compilation targeting Windows by
design ("Cross for d3d12.dll (default)" per its own README — "these serve
as a drop-in replacement for D3D12, and can be used in Wine (Proton or
vanilla flavors), or on Windows"), so `_WIN32` *is* defined for every real
Proton build. The `#else` path only matters for an explicitly-unsupported
"native Linux binary" variant the README itself says is "not intended to
be compatible with upstream Wine." Under the real `_WIN32` path,
`CreateSharedHandle` genuinely: tries `D3DKMTShareObjects` first (Wine's
own kernel-mode-thunk shared-object mechanism), and falls back to a real
`vkGetMemoryWin32HandleKHR` call against the resource's actual
`VkDeviceMemory` — i.e. the D3D12 shared handle is backed by a real
Vulkan external-memory-Win32 export, not a fake/local-only handle.

**HIP side — AMD publishes an official, first-party reference
implementation of exactly this bridge.** `ROCm/rocm-examples`'
`HIP-Basic/vulkan_interop` example (and its `vulkan_interop_mipmap`
sibling) is precisely "share a `VkDeviceMemory` with HIP." Its documented
pattern, directly applicable here:
1. Match the HIP device and the Vulkan physical device via
   `hipDeviceGetUuid` vs. `VkPhysicalDeviceIDProperties` (confirms same
   physical GPU).
2. Export the `VkDeviceMemory` to a native handle: `vkGetMemoryFdKHR` on
   Linux (handle type `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR`)
   or `vkGetMemoryWin32HandleKHR` on Windows.
3. `hipImportExternalMemory` with a matching HIP handle type —
   `hipExternalMemoryHandleTypeOpaqueFd` for the Linux fd path,
   `hipExternalMemoryHandleTypeOpaqueWin32` for the Windows handle path.
   Both are real, documented, first-class HIP API surface — this isn't
   speculative interop, it's AMD's own sample code.

**Putting the two together — a concrete architecture to spike (not yet
built, this is still research):** the shim doesn't need to reverse-engineer
vkd3d-proton's internals or patch it. It can use the *documented* D3D12
sharing API exactly as intended: open its own small Vulkan instance
matching the same physical device (as the ROCm example does), use
`OpenSharedHandle`/the underlying Vulkan Win32-external-memory import to
get its own `VkDeviceMemory` handle onto the *same* underlying allocation
vkd3d-proton created, then re-export *that* via `vkGetMemoryFdKHR` to get
a genuine Linux dmabuf fd (this step works because Wine's own Vulkan
subsystem, which vkd3d-proton itself is already built on, is necessarily
translating between Windows-side Vulkan external-memory calls and the
real Linux Vulkan ICD's fd-based external memory under the hood for every
Proton D3D12 game already — this isn't new plumbing, it's tapping into a
translation Wine already performs), then hand that fd to the real
`libamdhip64.so` via `hipImportExternalMemory(..., OpaqueFd)`.

This changes the honest risk assessment from "no prior art, might not be
possible at all" to "a documented pattern exists on both ends
independently; the open question is now the narrower, concrete one of
whether they compose inside a Wine process" — still needs a real spike
(§8 item 6) to confirm, but it's no longer a shot in the dark.

**Update (2026-09-13) — confirmed directly in Wine's own source, not just
inferred.** Read `dlls/win32u/vulkan.c` from `wine-mirror/wine` (upstream
Wine, which Proton is built on) line by line for its external-memory
handling. The exact chain hypothesized above is really there:
- `win32u_vkAllocateMemory` (the guest-visible entry point) handles a
  `VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR` for handle types
  `OPAQUE_WIN32`/`D3D11_TEXTURE`/`D3D12_HEAP`/`D3D12_RESOURCE` by calling
  `d3dkmt_open_resource(...)` — the same D3DKMT layer vkd3d-proton's
  `CreateSharedHandle` publishes through via `D3DKMTShareObjects`. So a
  handle produced by danielblnc's proxy's D3D12 resource sharing and a
  handle opened by a separate process/Vulkan instance genuinely resolve to
  the same underlying object through Wine's own kernel-mode-thunk
  emulation — this isn't a maybe.
- Elsewhere in the same function/file, when the request is for
  `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` instead, Wine calls the
  real host's `vkGetMemoryFdKHR` directly (`device->p_vkGetMemoryFdKHR(
  device->host.device, &get_fd_info, &fd)`) — confirming Wine's Vulkan
  layer really does sit on top of the genuine Linux Vulkan driver's
  fd-based external memory, translating to/from the Win32-shaped API the
  guest sees.
This means the win32-handle ↔ Linux-fd bridge our shim would need isn't
something to build from scratch or hope works — it's existing,
already-shipping Wine plumbing, and the shim's job is narrower than it
first looked: open the same shared handle via the documented D3DKMT/Vulkan
import path, then request the fd form the same way any Linux Vulkan app
would, then hand it to `libamdhip64.so`.

**Also confirmed: this isn't gated on a future/experimental Proton.**
vkd3d-proton's real `CreateSharedHandle` implementation dates to a commit
from 2023, and the current stable release is `v3.0.1` (2026-05-06) —
well downstream of that. Valve's official Proton (`proton-11.0-2`,
published 2026-08-21 — the newest stable tag as of this check) pulls
`vkd3d-proton` as a live git submodule from the same upstream repo, so the
target environment specified in §1 (Valve's official Proton via Steam)
should already carry this support without needing Proton-GE or a beta
branch.

## 8. Open questions to resolve before implementation starts

Roughly in the order they should be investigated (cheap/read-only first):

1. ~~Reconcile the two DLL sets~~ — **superseded by the pivot.** Moot now
   that OptiScaler is out of scope: the reference build going forward is
   danielblnc's own current standalone release (v0.3.0 as of this
   writing), obtained directly from his GitHub releases and extracted
   read-only with zmodelerlover's `tools/extract_runtime.py` (§1b), not
   either of the two mismatched DLL sets already in this repo. Those two
   sets remain relevant only as historical/OptiScaler-path reference.
2. ~~Recover the real weights file~~ — **RESOLVED**, see §2 above: the
   real file was already present at
   `windows/Arquivos necessarios/dlssnr_on_amd_weights.bin`; just needs
   copying over the stale stub in the v1.2 subfolder before testing.
3. ~~Dump the `.hip_fat` code objects~~ — **RESOLVED, good news.** Parsed
   the fat binary directly (it uses the standard, public
   `__CLANG_OFFLOAD_BUNDLE__` container format — 24-byte magic + bundle
   count, then offset/size/id-length/id per entry — nothing exotic). It
   contains **9 code objects**, one of them:
   ```
   id=hipv4-amdgcn-amd-amdhsa--gfx1201   offset=5681152   size=424840
   ```
   `gfx1201` is exactly the RDNA4/9070 XT target — **confirmed present**,
   real compiled code (not a stub). Full target list: `gfx1100`, `gfx1101`,
   `gfx1102` (RDNA3), `gfx1200`, `gfx1201` (RDNA4), plus three
   forward-compatible "generic" targets (`gfx9-generic`, `gfx10-3-generic`,
   `gfx11-generic`) and a `host-x86_64-unknown-linux-gnu-` stub entry.
   The generic-target entries are notable: AMD's "generic ISA" codegen
   feature is fairly recent (ROCm ~6.2+) and lets one code object run
   across a whole GPU family via runtime compatibility translation — its
   presence here is further evidence this was built with a very current
   toolchain (see §8).
   One incidental but interesting side-finding: the bundle's "host" triple
   is `x86_64-unknown-linux-gnu`, even though the final artifact is a
   Windows PE DLL — clang's offload bundler always stamps the *host*
   triple metadata from the compiler's own default target, so this reveals
   danielblnc's actual build pipeline compiles from a **Linux** host and
   cross-targets Windows for the final DLL. That's a hint the underlying
   HIP/device code is not inherently Windows-coupled at the build-system
   level — but this is circumstantial, not something actionable without
   source access.
4. ~~Install ROCm on the host~~ — **RESOLVED, 2026-09-13.** Installed
   `rocminfo`/`rocm-smi` via Ubuntu 26.04's own `universe` repo (ROCm
   7.1.1-0ubuntu1); the HIP runtime (`libamdhip64-7`, 7.1.52801) was
   already present as a pre-existing dependency. `rocminfo` output:
   ```
   Agent 2
     Name:            gfx1201
     Marketing Name:  AMD Radeon RX 9070 XT
     Vendor Name:     AMD
   ```
   Confirmed: ROCm correctly enumerates the 9070 XT as `gfx1201`, exactly
   matching the target embedded in danielblnc's fat binary (§3).
   `rocm-smi` also shows live telemetry (clock/temp/power) for device
   `0x7550`, confirming actual communication with the GPU, not just
   enumeration. One more detail directly relevant to §7: `rocminfo`'s
   system attributes report **`DMAbuf Support: YES`** — the Linux kernel
   mechanism the whole fd-based interop bridge in §7 depends on is
   confirmed present on this host.
5. ~~Verify danielblnc's own standalone proxy DLL launches under (Valve)
   Proton~~ — **RESOLVED, 2026-09-13, and cleanly successful on both
   sub-questions.**

   **Method:** built a clean-room stub `amdhip64_7.dll` (Rust cdylib,
   `windows-runtime-bridge/hip-stub/`, no proprietary code — just the ~29 public HIP
   function names from §3's import table, wired to trivial
   "unsupported"/"no device" responses, with every call logged to
   `amdhip64_7_stub.log`). This exists solely to satisfy the Windows
   loader's import resolution so danielblnc's real proxy can load and run
   its own logic under Proton, isolating *this* question from #6 (whether
   HIP actually works). Extracted v0.3.0 runtime (§1b, hash re-verified
   `8321cae7...` immediately before use) placed as `version.dll` alongside
   the stub `amdhip64_7.dll`, both dropped into
   `Cyberpunk 2077/bin/x64/` next to `Cyberpunk2077.exe`, with FSR 3
   enabled in-game (required — danielblnc's runtime hooks the FidelityFX
   dispatch, so it needs a live FSR call to attach to) and Steam launch
   options `PROTON_LOG=1 WINEDEBUG=+loaddll,+module %command%`.

   **First attempt failed silently — important pitfall found:** Wine
   substitutes its own **builtin** `version.dll` (a common system DLL name
   it ships a compatibility shim for) ahead of the one placed on disk,
   confirmed directly in the Wine trace (`find_builtin_dll` /
   `build_module loaded ...: builtin`). Neither `version.dll` nor the stub
   ever loaded — the game ran fine, but for the wrong reason (nothing was
   injected at all). **Fix:** added `WINEDLLOVERRIDES="version=n,b"`
   (native-then-builtin) to the launch options. This is a load-bearing
   finding for anyone reproducing this: **the `version.dll` proxy
   technique requires an explicit Wine DLL override to work under
   Proton at all** — it is not automatic the way it is on native Windows.

   **After the fix — (a) does it load and hook? Yes, fully:**
   ```
   dlssnr_amd v0.3.0 (build af5027d8) loaded into Cyberpunk2077.exe as version.dll
   hooked ID3D12CommandQueue::ExecuteCommandLists
   hooked IDXGIFactory2::CreateSwapChainForHwnd
   hooked IDXGIFactory::CreateSwapChain
   hooked IDXGISwapChain::Present
   hooked IDXGISwapChain1::Present1
   ```
   It also correctly enumerated the real adapter through vkd3d-proton:
   `adapter 0: AMD Radeon RX 9070 XT (RADV GFX1201) (vendor 0x1002, device
   0x7550, 16304 MB, ...)`, and confirmed `d3d12 device yes; d3d11 device
   no` — live confirmation that the D3D12-on-Vulkan path this whole effort
   depends on is real and working end to end for this binary.

   **(b) Does it fail gracefully with no HIP? Yes, perfectly:**
   ```
   env: HIP: no usable device (amdhip64_7 stub: HIP is not available (Proton smoke-test stub, no real ROCm runtime linked))
   ```
   It read the stub's own error string, logged it cleanly, and degraded to
   no-NR — no crash, no hang. Confirmed via our own stub log
   (`amdhip64_7_stub.log`) that it got as far as
   `__hipRegisterFatBinary`/`__hipRegisterFunction` (registering its
   kernels) and `hipGetDeviceCount()` before bailing out on our stub's
   `HIP_ERROR_NO_DEVICE` response — exactly the graceful-degradation path
   hoped for.

   **Side finding (from the exposed kernel symbol names in
   `__hipRegisterFunction`, not from anything proprietary/binary
   internals):** confirms the network is a Swin-transformer-style
   attention architecture running in fp8 —
   `k_swin_1h_32_fp8`, `k_qkv_attn`, `k_attention`, `k_ffwd`,
   `k_conv_res`, etc. — useful context for understanding what a real
   HIP backend (§6 below) would actually need to execute correctly.

   **Practical takeaway for #6:** the whole chain up to the HIP boundary
   is proven working under Proton with this exact GPU. The stub's
   `HIP_ERROR_NO_DEVICE`/`HIP_ERROR_NOT_SUPPORTED` numeric values used in
   `hipGetDeviceCount`/`hipGetErrorString` were best-effort guesses
   (written without a local copy of `hip_runtime_api.h`) and should be
   double-checked against real ROCm headers before building the real
   HIP-backed replacement, but they were clearly good enough to be
   recognized and handled correctly here.
6. ~~Spike the dmabuf/external-memory bridge~~ — **RESOLVED, 2026-09-13:
   the full transport works end to end, under Proton, against real
   hardware.** (Renamed from "dmabuf/external-memory bridge" to reflect
   the architecture change below — no dmabuf ended up being involved.)

   **Architecture change from the original plan above:** reading
   zmodelerlover's `src/vkbridge/vkbridge.cpp` (see below) revealed that
   Vulkan has a specific external-memory handle type for exactly this
   case — `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT` — importable
   directly from a Win32 `HANDLE` via `VkImportMemoryWin32HandleInfoKHR`,
   entirely through the guest-visible Windows Vulkan API. No raw Linux
   dmabuf fd, D3DKMT internals, or Wine-unixlib/native-module patch is
   needed for the memory transport itself, contradicting the original
   framing of this item. The trade-off: one extra GPU→host→GPU copy
   (via a host-visible/coherent Vulkan staging buffer) instead of true
   zero-copy import into HIP — acceptable given danielblnc's own pipeline
   is already copy-heavy (the pass1/2/3 DLL structure from §3). The
   Wine-unixlib approach from earlier in this investigation remains a
   possible later optimization, not a current blocker.

   **Revised design** (now built as `windows-runtime-bridge/hip-bridge/`, two Rust
   programs instead of one native module):
   - `native-daemon`: a plain native Linux binary. Loads
     `libamdhip64.so.7` via `dlopen`/`dlsym` at runtime (no ROCm dev
     package/headers needed — none are installed on this host), listens
     on a TCP loopback socket, and round-trips whatever bytes it receives
     through a real `hipMalloc`/`hipMemcpy` device allocation.
   - `pe-client`: a cross-compiled Windows program (Rust, `windows-rs` +
     `ash`), meant to run under Proton. Creates a D3D12 shared texture,
     imports it into its own Vulkan device (same technique as
     `vkbridge.cpp`), copies it to a host-visible staging buffer, and
     sends those bytes to `native-daemon` over TCP.

   **Validated so far:**
   - `native-daemon` **actually run against the real RX 9070 XT**: a 1MB
     random payload survived `hipMalloc` → `hipMemcpyHostToDevice` →
     `hipMemcpyDeviceToHost` → host, byte-identical. This is real,
     executed confirmation — not a design argument — and it also
     empirically validates the `hipMemcpyKind`/`hipError_t` numeric values
     used here and in the `amdhip64_7` stub (§8 item 5), since they worked
     correctly against the real installed ROCm 7.1 runtime.
   - `pe-client` **compiles cleanly** for `x86_64-pc-windows-gnu` and
     imports `d3d12.dll`/`dxgi.dll` as expected (Vulkan is loaded
     dynamically via `ash::Entry::load()`, so no static Vulkan import is
     expected or present).

   **Then actually run under Proton 11.0 (invoked directly via
   `proton run`, no Steam library entry needed — a fresh throwaway
   compat-data prefix works fine, doesn't need to be Cyberpunk's own):
   full success, end to end.**
   ```
   D3D12 device + shared 256x64 texture created (adapter LUID 00000000:000003f2)
   Vulkan device created with external-memory-win32 extensions enabled
   D3D12 resource imported as VkImage (65536 bytes)
   connected to hip-bridge-daemon on 127.0.0.1:47411
   comparing 65536 sent vs 65536 received bytes
   SUCCESS: 65536 bytes identical after D3D12 -> Vulkan -> TCP -> HIP device -> TCP -> Vulkan
   ```
   Independently confirmed by `native-daemon`'s own log on the Linux side
   (`got 65536 bytes, round-tripping through HIP device` /
   `round-trip OK, 65536 bytes identical`) — both ends agree, so this
   isn't a case of one side's logging being wrong.

   This resolves the actual open risk cleanly: **`winevulkan` under
   Proton 11.0 does expose `VK_KHR_external_memory_win32` to guest
   applications, and the D3D12→Vulkan import genuinely succeeds against
   RADV**. Bytes written into the imported Vulkan memory survive a real
   trip through `libamdhip64.so` on the Linux host and back, unmodified.

   **A real pitfall hit and fixed along the way, worth recording:**
   console output (`println!`/`eprintln!`) from a Wine "console
   subsystem" program is not reliably forwarded when no real terminal is
   attached — the very first `println!` in `main()` never appeared
   anywhere on the first two runs, even though (as later confirmed by
   `native-daemon`'s log) those runs had actually already succeeded all
   the way through. Switched `pe-client` to file-based logging
   (`hip-bridge-pe-client.log`, next to the exe), matching this project's
   established §1e logging approach — this should be the default for any
   future Windows-side component here, not an afterthought.

   **Follow-up (same day): both remaining gaps closed.**
   - **Real D3D12 upload-heap write path wired in**, matching
     `vkbridge.cpp`'s pattern exactly (upload-heap buffer → GPU
     `CopyTextureRegion` into the shared texture, with proper
     `COMMON`↔`COPY_DEST` transitions and an `ID3D12Fence` CPU wait for
     completion) — replacing the earlier simplified version that wrote
     straight into the mapped Vulkan staging buffer. That simplification
     had an actual gap it was masking: the original "success" run never
     read data back *through the imported Vulkan image* at all, only
     through Vulkan's own staging buffer directly, so it hadn't actually
     verified the D3D12→Vulkan image import carries real content. Adding
     the missing `vkCmdCopyImageToBuffer` step (image → staging buffer)
     closed that gap and re-ran clean:
     `VERIFIED: D3D12 write survived the trip through the imported Vulkan
     image intact`, immediately followed by the same
     `SUCCESS: 65536 bytes identical ...` through TCP/HIP as before. This
     is now a genuine, complete D3D12-write → Vulkan-image-import →
     HIP-device → back round-trip, not a partial one.
   - **Teardown crash investigated and no longer reproducing.** Restored
     full explicit Vulkan/D3D12 teardown with a log line before/after
     every individual destroy call, to pinpoint exactly which call would
     be responsible if it recurred. Ran 4 times in a row: every run
     printed the complete teardown sequence through
     `teardown: complete, exiting normally` with no crash. The original
     crash was real (confirmed via the daemon-side log agreeing the
     transport had succeeded before the client went silent) but did not
     recur once the write path was corrected to actually route through
     the imported image rather than bypassing it — plausibly the same
     gap was involved, though this isn't conclusively proven, just no
     longer observed across repeated runs.
   - No actual NR kernel execution was attempted (out of scope per §9 —
     this proves the transport, not the neural network). See the new
     analysis below on what that would actually require.

   **What a real kernel-launch path would need beyond this transport
   (analysis, 2026-09-13, no code — this is squarely licensing/research
   territory, not an engineering gap the transport spike closes):**
   1. Danielblnc's runtime calls `__hipRegisterFatBinary`/
      `__hipRegisterFunction` with a pointer to his actual compiled
      kernel bytes (already observed live against our stub, §8 item 5) —
      a real backend would need to load that `gfx1201` code object via
      `hipModuleLoadData` and track each `host_fn` pointer → device
      kernel name mapping so `hipLaunchKernel` calls can resolve to it.
   2. That's ~30 distinct kernels (from the `__hipRegisterFunction` names:
      `k_swin_*`, `k_qkv_attn`, `k_pre_block`, `k_post_block`, `k_ffwd`,
      `k_conv_res`, `k_expand`, `k_dec_upsample`, `k_final_head`, etc.) —
      a full multi-stage Swin-transformer pipeline whose exact launch
      order and buffer wiring is only knowable from source or by
      observing a genuine working backend, not from symbol names alone.
   3. Each kernel's parameter struct layout (`SwinParams`, `AttnParams`,
      `ConvParams`, ...) is unknown without source or reverse
      engineering — the same licensing line already flagged in §8
      item 7, not a new problem.
   4. `dlssnr_on_amd_weights.bin` needs uploading to device with each
      kernel referencing specific offsets into it — same unknown as #3.
   5. Notable finding: `__hipRegisterFatBinary(data)`'s `data` pointer is
      his actual compiled kernel bytes, already resident in our own
      process's memory the moment his runtime calls it — not something
      that would need extracting from a file. Capturing and executing
      that is a materially different technical act than static
      extraction from the installer, but almost certainly still crosses
      the "reverse engineering / unauthorized execution" line in his
      license (§8 item 7). This is a licensing question for the user to
      decide, not a technical loophole to exploit.
   6. Performance: the current socket-IPC design adds a TCP hop plus an
      extra GPU→host→GPU copy per use, on top of whatever the kernels
      themselves cost. Fine for this correctness spike; likely too slow
      for real-time use without the Wine-unixlib optimization mentioned
      above and pursued next (see below).

   Note per §7's cross-reference:
   zmodelerlover's `src/vkbridge/vkbridge.cpp` (MIT-licensed) already
   solves an analogous D3D12-to-Vulkan-host bridging problem for this same
   runtime on Windows — reading it before spiking this from scratch is
   likely worth the time.
7. ~~Licensing/distribution check~~ — **RESOLVED, and this is a serious
   problem, not a minor caveat.** `danielblnc/DLSS-NR-on-AMD` *does* have a
   `LICENSE` (the README just doesn't mention it). It's an all-rights-
   reserved custom license, not open source. Key clauses:
   - **§1 Permitted use**: personal, non-commercial use only, on your own
     machines.
   - **§2 Redistribution**: **prohibited outright** — "You may not
     redistribute the Software, in whole or in part, modified or
     unmodified. This includes bundling it with, or embedding it in,
     another mod, tool, launcher, installer, package, or download, and
     re-uploading it to any site or service." Users are told to link to
     the official release page instead.
   - **§3 Modification/reverse engineering**: **prohibited** — "You may
     not modify, patch, repackage, decompile, disassemble or otherwise
     reverse engineer the Software... except to the extent that applicable
     law expressly permits this despite this restriction." (Some
     jurisdictions, e.g. EU Software Directive Art. 6, carve out narrow
     interoperability exceptions to reverse-engineering bans — whether
     that applies here is a real legal question, not something to assume
     either way.)
   - **§4 Commercial use**: prohibited (donations excepted).
   - Only Microsoft Detours and AMD's public FidelityFX headers (both MIT)
     are called out as sublicensed/redistributable third-party components
     — the pass DLLs and weights are covered by the restrictive terms
     above, full stop.

   **Practical consequences:**
   - This repository, as it stands right now — a public GitHub repo with
     `dlssnr_amd_pass*.dll`, `dxgi.dll`/`OptiScaler.dll`, and the weights
     file committed/staged for commit — is already doing exactly what §2
     forbids (redistributing, and bundling with another tool/package),
     independent of anything Linux-related. This is true today, before any
     shim work starts.
   - The proposed HIP-shim approach requires understanding this binary's
     import ABI closely enough to reimplement it — which is reverse
     engineering under a plain reading of §3, regardless of whether the
     goal is interoperability rather than piracy.
   - This isn't something I can resolve by reading more source — it's a
     legal/policy decision for the user: whether to proceed privately
     (not distributing anything, purely personal use — arguably closer to
     what §1 permits, though §3 still nominally bars the reverse
     engineering needed to build a shim), to reach out to danielblnc
     directly for permission, or to not publish this repository publicly
     in its current state regardless of what happens with Linux support.
     Recommend treating this as a blocking decision, not a checkbox, and
     probably worth a real conversation (or actual legal advice) before
     any further binaries go into version control or before a shim gets
     built or shared.

   **User's decision (2026-09-13):** proceed with real kernel-execution
   work now, privately (not distributed, not committed to the repo, per
   the existing hard rule on proprietary material) — and reach out to
   danielblnc for permission separately, later, rather than blocking
   engineering work on that conversation happening first. This resolves
   the "which of the three paths" question above for now: private use
   while a permission conversation is pursued in parallel, not before.
   Doesn't change anything about the *publishing* side of the hard rule
   (no proprietary binaries, patched builds, or captured kernel bytes
   committed to git, ever, regardless of this decision).

   **Addendum (2026-09-13) — a second, likely more serious licensing
   layer underneath danielblnc's own: NVIDIA's.** danielblnc's license
   restricts what *users* of his software may do (§2–§4 above), but as
   copyright holder he isn't bound by his own §4 commercial-use ban —
   nothing in his license states he won't monetize this himself, and the
   text is silent on his own intent (his README's Ko-fi donation link is
   the only signal, and that documents a chosen model, not a promise).
   The more load-bearing question is whether monetizing — or possibly
   distributing at all — is even legally available to him, given §1c's
   finding that zmodelerlover characterizes the weights as
   "NVIDIA-derived." Checked NVIDIA's actual DLSS SDK license terms
   directly (not just danielblnc's or a summary): they prohibit reverse
   engineering, decompiling, or disassembling the SDK outright; prohibit
   distributing it as a stand-alone product (distributed components must
   be embedded in an application with "material additional functionality
   beyond the included portions of the SDK"); and prohibit copying,
   selling, sublicensing, or creating derivative works of any portion of
   the SDK except as expressly provided. If the weights genuinely trace
   back to extracting/converting NVIDIA's own `nvngx_dlssnr.dll`, this
   project sits on top of exactly the category of material those terms
   forbid touching at all — independent of price. That's a plausible,
   though unconfirmed, explanation for why this stays donation-based
   rather than sold: commercial exploitation of unlicensed derivative IP
   is generally treated far more seriously by rights holders than free
   hobbyist distribution, so a donation model is the more defensible
   posture even absent any clause forcing it.

   **Net effect on this project's risk picture:** there are now two
   independent rights holders in the chain — danielblnc (his own custom
   license, §2–§4 above) and NVIDIA (the SDK/model terms above, one layer
   further back). Anything this project builds or shares inherits
   exposure from both, not just the one whose license text is easiest to
   read. Nothing here changes the practical recommendation already given
   above (private, non-distributed use; talk to danielblnc before
   anything more) — it just means that conversation, if it happens, should
   go in clear-eyed about NVIDIA being a second party with a plausible
   interest in this, not just danielblnc.

8. ~~HIP fat binary wrapper format compatibility~~ — **RESOLVED/de-risked
   at the format level, one residual risk remains.** The fat binary uses
   the standard public `__CLANG_OFFLOAD_BUNDLE__` wrapper (see §3 above) —
   this parsed cleanly with a straightforward from-scratch script using
   only publicly documented layout, and this exact format is what stock
   ROCm/HIP's fat-binary un-bundler has read for years across OS. So the
   *container* format is not a compatibility concern.
   The residual risk is the **compiler that produced the code objects
   inside it**: version strings in the DLL read `clang version 21.0.0git
   (git@github.com:AMD-Lightning-Internal/llvm-project ...)` — an internal
   AMD LLVM fork, at a `21.0.0git` (tip-of-tree/dev) version far ahead of
   any public LLVM/ROCm release as of this writing. Combined with the
   "generic ISA" target entries (a recent, still-evolving ROCm feature),
   this means the actual compiled kernels may rely on codegen or runtime
   ABI details newer than what's in the publicly released ROCm version
   available for RDNA4 today. The container will load; whether the
   specific `gfx1201` code object *executes correctly* against a public
   ROCm `libamdhip64.so`/kernel driver is not guaranteed by this analysis
   alone and needs the real runtime smoke test from item 4.

## 9. Non-goals (for this spec)

- Not attempting to reimplement or reverse-engineer the neural network
  itself — only to run the existing closed runtime unmodified.
- **Not using OptiScaler** (superseded, see the 2026-09-13 pivot note at
  the top) — the target is danielblnc's own standalone proxy DLL.
- **Not publishing danielblnc's (or NVIDIA's) proprietary material in any
  form** — binaries, extracted/patched builds, weights — regardless of
  what happens with our own code's open-source release (see the hard rule
  in the scope-of-intent note at the top). This is a permanent
  constraint, not a "for now."
- Publishing **our own original code** (the shim, tooling, docs) as open
  source is now an explicit goal, not a non-goal — updated 2026-09-13,
  see the scope-of-intent note at the top. What remains a non-goal is
  publishing before the repo's existing proprietary-file cleanup (also
  flagged at the top) is actually done.
- Not targeting Proton-GE or plain Wine specifically (Valve's official
  Proton, per user's setup) — though the shim, if built as a Wine builtin
  DLL, should in principle work under any modern Proton/Wine build with
  vkd3d-proton.
- Not targeting Vulkan-native games — Cyberpunk 2077 is DX12, matching
  danielblnc's own validation target and this mod's DX12-only current
  scope.

## 11. Distribution architecture: socket-IPC vs Wine-unixlib (2026-09-13)

With §8 item 6 fully resolved (real transport proven working end-to-end
under Proton), the natural next question was raised: not just "can this
work" but "what does installing and running this actually look like for
a future user," under each of the two architectures considered for the
production shim.

**Common to both**, regardless of architecture:
- Real ROCm/HIP runtime installed on the user's system
  (`libamdhip64.so` + kernel driver support for their GPU).
- Danielblnc's actual runtime (`version.dll`) can't be shipped by us
  either way (§8 item 7's licensing constraint) — the user obtains it
  themselves.
- `WINEDLLOVERRIDES="version=n,b"` in launch options either way (§8
  item 5's pitfall isn't architecture-specific).

**Socket-IPC (what's built and proven today, `windows-runtime-bridge/hip-bridge/`)** —
sketched install steps:
```
1. Install ROCm for your GPU (rocminfo/rocm-smi + libamdhip64 runtime).
2. Download our shim (amdhip64_7.dll) and native-daemon binary.
3. Obtain danielblnc's runtime yourself, place as version.dll.
4. Drop version.dll + amdhip64_7.dll into the game's exe folder.
5. Steam launch options:
   WINEDLLOVERRIDES="version=n,b" /path/to/native-daemon & %command%
6. Launch the game normally.
```
Step 5's `&`-prefix trick works because Steam launch options accept an
arbitrary shell prefix before `%command%`, so the daemon auto-starts
alongside the game with no extra tooling. The weakness: a **separate
background process** the shim now depends on being alive — if it isn't
(crashed, port conflict, different launch method used), the shim needs
to detect and fail gracefully rather than hang.

**Wine-unixlib (the PE/Unix-split "builtin module" pattern, not yet
attempted)** — sketched install steps:
```
1. Install ROCm for your GPU (rocminfo/rocm-smi + libamdhip64 runtime).
2. Download our shim (a matched .dll + .so pair).
3. Obtain danielblnc's runtime yourself, place as version.dll.
4. Drop version.dll + our shim pair into the game's exe folder.
5. Steam launch options:
   WINEDLLOVERRIDES="version=n,b" %command%
6. Launch the game normally.
```
Simpler for the end user — no daemon, no port, nothing to keep alive;
Wine's own loader wires the native `.so` side in automatically the
moment the DLL is requested (confirmed via research into
`include/wine/unixlib.h`, `__wine_unix_call`/`WINE_UNIX_CALL`, and a
minimal reference implementation — see below). The real weakness is the
opposite of the socket design's: **the module is built against one
specific Wine/Proton ABI.** A user on a different Proton version could
hit a silent load failure even though it worked on our machine — this
would likely mean publishing separate builds per major Proton version
and documenting exactly which are supported, an ongoing maintenance cost
rather than a one-time one.

**Comparison:**

| | Socket-IPC | Wine-unixlib |
|---|---|---|
| End-user setup | 1 extra background process | Simpler, single drop-in |
| Cross-Proton robustness | High (stable public D3D12/Vulkan APIs) | Fragile (private ABI, version-pinned) |
| Our build complexity | Low (cargo + mingw, done) | High (winegcc/winebuild, new toolchain) |
| Runtime performance | Extra TCP hop + GPU↔host↔GPU copy | Native, no extra hop |
| Failure mode if broken | Daemon not running → shim can detect & report | Silent load failure, harder to diagnose |

**Recommendation:** ship the socket-IPC version first as the thing that
actually reaches users, treating unixlib as a later performance
optimization once there's a working v1 to optimize against — rather than
gating the whole project on solving a harder, more fragile build first,
especially since real kernel execution is still gated on the licensing
question in §8 item 7 regardless of which transport architecture is
used.

### 11a. De-risking Wine-unixlib/Proton 11 compatibility specifically

Rather than reasoning about ABI compatibility from documentation alone
(genuinely unreliable — this is exactly the kind of internal Wine
plumbing that shifts between versions and forks), the same
evidence-over-theory approach used for the whole interop spike applies
here too: **build the smallest possible unixlib "hello world" module
with the distro Wine toolchain, and actually try loading it under Proton
11.0**, the same way `pe-client` was empirically tested rather than just
assumed to work from reading `vkbridge.cpp`.

Concrete plan, not yet executed:
1. `winegcc`/`winebuild` are available via Ubuntu's `wine64-tools`
   package (`10.0~repack-12ubuntu1` candidate, not installed on this
   host) — needs a `sudo apt install`, same pattern as the ROCm/rustup
   installs earlier in this session.
2. Check what upstream Wine version/commit Proton 11.0 itself is
   actually built from (its own `files/` tree should carry version
   strings) and compare against `wine64-tools`' 10.0 — a large version
   gap would be an early warning sign before even attempting a build,
   though the only real test is the empirical one below.
3. Build a minimal reference unixlib module (the pattern demonstrated in
   the public `brainrom/winedll-example` project: a PE-side stub calling
   `WINE_UNIX_CALL` into a native `.so` implementing one trivial
   function) using the distro `winegcc`/`winebuild` toolchain.
4. Place the built module pair under `WINEDLLOVERRIDES` exactly as done
   for `version.dll`/`amdhip64_7.dll`, and run a tiny test program under
   Proton 11.0 (same `proton run` + file-logging approach already
   established) that calls the exported function and logs whether the
   native `.so` side actually executed.
5. If that works: real, direct evidence Proton 11.0's Wine fork is
   unixlib-ABI-compatible with distro Wine 10.0's toolchain output, and
   the production `amdhip64_7` shim could be built the same way. If it
   fails: concrete, specific failure information (load error vs. wrong
   behavior vs. crash) rather than a guess, and grounds to decide whether
   to chase a Proton-source-matched toolchain instead or stay on
   socket-IPC.

**Executed and confirmed working, 2026-09-13.** `wine64-tools`
(10.0~repack-12ubuntu1) plus `clang`+`lld` (both needed - the reference
project's build uses clang's cross-target flags, not mingw) installed via
apt. Cloned `brainrom/winedll-example` and adapted its Makefile's
hardcoded RPM-style paths (`/usr/lib64/wine`, `/usr/include/wine`) to
Ubuntu's actual layout (`/usr/lib/x86_64-linux-gnu/wine`,
`/usr/include/wine/wine`) - only 32-bit (i386) targets were skipped as
irrelevant to this project.

**Two real placement bugs found and fixed before it worked, both worth
recording for future reference:**
- First attempt: DLL loaded from an arbitrary path (next to the test
  exe) and the `.so` placed on `LD_LIBRARY_PATH`. Failed -
  `DllMain`/`__wine_init_unix_call()` returned failure, aborting the
  process (`STATUS_DLL_INIT_FAILED`). Root cause, confirmed by reading
  ntdll's actual source (`dlls/ntdll/unix/loader.c`,
  `find_builtin_dll`/`load_builtin_unixlib`): the DLL↔`.so` pairing is
  established only when the DLL is discovered via Wine's own internal
  builtin-search across its configured `dll_paths[]` - loading a DLL from
  an arbitrary path via normal "next to the exe" resolution never
  triggers that pairing at all, regardless of `LD_LIBRARY_PATH`, which
  this particular mechanism doesn't consult.
- Second attempt: DLL placed in the actual Wine prefix's
  `drive_c/windows/system32/` (a real, searched system directory) - still
  failed the same way. `dll_paths[]` refers specifically to *Wine's own
  installation lib directories* (for Proton: `files/lib/wine/`), not
  general Windows system directories.
- **Fix**: installed the `.dll` into Proton's own
  `files/lib/wine/x86_64-windows/` (matching where its paired `.so`
  already lived in the sibling `x86_64-unix/`), then ran
  `winebuild --builtin` on it - exactly mirroring the reference project's
  own `install` Makefile target, which was the detail missed at first by
  focusing on the build steps rather than the install step.

**Result: clean, complete success.** `DllMain`/`PROCESS_ATTACH` succeeded
with no failure warning, the process ran to a clean `PROCESS_DETACH` and
exit code 0. Confirmed with certainty (not just inferred from clean exit)
by patching the native `.so` side to write a marker file directly rather
than relying on `printf` - console output proved just as unreliable from
native-side code under Proton as it was from the PE side (§8 item 5's
finding), so this project's file-logging discipline applied here too. The
marker file contained the exact argument (`"test1"`) passed from the
PE-side call, confirmed end to end: **Proton 11.0 is unixlib-ABI-compatible
with modules built using the distro `wine64-tools` (10.0) toolchain.**

All test artifacts (the built `.dll`/`.so` pair, installed into Proton's
own directory for this test) were removed afterward - nothing was left
modified in the Proton installation.

**Practical takeaway:** the real `amdhip64_7` shim, if rebuilt on this
architecture, needs the same installation convention discovered here -
its `.dll` and `.so` must both live inside Proton's own
`files/lib/wine/x86_64-windows/` and `x86_64-unix/` directories
respectively (installed there, not shipped loosely with the game), with
`winebuild --builtin` applied to the `.dll`. This is a real distribution
consideration for §11's comparison table: end users would need an
installer step that writes into their Proton version's own directory
tree, not just the game folder - a meaningfully different (and more
invasive) install footprint than the socket-IPC design's "drop two files
next to the game" simplicity. Worth weighing against the eliminated
per-call TCP overhead when deciding whether/when to actually port stage
2's logic onto this mechanism.

## 12. Real kernel execution: engineering plan (2026-09-13)

Following the user's decision in §8 item 7 to proceed privately, this is
what "get it actually working" breaks down into, beyond the transport
spike already proven in §8 item 6.

**Key realization that changes the risk/effort picture:** most of what
looked like a hard unknown — the exact kernel launch sequence and each
kernel's parameter struct layout — doesn't actually need to be
reverse-engineered at all, *if* our shim becomes a complete, transparent,
real HIP runtime instead of a stub. Danielblnc's own runtime is the one
deciding what to call, in what order, with what arguments — a shim that
correctly forwards every call to a real ROCm backend never needs to
understand what's *inside* the argument blobs it's relaying. The
remaining work is faithfully implementing the HIP runtime surface, not
decoding his network's internals.

**Staged plan:**
1. **Real device/memory functions.** Replace the stub's
   `hipGetDeviceCount`/`hipSetDevice`/`hipMalloc`/`hipMemcpy`/etc. with
   real forwarding to `native-daemon` over the existing TCP transport
   (extending its protocol from the single "round-trip a buffer" opcode
   used in the spike to proper alloc/free/copy-to-device/copy-from-device
   operations, each returning real device-side handles/addresses).
2. **Capture the real fat binary.** `__hipRegisterFatBinary(data)`
   already hands our shim a live pointer to danielblnc's actual compiled
   kernel bytes (§8 item 5's log confirms this happens). Its container
   format is fully understood (§8 item 3's `__CLANG_OFFLOAD_BUNDLE__`
   parsing) and self-describing, so the real byte length can be
   determined from the pointer alone at runtime — no separate
   static extraction step needed for this part. Forward that blob to
   `native-daemon`, which loads it for real via `hipModuleLoadData`.
3. **Track function registration.** Implement `__hipRegisterFunction` to
   record the `host_fn` pointer → device kernel name mapping his runtime
   provides, so later `hipLaunchKernel(host_fn, ...)` calls can be
   resolved to the right loaded kernel via `hipModuleGetFunction`.
4. **Forward kernel launches mechanically.** `hipLaunchKernel` becomes a
   relay: look up `host_fn`'s device kernel via step 3's table, forward
   grid/block dims and the args blob to `native-daemon`, which calls the
   real `hipModuleLaunchKernel` — passing the argument bytes through
   unchanged, without needing to know their internal structure.
5. **Weights.** `dlssnr_on_amd_weights.bin` gets uploaded to device once
   at startup via the now-real `hipMalloc`/`hipMemcpy`, the same way any
   other buffer his runtime allocates would be.

Each stage should be validated the same evidence-over-theory way as
today's spike: build it, run it against the real GPU/Proton, and trust
the observed log output over any assumption about what "should" happen.

**Explicitly still true regardless of this plan:**
- Nothing captured or produced by this work (fat binary bytes, patched
  binaries, weights) is ever committed to git — same hard rule as always.
- This is private/personal use per the user's decision above, not a
  publishing plan, until a separate conversation with danielblnc happens.
- Performance is untested; the socket-IPC hop may or may not be fast
  enough for real-time use once actual kernels are running, which is
  part of why §11a's unixlib de-risking remains worth doing in parallel.

## 12a. Stage 1/2 execution log (2026-09-13) — real kernels ran, then a
real performance wall

Everything below actually happened, in this order, each step tested
against the real GPU in Cyberpunk 2077 - not a plan, a record of what was
built, what broke, and what it proved.

**Push/pop bug found and fixed.** `__hipPushCallConfiguration`/
`__hipPopCallConfiguration` always returning an error meant the
compiler-generated `<<<>>>` kernel-launch stub bailed out silently
before ever calling `hipLaunchKernel` - every kernel call site was a
no-op, not because `hipLaunchKernel` itself was stubbed (it also was,
separately), but because it was never reached at all. Fixed with a real
thread-local call-configuration stack. Confirmed immediately and
dramatically: `hipLaunchKernel` went from 0 calls logged to 158.

**Fat-binary pointer indirection bug found and fixed.** The parser
(§8 item 3's `__CLANG_OFFLOAD_BUNDLE__` format, confirmed correct via
static analysis) failed against the *live* pointer `__hipRegisterFatBinary`
receives, because that pointer is actually a small wrapper struct
(`{magic, version, data, unused}`, the standard compiler-generated
layout) - the real bundle pointer is the `data` field inside it, at byte
offset 8, not the outer pointer itself. Fixed by trying direct parsing
first, then the one-level-indirect form. Confirmed: bundle found, parsed
to 6,649,024 bytes, matching the size we'd expect from a real fat binary
of this scale.

**A stale-binary gotcha, not a real bug**, cost real debugging time: the
native daemon process had been running continuously since the very first
transport-spike test hours earlier and was never restarted after being
rebuilt with the new `LoadModule`/`GetFunction`/`LaunchKernel` opcodes -
so it correctly reported "unknown opcode 8" against its own *old* build.
Worth remembering for any future long-running-daemon workflow: rebuilding
the binary on disk does nothing for an already-running process.

**A real GPU memory fault, and the actual hard problem of this stage.**
Once the module genuinely loaded and the first kernel launch reached real
hardware, the daemon's own HSA runtime hard-crashed:
`Assertion `false && "GPU memory access fault."' failed.` Root cause:
every kernel's one argument (a struct, confirmed by demangling names like
`_Z16k_swin_1h_32_fp810SwinParams` → `k_swin_1h_32_fp8(SwinParams)`)
almost certainly embeds pointer fields - pointers to its input/output/
weight buffers. Forwarding the struct's raw bytes verbatim meant those
pointer fields still contained *our own fake PE-side addresses*
(the `0x7000...`-range values the address-space tracker invents, §12's
handle-arithmetic fix) - meaningless to the real GPU, which faulted
trying to dereference one.

**Fix: pointer fixups.** Before sending a kernel's argument bytes, the
PE side scans every 8-byte-aligned position for a value that resolves
against its own address-space tracker (the same `resolve()` used for the
earlier memcpy pointer-arithmetic fix) and records
`(byte_offset_in_args, handle, offset_within_that_allocation)` for each
hit. The daemon patches the real device address into the argument buffer
at each such offset - using the same real device pointer it already
tracks internally in its own handle table - immediately before calling
`hipModuleLaunchKernel`. The wire protocol's `LaunchKernel` message
carries this fixup list alongside the raw argument bytes.

**Result: complete, unambiguous success on realness and correctness.**
- All 33 kernels named in the fat binary resolved via
  `hipModuleGetFunction`.
- **All 158 kernel launches succeeded** (`hipLaunchKernel: ok (real)` ×
  158, zero failures) — every fixup found and applied correctly across
  wildly different per-kernel pointer counts (from a handful up to 58
  fixups in one call, entirely plausible for a real compute kernel's
  parameter struct).
- The daemon stayed completely healthy throughout - no crash, no
  assertion, same process the whole run.

**Then: a real hang, not a crash, not a correctness bug.** After the
158-launch warm-up batch (which itself took **8.4 seconds** - remarkably
slow for what should be sub-millisecond dispatches on real hardware, a
direct symptom of one full synchronous TCP round-trip per single kernel
launch), the game continued doing further real work for a while (the
daemon kept logging new `malloc`/`free` calls, handles climbing past
100) - then went completely silent. Five full seconds with zero daemon
activity confirmed a genuine hang, not just slowness: this is a real
process stall, evidenced by `REDEngineErrorReporter.exe <pid>` being
invoked as a standalone crash-report tool against the hung process,
distinct from the earlier pattern where that same executable just
happened to also load `version.dll` early during normal startup.

**What this proves and what it doesn't:** the entire real-kernel-execution
pipeline is *correct* - fat binary capture, function resolution, argument
marshaling, pointer fixups, all verified against real hardware with zero
launch failures. The blocker from here on is **not** a logic bug to hunt
down; it's the socket-IPC transport's per-call overhead, exactly the risk
flagged (but not yet quantified) back when that architecture was chosen
over Wine-unixlib for being cheaper to build first. It's now been proven
too slow for sustained real kernel traffic, not just theoretically likely
to be.

**Decision point reached: revisiting the Wine-unixlib path (§11a) is now
the load-bearing next step**, not a nice-to-have optimization for later -
the socket design has hit a real, demonstrated wall, not a hypothetical
one. §11a's plan (build a minimal unixlib "hello world" against distro
`wine64-tools`, test it under Proton 11 specifically) is the right next
move to de-risk before investing further in a from-scratch native-module
rewrite of everything stage 2 just built.

## 12b. Stage 2 ported onto Wine-unixlib (2026-09-13) — deployed, debugged,
and proven stable under real gameplay

Following §11a's confirmed unixlib compatibility, stage 2's entire proven
logic (fat-binary capture, function resolution, pointer handling - now
trivial, see below) was ported from the socket-IPC transport onto a real
Wine-unixlib module (`windows-runtime-bridge/hip-unixlib/`): `pe_shim.c` (PE side, C,
built with `winegcc`) paired with `native.c`/`registry.c` (native side,
built with `clang -DWINE_UNIX_LIB`). Full parity with the Rust
`hip-stub`'s function list, same real/stubbed split.

**Architectural simplification confirmed real, not just theoretical:**
since Wine runs PE and native code in the same actual process address
space, device pointers, argument-struct pointers, and the fat-binary data
pointer are all just real memory addresses usable directly on both
sides - no address-space translation, no handle tables, no pointer-fixup
scanning at all (all three of which the socket-IPC port needed). This
removed an entire category of bugs by removing the code that could have
them.

**Three real deployment bugs found and fixed, each independently
important:**

1. **A stray leftover `amdhip64_7.dll`** sitting in the game's root
   folder (not `bin/x64` - a leftover from very early in this session,
   before the actual exe location was known) was silently shadowing the
   properly-installed Proton builtin. Removed.

2. **`dlopen` inside the sandboxed game process couldn't find
   `libamdhip64.so`** by bare soname, even though the exact same bare
   name worked fine for the earlier socket-IPC daemon. Root cause: the
   daemon ran as a normal, unsandboxed host process; this native code now
   runs *inside* Proton's pressure-vessel/bwrap sandbox, whose library
   search path differs from a normal host process. Fixed by trying
   absolute host paths (`/usr/lib/x86_64-linux-gnu/libamdhip64.so.7`)
   first, falling back to bare sonames.

3. **Danielblnc's real v0.3.0 import table needs more functions than
   originally catalogued.** The original ~29-function list (§3) came
   from static analysis of the older v0.2.14/v1.2 pass DLLs and wasn't a
   complete match for the current standalone runtime's actual imports.
   Wine sets unresolved imports to dummy addresses that crash on first
   call - which is what was happening. Missing and now added:
   `hipEventCreateWithFlags`, `hipEventQuery`, `hipStreamCreateWithFlags`,
   `hipStreamSynchronize` (all stubbed, matching the existing event/stream
   non-implementation pattern).

**A fourth finding, subtler and worth real attention for anyone
reproducing this:** even after fixing all three of the above, the module
still failed to load with `STATUS_DLL_NOT_FOUND` and no
`find_builtin_dll` trace at all - a *different* failure mode than a
missing-export crash. Root-caused by reading Wine's actual
`dlls/ntdll/unix/loadorder.c`/`loader.c` source directly rather than
guessing: **a DLL imported by another DLL (danielblnc's `version.dll`
importing our `amdhip64_7.dll`) appears to need a real on-disk file
present somewhere in the normal Windows search path *in addition to* the
Proton-level builtin install**, unlike a top-level EXE's own direct
import (which is how §11a's `winedll-example` validation test loaded its
module, and why that test alone didn't surface this). The working fix:
place a real copy of the built `.dll` in the game's own folder next to
`version.dll` *and* set `WINEDLLOVERRIDES="amdhip64_7=b"` to force Wine
to still prefer the builtin (so the real unixlib module handles the
calls, not the local file's own content, which has no paired `.so` in
that location). The exact mechanism inside Wine's loader that requires
this dual placement wasn't fully traced to a single line of source in
the time spent - worth revisiting if it becomes a real distribution
concern (see §11's install-footprint trade-off), but the empirical fix
is solid and reproducible.

**Result, tested against real Cyberpunk 2077 with real launch options
(`WINEDLLOVERRIDES="version=n,b;amdhip64_7=b" %command%`):**

- Warm-up time: **79 ms**, down from the socket-IPC design's
  2000-8400 ms - roughly a 25-100x improvement, confirming the whole
  point of this port (eliminating the per-call TCP round-trip that
  caused §12a's hang).
- **Zero crashes, zero unhandled exceptions.**
- Upscaler route correctly switched from `backbuffer` to `fsr` and
  stayed there.
- **Confirmed stable over a genuinely long run: 111,600+ frames,
  55+ minutes of continuous runtime**, not just a short test window -
  this is real evidence of stability, not a lucky short sample.
- `hipImportExternalMemory` remains stubbed, so the engine still
  correctly falls back to "passing the colour through untouched" rather
  than running live per-frame NR inference (`dispatches 0` throughout) -
  expected, matches §12's non-goals; the one-time warm-up network run
  is real, ongoing per-frame inference is the next unimplemented piece,
  not something broken.

**Practical takeaway for §11's distribution comparison:** the unixlib
approach's real install footprint turns out to be even more involved
than §11a first suggested - it needs files in *both* Proton's own
directory tree *and* the game folder, plus a `WINEDLLOVERRIDES` entry
specifically for `amdhip64_7`, not just for `version`. Worth updating
that comparison table's "install footprint" row accordingly, though the
performance case for this architecture is now proven, not theoretical.

## 13. Suggested next action (updated 2026-09-13, post §11a/§12a)

Of the open questions in §8, all of 4, 5, and 6 are resolved. §8 item 7's
blocking decision has been made. §12's staged real-kernel-execution plan
is fully executed and verified correct end to end (§12a). §11a's
Wine-unixlib/Proton-11 compatibility spike is also now executed and
confirmed working. All three major risk items for this whole effort have
been resolved by direct evidence, not assumption.

**The concrete next step is porting stage 2's already-proven logic
(fat-binary capture, function resolution, pointer-fixup argument
marshaling — none of it needs rework, all verified correct against real
hardware) from the socket-IPC transport onto the confirmed-working
Wine-unixlib mechanism**, eliminating the per-call TCP round-trip that
caused §12a's hang. Concretely this means rebuilding `hip-stub`'s device/
memory/kernel-launch forwarding as a unixlib module (winegcc/winebuild/
clang, not mingw, for the PE-side glue; the native-side logic can stay
close to what `native-daemon` already does, just called in-process
instead of over a socket) - and revisiting §11a's noted installation-
footprint trade-off (installing into Proton's own directory tree vs. the
socket design's "drop two files next to the game") when it's time to
think about end-user distribution again. Not yet started.


## 14. `hipImportExternalMemory` investigated: root cause is upstream in Wine/vkd3d-proton, not our shim (2026-09-13)

**Symptom:** with real diagnostic logging added to `pe_shim.c`'s
`hipImportExternalMemory` (which currently just logs and returns
`HIP_ERROR_NOT_SUPPORTED`, matching §12b's "not yet implemented" state),
a real, stable, 60,000+-frame live Cyberpunk session showed it being
called several times with sane-looking `type=5 (D3D12Resource)` and real
buffer sizes (29491200, 14745600, 8388608, 29556736 bytes, matching the
staging buffers logged elsewhere as colour/motion/depth/exposure), but
`handle=0000000000000000` and `name=0000000000000000` in every single
call.

**First checked and ruled out: our own code.** `pe_shim.c` only reads
and logs the fields of the `hip_ext_mem_handle_desc` struct danielblnc's
runtime passes in - it never writes to `handle`. So a null handle here
means danielblnc's runtime handed us a null handle; it isn't something
our shim produced or corrupted. (The `hipGetErrorString(801)` "operation
not supported" string that shows up in danielblnc's own log is *our*
stub's return code being correctly read back and handled by danielblnc's
runtime's existing fallback logic — not evidence of a separate failure.)

**Root cause, confirmed by static analysis of danielblnc's real
`version.dll` plus direct reading of vkd3d-proton's own source:**

1. `strings` on `version.dll` reveals the exact flow: danielblnc's
   runtime calls `ID3D12Device::CreateSharedHandle` on each staging
   resource to get a Win32 `HANDLE` it can later hand to
   `hipImportExternalMemory`. It already has an explicit, working
   failure path for this: `"interop: CreateSharedHandle failed 0x%08lx
   (size %llu, alloc %llu, uav %d); this input falls back to CPU
   readback"` - and the live log confirms this fallback actually engages
   (`interop: hipImportExternalMemory: operation not supported` →
   `interop: inputs readback, output upload; mode async`). This is why
   the game is stable: danielblnc's own code was already written to
   tolerate a Windows/NVIDIA-only zero-copy path not being available and
   degrade gracefully to CPU-mediated readback/upload instead of
   crashing or hanging.
2. Reading vkd3d-proton's actual source
   (`libs/vkd3d/device.c:d3d12_device_CreateSharedHandle`,
   `libs/vkd3d/resource.c` around `D3D12_HEAP_FLAG_SHARED` handling)
   shows *why* this fails structurally under Wine/Proton, not just as a
   transient bug:
   - `CreateSharedHandle` first tries `D3DKMTShareObjects` - a
     kernel-mode Windows GDI/D3DKMT primitive that Wine only partially
     implements.
   - On failure it falls back to `vkGetMemoryWin32HandleKHR` with
     `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT` - a Vulkan
     external-memory handle type that is only meaningful against a
     native-Windows Vulkan ICD (e.g. AMD's real Windows driver). Under
     Proton, the actual Vulkan implementation backing this call is
     Linux's native driver (RADV/AMDVLK) via winevulkan's thunk layer,
     and Linux Vulkan drivers do not implement `..._WIN32_BIT` handle
     export - only `..._OPAQUE_FD_BIT` (a real Linux file descriptor).
   - The exact same pattern repeats in `resource.c` for
     `D3D12_HEAP_FLAG_SHARED` heap allocation (`import_info`/
     `export_info` built around `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_
     WIN32(_KMT)_BIT` only).
   - Conclusion: **Win32-handle-based D3D12 resource sharing
     (`CreateSharedHandle`, `D3D12_HEAP_FLAG_SHARED`) is a structural
     dead end under Wine/Proton**, not a bug we can work around by
     changing anything in our HIP shim. A null/invalid handle reaching
     `hipImportExternalMemory` is the expected, deterministic outcome of
     this code path being taken on Linux, not a fluke of this one
     session.

**Alternate path forward, real and precedented - Vulkan-native interop,
bypassing Win32 handles entirely.** vkd3d-proton ships (and NVIDIA
themselves contributed) a private-but-public COM extension interface for
exactly this class of problem, defined in
`include/vkd3d_device_vkd3d_ext.idl` and exercised in vkd3d-proton's own
test suite (`tests/d3d12_dxvk_interop_device.c`):

- `ID3D12DXVKInteropDevice` (`QueryInterface` off the real `ID3D12Device`):
  - `GetVulkanHandles(&vkInstance, &vkPhysicalDevice, &vkDevice)` - the
    real native Vulkan handles backing the D3D12 device.
  - `GetVulkanResourceInfo(resource, &vk_handle, &buffer_offset)` /
    `GetVulkanResourceInfo1(..., &format)` (interop v1+) - the real
    `VkBuffer`/`VkImage` handle and offset for a given `ID3D12Resource*`.
- `ID3D12DXVKInteropDevice3::GetVulkanHeapInfo(heap, &vk_memory,
  &heap_offset, &vk_memory_type)` - the real `VkDeviceMemory` and offset
  for a given `ID3D12Heap*`.

None of this touches Win32 handles at all - it hands back genuine native
Vulkan objects, which can then be exported with the Linux-native
`vkGetMemoryFdKHR` (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT`,
which Linux Vulkan drivers *do* support) to get a real POSIX file
descriptor, importable into HIP via
`hipImportExternalMemory(type=hipExternalMemoryHandleTypeOpaqueFd,
handle.fd=<fd>)` - a path HIP fully supports per AMD's own docs
("External resource interoperability").

**Confirmed real prior art doing almost exactly this.** A public project
(`guideahon/DLSS5-MGPU-Ampere`) solves a closely related Linux/Proton
DLSS-NR-style interop problem (dual-RTX-3090 P2P for neural rendering)
using precisely this mechanism: `ID3D12DXVKInteropDevice3` to get
`VkBuffer`/`VkDeviceMemory`, `vkGetMemoryFdKHR` (gated by vkd3d-proton's
own `VKD3D_EXPORT_OPAQUE_FD_MEMORY=1` / `VKD3D_EXPORT_RESOURCE_FD=1`
config flags) to export an opaque fd, then CUDA importing that fd
directly. Reported working, with measured P2P bandwidth (4.4-12.4 GB/s
depending on path) and one caveat worth carrying forward: they found
`vkGetMemoryFdPropertiesKHR` returns `VK_ERROR_UNKNOWN` under Wine's
Vulkan thunk layer, but this did not block a successful import once file
descriptor `FD_CLOEXEC` inheritance was corrected (their P2P daemon used
a separate process; irrelevant to our single-process unixlib design
where everything already lives in one address space and shares fd table
naturally).

**What actually plugging this into our own architecture would take.**
danielblnc's `version.dll` is proprietary and unmodifiable (per the
licensing constraints in earlier sections), and it only knows the
Win32-handle `CreateSharedHandle` → `hipImportExternalMemory
(D3D12Resource)` path - it has no idea `ID3D12DXVKInteropDevice` exists.
To bridge this without touching danielblnc's binary at all, the design
that fits is:

1. A new component - a **`d3d12.dll` proxy** (same "wrapper DLL in front
   of the real one" pattern already used for `version.dll` and
   `amdhip64_7.dll`), intercepting the real, exported
   `D3D12CreateDevice` and returning a thin COM wrapper around the real
   `ID3D12Device` that forwards every call unchanged *except*
   `CreateSharedHandle`.
2. On `CreateSharedHandle(resource, ...)`: query the real device for
   `ID3D12DXVKInteropDevice`, call `GetVulkanResourceInfo`/
   `GetVulkanHeapInfo` to get the real Vulkan memory object, call
   `vkGetMemoryFdKHR` ourselves (via a `dlopen`'d `libvulkan.so.1`,
   same technique already used for `libamdhip64.so`) to mint a real
   Linux fd, and store `(synthetic_handle -> fd)` in a small in-process
   table shared with `amdhip64_7.dll`.
3. Return a synthetic, non-null `HANDLE` we mint ourselves as if
   `CreateSharedHandle` had succeeded - danielblnc's existing,
   *unmodified* logic then proceeds exactly as designed and calls
   `hipImportExternalMemory(D3D12Resource, handle=<our synthetic
   handle>)`.
4. `amdhip64_7.dll`'s `hipImportExternalMemory` recognizes handles from
   this table, looks up the real fd, and performs a genuine
   `hipImportExternalMemory(hipExternalMemoryHandleTypeOpaqueFd,
   handle.fd=<fd>)` against the real HIP runtime instead of the
   unsupported Windows-only type.

This is a materially new, more invasive component (COM vtable wrapping,
not just DLL-export forwarding) but requires **zero interaction with or
modification of danielblnc's binary** beyond relying on behavior it
already exhibits unconditionally (it always calls `CreateSharedHandle`
first, then always forwards whatever handle results into
`hipImportExternalMemory`) - consistent with the licensing stance of
never bundling or reverse-engineering-modifying danielblnc's own files.

**Status: analysis only, nothing implemented yet.** This is a real,
concrete, precedented path to true zero-copy DLSS-NR interop, but it's a
genuinely new subsystem (a D3D12 COM proxy) beyond anything built so
far, and worth scoping deliberately before starting - not a quick
follow-on to the existing `amdhip64_7.dll` work.

Sources consulted:
- [HansKristian-Work/vkd3d-proton — device.c `CreateSharedHandle`](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/device.c)
- [HansKristian-Work/vkd3d-proton — resource.c `D3D12_HEAP_FLAG_SHARED` handling](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/resource.c)
- [HansKristian-Work/vkd3d-proton — `vkd3d_device_vkd3d_ext.idl` (interop interfaces)](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/include/vkd3d_device_vkd3d_ext.idl)
- [HansKristian-Work/vkd3d-proton — `d3d12_dxvk_interop_device.c` test](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/tests/d3d12_dxvk_interop_device.c)
- [AMD ROCm HIP docs — External resource interoperability](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/external_interop.html)
- [guideahon/DLSS5-MGPU-Ampere](https://github.com/guideahon/DLSS5-MGPU-Ampere)

## 15. D3D12 proxy: detailed design scoping (2026-09-13, not started)

Scoping §14's proposed `d3d12.dll` proxy in enough detail to estimate
effort and sequence the work, before any code is written.

### 15a. Where it sits, and what it must NOT break

It sits between *everyone* (the game engine's own rendering code, and
danielblnc's `version.dll`) and the real `d3d12.dll`, which under Proton
*is* vkd3d-proton. It is a peer of the existing `amdhip64_7.dll` shim,
not a layer between `version.dll` and it - the two proxies coordinate
directly in-process once built. Critically, this proxy must forward the
overwhelming majority of D3D12 traffic (every draw call, every resource
creation, all of Cyberpunk's own rendering) completely unchanged - it
only needs to actively intervene in exactly one method,
`CreateSharedHandle`. Any mistake in the forwarding plumbing risks
breaking rendering entirely, unlike `amdhip64_7.dll` where a bug just
means DLSS-NR doesn't activate. This raises the stakes/care needed
relative to everything built so far.

### 15b. Export-level proxying

`d3d12.dll`'s export surface is small and stable:
`D3D12CreateDevice`, `D3D12GetDebugInterface`,
`D3D12CreateRootSignatureDeserializer`, `D3D12SerializeRootSignature`,
`D3D12CreateVersionedRootSignatureDeserializer`,
`D3D12SerializeVersionedRootSignature`,
`D3D12EnableExperimentalFeatures`, `D3D12GetInterface` (exact list to
confirm via `objdump -p` on the real `d3d12.dll`, same technique already
used against `version.dll` in §14). All of these except
`D3D12CreateDevice` can be pure passthrough trampolines to the real DLL.

Loading the real DLL needs the same "rename and load by a different
name" trick proxy DLLs always use (can't `LoadLibrary("d3d12.dll")` from
inside a DLL that's itself already claiming that name in the search
path): stage a renamed copy of the real, Proton-provided `d3d12.dll`
(e.g. `d3d12_orig.dll`) next to our proxy at install time, and
`LoadLibraryW` that by relative path. This is a new install-step detail
to add wherever §12b's install steps live (README, spec §11/11a).

### 15c. The hard part: wrapping `ID3D12Device`

`D3D12CreateDevice` returns a full `ID3D12Device` (or a newer
`ID3D12DeviceN`) COM interface with a large vtable (~44 methods on the
base `ID3D12Device` alone by the published `d3d12.h` layout: 3 from
`IUnknown`, 4 from `ID3D12Object`, then `GetNodeCount` through
`GetAdapterLuid`; `CreateSharedHandle` sits at a fixed slot in that
run - offset needs confirming exactly against a real SDK header at
implementation time, but D3D12 vtables only ever *append* methods across
device versions, never reorder, so the slot is stable across
`ID3D12Device`...`ID3D12DeviceN`). Newer device interfaces
(`ID3D12Device1`, `...5` for DXR, `...8`/`9`, etc.) are common in a
modern engine like Cyberpunk's (ray tracing alone requires querying up
to at least `ID3D12Device5`), so the wrapper must handle *whichever*
device-interface version gets requested, not just the base one.

**Design:** on `QueryInterface` for any `ID3D12DeviceN` IID (including
the one returned directly by `D3D12CreateDevice`), always hand back one
of *our* wrapper objects sized to that interface's real vtable length,
never the raw real pointer - so no matter which device interface version
danielblnc's runtime or the engine ends up holding, a later
`CreateSharedHandle` call still routes through us. Every wrapper slot
except `CreateSharedHandle` is a thin forwarding trampoline: swap `this`
for the stored real device pointer, tail-call the real device's own
vtable at the identical offset. `AddRef`/`Release`/`QueryInterface`
themselves are also intercepted (need to keep returning wrapped
pointers, and manage the wrapper's own lifetime tied to the real
object's refcount). For any IID *unrelated* to `ID3D12Device`
(e.g. if something queries `ID3D12DXVKInteropDevice` itself, though
danielblnc's code has no reason to), passthrough the real, unwrapped
pointer - only the device family needs wrapping.

This is mechanical but voluminous: on the order of 40-90+ generated
forwarding functions depending on how many device versions need
support. Realistic to auto-generate from a table (method name → arg
count/types) rather than hand-write each one, but still the single
largest chunk of new code in this whole project so far by function
count, even though each function is trivial.

### 15d. `CreateSharedHandle` interception logic

On the intercepted call:
1. Filter to the case that matters: `object` implements `ID3D12Resource`
   (danielblnc's staging buffers) - if it's an `ID3D12Fence` or anything
   else, just forward to the real `CreateSharedHandle` unchanged (let
   vkd3d-proton's existing, already-correct-for-those-cases logic run).
2. `QueryInterface` the *real* device (cached once at `D3D12CreateDevice`
   time, not re-queried per call) for `ID3D12DXVKInteropDevice`.
3. Call `GetVulkanResourceInfo(resource, &vk_handle, &buffer_offset)` to
   get the real `VkBuffer`. (Buffer vs. heap-memory export mechanics -
   whether a bound `VkDeviceMemory` is directly reachable from a
   committed resource's buffer, or whether `GetVulkanHeapInfo` on an
   associated heap is needed instead - is real implementation-time
   plumbing to work out against vkd3d-proton's actual allocation
   behavior for committed resources; flagged here as an open detail,
   not yet resolved.)
4. Export a real Linux fd for that memory via `vkGetMemoryFdKHR`
   (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT`).
5. Mint a synthetic, non-null `HANDLE` value (e.g. a small counter or
   the resource pointer itself, tagged so it's distinguishable from a
   real Windows handle), store `(synthetic_handle -> fd)` in a lookup
   table, write it to `*handle`, return `S_OK`.
6. On any failure in 2-4, fall through to actually calling the real
   `CreateSharedHandle` (preserving today's working CPU-readback
   fallback behavior exactly as-is) rather than returning a hard error -
   this proxy should only ever make things *better*, never regress the
   already-proven-stable fallback path.

### 15e. Getting the real Vulkan fd - likely no new native/unixlib side needed

Because Wine's own PE-side `vulkan-1.dll` already thunks Win32 Vulkan
calls transparently through to the real, native Linux Vulkan driver
(this is simply how every Vulkan game already works under Proton), the
proxy can plausibly call `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`/
`vkGetMemoryFdKHR` as ordinary Win32 Vulkan API calls directly from PE
code, linking against Wine's real `vulkan-1.dll` - no new native `.so`
counterpart needed for this proxy the way `amdhip64_7.dll` needed
`native.c`. Wine's Vulkan thunk layer is known to special-case
fd-returning functions like this correctly (this is exactly the
mechanism that already lets Vulkan/DXVK games do real DMA-BUF-style
interop with the host compositor under Wine). This needs a small,
cheap, isolated validation spike before relying on it (mirroring how
§11a de-risked the unixlib/Proton-11 compatibility question empirically
rather than assuming it) - if it doesn't hold up, the fallback is
routing the fd-export call through `amdhip64_7.dll`'s existing native
side instead (it already dlopens things natively; could dlopen
`libvulkan.so.1` too), at the cost of one extra unixlib round-trip per
resource (a one-time cost per staging buffer, not per-frame, so even
the slow path would be acceptable).

### 15f. Handing the fd to `amdhip64_7.dll`

Both proxies are ordinary PE DLLs loaded into the *same* process
address space, so this is a plain in-process function call or shared
table, not IPC - e.g. `amdhip64_7.dll` exports a small internal
(non-HIP, our own) entry point like
`__dlssnr_register_shared_fd(HANDLE synthetic_handle, int fd)` that
`d3d12.dll`'s proxy calls directly after step 15d.5, or a shared
`.dll`-level global table if simpler to wire up given how these two
modules currently build. Either way, this is by far the simplest part
of the whole design - a consequence of controlling both ends.

### 15g. `amdhip64_7.dll`-side changes

`hipImportExternalMemory` stops unconditionally returning
`HIP_ERROR_NOT_SUPPORTED`: if the incoming handle is one of our own
synthetic ones (recognizable via the 15f table), look up its fd and
call the real `hipImportExternalMemory` with
`type=hipExternalMemoryHandleTypeOpaqueFd, handle.fd=<fd>` instead of
the unsupported `D3D12Resource` type danielblnc's runtime originally
asked for - danielblnc's code never needs to know this substitution
happened. `hipExternalMemoryGetMappedBuffer` (already imported by
danielblnc's runtime per §14's `objdump` output, currently unstubbed)
and `hipDestroyExternalMemory` (already stubbed, `HIP_ERROR_NOT_
SUPPORTED`) need real implementations alongside it - both are
straightforward direct forwards to the real HIP functions once a real
`hipExternalMemory_t` exists to operate on, following the same unixlib
opcode-dispatch pattern already used for every other real HIP call in
`native.c`.

### 15h. Effort/risk summary, ordered roughly by uncertainty

1. **Biggest unknown, check first:** whether `GetVulkanResourceInfo`/
   `GetVulkanHeapInfo` actually yields something `vkGetMemoryFdKHR` can
   export for danielblnc's specific staging-buffer allocation pattern
   (committed resources vs. explicit heaps) - worth a standalone,
   throwaway test program against vkd3d-proton before touching any real
   proxy code, same spirit as §11a's isolated compatibility spike.
2. **Second unknown:** whether PE-side Wine `vulkan-1.dll` calls really
   do yield a directly-usable native fd without needing a unixlib
   native side (§15e) - a second, cheap, isolated spike.
3. **Known-but-large:** the `ID3D12Device` vtable-wrapping mechanics
   (§15c) - not risky, just a lot of mechanical generated code across
   however many device-interface versions actually get queried in
   practice (determinable by logging `QueryInterface` IIDs during a
   real run before writing any wrapper code).
4. **Small, low-risk:** the export-level proxy shell (§15b), the
   cross-DLL handoff (§15f), and the `amdhip64_7.dll`-side real
   implementations (§15g) - all straightforward given everything already
   built.

**Suggested build order:** 15h.1 spike → 15h.2 spike → (only if both
hold up) build the export-proxy shell and vtable wrapper → wire up
`CreateSharedHandle` → wire up `amdhip64_7.dll`'s side → test against
the real game. Not started; this section is scoping only.

## 16. §15h.2 spike result: PE-side Vulkan cannot export an opaque fd (2026-09-13)

Built and ran a throwaway spike (`windows-runtime-bridge/hip-unixlib`'s
`DlssnrVulkanFdSpikeW`, invoked via `rundll32 amdhip64_7.dll,
DlssnrVulkanFdSpike` against the real Cyberpunk Proton prefix) to answer
§15h.2: does calling Vulkan directly from PE code (Wine's own
`vulkan-1.dll` thunk) yield something HIP can import as an opaque fd?

**Answer: no, and the reason is clean and structural, not a bug.**
Creating a real `VkInstance` and enumerating device extensions from PE
code found the real hardware correctly (`"70 XT (RADV GFX1201)"`, the
actual RDNA4 GPU, 224 extensions reported) - but `VK_KHR_external_
memory_fd` is not among them. Only the Windows-shaped external-memory/
fence/semaphore extensions are exposed to PE code
(`VK_KHR_external_memory_win32`, `_external_fence_win32`, `_external_
semaphore_win32`, `_win32_keyed_mutex`). This makes sense once seen:
`winevulkan` presents a Windows-looking Vulkan surface to Windows-side
code - no genuine Windows application would ever request a Linux-only
fd-based extension, so Wine doesn't offer it there, even though the
underlying native RADV driver fully supports it.

**This does not block the §15 design - it simplifies where one step
happens.** vkd3d-proton's `ID3D12DXVKInteropDevice::GetVulkanHandles`/
`GetVulkanResourceInfo` (§14/§15d) hand back *raw native* Vulkan
handles - a private, internal extension that bypasses `winevulkan`'s
Windows-shaped filtering entirely, not the restricted PE-facing surface
this spike just tested. So the actual `vkGetMemoryFdKHR` call needs to
happen on `amdhip64_7.dll`'s **native side**, not the PE side: `native.c`
already `dlopen`s `libamdhip64.so` directly (see §12b); it can equally
`dlopen("libvulkan.so.1")` and call `vkGetMemoryFdKHR` there, using the
raw native `VkDevice`/`VkDeviceMemory` values handed across the unixlib
boundary as plain integers from the (PE-side) `d3d12.dll` proxy once it
queries them via the interop interface. §15e's "likely no unixlib native
side needed" guess is now corrected: it *is* needed, but it's a small,
low-risk extension of a pattern already fully proven, not new
architecture.

**§15h status:** spike 2 done, answered, and the design updated
accordingly. Spike 1 (§15h.1 - whether `GetVulkanResourceInfo`/
`GetVulkanHeapInfo` yields something exportable for danielblnc's actual
committed-resource allocation pattern) remains open and unstarted; it's
the next thing to check before writing any real proxy code, and now
also needs to confirm the raw handles it returns are usable by a
natively-`dlopen`'d `libvulkan.so.1` `vkGetMemoryFdKHR` call (plausible -
they're the same underlying driver objects - but not yet verified).

The throwaway spike code (`DlssnrVulkanFdSpikeW` in `pe_shim.c`, the
`HIP_CALL_TEST_IMPORT_FD` opcode in `unixlib.h`/`native.c`, the
`.spec` export) has served its purpose and can be deleted once this
finding is reviewed - it was never meant to be permanent.

## 17. §15h.1 spike result: `CreateSharedHandle` confirmed broken in a clean, minimal repro; interop query path confirmed working (2026-09-13)

Built and ran a second throwaway spike (`DlssnrD3D12InteropSpikeW` in
`windows-runtime-bridge/hip-unixlib/pe_shim.c`, same `rundll32` harness as §16) that
creates its own real D3D12 device via the real `d3d12.dll`
(vkd3d-proton), allocates a plain `D3D12_HEAP_FLAG_SHARED` buffer
(8 MiB, matching a real size seen in §14's live log) with no
danielblnc/Cyberpunk involvement at all, and exercises the exact same
calls §14/§15 reasoned about from source and strings alone.

**Result, decisive:**

```
CreateCommittedResource(SHARED, size=8388608) -> hr=0x00000000 resource=...
CreateSharedHandle -> hr=0x00000000 handle=0000000000000000
```

`CreateSharedHandle` returns **`S_OK` with a null handle** - not a
failure code, a "successful" call that produces nothing usable. This is
a slightly more precise finding than §14's source reading predicted
(which expected either a hard failure from `D3DKMTShareObjects`
followed by a Vulkan Win32-handle fallback, or an outright error): it
now looks like Wine's `D3DKMTShareObjects` implementation reports
success without producing a real handle, short-circuiting
`d3d12_device_CreateSharedHandle` before it ever reaches the Vulkan
fallback path at all. Either way, the conclusion is the same and now
**empirically confirmed in a clean, isolated repro, not just inferred**:
this is a structural Wine/Proton limitation, not something specific to
Cyberpunk's or danielblnc's particular resource usage.

**The other half of the same spike is genuinely good news:**

```
QueryInterface(ID3D12DXVKInteropDevice) -> hr=0x00000000 interop=...
GetVulkanHandles -> hr=0x00000000 instance=... phys=... device=...
GetVulkanResourceInfo -> hr=0x00000000 vk_handle=0x555571f14aa0 offset=0
```

`ID3D12DXVKInteropDevice::GetVulkanHandles` and `GetVulkanResourceInfo`
both work exactly as documented, on a real, ordinary
`CreateCommittedResource`-created buffer (not a placed resource on an
explicit heap) - so §15's concern about committed-resource support was
overly cautious. A real, genuine `VkBuffer` handle and real native
`VkInstance`/`VkPhysicalDevice`/`VkDevice` handles are obtainable today,
with no proxy or interception needed to prove it.

**What's still open, narrowed by this result:** `GetVulkanResourceInfo`
hands back a `VkBuffer`, not a `VkDeviceMemory` - and Vulkan has no
public "what memory backs this buffer" query. Exporting via
`vkGetMemoryFdKHR` needs the actual `VkDeviceMemory`. For an explicit
`ID3D12Heap` this is solved (`GetVulkanHeapInfo`, interop v3), but
committed resources like this one never expose a heap object to the
caller. This is now a well-scoped, narrow engineering question rather
than an open unknown - not "does the interop mechanism work" (now
proven yes) but "how do we reach the backing memory of a committed
resource specifically." Worth a focused follow-up spike before writing
the real §15 proxy: either (a) test whether vkd3d-proton exposes the
memory some other way for committed resources specifically, or (b) plan
for the real proxy to intercept `CreateCommittedResource` itself and
transparently redirect it to `CreateHeap` + `CreatePlacedResource`
internally, so the proxy ends up holding a real `ID3D12Heap*` it made
itself and can hand to `GetVulkanHeapInfo` cleanly - danielblnc's
runtime would be none the wiser, since it only ever sees the
`ID3D12Resource*` it asked for.

Both §15h spikes are now done. Both open questions from §15h have real,
evidence-based answers; neither blocks the design, but (b) above should
be scoped into §15's `CreateCommittedResource`-interception plan before
implementation starts, rather than assumed away.

## 18. §15h.1 follow-up spike result: real dead end found - stock vkd3d-proton never enables `VK_KHR_external_memory_fd` (2026-09-13)

Extended `DlssnrD3D12InteropSpikeW` to close the last open question from
§17: create our own `ID3D12Heap` + `CreatePlacedResource` (working
around the "committed resource has no heap" gap), get its real
`VkDeviceMemory` via `ID3D12DXVKInteropDevice3::GetVulkanHeapInfo`, then
- on the native side, per §16's correction - `dlopen("libvulkan.so.1")`
and try `vkGetMemoryFdKHR`.

**Every step up to the actual export call succeeded, with real values:**

```
CreateHeap(size=8388608) -> hr=0x00000000 heap=...
CreatePlacedResource -> hr=0x00000000 resource=...
QueryInterface(ID3D12DXVKInteropDevice3) -> hr=0x00000000 interop3=...
GetVulkanHeapInfo -> hr=0x00000000 vk_memory=0x555593aca8b0 offset=0 type=0
```

Then, on the native side:

```
test_heap_fd_import: vkGetDeviceProcAddr(vkGetMemoryFdKHR) returned NULL
```

**Root cause, confirmed directly against vkd3d-proton's own source**
(`libs/vkd3d/device.c`'s `optional_device_extensions[]` table, the exact
list of Vulkan device extensions vkd3d-proton enables when it creates
its internal `VkDevice`): it enables `VK_KHR_EXTERNAL_MEMORY_WIN32` and
`VK_KHR_EXTERNAL_SEMAPHORE_WIN32` - **`VK_KHR_EXTERNAL_MEMORY_FD` is not
in the list at all**, confirmed by grepping the fetched source directly
(no match, and the surrounding ~100 other optional extensions are all
present and accounted for, so this isn't a fetch/parsing miss).

**This is a hard, structural dead end for the whole §15 D3D12-proxy
approach, not a workaround-able gap.** Vulkan device extensions must be
requested at `vkCreateDevice` time; `vkGetDeviceProcAddr` correctly
returns NULL for a function whose extension wasn't enabled on that
specific device, regardless of how the device handle was obtained. Since
vkd3d-proton owns its own internal `vkCreateDevice` call entirely - no
external proxy, hook, or interop extension can retroactively add an
extension to an already-created `VkDevice` - **there is no way to
export a real Linux fd from vkd3d-proton-backed D3D12 memory under
stock Proton, full stop**, regardless of how cleverly the §15 proxy
intercepts `CreateSharedHandle`/`CreateCommittedResource`. The entire
mechanism §15 was designed around (mint a synthetic handle, hand
danielblnc's runtime something it thinks is a real shared handle,
resolve the real fd on our own side) has nothing to resolve to - the fd
genuinely cannot be produced by this Proton install's D3D12 backend.

**This also explains `guideahon/DLSS5-MGPU-Ampere`'s env vars.**
`VKD3D_EXPORT_OPAQUE_FD_MEMORY`/`VKD3D_EXPORT_RESOURCE_FD` (§14) do not
exist anywhere in mainline `HansKristian-Work/vkd3d-proton` (confirmed
by source search, not just absence of documentation) - that project is
almost certainly running a **custom-patched fork of vkd3d-proton**, not
stock Proton, to add `VK_KHR_EXTERNAL_MEMORY_FD` enablement itself. That
is a materially different, much larger undertaking than anything scoped
in §15: shipping and maintaining a patched Direct3D-on-Vulkan
translation layer for end users, not a small standalone DLL.

**Revised conclusion: §15's D3D12 proxy should not be built as
scoped.** Its core premise - that intercepting `CreateSharedHandle`/
resource creation at the D3D12 API level is enough to route around
Wine's Win32-handle limitation - is now proven false: the block is one
layer deeper, inside vkd3d-proton's own Vulkan device initialization,
genuinely unreachable from outside without patching vkd3d-proton itself.
Real remaining options, none of them "build the proxy as planned":

1. **Patch/maintain a custom vkd3d-proton fork** that adds
   `VK_KHR_EXTERNAL_MEMORY_FD` to its device extension list - real,
   precedented (§14's other project), but a much bigger scope and
   maintenance commitment than this project has taken on so far, and a
   much bigger ask of end users (replacing part of their Proton
   install, not just adding a DLL).
2. **Accept the CPU-readback fallback as the realistic ceiling under
   stock Proton.** Danielblnc's runtime already does this gracefully
   and it's proven stable (§12b: 111,600+ frames, zero crashes). Real
   kernel execution already works end-to-end; only the zero-copy
   fast-path is unreachable, not DLSS-NR itself.
3. **Revisit if/when Daniel responds** (per the standing hold on public
   work) - he may know of a path specific to his runtime's actual needs
   that doesn't require true zero-copy interop, or may have encountered
   this exact wall himself.

The throwaway spike code (`DlssnrD3D12InteropSpikeW` in `pe_shim.c`, the
two `HIP_CALL_TEST_*` opcodes) has fully served its purpose and should
be deleted - it answered the question decisively and there's no reason
to keep iterating on it further.

## 19. Broader search: real, active upstream work exists on exactly this problem (2026-09-13)

Following up on §18's "dead end under stock Proton" conclusion, searched
for prior art beyond CachyOS specifically (CachyOS's own vkd3d-proton
patches - descriptor heap, low-latency Reflex, misc game fixes - touch
nothing relevant here, confirmed via their changelog and package
sources). The broader search found something real and directly on
point:

**vkd3d-proton's own CHANGELOG (`## 3.0` section, a released version)
lists:** `"Add support for shared resource path on upstream Wine."`
Tracing this back: DXVK PR #5257 by Wine/CodeWeavers developer rbernon,
*"Implement shared resources using the D3DKMT API"*, is a real, current,
active effort to properly implement `D3DKMT` object sharing (the exact
mechanism §14/§17 found broken - `D3DKMTShareObjects` returning `S_OK`
with a null handle) directly in **upstream Wine itself**, evolving from
earlier Proton-only patches (Guy1524's original work) toward something
that could land in mainline Wine for everyone, not just Proton forks.

**Confirmed this isn't vapourware:** fetched Wine's actual current
source, `dlls/win32u/d3dkmt.c` (1963 lines) - `NtGdiDdDDIShareObjects`
is a real, substantial implementation (`alloc_object_handle`,
`d3dkmt_object_open`, global handle tracking for resources/heaps/
fences/keyed-mutexes), not a stub. The file's most recent commit is
dated **2026-08-10** - genuinely active, current development, not an
old abandoned attempt.

**What this means for us:** our own §17 empirical test (`S_OK` +
null handle) was run against Proton 11.0's specific bundled Wine +
vkd3d-proton snapshot, which evidently predates or lacks a complete,
working version of this real feature. This reframes §18's "hard,
structural dead end" finding more precisely: the dead end is specific
to *our currently-installed Proton build*, not to Wine/vkd3d-proton as
a technology - the actual missing piece is real, in progress, and
possibly already working in a sufficiently recent build.

**Concrete next step, not yet done:** re-run the same decisive test
from §17 (recreate `DlssnrD3D12InteropSpikeW`'s `CreateSharedHandle`
check - simple to rebuild, it's preserved in this conversation and the
git history even though the code itself was cleaned up per §18) against
a **newer Proton build** than the currently-installed Proton 11.0.
Candidates, in order of ease:
1. **Valve's own Proton Experimental (bleeding-edge)** - selectable
   directly in Steam's compatibility tool settings, no separate
   download/install management needed, most directly comparable to
   what "stock Proton" will eventually ship.
2. **CachyOS's `proton-cachyos`** - per its own changelog, frequently
   rebases onto very recent Proton Experimental snapshots and backports
   Wine/vkd3d-proton updates specifically to fix newly-reported games,
   so it may already carry a working version of this feature even
   without a dedicated patch for it.

This is a real download/setup step (a new compatibility tool, multiple
GB, a separate Proton prefix for testing) - worth doing deliberately
rather than assumed away, but it is now the single most promising
concrete lead in the whole investigation: if it works, it resolves §18's
dead end entirely without needing a custom-maintained vkd3d-proton fork
at all, just a newer stock/community Proton build as a version
requirement for end users.

Sources:
- [vkd3d-proton CHANGELOG.md](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/CHANGELOG.md)
- [DXVK PR #5257 - Implement shared resources using the D3DKMT API](https://github.com/doitsujin/dxvk/pull/5257)
- [Wine source - dlls/win32u/d3dkmt.c](https://github.com/wine-mirror/wine/blob/master/dlls/win32u/d3dkmt.c)
- [CachyOS/proton-cachyos CHANGELOG](https://github.com/CachyOS/proton-cachyos/blob/cachyos_main/CHANGELOG.md)

## 20. §19 tested against Proton Experimental (2026-09-13): still broken, even on the freshest build available

Rebuilt and re-ran the exact §17/§18 decisive test against
**Proton Experimental `experimental-11.0-20260910b`** - Valve's own
bleeding-edge build, with a `d3d12.dll`/`d3d12core.dll` file timestamp
of **2026-09-12** (literally the day before this test), run against a
brand-new, freshly-bootstrapped Wine prefix with no history from any
earlier test.

**Result: identical to Proton 11.0.**

```
CreateSharedHandle -> hr=0x00000000 handle=0000000000000000  <-- same S_OK+null
...
test_heap_fd_import: vkGetDeviceProcAddr(vkGetMemoryFdKHR) returned NULL
```

Given the build date, this install is certain to include vkd3d-proton
3.0/3.0.1 - i.e. it certainly contains the "Add support for shared
resource path on upstream Wine" code §19 found promising. **The code
being present does not mean the feature is functional for our specific
use case.** Two live possibilities, not yet distinguished:

1. Valve's own Proton fork of Wine (not `wine-mirror/wine` master)
   hasn't yet merged rbernon's upstream `dlls/win32u/d3dkmt.c` work,
   even though vkd3d-proton's *calling* code for it has landed.
2. The feature is scoped to same-process interop (D3D12↔D3D11/DXVK
   sharing *within* Wine, which the older, separate changelog line -
   "Allows interop with DXVK" - specifically describes) rather than
   exposing a genuine POSIX file descriptor to an external, non-Wine
   consumer like our HIP runtime. This would mean it was never going to
   solve our problem regardless of Wine version, because our need
   (handing a real fd to a native library outside Wine's own object
   model entirely) is a fundamentally different shape of "sharing" than
   DXVK interop.

**One small artifact worth keeping in mind for any future top-level
`rundll32`-style testing:** this run also re-confirmed §12b's
"dependent DLL needs a local on-disk copy in addition to the Proton
builtin" finding applies more broadly than originally scoped - it was
needed here too, for a plain top-level `rundll32` load with no
dependent-DLL relationship at all, on a totally fresh prefix. The
narrower "only for dependent-DLL imports" characterization in §12b was
likely coincidental (the working real-game test always happened to run
from a directory that already had a local copy); the real rule appears
to be closer to "Wine's builtin-DLL resolution wants a local file
present in the search path pretty much unconditionally," at least for
unixlib modules built this way.

**Updated conclusion: §18 stands.** The broader search in §19 was worth
doing - it surfaced a real, credible, active fix in progress - but
testing against the actual newest available build shows it isn't
functional yet for this exact purpose, at least not through Proton.
Zero-copy D3D12→HIP interop remains blocked under every Proton build
actually available to test today. The realistic options from §18 are
unchanged: patch a Proton/Wine fork ourselves (large undertaking,
unclear if even the right fix given possibility 2 above), accept the
already-working CPU-readback path as the practical ceiling, or revisit
if Daniel responds with insight specific to his runtime's needs.

Test artifacts (`DlssnrD3D12InteropSpikeW` and its two `HIP_CALL_TEST_*`
unixlib opcodes) were re-added temporarily for this test and should be
removed again now that the answer is in - no further iteration planned
on this specific spike.

## 21. Alternatives explored beyond zero-copy GPU memory export (2026-09-13)

Following §20's confirmation that zero-copy D3D12→HIP interop is blocked
on every Proton build tested, explored whether other technical routes
exist.

**Ruled out: routing through D3D11/DXVK (D3D11on12).** Checked how
DXVK's D3D11on12 support actually achieves "shared resources" with
vkd3d-proton: it imports the same raw native Vulkan handles from the
same `ID3D12DXVKInteropDevice` extension already tested in §17/§20,
directly into DXVK's own Vulkan context - same process, same `VkDevice`
instance. No fd export ever happens because both sides share one
Vulkan device already. This is structurally unavailable to us (HIP is
a genuinely separate Vulkan/HSA context, not sharing vkd3d-proton's
device), so this route would hit the identical missing-extension wall
as the direct approach.

**New, real alternative: pinned host memory instead of device-memory
export.** Rather than sharing GPU-resident (`DEFAULT` heap) memory
across API boundaries - which is what every blocked path so far has
been trying to do - use an ordinary CPU-visible D3D12 heap
(`D3D12_HEAP_TYPE_UPLOAD` or `CUSTOM`) for the shared buffer instead.
This is core D3D12 functionality with no interop extension dependency
at all. `ID3D12Resource::Map()` on such a heap yields a real CPU virtual
address; HIP's `hipHostRegister`/`hipHostRegisterMapped` (standard,
always-available HIP/CUDA host-memory-pinning API, unrelated to
external-memory-fd machinery entirely) can register that same address
and hand back a real, GPU-usable device pointer via
`hipHostGetDevicePointer`.

Why this sidesteps everything blocking §18/§20: both halves are
unconditionally-supported core functionality, not gated behind any
Vulkan device extension vkd3d-proton might or might not enable. The
resulting pointer is a plain virtual address in the same shared process
space our native side already operates in - the same pattern that has
worked for every other real HIP call in this project (§12b).

**What it would still need, smaller than the original §15 proxy:** the
same `CreateCommittedResource`-interception trick (redirect danielblnc's
resource creation to a heap type we control) - but *not* the
`GetVulkanHeapInfo`/`vkGetMemoryFdKHR` machinery that turned out to be
the actual blocker. A meaningfully smaller, lower-risk slice of the
original proxy design.

**What it does NOT give us:** true zero-copy GPU-to-GPU sharing. GPU
kernel access to `UPLOAD`-heap memory still crosses PCIe per-access
under the hood; this trades "full CPU round-trip copy" (today's working
fallback) for "no explicit copy step, GPU/HIP both touch the same
pinned allocation directly" - a real but smaller win than the original
goal, not a full resolution of it.

**Open questions, not yet tested:**
1. Whether GPU-side passes (FSR reading these buffers) tolerate an
   `UPLOAD`/`CUSTOM`-heap-backed resource in place of `DEFAULT` heap
   correctly and performantly. Plausible - these are plain data buffers
   per §14's staging-buffer naming (colour/motion/depth/exposure), not
   render targets - but unverified.
2. Whether `hipHostRegister`/`hipHostGetDevicePointer` actually work
   end-to-end against a pointer obtained from D3D12's `Map()` running
   inside Wine, called from our native unixlib side.

Not yet spiked; a natural next step if this direction is pursued,
using the same throwaway-spike-then-clean-up methodology as §16/§17/§20.

## 22. §21 spiked: pinned-host-memory alternative also fails, cleanly and decisively (2026-09-13)

Built and ran `DlssnrHostRegisterSpikeW` (rundll32 harness, same
methodology as §16/§17/§20): create a real D3D12 device, allocate an
8 MiB `D3D12_HEAP_TYPE_UPLOAD` buffer (core, always-supported D3D12
functionality - no interop extension, no vkd3d-proton dependency at
all), `Map()` it for a real CPU pointer, then try
`hipHostRegister`/`hipHostGetDevicePointer` on that pointer from the
native side.

**D3D12 side worked perfectly:** device creation, `CreateCommittedResource`
on the UPLOAD heap, and `Map()` all succeeded with real values
(`mapped_ptr=0x7cda46dbd000` etc., cleanly page-aligned).

**`hipHostRegister` rejected the pointer outright:** `rc=1`
(`hipErrorInvalidValue`, "invalid argument"), identically whether called
with `hipHostRegisterMapped` or plain `hipHostRegisterDefault` (flags=0)
- ruling out "it's specifically about requesting a device-mapped view."
**Control test, decisive:** the exact same call, same size, against a
plain `malloc()`'d buffer with no D3D12/Vulkan involvement at all,
succeeded immediately (`rc=0`). This isolates the cause precisely: it
is not a bug in this test's parameters (alignment, size, flags all
fine) - HIP's `hipHostRegister` specifically refuses to register memory
that vkd3d-proton's Vulkan allocation has already placed under the
AMDGPU driver's own GPU-visible memory management. Two independent GPU
compute/graphics stacks (RADV/Vulkan and ROCm/HSA) cannot both claim
ownership of the same physical pages for GPU access, and HIP's
user-space check catches this and refuses rather than risking an
unsafe double-mapping.

**Conclusion: §21's alternative does not work, for a different but
equally hard reason than §18/§20's Vulkan-fd dead end.** This isn't a
version/build gap that a newer Proton might fix (unlike §19's genuine
uncertainty) - it is a fundamental exclusivity between two GPU
subsystems both wanting DMA-capable ownership of the same memory, which
is expected, correct behavior from HIP's side, not a limitation of
Wine/Proton specifically. Any approach requiring one GPU API's already
GPU-mapped memory to be *additionally* registered by a second, entirely
separate GPU runtime is likely to hit the same wall regardless of which
two APIs are involved.

**What this leaves standing, for real:** every technical avenue found
so far for genuine zero-copy (or copy-avoiding) D3D12→HIP sharing is
now closed - the Vulkan external-memory-fd path (§18, confirmed
structurally absent from vkd3d-proton's enabled extensions, §20:
confirmed still true on the newest available Proton build) and the
pinned-host-memory path (§22, confirmed as a hard cross-driver
exclusivity, not a version gap). The three options from §18 stand
unchanged and are now on firmer ground: patch a Proton/Wine fork
(large, uncertain value), accept the working CPU-readback fallback as
the practical ceiling (real, stable, already proven at 111,600+ frames),
or wait for Daniel. Test spike code (`DlssnrHostRegisterSpikeW`, the
`HIP_CALL_TEST_HOST_REGISTER_BENCH` opcode) should be removed now that
the question is answered.

## 23. Real deployment gap found and fixed: `proton run` test harness ≠ genuine Steam-launched sandbox (2026-09-13)

While chasing "DLSS settings not visible" with the user, discovered that
**every prior "real game" validation in this project (§12b's 111,600+
frame stability run included) was captured via manually-scripted
`"$PROTON_DIR/proton" run ...` invocations, not genuine Steam-client
launches.** These take a meaningfully different code path: a real
"Play" click wraps the game in Steam's full pressure-vessel/bwrap
sandbox container; the scripted invocations used throughout this
project's testing do not go through the same wrapping. This was
invisible until this session because the two categories of test always
agreed - until they didn't.

**Two real, distinct bugs found and fixed, both invisible to the
scripted test harness:**

1. **`libamdhip64.so.7` itself unreachable at its normal host path.**
   Confirmed directly against a live, real Steam-launched process
   (`/proc/<pid>/root/...`): `/usr/lib/x86_64-linux-gnu/` inside the
   sandbox is the container's own bundled directory, not a bind-mount of
   the host's - the real host filesystem is instead available under
   `/run/host/`. Fixed by trying `/run/host/usr/lib/x86_64-linux-gnu/
   libamdhip64.so.{7,6}` first in `native.c`'s `dlopen` candidate list,
   ahead of the plain host path and bare sonames (kept as fallbacks).

2. **Even once the main library resolved, its own transitive
   dependencies did not.** `ldd libamdhip64.so.7` lists `libhsa-
   runtime64.so.1`, `libhsakmt.so.1`, `libdrm_amdgpu.so.1`, and even
   plain `libc.so.6`/`libstdc++.so.6` - none resolvable at their host
   paths from inside the sandbox either (confirmed the same way). Fixed
   by setting `LD_LIBRARY_PATH` to prepend `/run/host/usr/lib/x86_64-
   linux-gnu:/run/host/lib/x86_64-linux-gnu` in `ensure_loaded()`
   *before* the `dlopen` call - glibc's dynamic linker consults
   `LD_LIBRARY_PATH` live for each `dlopen`'s own dependency resolution,
   not only once at process startup, so this fixes the whole transitive
   chain without hand-listing every dependency.

**Verified working end to end against a real Steam-launched session
after both fixes**: real fat-binary registration, all kernels resolved,
`warm-up: first network run took 75 ms`, real `hipLaunchKernel`/
`hipMalloc` calls succeeding continuously. This is the first time in
the whole project that real HIP execution has been confirmed under a
genuine, fully-sandboxed Steam launch rather than the scripted test
harness.

**Ruled out along the way, for the record:** neither device-node
visibility (`/dev/kfd`, `/dev/dri/renderD128` both correctly bind-
mounted with matching permissions) nor POSIX ACLs (confirmed identical
and functional inside the sandbox) were the problem - it was purely
library path resolution.

**Standing risk this raises:** anything validated only through this
project's own `proton run` scripts should be treated as *unconfirmed
under real deployment* until similarly re-verified. The interop/
CPU-readback findings (§14, §17, §18, §20, §22) were all diagnosed via
a mix of both real sessions and scripted tests and are believed sound,
but this is now a known gap worth remembering for anything tested only
one way.

**Also clarified for the record (user question):** requiring
`ResolutionScaling: DLSS` in Cyberpunk's own settings (not just a
working HIP runtime) before DLSS-NR engages at all is standard behavior
across DLSS-on-non-NVIDIA projects generally (OptiScaler, DLSS Enabler,
etc.) - the game engine only invokes the NGX/DLSS code path danielblnc's
runtime hooks when the game itself is configured to use it, confirmed
directly in this install's own `UserSettings.json` (`ResolutionScaling`
was set to `'FSR3'`, with `'DLSS'` listed as a sibling selectable
value). This is unrelated to, and does not depend on, this session's
HIP-initialization fix - the real test of dispatches finally engaging
still awaits both fixes being in place together.

## 24. The overlay, real-world usage pattern, and final settling of the interop question (2026-09-13)

A user correction mid-session led to the actual resolution of the
"DLSS not in settings menu" investigation (§14 onward), which had been
proceeding on a wrong assumption. Recorded here in full since it
materially changes how this mod is meant to be used, and because
danielblnc's real Windows community (confirmed by the user: an active
Discord, ongoing per-game testing, and dated release notes specifically
mentioning Cyberpunk 2077 performance work) proves the underlying
premise of this whole project is sound - the wrong turn was in how
*we* were trying to activate it, not whether it's possible at all.

### 24a. DLSS is not meant to appear in Cyberpunk's native settings menu

Public reporting on danielblnc's `DLSS-NR-on-AMD` confirmed the real
usage pattern directly: the mod is used by enabling **FSR** (not DLSS)
in the game's own settings, then pressing a hotkey to bring up the
mod's own overlay, which is where the actual neural-rendering controls
live. `version.dll`'s own embedded strings confirm this precisely:

```
Danielblnc's DLSS-NR on AMD v0.3.0   (End to close)
End: hide   Up/Down: select   Left/Right: adjust   Enter: toggle
...
FSR is not active: enable FSR (any quality mode) in the game's graphics settings to use DLSS-NR.
```

This means every earlier line of investigation premised on "make DLSS
selectable in the menu" (§14's `UserSettings.json` analysis, the direct
JSON edit that briefly broke the settings UI and was reverted, the
`d3d12-proxy` DXGI vendor-spoofing test, the NVAPI-spoofing analysis)
was solving a problem that didn't need solving - `ResolutionScaling:
FSR3` was already the *correct*, required configuration the entire
time, not a misconfiguration.

### 24b. The overlay was tested directly and confirms the real blocker precisely

With FSR active and HIP fully working (post-§23's fixes), the user
opened the real in-game overlay (`End` key) and toggled NR on:

```
overlay shown (ready 1, queue 00000000768F45A0, enabled 1)
...
overlay hidden
...
overlay shown (ready 1, queue 00000000768F45A0, enabled 1)
```

`enabled 1` confirms NR is genuinely turned on via the overlay - yet
`dispatches` stayed at exactly 0 throughout, identical to every prior
session. **This rules out every remaining non-technical explanation**
(wrong settings, overlay not found, NR not enabled) and isolates the
cause precisely to the interop gap already fully diagnosed in §18/§20/
§22: `hipImportExternalMemory: operation not supported` →
`passing the colour through untouched`. With every other variable now
controlled for and confirmed correct, this is no longer one of several
possible explanations - it is confirmed as the sole remaining blocker.

### 24c. The OptiScaler/multipass alternative in this repo does not avoid the blocker either

The user asked whether the older `OptiScaler-AMD-PreSR-Multipass-v1.2`
package already present in this repo (`windows/Arquivos necessarios/`,
and a legacy copy at `windows/optiscaler-approach-root-legacy/`) offers
a way around the interop wall. Checked directly:

- Its own installer (`INSTALAR_AMD.ps1`) explicitly **removes**
  danielblnc's `version.dll` before installing (backs it up and moves it
  aside), with the comment *"The original AMD proxy would otherwise
  evaluate NR a second time after FSR"* - confirming this project's own
  prior work independently discovered and hit the exact same
  "two hook chains conflict" risk flagged in §24 (and earlier, re:
  OptiScaler generally) - the fix chosen was to run one or the other,
  never both.
- Critically, `dlssnr_amd_pass1.dll` (one of the three DLLs this
  alternative installs) **imports the identical
  `hipImportExternalMemory`/`hipExternalMemoryGetMappedBuffer`/
  `hipDestroyExternalMemory` functions** from `amdhip64_7.dll` that
  danielblnc's current runtime uses. Confirmed via `objdump -p`.

**This alternative pipeline is built on the exact same D3D12↔HIP
zero-copy interop mechanism, and would hit the identical Wine/
vkd3d-proton wall.** It is also a strictly older, less capable
architecture (three separate pass DLLs instead of one runtime,
requires OptiScaler's own conflicting hook chain) predating
danielblnc's current single-runtime design. There is no technical
reason to switch to it - it offers no way around §18/§20's finding, only
added complexity and a real capability regression.

### 24d. Community Proton forks checked directly for the specific missing feature - both negative

Beyond §19/§20's check of CachyOS, GE-Proton's actual patch source was
checked directly (`patches/protonprep-valve-staging.sh`, not just
changelog summaries): no mention of `external_memory`, `shared_handle`,
`opaque_fd`, or `CreateSharedHandle` anywhere. Its only OptiScaler-
related patch is a Wine-side auto-injection convenience hook (`0002-
HACK-ntdll-add-optiscaler-inection-hack.patch`) for the spoofing/
upscaler-replacement use case already ruled out in §24c - unrelated to
the interop gap.

**Conclusion: three community/upstream Proton variants checked
directly against source (mainline mainline vkd3d-proton via §18,
CachyOS via §19, GE-Proton via this section), plus the newest available
Proton Experimental build tested empirically (§20) - none patch or fix
the specific missing `VK_KHR_external_memory_fd` device-extension
enablement that blocks this feature.** This is not a version-lag
problem findable by trying yet another Proton build; it would require
an actual, currently-nonexistent patch to vkd3d-proton itself.

### 24e. Where this leaves the project

Every configuration, overlay, settings, and alternative-pipeline
avenue has now been tried and precisely diagnosed. The real HIP/kernel
execution engine works correctly end-to-end (§23, confirmed under a
genuine Steam-launched sandboxed session, not just scripted tests) -
danielblnc's Windows implementation and this Linux port's own kernel-
execution layer are both proven sound. The sole remaining gap is real
per-frame zero-copy D3D12↔HIP memory sharing, which depends on a
Wine/vkd3d-proton-side fix that does not currently exist in any variant
checked. This is now a well-evidenced, precisely-scoped, externally-
blocked state - not an unexplored one.

## 25. vkd3d-proton patch written, built, and tested - real blocker found one layer deeper, in Wine itself (2026-09-13)

Per user request, wrote and built the vkd3d-proton patch scoped in
§18's investigation, cloned to `~/repos/vkd3d-proton` for continued
work. The patch itself is complete, correct, and cleanly built:

- `include/private/config_flag_decl.h`: new opt-in
  `VKD3D_CONFIG=external_memory_fd` flag.
- `include/private/config_flags.h`: `reserved0` bit count adjusted
  (24→23) to keep the config bitfield's expected size, per the
  existing code's own comment warning this would be needed.
- `libs/vkd3d/vkd3d_private.h`: new `bool KHR_external_memory_fd;`
  field.
- `libs/vkd3d/vulkan_procs.h`: new `vkGetMemoryFdKHR`/
  `vkGetMemoryFdPropertiesKHR` function-pointer declarations, mirroring
  the existing win32 pair exactly.
- `libs/vkd3d/device.c`: `VK_KHR_EXTERNAL_MEMORY_FD` added to
  `optional_device_extensions[]`, gated behind the new config flag
  (`VK_EXTENSION_COND`) so it is off by default and cannot affect any
  other game.
- `libs/vkd3d/resource.c`: `D3D12_HEAP_FLAG_SHARED`'s memory-export path
  now checks `device->vk_info.KHR_external_memory_fd` at runtime and
  requests `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` instead of
  `..._WIN32_BIT` when it's available - the actual behavioural change.
- Two new tests added following the project's own conventions
  (`tests/d3d12_external_memory_fd.c`, registered in `tests/meson.build`
  and `tests/d3d12_tests.h`): one exercising the real fd-export path,
  one confirming the opt-in flag correctly leaves default behaviour
  (`vkGetMemoryFdKHR` unavailable) untouched when unset.

Built successfully via `./package-release.sh dev-patch /tmp/... --no-package --dev-build`
(meson/ninja/mingw-w64/glslang-tools) with no errors, producing a real
`d3d12.dll`/`d3d12core.dll` pair with the new code confirmed compiled in
(`strings` shows `external_memory_fd`/`VK_KHR_external_memory_fd` in
`d3d12core.dll`, which is where vkd3d-proton's real implementation
lives - `d3d12.dll` is a thin loader shim).

**Tested against a fully isolated copy of Proton 11.0** (the entire
Proton directory duplicated to `Proton 11.0 - vkd3d-patch-test`, never
touching the user's real, working Proton install or Steam configuration
- only the two vkd3d-proton files were swapped inside the copy, and it
was run only via manually-scripted `proton run` invocations, never
registered with Steam). Re-ran the exact §17/§20 chain
(`CreateHeap`+`D3D12_HEAP_FLAG_SHARED` → `CreatePlacedResource` →
`GetVulkanHeapInfo` → native-side `vkGetMemoryFdKHR`) with
`VKD3D_CONFIG=external_memory_fd` set.

**Result: still `vkGetDeviceProcAddr(vkGetMemoryFdKHR)` returns NULL,**
identically to the unpatched build. Confirmed via `VKD3D_DEBUG=info`
that the config flag itself was read correctly
(`VKD3D_CONFIG='external_memory_fd'.`), and confirmed via
`vulkaninfo --summary` that the real RADV driver on this host does
support `VK_KHR_external_memory_fd` - the flag parsing and the driver
both work. The failure is neither of those.

**Root cause, confirmed decisively:** a `WINEDEBUG=+loaddll` trace
during the test shows `winevulkan.dll` (Wine's own Vulkan
implementation, the real target `vulkan-1.dll` forwards to) loading
into the process - vkd3d-proton is a genuine Windows PE binary and,
like any other Windows Vulkan/D3D12 application running under Wine,
must go through this same layer to reach Vulkan at all; it has no
special/private access path around it. Fetching Wine's actual generated
`dlls/winevulkan/vulkan_thunks.c` (73,600 lines, real, complete thunks
for hundreds of Vulkan functions including a full implementation of
`vkGetMemoryWin32HandleKHR`) confirms **zero occurrences of
`vkGetMemoryFdKHR` or `VK_KHR_external_memory_fd` anywhere in the
file.** Wine's own Vulkan layer has never implemented this extension at
all - no thunk exists, so it can never be advertised to *any* PE-side
Vulkan consumer, vkd3d-proton included, regardless of what vkd3d-proton
itself does with its own device-extension list.

**This means the vkd3d-proton patch, while complete and correct for
what it controls, is necessary but not sufficient.** A working fix
needs a second, separate, and materially larger patch to Wine's
`winevulkan` component itself - implementing real
`vkGetMemoryFdKHR`/`vkGetMemoryFdPropertiesKHR` thunks and exposing the
extension through `winevulkan`'s own generation/allowlist mechanism
(`dlls/winevulkan/make_vulkan`). This is a different, larger, more
heavily-scrutinized upstream project than vkd3d-proton, and changes the
practical shape of the "patch it ourselves" option from §18: it is now
understood to require coordinated changes across two upstream projects,
not one contained one.

**Where this leaves the PR idea:** the vkd3d-proton half of the work is
real, tested (to the extent testable without the Wine half), and
plausibly upstreamable on its own merits once a genuine consumer exists
- but it cannot demonstrate working end-to-end behaviour, and therefore
cannot be validated as *correct* beyond "builds and doesn't break
anything," without the Wine counterpart also existing. Submitting it
alone would be reasonable as a preparatory contribution, but it would
not, by itself, unlock this project's feature.

Test artifacts from this investigation (`DlssnrPatchTestSpikeW` and the
`HIP_CALL_TEST_HEAP_FD_IMPORT` opcode in `windows-runtime-bridge/hip-unixlib`, the
duplicated `Proton 11.0 - vkd3d-patch-test` directory, and the
throwaway `.hip-bridge-patch-test-prefix`) should be cleaned up now
that the question is answered. The vkd3d-proton patch itself
(`~/repos/vkd3d-proton`, uncommitted working-tree changes) and the new
test file are being kept as real, reviewable work product.

## 26. Session wrap-up: full patch stack built, real crash found, stopping here (2026-09-13)

Following §25's finding that the vkd3d-proton patch alone wasn't
sufficient, built the companion Wine `winevulkan` fix and tested the
complete three-part stack together:

1. **vkd3d-proton patch** (§25) - opt-in `VKD3D_CONFIG=external_memory_fd`,
   built and confirmed compiled in.
2. **Wine `winevulkan` patch** - traced the real gap to
   `dlls/winevulkan/make_vulkan`'s `UNEXPOSED_EXTENSIONS` set, which
   already listed `VK_KHR_external_memory_fd` under a comment reading
   "generate headers for... but not expose to applications (useful for
   test commits)" - meaning the generator infrastructure already existed
   and only needed one line removed to produce a real, complete,
   correctly-wired thunk (confirmed: full 32/64-bit marshaling code,
   registered in the runtime dispatch table exactly like the working
   `vkGetMemoryWin32HandleKHR`).
3. Getting a working build required matching Wine versions precisely:
   a first attempt against mainline `wine-mirror/wine` produced a real
   binary but hit an internal ABI version mismatch against Proton
   11.0's other components (`vulkan wants 48 but win32u has 47`, then a
   cascading `undefined symbol: PsGetCurrentProcessId` once `win32u`
   was also swapped) - resolved by rebuilding against Valve's actual
   `ValveSoftware/wine` fork at the exact matching tag
   (`proton-wine-11.0-2c`, confirmed via the installed Proton's own
   `version` file), which eliminated the ABI mismatch entirely without
   needing to touch any other Wine component.

**Result of the full end-to-end test (all three patches installed
together in an isolated test Proton copy): a real crash**, not a clean
pass or a clean "still not supported":

```
err:vulkan:vkEnumeratePhysicalDevices Exception 0xc0000005 in Unix call.
```

An access violation inside the native Vulkan call itself. Correlating
with system logs from the same window turned up a genuine OOM event
(`Out of memory: Killed process ... xalia.exe ... anon-rss:13705532kB`)
that appears to have caused broader system distress during this test -
`xalia.exe` is a standard Proton accessibility helper unrelated to any
of this session's own code, but the test's own `rundll32.exe` process
was independently observed using ~3.4 GB of RAM for what should have
been a trivial single-function-call test, strongly suggesting the crash
above triggered a runaway allocation or bad recovery path rather than
failing cleanly.

**Decision: stop here rather than keep iterating live.** The source-level
fix (both halves) is real, complete, well-understood, and separately
buildable - not a dead end - but the specific combination now has a
demonstrated crash bug that hasn't been diagnosed, and continuing to
test it live carries a real, evidenced risk of further system
instability. This is a deliberate stopping point, not an abandoned
trail: the two patches, this section's build/version-matching notes,
and the crash signature are all preserved for whenever this is picked
back up.

**State left behind, for continuation:**
- `~/repos/vkd3d-proton` - uncommitted working-tree patch + tests
  (`tests/d3d12_external_memory_fd.c`), builds cleanly on its own.
- `~/repos/wine-proton` - Valve's fork at `proton-wine-11.0-2c`,
  uncommitted one-line `make_vulkan` change, builds cleanly on its own
  (`winevulkan.dll`/`.so`), verified ABI-compatible with the real Proton
  11.0 install (no more version-mismatch errors once matched correctly).
- `~/repos/wine` (mainline `wine-mirror/wine`) - same one-line change,
  kept only as a reference for how the generator behaves upstream; not
  ABI-compatible with Proton 11.0 directly, not needed for further work
  here.
- This project's own production files (`windows-runtime-bridge/hip-unixlib/`) are back
  to their clean, working, pre-spike state and redeployed to the real
  Proton 11.0 install - confirmed unaffected by any of this session's
  patched-Proton testing (`d3d12core.dll`/`winevulkan.dll` in the real
  Proton 11.0 install verified as pristine, unpatched originals
  throughout).
- The crash itself (`vkEnumeratePhysicalDevices` access violation) is
  not yet diagnosed - the next real step, if this is picked back up,
  would be isolating which of the two patches (or their interaction)
  causes it, likely by testing each patch alone against a
  version-matched Wine/vkd3d-proton pair rather than both together.

No changes were made to the user's real Proton 11.0 install, Steam
configuration, or the real Cyberpunk 2077 installation at any point in
this investigation - all testing used isolated, disposable copies,
verified clean at the end of each phase.

## 27. The fix confirmed working live, in the real game - and the real next blocker (2026-09-14)

Picked back up from §26's stopping point (an undiagnosed crash when
testing the vkd3d-proton + Wine patch stack together). This session
re-scoped the patch entirely, built it through Valve's own official
build pipeline instead of a hand-reconstructed one, and - for the
first time in this whole project - watched real, non-zero file
descriptors flow through `hipImportExternalMemory` in a live,
in-game session, with Daniel's runtime running against them.

### 27a. The original patch was scoped to the wrong code path

Before touching code, investigated Daniel's real runtime directly
(`version.dll`'s own debug strings) rather than continuing to guess.
Two decisive findings:

- **Daniel's interop allocations are buffers, not textures** (`dlssnr
  buffer #%d heap %d %llu B`). The §25 patch's export-capable-memory
  logic lived entirely inside the *texture* branch of
  `d3d12_resource_create_committed()` - buffers never reached it at
  all, regardless of the crash.
- **Daniel's runtime gets its handle via `ID3D12Device::CreateSharedHandle`**
  (`interop: CreateSharedHandle failed 0x%08lx ...; this input falls
  back to CPU readback`), a separate vkd3d-proton function from the
  allocation-time `export_info` logic §25 touched. That function
  (`d3d12_device_CreateSharedHandle` in `device.c`) tries
  `D3DKMTShareObjects` first (confirmed broken under Wine since §14),
  then a Win32 handle export - never anything FD-shaped, patched or
  not.

So the §25 patch, even if the crash had been fixed, would have done
nothing for the actual caller: it modified a function Daniel's DLL
never calls, for a resource type it never allocates. Also worth
recording as a genuinely good sign found along the way: Daniel's own
runtime already has a graceful CPU-readback fallback for exactly this
failure - nothing in Cyberpunk itself was ever crashing from this bug,
it was silently taking the slow path.

**Re-scoped the patch** to the two real allocation routes for shared
buffers - `d3d12_resource_create_committed()`'s buffer branch and
`d3d12_heap_init()` (covering both `CreateCommittedResource` and
`CreateHeap`+`CreatePlacedResource`) - and added the actual fix inside
`d3d12_device_CreateSharedHandle()` itself: when the opt-in flag is
set, the resource is a buffer, and the underlying memory was allocated
FD-exportable, skip `D3DKMTShareObjects`/the Win32 path entirely and
call `vkGetMemoryFdKHR` directly, returning the fd smuggled through the
`HANDLE` out-parameter as a plain integer (this only ever runs under
Wine, so no real Win32 handle-table semantics need to be honored).
Guarded on `resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER`
explicitly - not just the device-level capability flag - so a shared
*texture* (still Win32-only per the untouched original code) can never
mistakenly take this path. Also added a null-check on the resolved
`vkGetMemoryFdKHR` proc pointer itself, not just the capability flag,
after finding (see §27c) that the two can legitimately disagree.

Rewrote the test suite to match: six tests covering both allocation
routes with the opt-in flag on, the same six with it off (regression
coverage), a shared-texture test proving the fd path never fires for
textures, and a plain non-shared-buffer test proving the common case
is untouched. All six passed cleanly, repeatably, memory-capped, once
the real crash (see §27c) was actually diagnosed and fixed.

### 27b. Building through Valve's own pipeline instead of guessing at it

The §26 crash was hunted down properly this session by first ruling
out version drift: cloned `ValveSoftware/Proton` at tag
`proton-11.0-2c` and confirmed its pinned `wine` submodule commit
(`dc26e618...`) and vkd3d-proton's own Vulkan spec version
(`VK_XML_VERSION = "1.4.339"`, hardcoded and pinned in source) both
matched exactly what had already been hand-built. The wine *source*
was never the problem.

The real gap: Proton's actual build recipe configures wine with just
`--enable-werror --with-mingw=<mingw> --disable-tests
--enable-archs=x86_64,i386 --enable-win64` - none of the roughly
fifteen `--without-X` flags (`--without-dbus`, `--without-udev`,
`--without-opengl`, etc.) the earlier hand-built tree needed just to
get past missing host packages. The official build gets all of that
for free by never touching host libraries at all - it runs inside a
pinned Docker/Podman "Proton SDK" container
(`registry.gitlab.steamos.cloud/proton/steamrt4/sdk/x86_64:4.0.20260331.220802-0`
for this tag) with the exact dependency versions Valve builds against.
A partially-stripped-down build mixed into an otherwise fully-featured
official binary is a far more mundane explanation for §26's crash than
anything Vulkan-specific - and one of that session's actual crash logs
(`device_notify_proc failed to get event`) is exactly udev/dbus-shaped.

Built via this pipeline (`make redist`, out-of-tree on the larger
disk partition; Docker's own storage also had to be relocated there
first - root filesystem was at 99% full, `/var/lib/docker` and the
separate `/var/lib/containerd` both needed their `data-root`/`root`
config pointed at the new location, each requiring a stop → copy →
repoint → restart → verify → delete-old cycle done manually by the
user since it needs `sudo` with an interactive terminal this session
doesn't have). One real build failure along the way, unrelated to any
patch: the initial `git submodule update --init` wasn't recursive, so
`dxvk-nvapi`'s own nested `Vulkan-Headers` submodule (and several
inside vkd3d-proton's `dxil-spirv` chain) were never fetched, produc­ing
`Include dir ./external/Vulkan-Headers/include does not exist` at
configure time. Fixed with `git submodule update --init --recursive`
across the whole tree; the build then completed cleanly.

Applying the vkd3d-proton patch to this pinned commit
(`212991f`, 2026-07-29) needed real porting, not a plain copy: this
commit predates an upstream restructuring of vkd3d-proton's config-flag
system (from a generic `VKD3D_DECL_CONFIG`/`config_flag_decl.h`
X-macro mechanism back to a simpler plain `#define
VKD3D_CONFIG_FLAG_*` bitmask list in `include/vkd3d.h`, with a
name-string table in `device.c`). Ported the same logic onto the older
scheme, reusing a genuinely vacant bit (`/* Bit 46 is vacant */`) for
`VKD3D_CONFIG_FLAG_EXTERNAL_MEMORY_FD`. Everything else (the buffer
allocation changes, the `pNext` plumbing, the `CreateSharedHandle`
logic and its guards, all six tests) applied identically, since none
of it touches the refactored area. The companion Wine `winevulkan`
one-line fix applied with zero changes, since that submodule's commit
matched exactly.

### 27c. The real crash, and why it wasn't in our code at all

First attempts to run the isolated test suite against this properly-built
Proton produced three different, confusing symptoms across three
different environments in one evening (a null-pointer crash reachable
even with the opt-in flag off; the same crash disappearing on a retry
against the same binaries; a "divide by zero" at a fixed address
inside the test binary itself when run against the patched Wine
build). Root-caused methodically rather than guessed at:

1. **The first "crash" was a fresh-Wine-prefix flake**, not a bug -
   re-running the exact same test against the same binaries once the
   prefix had been through one full wineboot passed cleanly, repeatably.
2. **The real, reproducible bug**: `vkGetMemoryFdKHR` is a null
   function pointer at call time (`rip:0000000000000000`, backtrace
   return address inside `d3d12core.dll`) when the opt-in flag is set.
   Root cause: vkd3d-proton's device-extension enablement only checks
   that the extension *name* enumerates via
   `vkEnumerateDeviceExtensionProperties` - it doesn't verify the
   specific function pointer actually resolved via
   `vkGetDeviceProcAddr`. Under an unpatched Wine, the name enumerates
   fine (Wine's thunk generator produces the code) but the function
   pointer itself never resolves (deliberately hidden per §19's
   original finding). Fixed by checking `vk_procs->vkGetMemoryFdKHR`
   itself, not just the capability bool, before ever calling through it
   - falling back to the pre-existing Win32/KMT path if it's unset,
   with a `WARN` explaining why.
3. **A second, real logic bug this same investigation surfaced**: the
   original `CreateSharedHandle` check only gated on the *device-level*
   capability flag, not whether *this specific resource's* memory was
   actually allocated FD-exportable. A shared *texture*'s memory is
   Win32-only (deliberately, per §27a's scoping), so calling
   `vkGetMemoryFdKHR` against it is invalid Vulkan usage against memory
   that was never allocated with a matching export type - a second,
   independent way to reach the same null-pointer crash. Fixed by also
   gating on `resource->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER`.
4. **The "divide by zero" against the patched Wine build**, investigated
   last, turned out not to be either of the above: with both fixes
   applied, the full six-test suite passed cleanly and repeatably
   against the byte-compatible build (§27b), including the opt-in-on
   tests actually exercising and succeeding through the new fd-export
   path (confirmed via the `FIXME: access ... not handled` line our
   own code prints immediately before returning success - direct proof
   the path was reached and returned `S_OK`). That earlier crash is
   now understood to have been specific to the *manually-built* Wine
   tested in §26/§20, not a property of the fix itself.

### 27d. Confirmed live, in the real game, for the first time in this project

Installed the byte-compatible build as a selectable Proton version
(`~/.steam/root/compatibilitytools.d/fdtest-11.0-2c`, copied from the
`make redist` output) so the user could launch Cyberpunk normally
through Steam rather than through a scripted `proton run` - closing
the "genuine Steam sandbox" gap §23 had flagged as a standing risk for
anything tested only one way.

First real launch attempt failed immediately - `REDprelauncher.exe`
aborted before ever reaching the real game
(`STATUS_DLL_INIT_FAILED`/`c0000142`). Root cause, found directly in
the trace log: the prelauncher does a routine AMD hardware-capability
probe that loads `amdhip64_7.dll` too, and this brand-new Proton build
had never had the project's own HIP shim deployed into it at all - no
`amdhip64_7.dll`/`.so` pair existed anywhere in it, only in the
original Proton 11.0 install. `__wine_init_unix_call()` correctly
failed (there was nothing to pair with), `DllMain` correctly returned
`FALSE`, and per ordinary Windows semantics a `DllMain` that returns
`FALSE` during process init is fatal to the *entire* process - taking
`REDprelauncher.exe` down with it before the real game ever started.
Not a design bug in the shim at all; fixed by copying the existing,
already-built `amdhip64_7.dll`/`.so` pair from
`windows-runtime-bridge/hip-unixlib/{x86_64-windows,x86_64-unix}/` into the new
build's `files/lib/wine/` directories, matching the original Proton
11.0 install exactly.

With that fixed, the real game launched, ran stably (memory checked
repeatedly throughout - never approached the caps that mattered, no
repeat of the §20-era OOM/crash pattern), and Daniel's overlay showed
and responded to toggling. And then, for the first time in this
project's history, real interop data:

```
# Earlier / before this fix - the original bug this whole project started from:
hipImportExternalMemory: type=5 (D3D12Resource) handle=0000000000000000 name=0000000000000000 fd=0 size=29491200 flags=1

# Live, in the real game, this session:
hipImportExternalMemory: type=5 (D3D12Resource) handle=0000000000000181 name=0000000000000000 fd=385 size=29491200 flags=1
hipImportExternalMemory: type=5 (D3D12Resource) handle=0000000000000182 name=0000000000000000 fd=386 size=29491200 flags=1
hipImportExternalMemory: type=5 (D3D12Resource) handle=0000000000000183 name=0000000000000000 fd=387 size=14745600 flags=1
```

Real, non-zero file descriptors, successfully imported by Daniel's
runtime. `hipMalloc`/`hipLaunchKernel` calls succeeding continuously
throughout. This is the concrete resolution of §14's original finding
- the fix works, end to end, in the real game.

Also settled along the way: the DLSS-NR neural pass never actually
dispatched during this session (`dispatches 0` for the entire
~120,000+ frame session, `route fsr` throughout) while FSR was set to
"Native AA" - because Native AA renders at full resolution with FSR
only handling anti-aliasing, so there was genuinely no upscaling work
for the neural pass to do. Switching to an actual FSR scaling preset
(Quality) immediately produced real, resolution-aware staging setup
(`staging ready: colour 1707x960 ...; motion 1707x960 ...; depth
1707x960 ...`) that had never appeared before - confirming the game's
own render-target/resolution change is correctly detected and drives
real reconfiguration inside Daniel's runtime, independent of anything
this session patched.

### 27e. The real next blocker: a second, separate synchronization requirement

Even with real interop memory flowing and FSR genuinely scaling, the
neural pass still never dispatched (`dispatches 0` throughout, in
active gameplay, not just menus). Daniel's own runtime explains why
directly, in its own log output:

```
inline wait: single spin dispatch (predication buffer unavailable)
inline mode needs zero-copy interop; running asynchronously
pre-upscale mode needs inline mode (a stale correction would be accumulated
  by the upscaler); passing the colour through untouched
```

This is a **second, distinct requirement from the one this session
fixed** - not a resolution or settings issue. Investigated directly
against `version.dll`'s own strings rather than guessing further, and
found a concrete, checkable, and very likely root cause:

Daniel's runtime supports an "inline" mode - genuine same-frame,
GPU-side synchronization where the game's own render queue writes a
flag the HIP side spin-waits on (`predicated spin slices (preemptible
between slices)`, `inline: %d clean jobs; wait budget back to %d ms`),
avoiding any CPU round-trip. "Pre-upscale" mode (whichever mode is
currently selected) requires this specifically - without it, applying
a correction one frame late would visibly smear ("a stale correction
would be accumulated"), so it deliberately refuses and passes the
frame through untouched instead of risking that. This "predication
buffer" is a separate GPU resource from the colour/motion/depth
buffers this session's fix already gets flowing correctly.

One string is the strongest lead by far:

```
inline flag check: a store from the game's queue was NOT seen by the
GPU wait within %u iterations (%.0f ms; readback %s it). Inline mode
will time out on this driver (HIP runtime %d; every working system
runs 70260201 / Adrenalin 26.x): update the driver, or set Inline=0
in dlssnr_on_amd.ini
```

Daniel's runtime checks the reported HIP runtime version against a
specific known-good value (`70260201`) and warns when it doesn't
match. Checked this project's own shim directly:
`windows-runtime-bridge/hip-unixlib/native.c`'s `unix_driver_get_version()` and
`unix_runtime_get_version()` - the functions that back
`hipDriverGetVersion()`/`hipRuntimeGetVersion()` - are **stubs that
hardcode `a->version = 0`** rather than forwarding to the real
`hipDriverGetVersion`/`hipRuntimeGetVersion` calls the way every other
function in this shim does (via `WINE_UNIX_CALL` to the real native
HIP library). Every version query Daniel's runtime makes currently
gets back `0`, not the real host driver's actual version - which
would explain a version-based compatibility check disabling or
derating "inline" mode outright, independent of whether the actual
GPU-side synchronization mechanism would otherwise work.

**Not yet fixed or tested this session** - found and recorded as the
concrete next step. The fix, if this hypothesis holds, is small and
well-scoped: make `unix_driver_get_version()`/`unix_runtime_get_version()`
forward to the real `hipDriverGetVersion()`/`hipRuntimeGetVersion()`
from the native HIP library, the same way every other real (non-stub)
function in `native.c` already does, instead of hardcoding `0`. Worth
testing in isolation before assuming it's the *whole* remaining gap,
since the "predication buffer" itself may still need its own,
separate GPU resource wired up even once the version is reported
correctly - but it is a concrete, checkable, single-line-diagnosis
away from being ruled in or out, which is a meaningfully better
position than "predication buffer unavailable" was on its own.

No changes were made to the user's real Proton 11.0 install at any
point this session either - the byte-compatible build lives entirely
in its own separate, clearly-named compatibility tool
(`fdtest-11.0-2c`), selectable and removable independently of the
user's normal setup. The real Cyberpunk installation and its
compatdata *were* used directly this session (with the user's explicit
authorization) for the final live validation in §27d, since that was
the whole point of this session's work - but no files were modified
inside it beyond what the game/Proton/Daniel's own runtime wrote
during normal operation, and the stray `d3d12-proxy` test `dxgi.dll`
left over from an earlier session (§ prior to this doc's numbering)
was found and removed before testing began.

## 28. HIP version-stub fix implemented (real bug, but not today's blocker) - and scoping the actual "predication buffer" gap (2026-09-14)

### 28a. The version-stub fix: implemented, tested, deployed - and cleared of blame

`windows-runtime-bridge/hip-unixlib/native.c`'s `unix_driver_get_version()` and
`unix_runtime_get_version()` were confirmed as genuine stubs
(hardcoded `version = 0`, never calling the real
`hipDriverGetVersion`/`hipRuntimeGetVersion` the way every other
function in this shim forwards to its real native counterpart via
`dlsym`). Fixed properly:

- Added `hipDriverGetVersion_t`/`hipRuntimeGetVersion_t` typedefs and
  `p_hipDriverGetVersion`/`p_hipRuntimeGetVersion` function pointers,
  resolved via `dlsym` in `ensure_loaded()` alongside every other real
  HIP entry point this shim uses.
- The actual forward-or-fallback decision was factored out into a new,
  pure, Wine/HIP-independent module - `version_query.c`/`.h` -
  mirroring the project's existing `registry.c`/`.h` separation
  exactly, specifically so it stays unit-testable with a plain host
  compiler. `hip_version_query_apply()` takes whether the real symbol
  resolved plus what it returned, and either forwards those values
  verbatim or falls back to `version=0, ret=-1` (never fabricating a
  version number) if the symbol never resolved.
- Four new tests added (`test_version_query.c`, wired into `make
  test` alongside the existing registry tests): missing-symbol
  reports zero+failure (two variants, including one confirming
  leftover/garbage "real" values are never read when the symbol is
  absent), and present-symbol forwards both a real success and a real
  failure verbatim. All passing, `make test` now runs both suites.
- Rebuilt cleanly (`make clean && make all`), redeployed to all four
  live locations this project currently touches: the real Cyberpunk
  game folder, the real Proton 11.0 install, and both copies (the
  Steam-installed compatibility tool and its `redist/` source) of the
  `fdtest-11.0-2c` build from §27b - `md5sum` confirmed identical
  everywhere.

**Tested live and found not to be today's blocker.** A fresh
in-game session's full `amdhip64_7_unixlib_pe.log` (1,719 lines,
covering from `amdhip64_7 unixlib PE side initialized` onward) shows
**zero calls** to either `hipDriverGetVersion` or
`hipRuntimeGetVersion`, even though `version.dll` genuinely imports
both (confirmed via `objdump -p`). Re-reading Daniel's own diagnostic
strings in the order they'd actually fire clarifies why: the
`"70260201 / Adrenalin 26.x"` version-check message belongs to a
*timeout* diagnostic (`"a store from the game's queue was NOT seen by
the GPU wait within %u iterations"`) that only fires once inline mode
has actually started spin-waiting. `"predication buffer unavailable"`
is an earlier, *setup*-stage failure - inline mode never gets far
enough to reach the point where a version check would matter. So the
fix was real (the stub was a genuine, silent bug - any future code
path that *does* reach a version check would previously always have
seen `0`) and is now correctly in place for whenever the gap below
clears, but it does not move today's actual blocker.

### 28b. Scoping "predication buffer unavailable"

Investigated directly rather than continuing to guess from the two
log lines alone. Two pieces of hard evidence, gathered without
changing or running anything new:

**1. Daniel's import table is closed, and it rules a few things out.**
`objdump -p version.dll` lists every HIP function this runtime can
possibly call - `hipMalloc`, `hipFree`, `hipMemcpy`/`hipMemcpyAsync`,
`hipMemset`/`hipMemsetAsync`, `hipEventCreate(WithFlags)`, `hipEvent
Record`/`Query`/`Synchronize`/`ElapsedTime`, `hipStreamCreateWithFlags`,
`hipStreamSynchronize`, `hipImportExternalMemory`/
`hipExternalMemoryGetMappedBuffer`/`hipDestroyExternalMemory`, plus
the version/device/error-string/launch-config functions already
covered. **Notably absent**: no `hipStreamWaitValue32`/
`hipStreamWriteValue32` (HIP/CUDA's actual stream-memory-ops API for
GPU-side wait-on-a-value-in-memory), no `hipHostMalloc`/
`hipHostRegister`/`hipHostGetDevicePointer` (pinned/mapped host
memory). Whatever the "predicated spin slices" mechanism is, it is
built *only* from the functions in this list - almost certainly a
plain HIP kernel (launched the ordinary way, via `hipLaunchKernel`,
same as every other kernel this runtime runs) that spins in a loop
reading ordinary device memory, not a specialized stream-memory-ops
primitive HIP itself provides.

**2. The tiny allocations in this session's own log are a red
herring - checked and ruled out.** The full session log has entries
as small as `size=4` and `size=16` bytes, which looked at first like
plausible predication-flag-buffer sizes. Checked directly: every one
of them is `hipMalloc(size=4)`/`hipMalloc(size=16)` - ordinary
internal HIP device memory Daniel's kernels allocate for their own
small scalar/parameter bookkeeping, with no D3D12/interop involvement
at all. There is no `hipImportExternalMemory` call anywhere in the
log at a size that looks like a predication buffer - the five
`hipImportExternalMemory` calls this session are all large
(colour/motion/depth/output-sized, matching §27d exactly). This means
the predication buffer's creation - whatever it is - is not reaching
HIP's import call at all; it is failing earlier, entirely on the
D3D12/vkd3d-proton side, before Daniel's runtime ever gets far enough
to ask HIP to import anything for it.

**Two ranked hypotheses for what "predication buffer" actually is,**
neither confirmed, both requiring live testing (not further static
analysis) to distinguish:

- **More likely, and independently confirmed to genuinely exist in
  vkd3d-proton**: literal D3D12 predication -
  `ID3D12GraphicsCommandList::SetPredication`, a real D3D12 API that
  lets a command list conditionally skip subsequent commands based on
  a value in a GPU buffer, entirely GPU-side, no CPU round-trip.
  Confirmed vkd3d-proton actually implements this
  (`d3d12_command_list_SetPredication` in `command.c`), mapped onto
  Vulkan's `VK_EXT_conditional_rendering`
  (`vkCmdBeginConditionalRenderingEXT`) - but only when the Vulkan
  device reports the `conditionalRendering` feature
  (`device.c`: `if (!physical_device_info->conditional_rendering_
  features.conditionalRendering) vulkan_info->EXT_conditional_
  rendering = false;`). If this feature isn't ending up enabled in
  this specific environment for any reason, `SetPredication` would be
  a silent no-op or failure, which lines up with Daniel's runtime
  falling back gracefully rather than crashing. Cannot be confirmed
  without a Vulkan device feature dump against the actual running
  vkd3d-proton instance (no D3D12 method names show up in binary
  strings or an import table the way HIP's C-linkage functions do,
  so this can't be checked the same way §28's HIP-import-table check
  was) - this would need either a `VKD3D_DEBUG`-driven feature log at
  device creation, or a small standalone Vulkan feature-query test
  against the exact same GPU/driver, to actually confirm or rule out.
- **Less specific, equally plausible**: an entirely custom mechanism
  of Daniel's own design, unrelated to D3D12's literal predication
  API - a small shared buffer (created and exported the same way as
  the colour/motion/depth buffers, just much smaller) that the game's
  compute/graphics queue writes a flag into and a HIP kernel polls,
  with "predicated" describing the *kernel's own internal branching*
  rather than any D3D12/Vulkan predication feature. Under this
  reading, the buffer's creation is failing for the same *kind* of
  reason §27a-c's fix addressed for the larger buffers - except
  something about this one (its size, its exact resource flags, or a
  code path this session's fix doesn't cover) still isn't going
  through correctly, and would need direct tracing of the relevant
  `CreateCommittedResource`/`CreateSharedHandle` calls to identify
  which.

### 28c. What implementation would concretely look like, for either case

**If it's literal D3D12 predication (hypothesis 1):** the fix, if
needed at all, would live entirely in vkd3d-proton/the Vulkan driver
configuration, not in this project's own HIP shim or D3D12 patch code
- there is no "buffer export" fix to write here, since
`SetPredication`'s buffer is a plain GPU resource the game already
creates and writes to directly; the gap (if this hypothesis holds)
would be why `conditionalRendering` isn't reporting as enabled/usable
in this environment. First real step: confirm whether
`VK_EXT_conditional_rendering` actually enables against the host's
RADV driver at all (this project already confirmed `VK_KHR_
external_memory_fd` was genuinely supported by RADV in §14-era
investigation via `vulkaninfo`; the same check, `vulkaninfo | grep -A2
conditional_rendering`, would answer this specific question directly
and cheaply, no game or build required). If RADV supports it but
vkd3d-proton still isn't enabling it in the byte-compatible build from
§27b specifically, that would point at a build/config gap similar to
§27b's own root cause, not a missing patch.

**If it's Daniel's own custom flag-buffer mechanism (hypothesis 2):**
the fix would be a direct extension of this session's own work -
tracing exactly which `CreateCommittedResource`/`CreateHeap`+
`CreatePlacedResource` call is for this specific buffer (most
efficiently done by instrumenting vkd3d-proton's own resource-creation
path with a temporary size-based log filter, e.g. flagging any shared
buffer request under ~64 bytes, then correlating against
`amdhip64_7_unixlib_pe.log`'s own timeline the way §27d's
`hipImportExternalMemory` correlation was done), then determining
whether it fails at D3D12 resource creation, at `CreateSharedHandle`,
or never gets attempted by the game at all because an earlier
capability check (possibly related to §28a's now-fixed version
report, or a different capability query entirely) short-circuits it
first.

**Neither path was attempted this session** - this is a scoping
exercise only, per explicit instruction. No code was changed or run
beyond the version-stub fix in §28a, which was implemented, tested,
and deployed, but confirmed separate from this specific blocker.

## 29. External-memory + event/stream bridge implemented, and tested live - "inline mode" confirmed working for the first time, hitting a new, deeper wall (2026-09-14)

### 29a. The real gap found and fixed: the import side was never built

Before implementing anything, checked `pe_shim.c` directly rather than
continuing to reason from log output alone, and found the actual
state of things was worse than believed: `hipImportExternalMemory`,
`hipExternalMemoryGetMappedBuffer`, and `hipDestroyExternalMemory`
were **all still stubs** - each one logs the call (which is what
produced every real-looking fd value shown in §27d) and then
unconditionally returns `HIP_ERROR_NOT_SUPPORTED`, regardless of
input. The comment above them said so plainly: `"Not yet implemented
for real - this is step one... before building the real bridge."`
Every HIP event/stream function
(`hipEventCreate(WithFlags)`, `hipEventRecord/Synchronize/Query/
ElapsedTime`, `hipStreamCreateWithFlags/Synchronize`) was the same -
always `HIP_ERROR_NOT_SUPPORTED`, no real forwarding at all.

So §27d's "confirmed working live" claim was real but incomplete:
vkd3d-proton's export side (real fd out) was genuinely fixed and
proven. This project's own *import* side was not - it received that
real fd and threw it away every time, reporting failure back to
Daniel's runtime regardless. That is a more complete and more likely
explanation for zero dispatches than the "predication buffer" theory
in §28 ever was.

**Implemented for real, this session:**
- `hipImportExternalMemory`/`hipExternalMemoryGetMappedBuffer`/
  `hipDestroyExternalMemory` now forward to the real, `dlopen`'d HIP
  library. New `HIP_CALL_*` opcodes and `native.c` dispatch functions,
  wired the same way every other real function in this shim already
  is.
- All of `hipEventCreate(WithFlags)`, `hipEventRecord/Synchronize/
  Query/ElapsedTime`, `hipStreamCreateWithFlags/Synchronize` likewise
  forward to the real library instead of always failing.
- The genuinely new, non-mechanical piece of logic - extracting a
  real fd from danielblnc's `hipExternalMemoryHandleDesc`, which
  arrives as either a real `OpaqueFd` or (danielblnc's actual usage)
  a fd smuggled through the `D3D12Resource`-typed handle field per
  vkd3d-proton's own `CreateSharedHandle` patch (§27a) - was factored
  into a new, pure, Wine/HIP-independent module, `ext_mem.c`/`.h`,
  which also now holds the single shared definition of the real HIP
  external-memory ABI structs (previously duplicated only on the PE
  side, now used by both PE and native code, removing that
  duplication).
- The shared "did the real symbol resolve, forward-or-fail-safely"
  pattern already established by `version_query.c` (§28a) was
  generalized for pointer-returning HIP calls (events, streams,
  external memory) into a second new pure module, `hip_forward.c`/`.h`.
- Four new tests (`test_ext_mem.c`, `test_hip_forward.c`) added
  alongside the existing `test_registry.c`/`test_version_query.c`,
  all wired into `make test`. `test_ext_mem.c` specifically covers
  the `D3D12Resource` fd-smuggling case using the exact real value
  observed live in §27d (a handle of `0x181` alongside `fd=385` - the
  same number read two ways). All four suites pass.
- Rebuilt and redeployed (with matching `md5sum` everywhere) to all
  four locations this project touches: the real game folder, the real
  Proton 11.0 install, and both copies of the `fdtest-11.0-2c` build.

### 29b. Tested live: "inline mode" works for the first time in this project's history

Launched through Steam with `fdtest-11.0-2c` selected and
`VKD3D_CONFIG=external_memory_fd` set, the same way as §27d. This
time, for the first time ever in this project:

```
inline flag check: a store from the game's queue reached the GPU wait after 0 iterations (0.1 ms)
first ffxFsr3ContextDispatchUpscale (pre-upscale)
first capture submitted on a direct queue
frames 3600 dispatches 975 (1.00/frame) fg 0 (+0) submitted 3 ready -1 skipped 972 (+600) timeouts 0 route fsr
```

The exact mechanism §28b speculated about - a GPU-side flag write the
HIP side polls for, with no CPU round-trip - now succeeds essentially
instantly (0 iterations, 0.1 ms). This settles §28b's two ranked
hypotheses without needing the Vulkan-feature investigation it
proposed: the real blocker was this project's own unimplemented event/
stream bridge, not a missing Vulkan device feature or a D3D12
predication gap. Dispatches are no longer stuck at zero
(`dispatches 975`, `1.00/frame`) - real, sustained neural-pass
dispatch, confirmed live, for the first time in this project.

### 29c. A new, deeper wall - and the session crashed hitting it

Immediately after the first real dispatch, Daniel's own runtime
logged a single, complete diagnostic block naming every layer of the
actual neural network, each with an identical, explicit real HIP
error:

```
job 1 GPU errors: operation not supported img:operation not supported pre_tin:operation not supported
pre_ds:operation not supported enc0:operation not supported ds0:operation not supported ...
[every named layer: enc1-3, ds1-3, vit512a, pooled, head, rep1d, b31-b38, vit1d, dec0-3, out]
...all "operation not supported"
```

Every kernel *launched* successfully (`hipLaunchKernel(...) -> ret=0
(real)` throughout this shim's own log, no launch failures at all) -
the failure is inside the real GPU execution of the kernels
themselves, not in this shim's dispatch plumbing. This happened
exactly once, on the very first real dispatch attempt, then the
pipeline never recovered: `"inline: too many frames in flight,
skipping one"` repeated with a climbing skip count (past 1,100 by the
time the log ends) as frames queued up waiting for GPU work that
never actually completed. The crash (`Unhandled page fault` at a null
address, and separately at address `0xBEEF` - a classic
uninitialized/poisoned-memory sentinel pattern) is consistent with
this runaway backlog: reading output buffers that a permanently-failed
compute graph never actually wrote real data into, not a bug
introduced by anything this session changed. **This is judged to be a
different, later, and likely more fundamental problem than anything
tackled so far** - not this shim's plumbing, and not vkd3d-proton's
memory export.

**Best working theory, not yet confirmed**: several of the network's
own kernel names are explicitly `fp8`-typed (`k_swin_1h_32_fp8`,
observed in §27's import list), and fp8 (8-bit floating point) tensor
instruction support in ROCm is newer and narrower than standard
fp16/fp32 support. The host's GPU is confirmed `gfx1201` (RX 9070 XT,
via `rocminfo`), a genuinely recent architecture - it is plausible the
installed ROCm/driver combination on this system does not actually
support whatever specific fp8 operation these compiled kernels use,
even though launching the kernel itself succeeds at the HIP API level.
Not confirmed this session - the next real step would be checking the
installed ROCm version's documented fp8 support for gfx1201
specifically, and/or capturing which exact operation within a single
kernel (not just which kernel) reports "operation not supported" via
whatever error-detail mechanism produced this diagnostic in the first
place (danielblnc's own runtime clearly has one - this message is far
more specific than a generic HIP error code).

Stopping here for the night - this is a good, deliberate point to
pause: real, meaningful progress happened (the actual import/event
bridge now exists and works, inline mode fires for the first time
ever), and what's left is a new, well-characterized, and likely
hardware/software-capability-driven question rather than a bug in
anything built so far.

## 30. Analysis: the "operation not supported" wall - what it is, and a way forward (2026-09-14)

Follow-up to §29c, done as pure investigation (no code changed or run)
in response to the direct question: we're running the exact same GPU
danielblnc himself supports (RX 9070 XT / gfx1201) - so is this a real
capability gap, and if so, is it fixable?

### 30a. Ruled out first: memory import is not the problem

Before theorizing further, checked this session's own
`amdhip64_7_unixlib_pe.log` directly: `hipImportExternalMemory` and
`hipExternalMemoryGetMappedBuffer` both succeeded repeatedly and
genuinely this session (`ret=0 (real)`, real non-null `dev_ptr`
values throughout). The crash is not "reading from memory that was
never really imported" - §29's new bridge (§29a) is working
correctly. The failure is specifically in kernel execution / status
checking, deeper than anything this project's own code touches.

### 30b. Confirmed: "operation not supported" is a real, genuine HIP error

`strings` against the actual installed native library
(`/usr/lib/x86_64-linux-gnu/libamdhip64.so.7.1.52801`, ROCm 7.1.1)
confirms the exact string `"operation not supported"` is the real
library's own text for `hipErrorNotSupported` - this is a genuine
return code from the real ROCm stack, not something fabricated by
this project or by danielblnc's own error handling. All the relevant
symbols this shim now calls for real
(`hipEventQuery`/`hipEventCreate`/`hipStreamSynchronize`/etc.) were
directly verified present in the installed library via `nm -D`, with
names matching exactly what `native.c` requests via `dlsym` - this
is not a symbol-resolution bug either.

### 30c. Research: is this a real, currently-immature capability gap?

Two research passes (web search, since gfx1201/RDNA4 is 2025-era
hardware not reliably covered by static knowledge) converged on a
consistent, well-evidenced picture:

- **The hardware genuinely supports fp8.** RDNA4/gfx1201 has real
  silicon-level fp8 matrix support (`v_wmma_f32_16x16x16_fp8_fp8`,
  the same E4M3FN format used on AMD's datacenter GPUs). This is not
  a hardware wall.
- **Windows is not ahead of Linux here - if anything, behind.**
  AMD's Windows-bundled HIP SDK has historically shipped older ROCm
  with zero precompiled kernels for gfx1201 at all. There is no
  evidence of a Windows-exclusive capability this project is missing.
- **The software stack around this very new GPU is real but still
  catching up.** Independently of this project entirely, AMD's own
  AITER library is documented as missing `gfx1201` from its
  architecture table as of ROCm 7.2.1 (silently falling back to
  FP32 dequantization rather than erroring - a different failure
  shape than what we see, but the same underlying "this very new
  architecture isn't fully wired up everywhere yet" pattern), and
  separate unrelated projects (e.g. ollama) have open issues about
  their ROCm backend failing to initialize at all on gfx1201.
- **A directly relevant data point, found on danielblnc's own
  repository**: [danielblnc/DLSS-NR-on-AMD#29](https://github.com/danielblnc/DLSS-NR-on-AMD/issues/29)
  - a user on RX 9060 XT (gfx1200, the non-XT sibling architecture)
  got a *different* error (`hipErrorInvalidKernelFile`, missing code
  object) diagnosed as "the project has only been tested on RX 9070
  XT (gfx1201)." This is real evidence that **gfx1201 - our exact
  GPU - is danielblnc's actual primary development/test target on
  Windows**, which argues against a wrong-architecture or
  never-tested-on-this-GPU explanation for our own, different error.
  No maintainer resolution is documented on that issue as of this
  writing.
- **ROCm 7.2.x shipped real, gfx1201-specific fixes** after the
  7.1.1 currently installed here - notably a gfx1201 memory-coherency
  fix in 7.2.1/7.2.2. No fp8-specific changelog entry was found in
  searchable release notes, so this is not confirmed to be *the*
  fix, but it is the most concrete, low-effort, evidence-backed next
  thing to try.
- **A second, independent hypothesis surfaced**: HIP's own
  documentation ties `hipErrorNotSupported` most commonly to
  cooperative-kernel-launch/group-size limits, checkable via
  `hipGetDeviceAttribute(hipDeviceAttributeCooperativeLaunch)` -
  worth ruling in or out before assuming this is purely an fp8 gap.
  This shim only exposes plain `hipLaunchKernel` (not a cooperative
  variant) to danielblnc's runtime, which argues against this being
  the direct cause, but the failure could originate from a
  cooperative-groups check *inside* one of his compiled kernels,
  which this project has no visibility into.
- **`HSA_OVERRIDE_GFX_VERSION`** is a real, documented ROCm mechanism
  for forcing a different (often better-supported) architecture
  target, confirmed to exist and work for other RDNA4-adjacent
  issues - but no confirmed report of it fixing this exact symptom
  was found, and using it changes what ISA the runtime targets,
  which could easily trade this failure for a different one given
  danielblnc's kernels are specifically compiled to use gfx1201's own
  real fp8 instructions. Not recommended as a first move.

### 30d. Way forward, ranked by effort and evidence strength

1. **Upgrade ROCm from 7.1.1 to the latest 7.2.x release.** The
   single most concrete, low-risk, evidence-backed next step - real
   gfx1201-specific fixes are confirmed to have landed in exactly
   this version range, even though fp8 was not specifically named in
   the changelogs found. A standard package upgrade, not a code
   change to anything this project owns.
2. **Narrow down exactly which HIP call reports the error**, not
   just which named layer - this project's own shim currently logs
   `hipLaunchKernel` and the external-memory calls, but not
   `hipEventQuery`/`hipEventSynchronize`/`hipStreamSynchronize`
   results. A small, targeted logging addition (not attempted this
   session, per instruction) would immediately show whether the
   rejection comes from an event/stream status check (this project's
   own new code from §29a) or from somewhere else entirely.
3. **Check `hipGetDeviceAttribute(hipDeviceAttributeCooperativeLaunch)`**
   against this GPU as a quick, independent sanity check on the
   second hypothesis in §30c, before assuming this is purely an fp8/
   architecture-maturity problem.
4. **Ask danielblnc directly**, per this project's standing plan -
   he has already solved real fp8 execution on this exact GPU model
   on Windows, and the ROCm version, HIP SDK version, or any special
   flag his own kernels were built against would settle this
   immediately rather than requiring this project to rediscover it.
   Issue #29 on his own repo is a concrete, current example of him
   (or his community) already fielding architecture-specific reports
   for this exact GPU family.
5. **Only after the above**, consider `HSA_OVERRIDE_GFX_VERSION` as
   an experimental workaround - understanding it may substitute one
   failure mode for another given danielblnc's kernels are compiled
   specifically for gfx1201's real instruction set.

**Overall assessment**: this looks like a real, currently-evolving
software-enablement gap for a very new consumer GPU architecture, not
a permanent hardware limitation and not a bug in anything this
project has built. Multiple independent, still-open upstream reports
describe the same general shape of problem for this exact chip across
unrelated projects, and real fixes are actively landing in ROCm point
releases. Feasible to continue pursuing, with a concrete, ranked list
of next steps above - none of which have been attempted this session,
per explicit instruction to analyze only.

### 30e. A second, independent variable found: the OS itself is outside ROCm's documented support matrix for this GPU class

Follow-up research, checking ROCm 7.1.1's own official documentation
directly (not just 7.2.x, which §30c already covered) turned up a
real, separate factor nobody had isolated yet.

`gfx1201`/RX 9070 XT genuinely **is** listed as "Supported" in ROCm
7.1.1's official compatibility matrix
([system-requirements.html, docs-7.1.1](https://rocm.docs.amd.com/projects/install-on-linux/en/docs-7.1.1/reference/system-requirements.html)).
But that same page restricts consumer RDNA cards (this GPU's whole
class, not just this one architecture) to a specific, short list of
officially supported operating systems: **Ubuntu 24.04.3, Ubuntu
22.04.5, RHEL 10.1, and RHEL 9.7 only.**

This machine runs **Ubuntu 26.04** ("resolute") - not on that list at
all. This is independent of the fp8 question from §30c: even setting
aside whether ROCm 7.1.1 supports the specific fp8 instructions
danielblnc's kernels use on this architecture, ROCm itself does not
officially claim to support this exact GPU class on this exact OS
version, at any level. The 7.1.1 release notes' FP8-specific mentions
are all scoped to AMD's Instinct-series datacenter GPUs (MI325X,
etc.) and never mention gfx1201/RDNA4 in connection with fp8 either
way - genuinely silent on that specific combination, not confirming
or denying it.

**This adds a second, independent candidate explanation** alongside
§30c's fp8-maturity theory: running a newer consumer OS release than
ROCm has validated for this GPU family could itself introduce
exactly this class of "some operations silently rejected as
unsupported" symptom, separate from whether the fp8 instructions
themselves are ready. The two are not mutually exclusive - both could
be contributing.

**Also implemented this session** (a small, targeted code change,
not just analysis - explicitly requested): logging was added to every
event/stream dispatch function in `windows-runtime-bridge/hip-unixlib/native.c`
(`hipEventCreate/Record/Synchronize/Query/ElapsedTime`,
`hipStreamCreateWithFlags/Synchronize`) that was previously silent,
and - the single most directly useful addition - `hipGetErrorString`
in `pe_shim.c` now logs the exact `hipError_t` code it's asked to
translate. Since danielblnc's runtime almost certainly calls this to
turn a failing HIP call's return code into the human-readable text it
prints (`"operation not supported"`), this logging will show the
*exact* numeric error code the next time this is tested live,
settling definitively whether it's really `hipErrorNotSupported`
(801) and, from the surrounding log sequence, which specific call it
followed - without needing to guess further. Rebuilt, all four unit
test suites still pass, redeployed (checksums matched) to all four
locations this project touches. Not yet tested live this session.

**Updated way-forward priority, given both findings together:**
before spending effort on a ROCm version upgrade (§30d item 1), it
may be worth checking whether ROCm behaves any differently under a
documented-supported OS version for this GPU class specifically
(24.04 or 22.04) - though this is a heavier, more disruptive change
than a ROCm package upgrade alone, and isn't something to undertake
lightly on a real, in-use system. The logging added this session is
the lower-cost, non-disruptive next step: run it once, read the exact
error code and surrounding call sequence, and let *that* evidence
decide whether the OS-version or the fp8-maturity theory (or both, or
neither) is actually correct, rather than guessing further.

Stopping here for the night.

## 31. A standalone ROCm probe settles it: the runtime itself is healthy, and Daniel's own binary names the exact fix (2026-09-15)

### 31a. A no-compiler-needed diagnostic, built to discriminate between §30's two theories

§30e left two live, independent candidate explanations for "operation
not supported": fp8 instruction-support maturity for `gfx1201`, or
running Ubuntu 26.04 (outside ROCm 7.1.1's documented OS support list
for this GPU class). Rather than guess further, built
`windows-runtime-bridge/investigations/rocm_probe.c` - a small, standalone Linux program
that `dlopen`s the real `libamdhip64.so.7` directly (the same
technique `windows-runtime-bridge/hip-unixlib` already uses, and for the same reason:
no HIP compiler or dev headers are installed on this system, so a real
`.hip` source file compiled with `hipcc` was never an option here).

It exercises real device selection, real memory allocation, and -
the actual point of the tool - the complete stream + event
completion-signaling pipeline: `hipStreamCreateWithFlags`,
`hipEventCreateWithFlags` (two events), `hipEventRecord`, a real
`hipMemcpyAsync` device-to-device copy on the stream between them
(real GPU DMA-engine work, going through the same completion-signal
path a kernel launch would, without needing anything compiled),
`hipStreamSynchronize`, `hipEventSynchronize`, `hipEventQuery`, and
`hipEventElapsedTime`. This is deliberately the same machinery
danielblnc's "inline mode" depends on (§29b), isolated completely
from his proprietary kernels and from this project's own Wine/Proton
code.

### 31b. Result: the runtime itself is completely healthy

Every single call succeeded:

```
hipStreamCreateWithFlags(nonblocking)    -> 0 (success)
hipEventCreateWithFlags(start/stop)      -> 0 (success)
hipEventRecord / hipMemcpyAsync          -> 0 (success)
hipStreamSynchronize / hipEventSynchronize -> 0 (success)
hipEventQuery(stop) [expect success=0]   -> 0 (success)
hipEventElapsedTime                      -> 0 (success), 0.1482 ms (a real, sane number)
```

This directly rules against the OS-version-mismatch theory from
§30e: if Ubuntu 26.04 had broken ROCm's basic runtime plumbing on
this GPU, this test - which has nothing to do with fp8 or with
danielblnc's kernels at all - would have failed too. It didn't.
Confidence shifts back toward the fp8-specific theory (§30c).

### 31c. The decisive finding: Daniel's own binary already names the required version, and this system doesn't have it

The probe also printed the real `hipDriverGetVersion`/
`hipRuntimeGetVersion` values - something this project's own shim
only started reporting honestly this week (§28a fixed a stub that
previously always reported `0`):

```
hipDriverGetVersion: 70152801
hipRuntimeGetVersion: 70152801
```

Decoded using HIP's own versioning scheme
(`major*10000000 + minor*100000 + patch`), that is **7.1.52801** -
matching this system's installed library exactly
(`libamdhip64.so.7.1.52801`). Compare against the string already
found inside danielblnc's own binary back in §29c's investigation:

```
"...Inline mode will time out on this driver (HIP runtime %d;
every working system runs 70260201 / Adrenalin 26.x): update the
driver, or set Inline=0 in dlssnr_on_amd.ini"
```

Decoded the same way, `70260201` is **7.2.60201** - HIP/ROCm
**7.2.x**. This system has **7.1.x**. Daniel's own code already told
us, in plain text, exactly which version it expects - this project
simply couldn't see the real number until §28a's fix stopped hiding
it behind a stub `0`. This is now a concrete, version-number-level
confirmation of §30d's ranked-first suggestion (upgrade ROCm to
7.2.x), not a general "try upgrading and see" - it's the specific
version danielblnc's own runtime already checks for.

### 31d. Practical ROCm-upgrade path research

Checked AMD's official repo directly (`repo.radeon.com`) again this
session: still only `jammy` (22.04) and `noble` (24.04) are listed -
`resolute` (26.04) support has not appeared. Latest available ROCm
version there: **7.2.4**.

One relevant system fact worth recording: this machine's `amdgpu` GPU
driver is the **in-kernel module built into Ubuntu's own kernel
package** (`dkms status` returns empty; `lsmod` shows `amdgpu` already
loaded from the stock `7.0.0-31-generic` kernel, not from a
separately-installed DKMS package). ROCm's own packages here
(`rocm-smi`, `rocminfo`, `librocm-smi64-7`, and the still-outdated
`libamdhip64`) are purely userspace libraries layered on top of that
already-working kernel driver - the kernel/driver side of this system
is not obviously implicated by any of tonight's findings, only the
ROCm userspace version is.

### 31e. The practical upgrade path, found

Follow-up research resolved the open questions from §31d with a
directly relevant, concrete result:

- **A real, confirmed match for this exact system's kernel range**:
  [ROCm/ROCm#6193](https://github.com/ROCm/ROCm/issues/6193) reports
  `amdgpu-install`'s DKMS build failing on Ubuntu 26.04 with kernel
  `7.0.0-14` - effectively identical to this machine's
  `7.0.0-31-generic` - with genuine kernel-API breakage (`pci_
  resize_resource` argument count changed, `drm_client_dev_suspend/
  resume` signature changed, `dma_map_ops.map_resource` removed
  entirely). This is a real, version-specific incompatibility between
  AMD's DKMS-built `amdgpu` kernel module and this kernel generation -
  not a hypothetical risk.
- **The confirmed working fix, documented by AMD itself**: pass
  `--no-dkms` to `amdgpu-install`. This flag explicitly skips
  building/installing AMD's own kernel module entirely and uses
  whatever `amdgpu` module is already loaded - which on this system is
  the in-kernel one from Ubuntu's own kernel package (§31d), already
  confirmed working (real GPU compute happening every session this
  investigation has run). AMD documents `--no-dkms` as intended for
  exactly this situation: "non-HWE kernels or custom kernels" / inbox-
  driver setups.
- **Ubuntu 26.04 does ship ROCm natively** now (`apt install rocm`,
  reported by [Phoronix](https://www.phoronix.com/news/Ubuntu-26.04-With-ROCm)
  and [fosslinux.com](https://www.fosslinux.com/157631/rocm-ubuntu-2604-native-apt-install.htm))
  - but it ships **7.1**, exactly what's already installed here, with
  no confirmed timeline for 7.2.x reaching Ubuntu's own archive.
  Phoronix's own recommendation for anyone wanting current ROCm:
  bypass Ubuntu's archive and use AMD's official install path
  directly.
- No ROCm-specific glibc/cross-release package risk was found beyond
  the DKMS issue above - ordinary forward glibc compatibility is not
  flagged as a problem anywhere. One unrelated repo-metadata hiccup
  ([ROCm/ROCm#5968](https://github.com/ROCm/ROCm/issues/5968), a
  missing `Release` file) has been reported even on genuine Ubuntu
  24.04 - worth knowing about if `apt update` fails after adding
  AMD's repo, not specific to this system's OS mismatch.

**Concrete, evidence-backed upgrade path**: add AMD's official ROCm
apt repo using the `noble` (24.04) codename (the closest AMD
officially supports; `resolute` isn't listed), then run

```
sudo amdgpu-install -y --usecase=rocm,hip --no-dkms
```

`--no-dkms` is the load-bearing flag - it is the documented,
AMD-acknowledged way to get ROCm's userspace (the actually-outdated
piece here, per §31c) onto 7.2.x while leaving the kernel driver
side - already working - completely alone, and it is specifically
what avoids the real, confirmed DKMS build failure this exact kernel
generation hits otherwise. This has not been attempted yet - it is
the concrete next step, not something done this session.

**Decision, explicitly made**: stay on ROCm 7.1.1 for now rather than
upgrade preemptively - the upgrade path above is documented and ready
as a backup route, not something to reach for until it's actually
needed.

## 32. More mitigating tests, and a real fp8-execution probe built and ready for next time (2026-09-15)

### 32a. `rocm_probe.c` extended - device-properties ABI and synchronous memcpy, both confirmed healthy

Given the decision to hold off on upgrading, added two more checks to
`windows-runtime-bridge/investigations/rocm_probe.c` that needed no new packages and could
be run immediately:

- **`hipGetDevicePropertiesR0600` ABI sanity.** `windows-runtime-bridge/hip-unixlib/
  native.c` has always assumed this struct is exactly 1472 bytes
  (confirmed once before via compiler-based analysis, per §8/§12) but
  had never actually been exercised against a live call and checked
  for real. Allocated exactly that many bytes (poisoned with `0xAA`
  first, so any real overflow would have been obvious), called the
  real function, and read back the struct's well-known, stable first
  field (`char name[256]`). Result: real success, and the real device
  name came back correctly - `"AMD Radeon RX 9070 XT"`. This is
  real, live confirmation that this project's own long-standing ABI
  assumption is correct against ROCm 7.1.1 specifically, not just
  inferred from documentation.
- **Synchronous `hipMemcpy`** (only the async variant had been tested
  before, both here and in real gameplay). A host pattern buffer was
  copied to the device and back, and compared byte-for-byte - a real,
  exact round trip.

Every test in the extended probe passes cleanly - device count/
selection, the properties ABI, both memcpy variants, and the full
stream/event pipeline from §31b. Nothing about this system's ROCm
7.1.1 userspace is unhealthy in any way this project can currently
test without a compiler.

### 32b. `fp8_kernel_probe.c` - built and ready, not yet run

The one thing that remained genuinely untestable without a compiler -
whether this GPU/ROCm combination actually executes real fp8
instructions - now has a real test ready to go, once
`libamd-comgr-dev` can be installed (needs `sudo`, not available
during this remote session - see `windows-runtime-bridge/investigations/README.md`).

Found that `libamd-comgr3` (AMD's code-object-manager library - the
compiler machinery HIP's own `hiprtc` convenience API sits on top of)
is already installed, even without any HIP dev package, and Ubuntu's
own archive has a `libamd-comgr-dev` package matching the installed
runtime version *exactly* (`7.1.1+dfsg-0ubuntu1`) - headers only, no
driver/runtime risk, trivially removable.

Built `windows-runtime-bridge/investigations/fp8_kernel_probe.c`: compiles two small,
original, clean-room HIP kernels *from source, at runtime*, via
comgr's own compile API, then loads and launches the result through
the exact same `hipModuleLoadData`/`hipModuleGetFunction`/
`hipModuleLaunchKernel` calls `native.c` already uses for
danielblnc's own kernels:

- `kernel_touch` - trivial fp32-only control, no fp8 at all. If this
  fails too, the problem is general kernel execution, not fp8.
- `kernel_fp8_roundtrip` - a real fp32→fp8(e4m3)→fp32 round trip via
  Clang's real AMDGPU fp8 conversion builtin - the actual instruction
  path under suspicion.

If `kernel_touch` succeeds but `kernel_fp8_roundtrip` reports a real
error at `hipDeviceSynchronize`, that would be direct, empirical
confirmation of §30c's fp8 theory - not an inference from
danielblnc's proprietary binary's behavior, an independent, original
test producing the same failure.

**Honest caveat, recorded deliberately**: this was written without
the real `amd_comgr.h` header present (remote session, `sudo` not
available to install `libamd-comgr-dev` at the time). It's syntax-
checked clean against a throwaway stub header (catches plain C
mistakes) but the actual AMD COMGR API function/enum names are from
memory of the public, documented API, not verified against the real
header yet. Any name mismatch will surface as an ordinary compiler
error the first time this is actually built against the real header
- safe and easy to fix, not a silent risk - but a first real build-
and-fix pass should be expected before this produces a result.

Not run this session. Concrete next step: `sudo apt install
libamd-comgr-dev`, build, run, fix whatever the compiler flags on
the first attempt, and read the result.

## 33. The fp8 theory is ruled out - real fp8 instructions genuinely execute correctly on this system (2026-09-15)

### 33a. `libamd-comgr-dev` obtained and used without installing anything system-wide

While remote (no `sudo` available), discovered `apt-get download
libamd-comgr-dev` fetches the real `.deb` without root, and `dpkg-deb
-x` extracts it to a plain directory - no install needed at all. The
package itself is genuinely tiny and inspected directly: one header
(`amd_comgr.h`, 94KB), one versionless `.so` symlink (pointing at the
`libamd-comgr3` runtime already installed), a few CMake helper files.
Nothing kernel-related, nothing that touches `amdgpu`, no services -
purely a userspace compiler library header, confirmed by inspecting
the actual file list before ever using it.

Compiled `fp8_kernel_probe.c` against the real, extracted header
(linking against the already-installed runtime `.so` directly, no
system changes) and ran it for real, immediately - no need to wait
for the "next session" §32b anticipated.

### 33b. Two real, informative compile errors along the way - fixed live

The first build attempt (written without the real header present, per
§32b) surfaced two genuine, useful mistakes once compiled against the
real API, exactly as anticipated - safe, loud compiler errors, not
silent problems:

- `__builtin_amdgcn_cvt_f32_fp8` was called with 3 arguments (guessed,
  by analogy with the encode-direction instruction); the real compiler
  error revealed its actual signature takes 2 - `(int packed, int
  byte_index)`, decoding one byte of a packed 32-bit value as fp8
  (e4m3) back to fp32. Simplified the test to use this exact,
  now-confirmed-correct signature, decoding a real, well-known e4m3
  bit pattern (`0x38` = exactly `1.0`) rather than guessing at an
  encode instruction's signature too.
- More significantly: compiling under `AMD_COMGR_LANGUAGE_HIP` with a
  raw `__attribute__((amdgpu_kernel))` produced a genuine compile
  *warning* ("calling convention is not supported for this target")
  and, even once the fp8 argument-count errors above were fixed and
  the overall compile reported success, `hipModuleGetFunction` could
  never find either kernel symbol at all - `HIP` language mode does a
  real two-target (host x86_64 + device gfx1201) split compile, and
  the raw kernel attribute was being dropped somewhere in that split.
  Switched to `AMD_COMGR_LANGUAGE_OPENCL_1_2` with standard OpenCL C
  `__kernel`/`__global` syntax instead - comgr's simpler,
  single-target-only device compilation path, with no host stub and
  no split to lose the kernel across. This reaches the exact same
  AMDGPU backend and the exact same Clang builtins; only the
  source-language framing changed.

Also added real diagnostic-log extraction (`AMD_COMGR_DATA_KIND_LOG`
entries, requiring `amd_comgr_action_info_set_logging(action_info,
true)` to be enabled - not on by default) so any future compile
failure shows the real compiler message directly, the way this
session's own two mistakes were actually found and fixed.

### 33c. The result: real fp8 execution, confirmed, byte-exact

```
kernel_touch (fp32 control, no fp8):
  hipDeviceSynchronize -> 0 (success)
  result: 5.000000 (expect 5.0)

kernel_fp8_decode (real __builtin_amdgcn_cvt_f32_fp8 call):
  hipDeviceSynchronize -> 0 (success)
  result: 1.000000 (0x38 is the documented e4m3 bit pattern for
                     exactly 1.0 - got exactly 1.0)
```

Both kernels compiled, loaded via the real `hipModuleLoadData`/
`hipModuleGetFunction`/`hipModuleLaunchKernel` calls
`windows-runtime-bridge/hip-unixlib/native.c` already uses for danielblnc's own
kernels, launched, and executed correctly - the fp8 one producing a
byte-exact correct numerical result from a real fp8 decode
instruction actually running on the GPU, not a fallback or emulated
path.

**This rules out §30c's leading theory.** This GPU, on ROCm 7.1.1, on
this exact Ubuntu 26.04 system, genuinely and correctly executes real
fp8 instructions. "This GPU/ROCm combination doesn't support fp8" is
no longer a credible explanation for danielblnc's runtime's
`"operation not supported"` error - it's been directly, empirically
tested and found false.

### 33d. What this means for the investigation

The real cause of `"operation not supported"` is now known **not**
to be basic fp8 instruction support. Two real candidate explanations
remain, both narrower and more specific than before:

1. **A more complex fp8 operation specifically** - this test only
   exercised a single scalar decode instruction. danielblnc's actual
   kernels (named `swin`, `attention`, `conv`, etc. - transformer-
   shaped) almost certainly use real matrix-multiply-accumulate
   instructions (`v_wmma_f32_16x16x16_fp8_fp8`, confirmed to exist on
   this hardware per §30c's research) rather than simple scalar
   conversions. A WMMA-based follow-up test - genuinely more complex
   to write correctly (needs real vector/matrix register operand
   types, not just scalars) - would be the direct next step if this
   remains the working theory.
2. **Something unrelated to fp8 entirely** - back to the other
   candidate from §30c (a cooperative-launch/group-size limit, or
   something else this project hasn't identified yet). The logging
   added in §30e/§31 (event/stream calls and, most usefully,
   `hipGetErrorString`'s requested error code) remains the concrete
   way to find out for certain from a real live session, now with
   one major wrong theory eliminated first.

`windows-runtime-bridge/investigations/fp8_kernel_probe.c` and its README are updated to
reflect the real, working, corrected version - including the two
real mistakes found and fixed, kept in the code's own comments as a
record of what was wrong and why, not silently cleaned up.

## 34. The real cause of "operation not supported" found and fixed - it was our own shim, not the GPU (2026-09-15)

Candidate 2 from §33d turned out to be it, and the mechanism was
mundane: a real bug in `windows-runtime-bridge/hip-unixlib/pe_shim.c`, not any
GPU/ROCm/fp8 limitation.

### 34a. Reading the actual crash-log context

The user's most recent game launch crashed again. With §29's
`hipGetErrorString`/`hipGetLastError` logging now in place, the log
around the first `801` was read directly instead of inferred:

```
hipMalloc(size=917504) -> 0000710ED0C00000 (real)
hipLaunchKernel(function=00006FFFE34443F8) -> ret=0 (real)
hipMalloc(size=234881024) -> 0000710EC2A00000 (real)
hipLaunchKernel(function=00006FFFE344BC68) -> ret=0 (real)
hipGetLastError() -> 801
hipGetErrorString(error=801) -> "operation not supported"
...
hipLaunchKernel(function=00006FFFE344BC70) -> ret=0 (real)
hipLaunchKernel(function=00006FFFE344BC70) -> ret=0 (real)
hipLaunchKernel(function=00006FFFE344BC70) -> ret=0 (real)
hipLaunchKernel(function=00006FFFE344BC70) -> ret=0 (real)
hipMemcpy: unsupported kind 3
hipGetLastError() -> 801
hipGetErrorString(error=801) -> "operation not supported"
```

`hipMemcpy: unsupported kind 3` is the shim's own log line, from
`pe_shim.c`'s `memcpy_forward()`. Kind `3` is real HIP's
`hipMemcpyDeviceToDevice`. `memcpy_forward()` only ever allowed
`HIP_MEMCPY_HOST_TO_DEVICE` (1) and `HIP_MEMCPY_DEVICE_TO_HOST` (2)
through to `native.c`/the real `hipMemcpy` - every other kind,
including the very common device-to-device case, was rejected
locally with `HIP_ERROR_NOT_SUPPORTED` before ever reaching real HIP.

Confirmed this is not the already-known `hipMemcpyToSymbol`/
`hipMemset`/`hipMemsetAsync` stubs from §29: those never appear in
the log at all (`grep -c` = 0) and aren't in Daniel's DLLs' import
tables either - ruled out cleanly, distinct bug.

### 34b. Why one rejected memcpy took down the whole frame's job

`dlssnr_on_amd.log`'s `job 1 GPU errors:` line for the same frame
lists every single named pipeline stage (`img`, `pre_tin`, `enc0`,
`ds0`, ... `out`) as `operation not supported`, not just one. The
failing kernel at the first `801` demangles
(`_Z10k_swin_varILi32ELb1EEv9VarParams` -> `c++filt`) to
`void k_swin_var<32, true>(VarParams)` - a real Swin-transformer
variance kernel, the very first stage of the pipeline. HIP's error
state is sticky: once `hipGetLastError()`/the runtime's internal
check reports a failure, every later per-stage check in the same job
echoes the same code until something clears it - which is exactly why
one early rejected memcpy read out as a total pipeline failure across
every named stage, not a single kernel.

### 34c. The fix

`native.c`'s `unix_memcpy` (`windows-runtime-bridge/hip-unixlib/native.c:318-324`)
already forwarded `kind` to the real `p_hipMemcpy` completely
unmodified - it needed no change; real HIP fully supports device-to-
device copies. The bug was entirely `pe_shim.c`'s gate rejecting the
value before it ever got there.

Fixed by extracting the kind check into a small pure, testable module
(matching this project's established `version_query.c`/`ext_mem.c`/
`hip_forward.c` pattern) rather than patching the inline check
directly:

- `windows-runtime-bridge/hip-unixlib/memcpy_kind.h`/`.c` - defines the real
  `HIP_MEMCPY_*` kind constants (0-4: `HostToHost`, `HostToDevice`,
  `DeviceToHost`, `DeviceToDevice`, `Default`) and
  `hip_memcpy_kind_supported(int kind)`, a pure range check.
- `pe_shim.c`'s `memcpy_forward()` now calls
  `hip_memcpy_kind_supported()` instead of the old two-value
  allowlist.
- `windows-runtime-bridge/hip-unixlib/test_memcpy_kind.c` - new unit test, plain host
  gcc, no Wine/HIP dependency: all 5 real kinds assert supported
  (with `DeviceToDevice` called out as the explicit regression case
  for this exact bug), out-of-range values assert rejected. Wired
  into the Makefile's `test` target alongside the other three
  existing pure-module suites; full `make test` passes.

Rebuilt (`make clean && make all`) and redeployed via md5-verified
copy to all five real locations this system actually has a copy of
`amdhip64_7.dll` in (one more than previously tracked - a
`proton11-fdtest` compatibility tool copy was found alongside the
game folder, the live Wine prefix's own `system32`, the Proton 11.0
install, and the `proton-build` redist tree): all five now match
md5 `3a854ea61041184376663a7d1dbc882a`. The native `.so` is
unchanged (correctly - `native.c` needed no edit) and still matches
its previous md5 `1b9060a2fce616a8b264c580a953e76f`.

### 34d. What this means

This was never a GPU, ROCm, or fp8 problem. §33's real, live fp8
execution test was a genuinely useful validation of the hardware/
driver stack, but the actual blocker the whole night was chasing was
a straightforward missing case in this project's own shim. Next real
game launch is the test of whether device-to-device memcpy support
gets the pipeline past this specific wall - if a new blocker appears
beyond this one, the same logging (`hipGetErrorString`,
`hipGetLastError`, and now this fixed memcpy path) is what will
surface it directly rather than requiring inference.

## 35. Correction to §34: the memcpy fix was real, but not the cause - the actual failing kernel identified (2026-09-15)

§34's conclusion was wrong. The `hipMemcpy: unsupported kind 3` fix
is a genuine bug fix (kept - device-to-device copies now work, and
`test_memcpy_kind.c` still exercises it), but it was not what caused
the crash under investigation. It was found by proximity in the log,
not by checking strict line-by-line adjacency to the actual first
`801`.

### 35a. What re-checking properly showed

Two full test cycles were run after §34's fix was deployed, the
second one after explicitly confirming zero lingering `wine`/`steam`/
`cyberpunk` processes before relaunch (ruling out a stale Wine
section-object DLL cache as an explanation for the first, inconclusive
retest). Both still crashed the same way. Isolating exactly the new
log lines added by the second run (using the previous total line
count as a hard boundary, so no older content could be
misattributed) shows the real, unedited sequence:

```
hipLaunchKernel(function=00006FFFE347BC68) -> ret=0 (real)
hipGetLastError() -> 801
hipGetErrorString(error=801) -> "operation not supported"
```

Nothing else logged between the launch and the error - no memcpy
call anywhere near it. `hipLaunchKernel` reports success because
kernel launches are asynchronous; the real GPU-side failure surfaces
later, at the `hipGetLastError()` check. This is a genuine deferred
execution failure inside the kernel itself, confirmed to happen on
every single launch of this specific kernel in the run (checked all
occurrences of its function pointer in the isolated log slice, not
just the first).

### 35b. The failing kernel, identified

`c++filt` on the mangled symbol from this kernel's earlier
`__hipRegisterFunction` call:

```
_Z10k_swin_varILi32ELb1EEv9VarParams  ->  void k_swin_var<32, true>(VarParams)
```

A Swin-transformer variance kernel, templated on an integer (likely a
tile size, `32`) and a boolean. It is the first kernel in the
pipeline to fail, every time. This directly matches §33d's candidate
1 - the `true` template boolean plausibly selects a real
matrix-multiply-accumulate (WMMA) code path rather than the simple
scalar fp8 decode §33's test exercised, which only proved the
underlying instruction *family* works, not this specific
usage.

### 35c. Corrected status

The `"operation not supported"` wall is real, GPU-execution-side, and
still open. §33's fp8-instruction-support test remains valid (rules
out "fp8 doesn't work on this hardware at all" as an explanation) but
does not by itself explain this specific kernel's failure. Next
concrete step: extend `windows-runtime-bridge/investigations/fp8_kernel_probe.c` (or a
new probe) with a real WMMA-based fp8 matrix kernel
(`v_wmma_f32_16x16x16_fp8_fp8`), the direct, narrower test §33d
proposed and this session did not yet build.

## 36. WMMA fp8 matrix instruction also ruled out - both fp8 theories now dead (2026-09-15)

`windows-runtime-bridge/investigations/wmma_fp8_probe.c` (new) tests §35c's leading
candidate directly: a real WMMA (matrix-multiply-accumulate) fp8
instruction, not just the scalar decode §33 already tested.

### 36a. Two real build obstacles, found and fixed by compiling against the real toolchain, not by guessing further

1. `int2`/`float8` OpenCL vector-type sugar produced "use of
   undeclared identifier" under this comgr `AMD_COMGR_LANGUAGE_OPENCL_1_2`
   action (reason not further investigated - not needed once worked
   around). Fixed with Clang's own portable
   `typedef int int2_t __attribute__((ext_vector_type(2)))` /
   `float8_t` equivalent, which doesn't depend on OpenCL's builtin-type
   registration.
2. The naive builtin name guessed from the LLVM IR intrinsic
   (`__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8`, matching
   `llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8` in this system's own
   `/usr/include/llvm-21/llvm/IR/IntrinsicsAMDGPU.td`) does not exist
   as a Clang builtin - "use of undeclared identifier" again. Found
   the real name by grepping builtin-name strings directly out of the
   installed `libamd_comgr.so.3.0.0` binary (`strings -a
   libamd_comgr.so.3.0.0 | grep wmma_f32`, since comgr bundles its own
   clang, separate from the system's llvm-21 package): a gfx12-
   specific, wave-width-suffixed family -
   `__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12` (also
   `_w64_gfx12`, and non-`_gfx12` w32/w64 variants for gfx11's older
   WMMA without fp8/bf8 operand types). RDNA4/gfx1201 uses the
   `_w32_gfx12` variant (wave32 is gfx12's default wave width).

### 36b. The result: real WMMA fp8 execution, confirmed clean

```
kernel_touch (fp32 control):
  hipDeviceSynchronize -> 0 (success)
  result: 5.000000 (expect 5.0)

kernel_wmma_fp8_touch (real WMMA fp8 matrix-multiply-accumulate,
all-zero operands - deliberately not testing numeric correctness,
only whether the instruction executes at all):
  hipDeviceSynchronize -> 0 (success)
  result: -14.002930
```

`hipDeviceSynchronize` reported clean success for the real WMMA fp8
instruction - no `hipErrorNotSupported`, matching `kernel_touch`'s
control result exactly in kind (both `0`/success). The non-zero,
non-"expected" numeric result (`-14.002930` instead of `0.0`) is not
a red flag: the kernel was deliberately launched with a single
work-item (`1,1,1` grid/block), but a real WMMA instruction is an
inherently cross-lane, whole-wavefront cooperative operation - each
of the wave's 32 lanes is defined to hold a different slice of the
A/B/C operands, and with only lane 0 actually launched, the other 31
lanes' operand state is undefined, not zero. This test was never
about numeric correctness (validating that would need reasoning
about this exact wave32 WMMA operand-layout convention, not available
without AMD's own internal docs) - it was about whether the
instruction *executes* on real hardware without the GPU/runtime
rejecting it, matching the real crash's actual failure signature
(`hipLaunchKernel` reports success, the real error surfaces later at
`hipGetLastError`/`hipDeviceSynchronize`). It does execute, cleanly.

### 36c. Where this leaves the investigation

Both real candidate fp8 explanations from §33d/§35c are now ruled out
empirically:
- A simple scalar fp8 decode instruction: works (§33).
- A real WMMA fp8 matrix-multiply-accumulate instruction, the
  specific operation danielblnc's transformer-shaped kernels almost
  certainly use: also works (§36b, this section).

"This GPU/ROCm combination cannot run fp8 instructions" - scalar or
matrix - is no longer a credible explanation for `k_swin_var<32,
true>`'s real, repeatable `"operation not supported"` failure in any
form. The remaining candidate is §33d's candidate 2: something
unrelated to fp8/WMMA entirely - most plausibly something about the
specific launch configuration (grid/block dimensions, dynamic shared
memory size, or a cooperative-launch/grid-sync requirement) that this
particular kernel needs and either isn't being satisfied or isn't
supported here. `windows-runtime-bridge/hip-unixlib/pe_shim.c`'s `hipLaunchKernel`
currently only logs the function pointer, not its grid/block/
shared-mem parameters - extending that logging is the direct next
step to find out which, from a live session, rather than continuing
to guess.

## 37. The real root cause, found and confirmed at the kernel level: a genuine GPU ring hang (2026-09-15)

The investigation's terminus. Every "crash"/"frozen picture" report chased
through §29-§36 was never a HIP error, a shim bug, or an fp8/WMMA
instruction-support gap - it is a real, repeatable AMD GPU hardware/
driver-level ring hang, confirmed directly in the kernel log, not
inferred.

### 37a. How this was found

Following up on §36's live "picture froze" session: `hipDeviceSynchronize`
had zero logging anywhere (neither `pe_shim.c` nor `native.c`) - a real
blind spot. Added entry+exit logging there, then to `hipEventSynchronize`,
`hipStreamSynchronize`, and `hipMemcpy` (all four real blocking HIP calls
in the shim), so any future hang inside one of them would show an
unmatched "entering" line with nothing after it.

Two more live test/freeze cycles with this logging in place showed
**all four calls cleanly paired every time** - the hang was not inside
any HIP call this shim makes at all. `dlssnr_on_amd.log` (danielblnc's
own runtime log) kept progressing through several more frame jobs after
the HIP-side logs went silent, then itself went silent too - ruling out
a HIP-level deadlock and pointing the investigation at the D3D12/Vulkan
layer instead.

Set `VKD3D_DEBUG=trace` (vkd3d-proton's own real trace-logging env var,
confirmed from its source - `libs/vkd3d-common/debug.c`) in the Steam
launch options, alongside the already-set `PROTON_LOG=1`, and relaunched.
The resulting `~/steam-<AppID>.log` immediately showed the real answer,
found by searching for `"is lost"`:

```
radv/amdgpu: The CS has been cancelled because the context is lost. This context is innocent.
162302.831:...:err:vkd3d-proton:d3d12_command_queue_execute: Failed to submit queue(s), vr -4.
162302.831:...:err:vkd3d-proton:vkd3d_wait_for_gpu_timeline_semaphore: Failed to wait for Vulkan timeline semaphore, vr -4.
162302.837:...:warn:vkd3d-proton:d3d12_device_mark_as_removed: Device ... is lost (reason 0x887a0005, "VK_ERROR_DEVICE_LOST").
```

"This context is innocent" is Mesa RADV's own real wording for: the
kernel driver reported a GPU-wide reset, and this specific Vulkan
context wasn't the one that caused it - something else sharing the same
physical GPU did.

Confirmed directly at the kernel level via `journalctl -k` (not `dmesg`,
which needs elevated `kptr_restrict`/dmesg-restrict permissions this
session doesn't have) - the real, root event, present in the journal
for **every single crash/freeze from this entire test session**, each
with the exact same shape:

```
amdgpu 0000:28:00.0: ring gfx_0.0.0 timeout, signaled seq=4307467, emitted seq=4307470
amdgpu 0000:28:00.0:  Process GameThread pid 1252198 thread vkd3d_queue pid 1252311
amdgpu 0000:28:00.0: Starting gfx_0.0.0 ring reset
amdgpu 0000:28:00.0: Ring gfx_0.0.0 reset succeeded
amdgpu 0000:28:00.0: [drm] device wedged, but recovered through reset
```

Four occurrences today (18:21:17, 18:24:02, 18:24:04, 18:28:56), every
one showing an **identical gap of exactly 3** between the last-signaled
and last-emitted sequence numbers on the `gfx_0.0.0` ring
(4307467->4307470, 4332451->4332454, 4332455->4332458,
4392130->4392133) - a strikingly consistent, reproducible pattern, not
random GPU instability.

### 37b. What this means

The `amdgpu` kernel driver's own timeout-detection-and-recovery (TDR)
mechanism is firing: real GPU commands are submitted to the graphics
ring and never signal completion, so after a timeout the kernel forces
a ring reset. The reset itself succeeds cleanly every time ("device
wedged, but recovered through reset") - this is not a catastrophic,
unrecoverable GPU crash - but the reset kills every Vulkan/D3D12
context sharing that GPU, including vkd3d-proton's, which is what
actually produces every downstream symptom this investigation chased:

- `VK_ERROR_DEVICE_LOST` / RADV "context is lost, this context is
  innocent" (§37a)
- D3D12 `d3d12_device_mark_as_removed` (device-removed state)
- `hipGetLastError() -> 801` ("operation not supported") - HIP's real,
  honest report that the underlying device is no longer usable, not a
  shim bug or an unsupported-instruction rejection (§34's memcpy-kind
  fix, §35's correction, and §36's WMMA test were all real, valid
  findings in their own right, but none of them was ever the actual
  cause of a crash - see §37c)
- danielblnc's own `"its capture never landed"` diagnostic (§34a) - the
  command list the runtime expected to execute never did, because the
  device it was queued on had just been reset out from under it
- the frozen picture itself - neither the game, Proton, nor
  danielblnc's runtime has real recovery logic for a lost D3D12 device
  (very few applications do; it's a notoriously hard case to handle
  gracefully), so the whole pipeline just hangs once it happens rather
  than crashing cleanly or recovering

### 37c. Correcting the record on §34-§36 in light of this

None of the fixes/findings from §34-§36 were wrong on their own terms -
they're real, valid, and worth keeping - but this finding means none of
them was ever the actual root cause of any observed crash:

- §34's `hipMemcpy` device-to-device kind fix: a real bug, correctly
  fixed, but coincidental timing in the original log reading, not the
  cause (corrected already in §35).
- §36's WMMA fp8 instruction test: genuinely proves WMMA fp8 executes
  correctly *in isolation, at trivial scale* (a single work-item). It
  says nothing about whether the same instruction, dispatched at real
  production scale with real data dependencies and real synchronization
  patterns (exactly what danielblnc's actual `k_swin_var` and related
  kernels do), can wedge the `gfx_0.0.0` ring - which is now confirmed,
  independently, to actually happen. The isolated test was real and
  correct; it just wasn't testing the condition that turns out to
  matter.

### 37d. Open, not yet answered

*What* specifically wedges the ring is not yet known - only that it
does, reproducibly, with an exact 3-command gap every time. The kernel
writes a real coredump to
`/sys/class/drm/card1/device/devcoredump/data` on each reset (confirmed
in the journal: `"AMDGPU device coredump file has been created"`), but
it is root-only and short-lived (gone by the time this session checked,
~2 minutes after the reset, without root access to grab it sooner) -
capturing and analyzing one of these (needs `sudo`, needs to happen
within roughly a minute of the reset) is the concrete next step, along
with correlating the exact HIP call/kernel dispatch immediately
preceding each ring-timeout event against the now-detailed
`hipLaunchKernel` grid/block logging from §34's earlier `native.c`
change.

## 38. The devcoredump: a real GPU page fault at exactly the 4GB boundary (2026-09-15)

§37 found *that* the GPU ring hangs. This section finds *why*, from a
real captured kernel devcoredump - not inference.

### 38a. Capturing it

The kernel clears `/sys/class/drm/card1/device/devcoredump/data`
within roughly a minute of writing it, and it's root-only - both of
which defeated the first attempt in §37d. Fixed with a small
`sudo`-run polling loop (started *before* the next launch, so it was
already watching):

```bash
sudo bash -c '
while true; do
  for f in /sys/class/drm/card*/device/devcoredump/data; do
    [ -e "$f" ] || continue
    ts=$(date +%Y%m%d-%H%M%S)
    cp "$f" "/path/to/gpucore-$ts.bin"
  done
  sleep 0.5
done
'
```

This grabbed 14 copies of the same coredump (one real ring-timeout
event at 18:34:23, confirmed via `journalctl -k` - the file just
happened to stay readable for ~15 seconds this time instead of its
usual ~2 minutes, so the 0.5s poll caught it repeatedly).

### 38b. The format is plain text, not opaque binary

AMD's devcoredump (`amdgpu_coredump`, kernel 7.0.0-31-generic) writes
a genuinely human-readable ASCII report - SOC/HW-IP version info,
firmware versions, the full register-level "IP Dump" state at fault
time, and the raw ring/IB contents - not a proprietary binary blob
needing vendor tooling to open. `strings`/`grep` are enough for the
useful parts.

### 38c. The real fault, in the driver's own words

```
Ring timed out details
IP Type: 0 Ring Name: gfx_0.0.0

[gfxhub] Page fault observed
Faulty page starting at address: 0x0000000100000000
Protection fault status register: 0x841050
```

`0x0000000100000000` is exactly 2^32 - the 4GB boundary, to the byte.
This is not a random invalid pointer; it's the textbook signature of
a **32-bit integer overflow / pointer-truncation bug**: some GPU-side
address or byte-offset calculation used 32-bit arithmetic where the
real, intended value needed 64 bits, and wrapped around exactly at
4GB. The kernel driver's own `[gfxhub] Page fault observed` line (not
a status register left over from a previous event - this is the
driver's own live-decoded summary of the fault that caused the
timeout) makes this the actual, direct, hardware-confirmed cause of
the ring hang - everything in §37 downstream of it (the reset, the
lost Vulkan context, the D3D12 device removal, the frozen picture) is
a real, correct consequence of a real page fault, not a separate
mystery.

### 38d. A concrete, checkable correlation

Summed every real `hipMalloc` call logged by `native.c` across this
session (`grep "hipMalloc(size=" /tmp/amdhip64_7_unixlib.log`, then
summed): **~8.2GB of cumulative allocation traffic across 540 real
`hipMalloc` calls**, against this GPU's real 16GB VRAM (`real vram
size: 17095983104` bytes, from the coredump's own SOC Memory
Information section). This doesn't mean live/resident usage hit 8.2GB
at once (many of those 540 allocations were freed between frames
across multiple launches this session), but it confirms the working
set genuinely operates at a scale where individual buffers or their
GPU virtual-address placement plausibly land near or past the 4GB
mark - exactly the range where a `uint32_t` offset/index bug would
first manifest, and would do so unpredictably (only once allocation
layout happens to place the relevant buffer past 4GB), matching how
this hasn't been a 100%-reproducible-on-every-frame failure but a
real, if frequent, intermittent one.

### 38e. What this means, and what's still open

This is the real, concrete, hardware-confirmed root cause this whole
investigation (§29-§37) was chasing. It is very likely a genuine bug
in danielblnc's own compute kernels (a 32-bit address/offset
computation somewhere in the pipeline that doesn't hold up once
addressed data crosses 4GB) - not a Linux/Wine/Proton/ROCm-side
problem, and not anything this project's own shim causes or can fix
by itself. It may be latent on Windows too (same kernel binary),
simply harder to trigger there if Windows-side testing never pushed
total addressed GPU memory as far past 4GB, or masked by different
timing/allocation-layout behavior under the native Windows HIP/ROCm
stack versus this project's own shim.

**Not yet done:** identifying exactly *which* kernel/buffer access
produces the offending address computation (would need either
disassembling the actual GPU ISA in danielblnc's kernels - real
reverse-engineering of his proprietary binary, which this project's
license posture (§7) avoids - or reporting this finding to danielblnc
directly, since he has the real kernel source and can find the
specific overflow immediately from this coredump's fault details).
The devcoredump itself
(`docs/gpucore-20260915-183423.bin` locally, `.gitignore`d, never
committed - see `gpucore.*` in `.gitignore`) is preserved for exactly
that purpose: handing to danielblnc, or to Mesa/RADV if it turns out
to be a driver-side gap in how gfxhub reports/recovers from this
specific fault class, rather than purely a kernel-side bug.

## 39. A real, opt-in test: capping total outstanding GPU allocation (2026-09-15)

Direct follow-up to §38's finding. Rather than only diagnosing further,
this adds a real, testable mitigation: an opt-in cap on this pipeline's
total *outstanding* (allocated, not yet freed) real GPU memory, to see
whether keeping every buffer comfortably under the 4GB boundary the
real page fault landed on (§38c) avoids the bug in practice - without
needing danielblnc's kernel source at all.

### 39a. Implementation

New pure module, matching this project's established
`registry.c`/`version_query.c`/`ext_mem.c` pattern (Wine/HIP-independent,
testable with plain host gcc):

- `windows-runtime-bridge/hip-unixlib/alloc_cap.h`/`.c` - a fixed-size (4096-entry)
  pointer->size tracking table plus a running outstanding-bytes total
  (mirrors `registry.c`'s own linear-scan table design).
  `alloc_cap_would_exceed(current_total, requested_size, cap_bytes)` is
  the pure decision function (`cap_bytes == 0` means no cap - always
  allows, so this is fully opt-in and changes nothing when unset).
  `alloc_cap_record`/`alloc_cap_release`/`alloc_cap_total` track real
  allocations as they happen.
- `windows-runtime-bridge/hip-unixlib/test_alloc_cap.c` - new unit tests: no-cap always
  allows, a request that would exceed the cap is rejected, one that
  fits is allowed, landing exactly on the cap is allowed (only
  strictly-over rejects), record/release correctly track the running
  total, and `NULL` pointers are safely ignored. Wired into the
  Makefile's `test` target; full `make test` passes.
- `native.c`'s `unix_malloc` reads a real cap value once, lazily, from
  a new `DLSSNR_VRAM_CAP_BYTES` environment variable (unset/`0` = no
  cap, unchanged behavior). Before making the real `hipMalloc` call, it
  checks `alloc_cap_would_exceed()` against the real running total; if
  the request would exceed the cap, it's rejected locally with real
  HIP's own `hipErrorOutOfMemory` (2) instead of ever reaching the real
  allocator - danielblnc's runtime sees an honest, standard HIP
  allocation failure, not a custom error code, so however it normally
  handles OOM (if at all) is what will happen here too. Successful real
  allocations are recorded via `alloc_cap_record`; `unix_free` releases
  them via `alloc_cap_release` on a successful real free. All of this
  is logged (`hipMalloc(...) -> REJECTED (would exceed
  DLSSNR_VRAM_CAP_BYTES=..., current outstanding=...)` /
  `hipMalloc(...) -> ..., ret=... (outstanding=...)`), so a live test
  run will show exactly when and how much outstanding allocation the
  real pipeline needs, cap-rejected or not.

Rebuilt, full `make test` passed, redeployed via md5-verified copy to
all six real `amdhip64_7.dll` and four `amdhip64_7.so` locations on
this system.

### 39b. How to run the test

Add `DLSSNR_VRAM_CAP_BYTES=<bytes>` to the Steam launch options,
alongside the existing env vars (order doesn't matter, still before
`%command%`):

```
VKD3D_CONFIG=external_memory_fd WINEDLLOVERRIDES="version=n,b;amdhip64_7=b" PROTON_LOG=1 DLSSNR_VRAM_CAP_BYTES=3221225472 WINEDEBUG=+loaddll,+module %command%
```

(`3221225472` = 3GiB - comfortably under the 4GB fault boundary from
§38c, while still leaving real headroom for the pipeline's actual
working set.) `<game>/bin/x64/amdhip64_7_unixlib_pe.log`'s underlying
native log (`/tmp/amdhip64_7_unixlib.log`) will show either:

- The pipeline runs its whole real workload under 3GiB outstanding at
  once, with `DLSSNR_VRAM_CAP_BYTES=3221225472 (real GPU alloc cap
  active)` logged once at startup and never a `REJECTED` line - and,
  critically, whether the `ring gfx_0.0.0 timeout`/page-fault pattern
  from §37/§38 still happens or not under this cap. If it stops
  happening, that's strong practical evidence the bug really is
  triggered by GPU virtual-address placement past 4GB, and a permanent
  lower default cap (or a smarter fix informed by exactly where the
  cap needed to bite) becomes a real, shippable mitigation from this
  project's side alone, with no need for danielblnc's source.
- Or real `REJECTED` lines appear - meaning the pipeline's genuine
  working set needs more than the chosen cap at once, in which case
  the cap value itself needs raising (try 3.5GiB, then closer to 4GiB)
  to find the largest safe value that still avoids the fault, or the
  experiment shows the bug isn't really about *total* outstanding
  allocation at all (e.g. it could be about one single buffer's own
  size/placement rather than the sum of several).

Not yet run against a live game session as of this writing - this
section documents the built, tested, deployed capability and the
exact next live test to run.

## 40. The VRAM cap does NOT prevent the crash - a real, informative negative result (2026-09-15)

Ran §39's test live: `DLSSNR_VRAM_CAP_BYTES=3221225472` (3GiB)
confirmed active (`"DLSSNR_VRAM_CAP_BYTES=3221225472 (real GPU alloc
cap active)"` logged at startup, env var confirmed present in the live
process via `/proc/<pid>/environ`).

**The ring hang happened again anyway** - confirmed in `journalctl -k`,
same exact signature as every prior occurrence (`ring gfx_0.0.0
timeout`, gap of 3 between signaled/emitted sequence numbers, reset
succeeds, device recovers). But this time, real outstanding allocation
at the moment of the fault was only **~720MB** - nowhere near the
3GiB cap, and nowhere near the 4GB fault-address boundary from §38c.
Zero `REJECTED` allocations occurred - the cap was never even
triggered, because the pipeline never actually needed that much
outstanding memory at once.

### 40a. What this rules out

§38d's supporting correlation (cumulative `hipMalloc` traffic summing
to ~8.2GB across the session) was real but, in hindsight, misleading:
that number was total churn across hundreds of alloc/free cycles over
multiple launches, not actual peak *resident* usage at any single
moment. This test directly measured real outstanding usage at the
moment of an actual fault and found it nowhere near 4GB. **The bug is
not caused by total live GPU memory approaching 4GB** - capping total
outstanding allocation, even aggressively (3GiB, well under this
card's real 16GB), does not prevent it.

### 40b. What still stands, and the refined theory

The fault address itself (`0x100000000`, exactly 2^32) is still
almost certainly a real 32-bit truncation/overflow bug - that part of
§38c isn't in question, only *what drives it*. Since it isn't total
data volume, the more likely mechanism is the **GPU virtual address a
specific buffer happens to be placed at by the allocator**, not how
much data is resident overall. GPU virtual address space is not
necessarily packed starting near zero - the kernel driver's VM manager
can place even a small, modest buffer anywhere across a much larger
address range, depending on allocation order, alignment, and internal
VM-manager state carried over from everything allocated and freed
before it in the same process. A kernel that computes an address as
something like `(some_index_or_offset) + buffer_base`, where the
index/offset side of that sum is computed in 32-bit arithmetic without
properly accounting for a large 64-bit base, would fault exactly this
way regardless of total data size - only *where* a particular buffer
landed in address space matters, not how much data is live at once.

### 40c. Implication for the way forward

A userspace-side mitigation (capping allocation volume, changing
allocation order/timing) is not a reliable fix from this project's own
side - it doesn't control GPU virtual-address placement precisely
enough to guarantee avoiding the bug, and this test is direct evidence
of that. The devcoredump from §38 (real fault address, real register
state) plus this negative result are together a strong, concrete
package worth handing directly to danielblnc - he has the real kernel
source and can identify the actual 32-bit-vs-64-bit address
computation bug directly, which is now clearly the more reliable path
than continued blind mitigation attempts from the Linux/shim side.

## 41. A real standalone test: out-of-bounds D3D12 UAV access at the exact fault address does NOT crash the GPU (2026-09-15)

Direct follow-up to §37-§40, and the "test instead of relaunching the
game" pivot: built a new, standalone `vkd3d-proton` test
(`tests/d3d12_external_memory_fd.c`,
`test_external_memory_fd_shared_buffer_far_offset_access`) that
answers one narrow, directly checkable question without the game,
without Wine/Proton game launches, and independent of danielblnc's
own kernels entirely: **does a real, deliberate, genuinely
out-of-bounds D3D12 UAV access at exactly the real fault address found
in §38 (`0x100000000`) crash the GPU on its own?**

### 41a. Why our own patch was ruled out first

Reviewed the full `external_memory_fd` diff (`device.c`, `heap.c`,
`resource.c`, `memory.c`) line by line: it only attaches
`VkExportMemoryAllocateInfo`/calls `vkGetMemoryFdKHR` - no address,
offset, or size arithmetic anywhere. Confirmed clean; not the fault
site.

### 41b. PM4/ring-content byte-pattern search - inconclusive, correctly not over-claimed

Before building the real test, searched the captured devcoredump's
raw ring/IB dword contents for the fault address encoded as an
LO=0x0/HI=0x1 pair. Found 380 occurrences, spread across every ring
type (graphics, both compute rings, both SDMA copy-engine rings) -
too broad to be one rogue application-level bug, and without a real
PM4 decoder (`umr` not packaged for this distro; building it from
source was judged not worth the detour), this could equally be an
entirely benign, common driver-internal address (a fence/EOP/signal
address the kernel driver itself conventionally uses) rather than
anything causal. Correctly not treated as confirmed - this is exactly
why a real, controlled test was built instead of continuing to
hand-decode raw bytes.

### 41c. The test

Reused the project's own `create_shared_buffer_desc()` helper to
allocate a real, small (256-byte) `D3D12_HEAP_FLAG_SHARED` buffer -
the same allocation shape this feature's real caller (danielblnc's
runtime, via this project's HIP shim) actually uses. Created a real
`D3D12_UNORDERED_ACCESS_VIEW_DESC` over it with
`Buffer.FirstElement = 0x100000000ull / 4` (`FirstElement` is a real
`UINT64` D3D12 API field - no shader-side arithmetic needed to reach
this exact address) and dispatched a single-thread compute shader
(reused unmodified from `tests/shaders/command/
root_parameter_preservation_cs.cs_5_0.hlsl` - the shortest existing
UAV-write shader anywhere in this test suite, chosen specifically to
avoid any shader-model gating: SM 5.0/DXBC, works everywhere) doing
one real `InterlockedAdd` at element `[0]` of that view - landing at
exactly byte `0x100000000`, nowhere near the real 256-byte backing
allocation. Checked `ID3D12Device_GetDeviceRemovedReason()` afterward.

Two real setup bugs were found and fixed by actually running it, not
by guessing further: the first shader reused
(`oob_behavior_vectorized_byte_address_32bit_write`, from
`d3d12_robustness.c`) needed shader model 6.2, which this system's
`d3d12.exe` test build doesn't report as supported - real error
`create_shader_stage Failed to compile shader, vkd3d result -4`,
found by rerunning with `WINEDEBUG=+vkd3d`. Fixed by switching to the
SM 5.0 shader described above instead of chasing the 6.2 requirement.
A real resource leak (missing `ID3D12PipelineState_Release`) was
caught by the test harness's own reference-count check and fixed
before the final clean run.

### 41d. The result

```
GetDeviceRemovedReason after a real UAV access at 0x100000000: hr 0.
```

**`hr 0` = `S_OK` - the device was NOT removed.** A real, deliberate,
genuinely out-of-bounds D3D12 UAV write at exactly the real fault
address does not, by itself, crash this GPU. The driver/hardware
safely handles it - consistent with the documented robustness
behavior D3D12/Vulkan drivers are supposed to provide for an
out-of-bounds access relative to a *declared descriptor's* range, even
when the underlying real virtual address is nowhere near any real
allocation.

Full suite run clean afterward (`VKD3D_CONFIG=external_memory_fd
VKD3D_TEST_MATCH=test_external_memory_fd wine build/tests/d3d12.exe`
- 0 failures across all `external_memory_fd` tests, no regressions
introduced).

### 41e. What this means

This rules out "any out-of-bounds access landing at this address" as
sufficient on its own to reproduce the real crash - the real trigger
needs something more specific than just touching this address through
a normal, driver-visible descriptor-bounded UAV access. Two real
candidates remain, both requiring information this project doesn't
have from outside danielblnc's own kernel source:

1. The real fault comes from an access that bypasses descriptor-level
   bounds checking entirely - e.g. a raw pointer dereference computed
   inside the shader itself (not through a UAV descriptor's declared
   range at all, which is what this test exercised), where hardware
   has no descriptor-level range to safely clamp against in the first
   place. This is very plausible for HIP/ROCm compute kernels
   specifically, which commonly use raw global-memory pointers rather
   than D3D12-style bounds-checked buffer views - genuinely different
   from how this test (and the game's own D3D12/Vulkan rendering work)
   accesses memory.
2. Something about the specific access *pattern* (a write vs. a read,
   an atomic vs. a plain store, a vectorized/wide access vs. this
   test's single scalar `InterlockedAdd`) that this narrow probe
   didn't exercise.

Candidate 1 is the stronger lead, and is consistent with everything
found so far: HIP compute kernels (unlike D3D12 shaders) really do use
raw pointers without hardware descriptor-level bounds checking, so a
kernel-side pointer/offset bug computing an address that happens to
land at `0x100000000` would fault exactly the way observed, with no
contradiction from this test's clean result. Confirming this
specifically would need either a HIP-side equivalent of this same
probe (a real HIP kernel doing a raw pointer write at exactly this
address, via `windows-runtime-bridge/investigations` - buildable, standalone, no game
needed, the natural next test) or danielblnc's own kernel source.

## 42. Confirmed, definitively: a raw HIP pointer fault reproduces the exact real crash (2026-09-15)

Direct follow-up to §41's negative result. Built
`windows-runtime-bridge/investigations/raw_pointer_fault_probe.c` - a standalone,
`windows-runtime-bridge/investigations`-style probe (comgr compile, `hipModule*` load/
launch, no game, no Wine/Proton) that compiles a real, original,
clean-room kernel taking a raw pointer argument and writing through
it with no bounds checking possible - exactly how HIP/ROCm kernels
normally address memory, unlike D3D12's descriptor-bounded UAVs
(§41). Launched with the argument set to the literal address
`0x100000000` - §38's real captured fault address, not a real
allocation.

### 42a. The result: an exact match to the real crash, reproduced on demand

Run memory-capped and wall-clock-limited
(`systemd-run ... -p MemoryMax=2G ... timeout --kill-after=10s 30s`),
since this deliberately tries to reproduce a real GPU fault:

```
Queue error - HSA_STATUS_ERROR_MEMORY_FAULT
Memory access fault by GPU node-1 on address 0x100000000. Reason: Page not present or supervisor privilege.
```

The ROCr runtime's own fault handler asserts and the process aborts
cleanly (`VMFaultHandler ... Assertion 'false && "GPU memory access
fault."' failed`, exit code 134/SIGABRT). Cross-checked directly
against the kernel journal for the same moment:

```
amdgpu 0000:28:00.0: [gfxhub] page fault (src_id:0 ring:40 vmid:8 pasid:1570)
amdgpu 0000:28:00.0:  Process raw_pointer_fau pid 1282895 thread raw_pointer_fau pid 1282895
amdgpu 0000:28:00.0:   in page starting at address 0x0000000100000000 from client 10
amdgpu 0000:28:00.0: GCVM_L2_PROTECTION_FAULT_STATUS:0x00841050
```

`GCVM_L2_PROTECTION_FAULT_STATUS: 0x00841050` is **byte-for-byte
identical** to the real game crash's fault register value from §38c
(`Protection fault status register: 0x841050`), same faulty address
`0x0000000100000000`. This is not a similar-looking fault - it is the
same fault, reproduced on demand, standalone, in under a second, with
a one-line kernel.

The kernel driver recovered cleanly this time via its `[gfxhub] page
fault` handler (killing only the offending process) rather than
needing a full `ring gfx_0.0.0 timeout`/TDR reset - `rocm-smi`
confirms the GPU idle and healthy immediately after (0% utilization,
no lingering process). The real game's crash needing a full ring
reset instead is consistent with the same underlying fault behaving
differently once embedded in Wine's process/signal handling, rather
than a different fault.

### 42b. What this settles

This is the real, definitive, standalone-reproduced mechanism behind
every crash chased through §29-§41:

- A raw HIP kernel pointer write to an invalid GPU virtual address
  genuinely, immediately faults the GPU at the hardware level -
  confirmed directly, not inferred.
- §41's D3D12 UAV test result is now fully explained rather than
  contradicted: D3D12's descriptor-bounded UAV access path safely
  clamps out-of-bounds requests; HIP/ROCm's raw-pointer kernel access
  path has no such protection and faults immediately. Both results are
  real and consistent - they just exercise genuinely different memory
  access paths, and only the raw-pointer path matches how HIP compute
  kernels (danielblnc's own kernels included) actually touch memory.
- This project's own shim, its vkd3d-proton patch, and ROCm/the Linux
  driver stack are all cleared as the cause - confirmed conclusively,
  not just by elimination. The mechanism is a real page fault from a
  bad raw pointer value, and finding *which* computation inside
  danielblnc's own kernels produces that exact `0x100000000` value
  needs his own kernel source (§40c's conclusion stands, now on much
  firmer footing).

### 42c. Where this leaves the project

The investigation has reached a real, evidence-complete endpoint from
outside danielblnc's source:
1. The crash is real, reproducible, and now understood at the exact
   hardware-fault level (§37-§42).
2. It is not caused by anything in this project's own code, patches,
   or the ROCm/Wine/Proton stack (§40, §41, §42b).
3. A real package now exists to hand to danielblnc: the devcoredump
   (§38), the exact fault register value confirmed reproducible in
   isolation (§42a), and the standalone repro itself
   (`windows-runtime-bridge/investigations/raw_pointer_fault_probe.c` - open source, no
   proprietary code, five minutes for him to run against his own
   kernel source to find the actual bad address computation).

No further blind mitigation attempts from this project's side are
likely to be productive - the next real step is getting this package
in front of danielblnc (or accepting the project stays blocked on a
bug only fixable in his closed-source kernels until he does).

## 43. ROCm 7.2.4 installed - and a real shim fix needed before it actually takes effect (2026-09-15)

Testing §40c's still-open version-gap theory (does upgrading to the
ROCm version danielblnc's runtime actually documents needing -
`70260201`/7.2.60201 - change anything about the real fault from
§37-§42?). Live-installed via the path researched in §31d/§31e.

### 43a. Two real apt obstacles hit and fixed live

- `amdgpu-install --usecase=rocm,hip` (the originally-researched
  command) pulled in the *entire* ROCm stack - `migraphx`,
  `mivisionx`, `rpp`, `miopen-hip`, none of which this project needs -
  and its huge dependency tree conflicted with Ubuntu 26.04's own
  natively-shipped `rocminfo` package. Fixed by using the narrower
  `hip` usecase (`amdgpu-install --list-usecase` confirms it installs
  exactly "HIP runtimes", nothing else).
- Still hit a real `rocminfo` version conflict even with the narrower
  usecase: AMD's `rocminfo` package uses its own independent, much
  lower-looking version scheme (`1.0.0.70204-93~24.04`) that doesn't
  follow the main ROCm release version the way `rocm-core` etc. do,
  so apt's default candidate selection kept Ubuntu's native
  `7.1.1-0ubuntu1` even though AMD's repo had a higher configured pin
  priority (600 vs. Ubuntu's 500) - because `amdgpu-install` itself
  had *already* dropped a pin file
  (`/etc/apt/preferences.d/repo-radeon-pin-600`, `Pin: release
  o=repo.radeon.com`, priority 600) that apt matches *before*
  evaluating any later-added stanza for the same origin, regardless of
  priority number - a real, non-obvious apt-pinning-order interaction,
  not a simple "highest priority wins" case. Fixed by editing that
  existing file's priority up to 1001 directly (`Pin-Priority: 1001` -
  above the "even allow a downgrade" threshold) rather than adding a
  redundant second file. Also needed `--allow-downgrades` on the final
  `amdgpu-install` invocation once apt was willing to select the
  correctly-pinned version at all, since `rocminfo`'s version string
  genuinely looks like a downgrade (`7.1.1` -> `1.0.0.x`) even though
  it's a real upgrade in ROCm-release terms.

Real, confirmed installed afterward: `hip-runtime-amd
7.2.53211.70204-93~24.04`, `rocminfo 1.0.0.70204-93~24.04`, `comgr
3.0.0.70204-93~24.04`.

### 43b. A real shim bug this surfaced: the old library was still being loaded

AMD's official installer deliberately does **not** touch
`/usr/lib/x86_64-linux-gnu` - versioned ROCm releases install
side-by-side under `/opt/rocm-<version>/lib`, with `/opt/rocm` kept as
a stable, version-agnostic symlink to whichever release is "current"
(confirmed: `/opt/rocm -> /opt/rocm-7.2.4`, with
`libamdhip64.so.7.2.70204` and its own `libamd_comgr.so.3`/
`libhsa-runtime64.so` living there). `windows-runtime-bridge/hip-unixlib/native.c`'s
`ensure_loaded()` only ever checked `/usr/lib/x86_64-linux-gnu` (and
its `/run/host/` sandbox-mapped equivalent) - which, confirmed live,
still held the **old 7.1.52801** build untouched after the "upgrade."
Without this fix, every future test would have silently kept running
the old ROCm version regardless of what got installed system-wide.

Fixed in `native.c`: added `/opt/rocm/lib/libamdhip64.so.7` (and its
`/run/host/opt/rocm/lib/...` sandbox equivalent) to the `dlopen`
candidate list, checked *before* the `/usr/lib` fallbacks, and added
the same two paths to the `LD_LIBRARY_PATH` construction so the new
library's own transitive dependencies (`libamd_comgr`,
`libhsa-runtime64`, etc.) resolve correctly too - matching the
existing, already-proven `/run/host/` sandbox-mapping pattern this
function already used for the `/usr/lib` case. Rebuilt (`make clean
&& make test` - all pure-module tests still pass, this change only
touches the Wine/ROCm-dependent `native.c` code those tests
deliberately don't cover), redeployed via md5-verified copy to all six
real `amdhip64_7.dll` and four `amdhip64_7.so` locations.

Not yet confirmed live in-game whether the shim is actually loading
the new 7.2.4 library now, or whether the real crash's fault signature
changes under it - the next live launch is the actual test.

## 44. The version-gap theory is ruled out too: identical crash under real ROCm 7.2.4 (2026-09-15)

Direct resolution of §40c's remaining open question. With the sandbox
library-loading fix from §43b/§44a in place, confirmed live via the
shim's own log that ROCm 7.2.4 was genuinely active in the real game
process - not just installed system-wide:

```
loaded /opt/rocm-7.2.4/lib/libamdhip64.so.7
hipDriverGetVersion() -> version=70253211, ret=0
```

`70253211` = 7.2.53211, matching the real installed `hip-runtime-amd
7.2.53211.70204-93~24.04` package exactly - danielblnc's own binary
string requirement (`"every working system runs 70260201"`, §31)
finally actually satisfied on this system, for the first time in this
entire investigation.

**The crash happened anyway, with the identical signature:**

```
amdgpu 0000:28:00.0: ring gfx_0.0.0 timeout, signaled seq=4891915, emitted seq=4891918
amdgpu 0000:28:00.0:  Process GameThread pid 1312953 thread vkd3d_queue pid 1313057
amdgpu 0000:28:00.0: Starting gfx_0.0.0 ring reset
amdgpu 0000:28:00.0: Ring gfx_0.0.0 reset succeeded
amdgpu 0000:28:00.0: [drm] device wedged, but recovered through reset
```

Same ring (`gfx_0.0.0`), same exact gap of 3 between the last-signaled
and last-emitted sequence numbers seen in every prior occurrence
(§37a, §38a, §43), same clean auto-recovery. The devcoredump had
already been cleared and this specific occurrence didn't log an
explicit `[gfxhub] page fault` detail line, so the exact fault address
isn't independently re-confirmed for this specific instance - but the
ring-timeout signature itself is identical in every other respect to
every prior occurrence under 7.1.1.

### Conclusion

The ROCm version gap is **not** a contributing factor. Running the
exact version danielblnc's runtime documents needing changes nothing
about the crash. Combined with §42's definitive standalone
reproduction (a raw HIP pointer write to the exact fault address
genuinely faults this hardware, independent of ROCm version, D3D12
UAV descriptor bounds, this project's shim, or its vkd3d-proton
patch), this closes out every environmental theory this investigation
raised. What remains is purely a bug inside danielblnc's own kernel
logic - not something version, environment, or platform-dependent at
all, as far as this project can determine from outside his source.

This also directly answers the "why does this work fine on Windows
for other people" question raised earlier: it is very unlikely to be
a ROCm-version-gap explanation specifically, since matching his exact
documented version requirement made no difference here. The remaining
plausible explanations (from that earlier discussion) are the ones
that don't depend on ROCm version at all - Windows' WDDM handling
device-lost/page-fault recovery differently than Linux's `amdgpu`
driver (masking the same bug as a corrupted frame or brief hitch
rather than a hard crash), a hardware-generation-specific trigger
(this is an RDNA4 card), or the interop path itself (still Linux-only
regardless of ROCm version, since it doesn't exist on Windows at all).

## 45. The portable Windows repro, compile-tested for real (2026-09-16)

Before sending `windows-runtime-bridge/investigations/windows-repro/raw_pointer_fault_repro.cpp`
to danielblnc, compile-tested it with the real `hipcc` toolchain
(installed via the `hiplibsdk` usecase - `hipcc`, `hip-dev`,
`rocm-hip-sdk` - needed for this compile test only, not for anything
else this project does day to day).

One real, Linux/26.04-specific obstacle, irrelevant to danielblnc's
own Windows build: ROCm 7.2.4's bundled `lld` linker was built against
`libxml2.so.2` (targeting Ubuntu 24.04/`noble`, since AMD's repo
doesn't officially support 26.04/`resolute` - see §31d), but this
system ships the much newer `libxml2.so.16`. Fixed locally with a
scoped compatibility symlink (`libxml2.so.2 -> libxml2.so.16` in a
scratch directory, added to `LD_LIBRARY_PATH` only for the `hipcc`
invocation) - a standard, low-risk ROCm/cross-distro workaround, since
`lld`'s actual use of libxml2 in this AMDGPU codegen path is
essentially unused (only "no version information available" warnings,
no functional impact). Not relevant to Windows at all.

**Result: the repro compiles clean and reproduces the exact fault**:

```
--- control: real allocated buffer ---
  result: 42.000000 (expect 42.0)

--- kernel_write_raw_ptr(0x100000000) - THE ACTUAL QUESTION ---
Memory access fault by GPU node-1 on address 0x100000000. Reason: Page not present or supervisor privilege.
```

Control kernel succeeds cleanly through the real, standard `hipcc`/HIP
runtime API path (not the `dlopen`/comgr path §42's original probe
used) confirming the portable version is correct, and the raw pointer
write to the real fault address reproduces the identical crash
signature. The file sent to danielblnc (`docs/message-to-danielblnc.md`)
is confirmed working, not just written and hoped to compile.

## 46. Code review follow-through: wine-proton cleanup, a real interop test, and a near-miss (2026-09-16)

Direct follow-up to the code-review analysis (readability/gaps/test
coverage across this project's own patches). Two concrete
improvements, plus a real incident caught and fixed along the way.

### 46a. `wine-proton` working tree cleaned

The tracked patch itself (one line in `make_vulkan`, removing
`VK_KHR_external_memory_fd` from `UNEXPOSED_EXTENSIONS`) was already
correct, but the working tree also carried unrelated modifications to
three generated files (`server_protocol.h`, `request_handlers.h`,
`request_trace.h`, +258/-33 lines) - almost certainly regenerated by
an earlier full build against a newer local toolchain than what's
committed at `HEAD`. Reverted with `git checkout --` so `git diff
HEAD` now shows exactly the one intended line, nothing else.

### 46b. A real end-to-end interop test, built

Every existing test in vkd3d-proton's `d3d12_external_memory_fd.c`
suite (7 tests, including this project's own §41 addition) checks
*mechanics* - does `CreateSharedHandle` succeed, does the handle look
fd-shaped - not whether real data actually flows correctly across the
shared-memory boundary. New file:
`windows-runtime-bridge/hip-unixlib/interop_roundtrip_test.c` - a standalone PE test
program exercising the real production path end to end: a real D3D12
device writes a known pattern into a `D3D12_HEAP_FLAG_SHARED` buffer
via a real GPU copy, exports it through `CreateSharedHandle`
(this project's own vkd3d-proton patch), imports the resulting fd
through this project's own `amdhip64_7.dll` (`hipImportExternalMemory`/
`hipExternalMemoryGetMappedBuffer` - not a mock, the exact same shim
code danielblnc's runtime is bridged through), and verifies the data
round-trips correctly in **both directions** - D3D12 write -> HIP
read, and HIP write -> D3D12 read via a real readback buffer.

Builds cleanly (`make interop_roundtrip_test.exe`, new Makefile
target added) - compiles and links against real D3D12 and this
project's own real HIP exports with no errors. **Not yet
runtime-verified** - see 46c for why, and the file's own header
comment for the two safe ways to actually run it later (a disposable
throwaway prefix, or a one-time drop into the real game's own folder
run through its already-proven-working Proton launch - never
experimentation against the live prefix directly, which is exactly
what caused 46c).

### 46c. A real near-miss, caught and fixed

Getting a bare `wine` invocation to correctly load this project's
*patched* `d3d12.dll`/`d3d12core.dll` (as opposed to Wine's own
built-in stub, or an unpatched vkd3d-proton) proved genuinely fiddly -
missing `wined3d`/`dxgi` dependencies when copying files by hand, and
`proton run` pulling in the real game's own launcher/`.NET` bootstrap
machinery instead of just running the given exe. One `proton run`
attempt against the real game's own compatdata prefix (1091500)
triggered Proton's own file-resync logic, which **reverted the live
prefix's `d3d12.dll`/`d3d12core.dll` to Wine's tiny built-in stub**
(114KB - not vkd3d-proton at all, patched or otherwise) - confirmed
via checksum/file-size comparison against the known-good redist build
(482KB, `md5 cfb8a26...`). This would have broken the real game's
DLSS-NR interop on the next launch.

Caught immediately via checksum comparison (not assumed - actively
verified) and fixed by restoring both files from
`/mnt/gaming-linux2/proton-build/build/redist/` (the original
Valve-pipeline build these deployments were always sourced from),
confirmed via matching MD5. `vulkan-1.dll` (the other patched
component, carrying the wine-proton fix) was checked too and found
untouched. This project's own shim was also rebuilt fresh and
redeployed to all ten real locations afterward, to remove any
remaining doubt about consistency after the incident.

**Lesson, recorded for next time**: standalone D3D12/Wine test
harnesses need a genuinely disposable environment (a throwaway
`WINEPREFIX`, never the real game's own live prefix) - `proton run`
in particular is not a safe "just run this one exe" tool, since it
carries real side effects (file resync) tuned for launching the
actual configured game, not arbitrary executables.

### 46d. Also fixed: a real `.gitignore` gap

While reviewing build-artifact hygiene here, found
`windows-runtime-bridge/hip-unixlib/test_alloc_cap` (a compiled test binary, §39) was
never added to `.gitignore` despite every other compiled test binary
in the same directory being listed. Fixed, and the new
`interop_roundtrip_test.exe` added alongside it.

## 47. Structural diff, v0.3.0 vs v0.3.1: the GPU kernel binaries are effectively unchanged (2026-09-16)

Direct follow-through on the "test coverage/further investigation"
analysis. New work folder for exactly this purpose:
`windows-runtime-bridge/vendor/releases/<version>/` (gitignored, matching this
project's existing "download read-only, never commit" convention for
danielblnc's real binaries) - keeps every version's downloaded
installer and extracted runtime cleanly separated, so comparisons
never risk cross-contaminating which bytes came from which release.

Fetched and extracted v0.3.0 the same read-only way as v0.3.1 (§43/
zmodelerlover's `extract_runtime.py`, no execution) - output hash
(`8321cae7...`) matches the tool's own documented reference value for
v0.3.0 exactly, confirming correct extraction.

### 47a. PE section table: only CPU-side sections grew

```
              v0.3.0      v0.3.1
.text         0x62b16     0x64f13   (+8,701 bytes)
.rdata        0x2ee04     0x2fd04   (+4,352 bytes)
.hip_fat      0x6574c0    0x6574c0  (identical size)
```

`.hip_fat` is the section holding the actual compiled AMDGPU code
objects (every kernel's real machine code, for every target
architecture the fat binary embeds) - it did not change size at all
between versions. `.text`/`.rdata` (plain CPU-side x86-64 code and
read-only data) both grew, consistent with the changelog's "new wait
method" being real, added CPU-side logic.

### 47b. `.hip_fat` byte diff: 0.068% differs, and it's reordering, not new code

Extracted and byte-compared the full `.hip_fat` section (6,649,344
bytes) between both versions: **4,539 bytes differ (0.068%)**, spread
across 48 same-shaped, evenly-spaced regions (roughly every 0x8A000
bytes - consistent with one recurring region per embedded target
architecture in the fat binary).

Inspected the actual content of the first such region directly: both
versions contain the *same set* of fixed-width (24-byte) table
records - the exact same byte sequence
(`5e080000110004000a0050...`) that appears near the *end* of this
region in v0.3.0 appears at the very *start* of the corresponding
region in v0.3.1. Same data, different order - not new or modified
records. This is the real, structural signature of an address-sorted
lookup/offset table that got re-sorted because addresses shifted
(exactly what `.text` growing by 8.7KB would cause), not evidence of
the underlying kernel code itself changing.

### 47c. What this means

The real, compiled GPU kernel instructions in v0.3.1 are, as far as
this structural analysis can tell without disassembling anything,
**effectively unchanged** from v0.3.0. This is consistent with, and
strengthens, §45's earlier inference from symbol names alone
(`k_swin_var<32,true>` still present, unchanged): the "new wait
method... reduces chance of stalls" fix almost certainly lives
entirely in CPU-side dispatch/synchronization logic, not in any
kernel's own address computation. If the real crash cause is a bug
inside a kernel's own arithmetic (§37-§42's conclusion), v0.3.1 is
very unlikely to have fixed it - this was inferred before from symbol
names; it's now much better supported by actual section-level
structural evidence, still without reverse-engineering any of his
actual kernel logic.

## 48. Index-overflow characterization probe: a real result, not the one intended (2026-09-16)

Built `windows-runtime-bridge/investigations/index_overflow_probe.c` to answer a narrower
question than §42 already settled: not "does a known-bad address
fault" but "at what scale does an ordinary, original, clean-room
flattened-tensor-index computation (`idx = ((z*H+y)*W+x)*C+c`, all
int32 - the shape, not the content, of what any real tensor kernel
needs) start producing invalid addresses on its own, via nothing more
than integer overflow?"

### 48a. A real bug in the test itself, found and fixed before the first real run

The first draft passed all-zero coordinates (`z=y=x=c=0`) to isolate
"does the *dimension product* alone matter" - but
`idx=((0*H+0)*W+0)*C+0` is `0` regardless of how large `H`/`W`/`C`
are, since every multiplication term is zeroed by the leading `0*H`.
This would have tested nothing. Fixed before running: coordinates set
to the *last valid element* of each dimension (`Z-1`, `H-1`, `W-1`,
`C-1`) instead, so the computed index actually approaches the real
dimension product - matching a real, ordinary access pattern (touching
the last element of a large tensor).

Second real bug, found the same way (by actually running it, not by
review): stdout is fully buffered, not line-buffered, when piped
through `systemd-run`/`timeout` rather than a real tty. The first real
run crashed with **zero captured output** - not because nothing
happened before the crash, but because the buffered output was never
flushed before the process aborted. Fixed with
`setvbuf(stdout, NULL, _IOLBF, 0)` at the top of `main()` before
rerunning - the same class of "silent, misleading absence of output"
mistake this project has hit before with premature conclusions, caught
this time before drawing one.

### 48b. The real result - and why it doesn't isolate what it set out to

```
Z=1 H=1 W=1 C=1000 (product ~1.0e3) -> hipDeviceSynchronize -> 0 (success)
Z=1000 H=1000 W=1000 C=2 (product ~2.0e9, UNDER int32 max ~2.1e9) -> real fault, process aborts
```

The second case's real computed index (`((999*1000+999)*1000+999)*2+1
= 1,999,999,999`) is **not** an overflowed value - it's a completely
valid, in-range `int32`. It faulted anyway, because this probe's
backing buffer is only 16KB (4,096 floats) and 1,999,999,999 is wildly
outside that regardless of overflow. This test's buffer was too small
to distinguish "the index overflowed and wrapped to garbage" from
"the index never overflowed but is still far larger than this small
buffer" - both produce the same observed fault, so this run doesn't
actually isolate the overflow-specific question it set out to answer.

What it *does* do: independently reconfirm, with a completely
original, clean-room kernel unrelated to §42's, that a raw HIP pointer
access meaningfully outside a real allocation's real bounds faults
this hardware immediately - the same general finding as §42, now from
a second, differently-shaped kernel, strengthening confidence in the
general mechanism without adding a new one.

### 48c. What a real, clean answer would need

Isolating "does the arithmetic overflowing matter, independent of
being merely far out of bounds" needs a backing buffer large enough
that a large-but-non-overflowed index would still land *inside* it -
i.e. a real allocation on the order of the dimension product itself
(roughly 8GB for the ~2e9-element case tested here). That's a real,
heavy allocation (half this card's VRAM) for a single test and wasn't
attempted this session - a genuine, buildable next step if this
specific question (rather than the already-settled general one) turns
out to matter, but not undertaken without deciding it's worth the
resource cost first.

A leftover ROCr-level `gpucore.*` dump this run produced (distinct
from the kernel-level `amdgpu` devcoredump §38 worked with) was
deleted immediately after inspection - same handling as every prior
occurrence of this file class in this project (real VRAM contents,
never kept, `gpucore.*`/`gpucore-*` already `.gitignore`d regardless).

## 49. A real shim improvement, prompted by §42/§48's general finding (2026-09-16)

Direct answer to "does the general raw-out-of-bounds-pointer finding
suggest anything worth adding to the shim itself": yes, one concrete
thing, unrelated to trying to detect or prevent the fault (§46's
analysis already covered why that's not realistically possible
without danielblnc's own kernel-argument struct layouts) - making a
future occurrence faster to diagnose from the log alone.

### The gap

`windows-runtime-bridge/hip-unixlib/registry.c`'s function registry already learns
each kernel's real mangled name at registration time
(`unix_register_function` receives it as `a->device_name`), logs it
*once*, then discards it - only the `host_fn` pointer and the
resolved `hipFunction_t` were kept. Every `hipLaunchKernel` log line
since then only ever had the bare function pointer to go by. Finding
out which kernel actually failed in a real crash log has meant
manually demangling a mangled symbol and cross-referencing its
pointer against an earlier `"resolved kernel"` log line, by hand,
every single time this session needed to know
(\xc2\xa735b's identification of `k_swin_var<32,true>` is a direct
example).

### The fix

`registry_add_function()` now takes the real kernel name as a third
argument and copies it into the registry's own fixed-size storage
(truncated safely if unusually long, always null-terminated) instead
of discarding it. New `registry_find_function_name()` looks it up.
`unix_launch_kernel` now looks up and logs the real kernel name on
*every* launch:

```
hipLaunchKernel(function=0x..., kernel=_Z10k_swin_varILi32ELb1EEv9VarParams) grid=(...) ...
```

instead of just `hipLaunchKernel(function=0x...)`. A future crash log
is now self-identifying without needing any manual cross-referencing
at all.

Four new unit tests in `test_registry.c` cover the new behavior:
name storage/lookup round-trips correctly, an unregistered host_fn
still correctly returns `NULL`, a `NULL` name is stored safely as an
empty string rather than crashing or misbehaving, and a name longer
than the fixed buffer truncates safely (still non-empty, still
null-terminated) rather than overflowing it. Full `make test` passes
(all pure-module suites, including the four new registry tests).
Rebuilt and redeployed via md5-verified copy to all ten real
`amdhip64_7.dll`/`amdhip64_7.so` locations on this system.

## 50. A live debugging chain that actually fixed "operation not supported" - four real bugs, found and fixed in sequence (2026-09-16)

**Important scope note up front:** everything in this section is about
the *`operation not supported` / broken-pipeline-output* problem this
project chased since §29-§35 - it is **not** about the raw-HIP-pointer
GPU fault §37-§42 confirmed and reproduced standalone. Those are two
different bugs on two different code paths. No run tonight crashed,
but every run tonight was short (~15-40s) - that is far too little
evidence to claim §37-§42's fault is fixed or even less likely to
occur. Treat this section as real, load-bearing progress on the
*correctness* problem, and §37-§42's conclusion as unchanged.

### 50a. The `.dll`/`.so` had silently drifted out of sync

Relaunching in-game with §49's kernel-name logging deployed, the
`.so`'s log (`/tmp/amdhip64_7_unixlib.log`) showed the new
`kernel=%s` format correctly, but the game-folder's
`amdhip64_7_unixlib_pe.log` (the `.dll`'s own log) still showed the
old bare-pointer format, and worse, 52 fresh `hipMemcpy: unsupported
kind 3` rejections - the exact §34/§35 bug, which should have been
fixed since that session. Rebuilding the `.dll` from current source
and diffing its md5 against what was actually deployed confirmed it:
the deployed `.dll` was stale (an old build predating the memcpy-kind
fix), while the `.so` was current - a previous redeploy pass had
only pushed the `.so` and silently left the old `.dll` in place
across all six of its locations. Rebuilt and redeployed the correct
`.dll` to all six; confirmed via `make test`'s `test_memcpy_kind` and
via the binary's own `strings` output that `hip_memcpy_kind_supported`
was actually linked in this time.

A second, previously undocumented gap surfaced in the same
diagnostic pass: `__hipRegisterVar(device_name=g_e4m3_lut) - not
implemented`, called once per session. `pe_shim.c`'s `__hipRegisterVar`
was a pure no-op (logged and discarded), so nothing could ever look
up which real device global a `host_var` pointer corresponded to -
meaning any later `hipMemcpyToSymbol` targeting it would silently
fail. This became §50d below.

### 50b. `hipMemset`/`hipMemsetAsync`/`hipMemcpyToSymbol` were silent, untraceable stubs

Redeploying the correct `.dll` fixed the memcpy-kind-3 rejections, but
`801`/`"operation not supported"` persisted at the same volume (389
hits in a 37-second session). The reason: `hipMemset`, `hipMemsetAsync`,
and `hipMemcpyToSymbol` were all unconditionally
`return set_last_error(HIP_ERROR_NOT_SUPPORTED)` with **zero logging
anywhere in either log file** - they never even reached `native.c`.
Any real call to any of the three would silently poison the shim's
sticky error state with no trace at all. (This also meant an earlier
claim in this same investigation - "`hipMemcpyToSymbol` was never
called, confirmed via log grep" - was wrong: grepping a log for a
function's name proves nothing when that function never logs its own
name. Corrected here.)

Fix: added a `log_msg` call to all three stubs first (`pe_shim.c`),
rebuilt, redeployed, relaunched. The very next session's log
immediately answered the question definitively: **45 real `hipMemset`
calls** (sizes from 64 bytes to 58MB - real buffer zero-init, clearly
load-bearing, not a fringe case), **10 `hipMemsetAsync` calls**, and
**1 `hipMemcpyToSymbol` call** (size 512 bytes - later confirmed to be
`g_e4m3_lut`).

Implemented real `hipMemset`/`hipMemsetAsync` forwarding
(`memset_forward()` in `pe_shim.c`, `unix_memset` in `native.c`,
mirroring the existing `hipMemcpy`/`unix_memcpy` pattern exactly;
`hipMemsetAsync` executes synchronously and ignores its stream
argument, matching this shim's existing `hipMemcpyAsync` convention).
New `HIP_CALL_MEMSET`/`HIP_CALL_MEMSET_ASYNC` unixlib opcodes,
`struct args_memset`. No new pure-logic unit test was added for this
specific change - unlike `memcpy_kind.c`'s real branching validation
logic, `hipMemset` forwarding is a straight passthrough structurally
identical to the existing (also untested) `hipMalloc`/`hipFree`/
`hipMemcpy` forwarding, and manufacturing a synthetic guard (e.g. a
speculative zero-size/null-pointer check nothing observed actually
needed) would have been exactly the kind of validation-for-a-scenario-
that-can't-happen this project avoids. Validated live instead, per
this shim's established testing split. Rebuilt, `make test` (still
all-pass), redeployed to all six `.dll` locations.

### 50c. `hipGetLastError()` never reset the sticky error - the real, deepest cause

Even with `hipMemset` now real, the next live run still showed 389
identical `801` hits - but now only **one** real failure occurred all
session (the still-stubbed `hipMemcpyToSymbol`). Root cause, finally
found: `pe_shim.c`'s `hipGetLastError()` read the sticky `last_error`
global and returned it, but **never reset it**. Real HIP/CUDA
semantics are explicit - `hipGetLastError()`/`cudaGetLastError()`
return the last error *and reset the stored code to success*. Without
that reset, a single failure anywhere in the process's entire
lifetime - even one early, one-off warmup call - poisons every later
`hipGetLastError()` check for the rest of the session, regardless of
whether the specific call being checked actually succeeded. This is
almost certainly the real mechanism behind every "one rejected call
poisoned every named pipeline layer" pattern this investigation ever
observed, going all the way back to §34.

Fixed via a new pure module, `last_error.h`/`.c`
(`hip_get_last_error_and_reset(current, success_code, *out_new_state)`),
wired into `hipGetLastError()`. Unlike §50b, this one has real
branching/state-transition logic worth isolating, so it got proper
unit coverage: 5 tests in `test_last_error.c`, including the specific
regression this fixes directly - a second read after the first reset
comes back clean instead of repeating the stale error.

**Result, confirmed live immediately after redeploy:** only 1
`error=801` all session (the one real `hipMemcpyToSymbol` failure),
and 388 of 389 `hipGetLastError()` calls now correctly report `0`.
Daniel's own runtime log went from every job showing ~38 failed named
layers to `job 1 GPU errors: no error img:operation not supported` -
every layer clean except the one plausibly depending on the still-
unfixed `g_e4m3_lut` table.

### 50d. The `g_e4m3_lut` symbol table, implemented for real

With `img` the only remaining failing layer and pointing straight at
the gap flagged in §50a, implemented the real fix: `__hipRegisterVar`
now resolves the real device global via `hipModuleGetGlobal` (mirroring
`__hipRegisterFunction`/`hipModuleGetFunction` exactly) and remembers
`host_var -> device_ptr` in a new registry table
(`registry_add_var`/`registry_find_var`, `windows-runtime-bridge/hip-unixlib/registry.c`,
kept separate from the function table on purpose - no reason a
host_var and host_fn address should ever be compared against each
other). `hipMemcpyToSymbol` now looks up that resolution and forwards
a real `hipMemcpy` at `device_ptr + offset`. New
`HIP_CALL_REGISTER_VAR`/`HIP_CALL_MEMCPY_TO_SYMBOL` opcodes,
`struct args_register_var`/`struct args_memcpy_to_symbol`. Four new
unit tests in `test_registry.c` (add-then-find, unknown-returns-null,
independence from the function table even sharing a numeric address,
capacity limit).

**Result, confirmed live:** `g_e4m3_lut` resolves to a real device
pointer (`0x73ed8f64aa80 (512 bytes)`), the real `hipMemcpyToSymbol`
write succeeds (`ret=0`), **zero `801` errors the entire session**,
and job 1 no longer reports a `GPU errors` line at all - not even
`img`. Independent, non-error-code confirmation this is a real fix,
not just clean plumbing: `auto-exposure: encoded mean 0.997`, versus
`encoded mean 0.000` (a dead/zeroed buffer) in every run before
tonight's fixes. Rebuilt, `make test` (7 suites, all pass), redeployed
`.dll`+`.so` to all ten locations.

### 50e. What's left: a real, separate, deterministic D3D12 capture-drop issue

With every GPU/HIP error gone, Daniel's own runtime log still shows a
real, consistent issue across every session tonight: `job N: its
capture never landed (captured word X, submitted Y): the game did not
execute the command list our capture was recorded into`. Across the
whole night's log (34 jobs, 51 such events): **job 1 always succeeds,
jobs 2 and 3 fail every single time** with near-identical numbers
(`captured word 1, submitted 3` almost every occurrence), **job 4
usually succeeds, job 5 always fails**. This determinism - the same
job *indices* failing the same way, run after run, not randomly -
rules out ordinary timing jitter and points at something structural:
Daniel's runtime records a command list and expects a specific "word"
to show up in a readback buffer as proof the game actually executed
that exact command list (a homemade marker/fence scheme, not a real
D3D12 fence); when the observed word doesn't match the expected
submission count, it reports the capture lost.

This is **not** a HIP-path issue - nothing about it touches
`hipMemcpy`/`hipLaunchKernel`/`hipGetLastError` at all, so it's outside
what `windows-runtime-bridge/hip-unixlib` can fix. Most likely real cause: vkd3d-proton's
D3D12->Vulkan translation is documented to batch/reorder
`ExecuteCommandLists` calls differently than a native Windows D3D12
driver in some cases; if Daniel's runtime's internal submission
counter assumes native-Windows-driver cadence, it would plausibly
miscount in exactly this job-index-dependent, repeatable way under
Proton. Confirming this fully needs either Daniel's source (closed) or
a real Windows reference log for the same scene to diff against -
neither available here. Worth raising with danielblnc directly.

A related, likely downstream symptom, also newly noticed: `inline:
too many frames in flight, skipping one` - a real, high-volume
counter (700-1100+ skips within a single ~15-40s session), each one
meaning a frame displayed with no network pass at all
("the frame shows without the network"). Plausibly a consequence of
the same capture-drop issue (dropped/never-landing network jobs
create backlog, and this is the runtime's own overload response to
that backlog) rather than a fourth, independent bug - not yet
confirmed either way.

**Deployment note:** every fix in this section was rebuilt, run
through `make test`, and redeployed via md5-verified copy to all real
`amdhip64_7.dll`/`amdhip64_7.so` locations on this system before the
next live test, same discipline as every previous session.

## 51. §50e's capture-drop issue: the memory-coherency theory, tested and ruled out (2026-09-16)

Two competing explanations for §50e's deterministic capture-drop
pattern: (a) vkd3d-proton reorders/batches `ExecuteCommandLists`
relative to native Windows scheduling, or (b) a CPU-visible read of a
GPU-written buffer isn't properly invalidated/coherent under this
system's Vulkan translation, so the CPU can see a stale value even
after the GPU write genuinely completed. (b) fit the data at least as
well as (a): jobs 2 and 3 report the *exact same* stale value every
single run (`captured word 1, submitted 3`), which looks more like a
stuck, un-invalidated cache read than randomly dropped/reordered
submissions.

Before investigating any specific solution (and before considering
work seen in other public Linux ports of this runtime - see below),
(b) was tested directly and in isolation:
`windows-runtime-bridge/investigations/readback_coherency_probe.c`, a standalone D3D12
program with no HIP/shim dependency at all. 200 iterations, each one:
CPU writes a known marker into an UPLOAD buffer, a real GPU
`CopyBufferRegion` copies it into a READBACK buffer, a **real blocking
`ID3D12Fence` wait** confirms the GPU copy is unambiguously complete
(no polling, no timeout, no assumption about scheduling - this is the
key difference from danielblnc's own check), then the CPU maps the
READBACK buffer and compares the value against the marker just
written.

**Result, run against the real patched Proton prefix
(`fdtest-11.0-2c`'s `d3d12.dll`/`d3d12core.dll`, `WINEPREFIX` pointed
at the real game's compatdata prefix, memory-capped and wall-clock-
limited per this project's safety convention): 200/200 iterations
passed, zero mismatches.** The coherency theory is ruled out on this
system - every real, fence-confirmed GPU write to a READBACK-heap
buffer was immediately visible to the CPU, every time. Whatever
causes §50e's capture-drop pattern, it is not a memory-visibility bug
in vkd3d-proton's `D3D12_HEAP_TYPE_READBACK` handling.

This leaves (a) - a real scheduling/reordering or timing difference
between how vkd3d-proton executes command lists and what danielblnc's
polling-based capture check assumes - as the remaining candidate,
unconfirmed but no longer competing against a ruled-out alternative.

### A note on prior art

While scoping what a fix might look like, two public forks
independently targeting this exact problem were found:
`guentra/DLSS-NR-on-AMD-Linux` (the original) and
`bulacha3/DLSS-NR-on-AMD-Linux` (a repackaging/hardening of it,
updated for upstream 0.3.1, with real gameplay validation and
automated ASan/UBSan contract tests). Their solution - a vkd3d-proton
patch that recognizes danielblnc's exact wait-shader by bytecode hash
and replaces the native polling wait with an explicit producer/
consumer handoff ABI (`nr_ordered.h`) - was read for understanding
only, not adapted or ported: neither repo grants a license for that
code (`bulacha3`'s own `LICENSE` states it "does not relicense the
recovered guentra code"; `guentra`'s repo carries no license at all).
This project's own continuation, including §51's probe above, is
independently built - informed by understanding what problem needs
solving, not by their implementation.

## 52. A second probe: idealized back-to-back submissions don't reproduce the drop either

§51 ruled out memory-coherency. The remaining candidate - a real
scheduling/reordering or timing gap between vkd3d-proton's submission
handling and what danielblnc's polling check assumes - was itself
really two different, conflated theories: (a) true reordering (a
later submission's result becoming visible before an earlier one's),
or (c) plain latency (everything completes strictly in order, just
slower under this stack than danielblnc's own check budget assumes -
tuned against native Windows/WDDM timing). The identical
`captured word 1, submitted 3` seen every single run for the same job
indices (§50e) fits (c) just as naturally as (a): a consistent latency
gap produces exactly that kind of deterministic "always short by the
same amount" pattern.

`windows-runtime-bridge/investigations/submission_timing_probe.c` tests both directly:
8 independent command lists, each copying a distinct marker into its
own offset of one READBACK buffer, submitted via 8 separate
`ExecuteCommandLists` calls in a tight loop with **no wait between
any of them** (the realistic pattern - a real game does not
blocking-wait after every submission). Immediately after issuing all
8, a spin-poll (mirroring the "predicated spin slices" wait style
seen in danielblnc's own log) reads a held-open mapping of the
READBACK buffer directly, recording the first wall-clock timestamp
(`QueryPerformanceCounter`) each slot's value becomes correct, within
a 1-second budget - checking both whether every slot resolves at all
(rules out (c)-style timeout) and whether completion order ever
violates submission order (would confirm (a)).

**Result, run against the real patched Proton prefix, memory-capped
and wall-clock-limited: all 8 slots resolved strictly in submission
order, all within the same ~0.4ms poll granularity - no timeout, no
reordering.** Neither (a) nor (c) reproduced for this idealized,
isolated, low-contention pattern.

**This does not clear vkd3d-proton of §50e's actual drop pattern** -
it only rules out the simplest form of the theory. Real, meaningful
differences remain between this probe and danielblnc's actual usage
that a faithful reproduction would need to account for:
- Real concurrent GPU load: the probe ran with nothing else competing
  for the queue; the real game has substantial concurrent rendering
  and other compute work queued at the same time.
- Danielblnc's actual wait mechanism is a compute/pixel **shader**
  dispatch writing a UAV, not a plain `CopyBufferRegion` - shader
  dispatch scheduling/latency is not necessarily identical to copy-
  engine scheduling.
- Possible multi-queue interaction: `bulacha3`'s own docs mention a
  `DLSSNR_SWAPCHAIN_QUEUE` fallback for identifying "the queue owned
  by [a] swapchain" - a hint that queue ownership/identification is
  itself a known tricky area on Linux, not exercised by this probe's
  single queue.
- CPU-thread-side timing: if danielblnc's actual check runs on a
  different thread than submission (e.g. once per frame on a present
  thread), real OS thread scheduling could itself introduce the gap,
  independent of anything GPU-side - not something this probe's
  single-threaded design can observe.

Next, if pursued further: a probe closer to real conditions - genuine
concurrent GPU load plus a real compute-shader-based wait/write,
rather than a plain copy - before concluding anything more about (a)
vs (c). Not started; reporting the honest limit of tonight's evidence
rather than continuing without deciding whether that's worth the
added complexity.

## 53. The real mechanism, found in `dmesg`: every session tonight had a silent GPU ring hang + hardware reset

While testing vkd3d-proton's `single_queue` config flag, a real
`VK_ERROR_DEVICE_LOST` appeared in the Wine trace log. `sudo dmesg`
told the real story - and it reframes this whole investigation:

```
amdgpu 0000:28:00.0: ring gfx_0.0.0 timeout, signaled seq=..., emitted seq=...
amdgpu 0000:28:00.0:  Process GameThread pid <N> thread vkd3d_queue pid <M>
amdgpu 0000:28:00.0: Starting gfx_0.0.0 ring reset
amdgpu 0000:28:00.0: Ring gfx_0.0.0 reset succeeded
amdgpu 0000:28:00.0: [drm] device wedged, but recovered through reset
```

**Every single one of tonight's six game launches triggered this**,
confirmed by matching each event's `pid <N>` against every session's
real Cyberpunk2077.exe PID - including every run this session called
"clean" (zero `801` errors, `no error` job status, real computed
output). The reset is **completely transparent to userspace**: AMDGPU
silently resets the hung ring and the game keeps running with no
visible error in either our own HIP/D3D12 logs or danielblnc's own
runtime log. Nothing tonight before this was actually a fully healthy
run - every one had a real GPU hang happening underneath that nothing
in this project's own logging surfaced, because none of it was ever
looking at the right layer (kernel/DRM, not HIP or D3D12).

### What this most likely explains

The hang is attributed specifically to `thread vkd3d_queue` -
vkd3d-proton's own command-submission worker thread. danielblnc's own
runtime log already describes exactly what kind of work that thread
is doing: `"inline wait: predicated spin slices (preemptible between
slices)"` - a **GPU-side spin-wait shader**, busy-polling on the GPU
for a CPU/game-side signal, with its own internal adaptive timeout
(`"host watchdog after 131072 iterations, cap 270336000, budget 200
ms, ... budget now 70 ms"`, seen throughout tonight's logs). A
GPU-side busy-spin is exactly the kind of workload that can genuinely
hang a hardware ring if it runs long enough to exceed AMDGPU's own
ring-timeout budget - and Linux's TDR/ring-timeout tuning has no
reason to match Windows/WDDM's. §50e's deterministic capture-drop
pattern (same job indices failing the same way, every run) fits this
mechanism precisely: if the *specific* in-flight spin-wait/capture
work is what the ring reset wipes out, "the capture never landed"
would be the exact, literal, accurate description of what happened -
not a metaphor for a scheduling race, but the actual, correct
observation of a piece of GPU work that was discarded by a hardware
reset mid-flight.

### Corroboration, read earlier tonight but not connected until now

`bulacha3`'s own documentation (read in this session's earlier
research into prior art) explicitly reports **"device loss"** with
danielblnc's *graphics*-wait mode specifically in 007 First Light, and
defaults new installs to *compute*-wait mode (`SpinDraw=0`) to avoid
it - the same failure class just captured directly in `dmesg` here,
independent confirmation from a different real installation hitting
the same underlying problem.

### Why this changes what tonight's config-flag tests actually mean

Neither `no_staggered_submit` nor `single_queue` were ever going to
fix a genuine ring hang - both flags govern command-submission
*scheduling*, not how long a shader is allowed to spin before AMDGPU's
own ring-timeout fires. §52's "not reproduced" result for the
idealized submission-order probe is now much better explained: that
probe never ran anything resembling a real GPU-side spin-wait shader,
so of course it didn't reproduce a spin-wait-induced ring hang. The
memory-coherency and submission-order theories (§51, §52) are not
wrong exactly - they're just answering questions one layer above where
the real problem actually lives.

### Real, unresolved question for next time

The devcoredump this specific reset produced
(`/sys/class/drm/card1/device/devcoredump/data`) had already expired
(kernel auto-removes these after a few minutes) by the time this was
investigated - not captured. A future reset should be captured
immediately (`sudo cat` it right after the `dmesg` line appears, same
urgency as §38's original devcoredump capture) for a real chance at
seeing exactly what GPU work was in flight when the ring timed out.
Also worth checking: whether danielblnc's own `dlssnr_on_amd.ini`
(mentioned in the runtime log's startup lines) exposes any user-facing
wait-method or timeout-budget setting - `bulacha3`'s docs reference a
"saved wait method" and `SpinDraw=0`/`Async=0`/`CpuWait=0`-style
settings existing in danielblnc's own runtime, which would be a real,
zero-code lever if a Linux-friendlier wait mode is already exposed
there and simply not being used by this project's setup.

## 54. §53's "unresolved question" answered directly: danielblnc's own binary documents the exact remedy

Checked the real `dlssnr_on_amd.ini` next to the game - it only sets
`Enabled=1`, nothing about wait method or timeouts, meaning every other
setting is at whatever danielblnc's binary defaults to. `strings` on
the real, already-extracted `windows-runtime-bridge/vendor/releases/v0.3.1/version_v0.3.1.dll`
(no execution, no disassembly - the same static, read-only technique
§47 used) surfaced the real ini keys and, critically, danielblnc's own
compiled log messages explaining them:

- `"re-create has waited 1 s for %d inline job(s) whose captures never
  landed (done %d, submitted %d); releasing them"` - this is the
  **literal source string** of the exact message this whole
  investigation (§50e onward) has been chasing. Confirms "capture
  never landed" is danielblnc's own name for exactly this condition,
  not something this project inferred.
- `"inline flag check: a store from the game's queue was NOT seen by
  the GPU wait within %u iterations (%.0f ms; readback %s it). Inline
  mode will time out on this driver (HIP runtime %d; every working
  system runs 70260201 / Adrenalin 26.x): update the driver, or set
  Inline=0 in dlssnr_on_amd.ini"` - danielblnc's own documented
  remedy for exactly this symptom class, unprompted, already compiled
  into the binary. `Inline=0` falls back to asynchronous mode
  (`"inline mode unavailable... running asynchronously"`), giving up
  same-frame waiting entirely rather than spin-waiting for it.
- `SpinDraw` - a separate setting. `"inline wait: spin slices run as
  1-pixel graphics draws (the compute-dispatch spin is not preempted
  by the OS scheduler: GPU watchdog resets every few minutes;
  SpinDraw=0 restores it)"` names the *exact* failure class §53 found
  in `dmesg` - directly. Our own real log line
  (`"inline wait: predicated spin slices (preemptible between
  slices)"`) shows we are *not* on that specific bad path already
  (a third, presumably safer, named mode) - and still hit the ring
  timeout every single run, so whatever's happening is either a gap in
  "predicated"'s actual preemptibility under this stack specifically,
  or a related but distinct issue. Worth testing directly rather than
  reasoning further from string content alone.
- `InlineWaitMs`, `CpuWait=%d` (`"the game thread waits for the network
  after each capture"`) - further tuning knobs, not yet tested.

**Next real test**: add `Inline=0` to `dlssnr_on_amd.ini` - danielblnc's
own documented fallback for this exact symptom, zero code, costs
nothing to try, and directly targets the mechanism §53 identified
(giving up the GPU-side spin-wait entirely rather than hoping it
finishes before AMDGPU's ring timeout).

## 55. `Inline=0` isn't a real key; `CpuWait=1` is real but doesn't fix the drop

Tested §54's leads directly, live:

**`Inline=0` in `dlssnr_on_amd.ini`: no effect, and now confirmed why.**
The runtime log still showed `mode inline (same-frame, game waits for
the network)` and the identical capture-drop pattern after adding it.
A closer look at the binary's own string table explains this: `Inline`
does not appear anywhere as a standalone string - only `SpinDraw`,
`CpuWait`, and `InlineWaitMs` do. The `"...set Inline=0 in
dlssnr_on_amd.ini"` text is only ever embedded inside one longer
message string, most likely message text that drifted from an actual
ini key renamed at some point in this runtime's development - not a
real, currently-read key. Correction to §54's recommendation, found by
testing rather than assumed.

**`CpuWait=1`: confirmed real and read correctly, but does not fix
the capture-drop pattern.** Real, measurable internal behavior change
between the previous (default/GPU-spin) run and this one: the
watchdog `cap` value dropped from `270336000` to `1500000`, the
reported `iter/ms` rate collapsed from `~450560` to `~0` (consistent
with switching away from a tight GPU-side spin loop), and
`"host watchdog fired 1 times"` appeared for the first time all
night (every prior run always read `0`). This proves `CpuWait` is a
real, currently-honored setting, not another `Inline`-style dead
string.

Despite that real change, the dominant capture-drop pattern was
unchanged: jobs 2, 3 and 5 remained the primary repeat failures (20,
19 and 20 occurrences within one ~13-second session - the `job N`
counter resets on every staging re-create, so this reflects many rapid
re-create cycles, not 20 distinct captures), with a handful of new
one-off failures at other job indices (4, 6, 7, 8) not seen before.
PE-side HIP state stayed clean throughout (1 `error=801`, matching the
single known, harmless `hipMemcpyToSymbol` stub-related event seen on
every good run since §50d - not a regression).

**Read on this**: switching away from the GPU-side spin-wait entirely
did not fix the drop, which is real, useful negative evidence against
"the GPU-side spin specifically is the whole story" - either the ring
hang (§53) has a cause independent of which wait method is active, or
`CpuWait=1`'s own implementation has a different but similarly-shaped
timing gap under this stack. Whether the `gfx_0.0.0` ring timeout in
`dmesg` still occurs under `CpuWait=1` specifically was not yet
checked this session - real next step, since a "yes" would decouple
the ring hang from the spin-wait mechanism entirely, and a "no" would
mean the ring hang and the capture-drop pattern are two separate
issues that happened to coincide every run so far.

## 56. Answered: `CpuWait=1` fixes the ring hang, but the capture-drop is a real, separate issue

`sudo dmesg -T` for the `CpuWait=1` session (PID 1768236, launched
19:00:39): **no `gfx_0.0.0` ring timeout at all.** Every other run
tonight - including the `Inline=0` run immediately before it (PID
1766789, timeout logged 18:59:19) - hit the ring timeout within its
first ~10-15 seconds. This is the first run all night that didn't.

Combined with §55's result (capture-drop pattern unchanged under
`CpuWait=1`), this cleanly decouples what looked, until now, like one
problem:

1. **The GPU ring hang (§53)** - caused by the GPU-side spin-wait
   specifically, **confirmed fixed** by `CpuWait=1`, with real
   hardware-level evidence (`dmesg`), not just internal counters
   changing.
2. **The capture-drop pattern (§50e)** - a real, independent issue.
   It happened to coincide with the ring hang on every run before
   tonight's `CpuWait=1` test purely because both were always present
   together under the default GPU-spin wait method - not because
   they're the same mechanism.

**Practical takeaway:** `CpuWait=1` in `dlssnr_on_amd.ini` is a real,
verified, zero-code mitigation for the GPU ring hang / silent
watchdog-reset problem, worth keeping regardless of the capture-drop
issue's own status. The capture-drop investigation (§51, §52, this
section) continues to need its own, separate resolution - the memory-
coherency and idealized-submission-order theories are both still
either ruled out or unreproduced, and now a third candidate (the ring
hang itself) is also ruled out as the cause. What's left: candidate
(a) from §52 (true reordering under real, non-idealized conditions -
concurrent load, an actual shader-based wait/write), not yet tested;
or something in `CpuWait`'s own implementation that has a differently-
shaped but analogous timing gap under this stack, not yet
investigated either.

## 57. Automated launch infrastructure built; a real, worse-than-expected `CpuWait=1` hang found by using it

Built real automated start/stop tooling this session so game tests
don't need a manual click each time:

- **`--launcher-skip`** (researched and confirmed real via both a
  community guide and a literal string in `REDprelauncher.exe` itself)
  as a Steam launch-option argument, appended after `%command%` -
  bypasses CD Projekt's own `REDlauncher.exe` GUI stage entirely.
  Confirmed live: a stable `Cyberpunk2077.exe` PID now appears within
  ~2 seconds of triggering `steam steam://rungameid/1091500`, versus
  needing a manual click through a launcher GUI before. (One internal
  re-exec still happens early - the first PID found is transient and
  is quickly replaced by the real long-lived one; automation needs to
  poll past that rather than trust the first match.)
- **Memory/CPU capping applied to an already-running process**, not
  wrapped at launch (Steam spawns the actual game process, not this
  session, so `systemd-run`'s normal "wrap a command" mode doesn't
  apply): create a transient user-scope via `systemd-run --user
  --scope --unit=<name> -p MemoryMax=16G -p CPUQuota=800% --collect --
  sleep <placeholder>`, then move the real game PID into that scope's
  `cgroup.procs` (writable without root under systemd's delegated
  per-user cgroup tree), then kill the placeholder - the scope
  persists as long as a real process remains in it. Confirmed active
  via `systemctl --user status` showing the real PID under the capped
  scope with the correct limits.
- **Periodic log watching during the run**, not just after - checked
  `dlssnr_on_amd.log` and both shim logs every 15s against a 60s
  budget, catching a real hang live rather than only discovering it
  after the fact.

### What that log watching actually caught

With `CpuWait=1` still active (the real, `dmesg`-confirmed fix for
§53's GPU ring hang), this run **still hung** - but differently and
arguably worse. Both shim logs stopped growing entirely at a fixed
point (19:43:12) while the process kept running for 2+ more minutes
at ~199% CPU, completely unresponsive, until the automated time-limit
kill terminated it. This is a different failure shape than the
GPU-spin path's own behavior: the GPU-spin wait has a real, visible
watchdog that eventually gives up and logs recovery attempts
(`"budget now 70 ms"`); whatever `CpuWait=1`'s own CPU-side wait loop
is doing here shows no evidence of any bounded retry or self-recovery
at all - it just spins forever with zero forward progress until
something external stops it.

**This is a real, meaningful downgrade to keep in mind**: `CpuWait=1`
verifiably fixes the specific AMDGPU ring-timeout/reset symptom (§56),
but this session's only sustained (multi-minute, unattended) test of
it produced a hang with *no* automatic recovery path at all - worse
in one sense than the ring-hang it replaces, since the ring-hang at
least self-heals via the kernel's own reset mechanism. Every prior
`CpuWait=1` test tonight was short (~13s), too brief to have ever
surfaced this. Real open question for next time: is this hang
reproducible, and does the previously-default GPU-spin path (without
`CpuWait=1`) ever exhibit the same unbounded-hang failure over a
similarly long unattended run, or does its watchdog reliably recover
every time? Not yet compared under matched conditions.

## 58. §57's comparison, run: neither wait method actually recovers to real gameplay - they just fail differently

Same automated launch/cap/watch infrastructure, `dlssnr_on_amd.ini`
reverted to the default GPU-spin wait method (`CpuWait` removed
entirely), watched for ~160s (longer than it took `CpuWait=1` to reveal
its hang, for a fair comparison).

**0-80s: identical hang signature to every GPU-spin run tonight.**
`dlssnr_on_amd.log` frozen at the same stale line, CPU pinned at
220-237%, process state `R` (running/runnable - actively spinning).

**~100s: a real state change, correlated with a real fault event.**
CPU usage began a steady, monotonic decline (209% -> 182% -> 162% ->
145% -> 123% -> 113% over the next ~80s), process state shifted from
`R` to `S` (sleeping), and the log gained two new lines - both
`FAULT: exception 0x80000003` breakpoint hits (the same class seen in
§53's `dmesg` correlation, `Cyberpunk2077.exe` and
`REDEngineErrorReporter.exe` both hit one), timestamped right at the
transition. This is consistent with §53's AMDGPU ring-reset
self-healing mechanism actually firing here and stopping the
pathological spin - real, observed evidence the kernel's own recovery
path does eventually intervene under the default GPU-spin method,
which `CpuWait=1`'s equivalent wait loop showed zero sign of over a
similarly long window in §57.

**But this is not a clean "GPU-spin recovers, CpuWait doesn't"
result - it's narrower than that.** Even after CPU usage dropped
substantially (down to 113%, well off its ~230% peak), the log never
produced a single new real job-progress line (no new `"network job N
done"` entries) for the rest of the observed window - it stayed
parked on the same stale capture-drop line, gaining only the two fault
lines. So what was actually observed is: the *pathological CPU spin*
stops (the kernel-level ring reset intervenes and halts it), but
nothing in the ~180s total observed window showed the *game's own
pipeline* resuming real, productive work either. Both wait methods
failed to produce a session that resumed real gameplay within the
observed window; they differ in what happens to the runaway CPU
consumption afterward, not in whether the underlying stall itself
gets fixed.

**Practical read:** `CpuWait=1` is not straightforwardly worse or
better than the default - it trades a self-limiting-but-still-stalled
failure (GPU-spin, CPU eventually drops via kernel intervention, game
still stuck) for a non-self-limiting one (CPU pegged indefinitely,
same stuck game, no intervention without an external kill). Neither
is an actual fix for the underlying stall each is stuck inside -
that's still the real open question this project's capture-drop
investigation (§50e, §52, §56) is chasing.

## 59. A trace-enabled vkd3d-proton rebuild, with a real build-system gotcha found and fixed along the way

Rebuilt this project's own patched vkd3d-proton with `UNSTRIPPED_BUILD=1`
(confirmed correct beforehand by tracing the exact meson logic: it
passes `-Denable_trace=true` explicitly, bypassing the buildtype-
dependent `auto` resolution entirely, so `buildtype: plain`/`-DNDEBUG`
- both still present - have no bearing on it; double-checked `NDEBUG`
isn't referenced anywhere in the breadcrumbs-relevant source at all).
This also auto-enables breadcrumbs support
(`enable_breadcrumbs = enable_trace` in vkd3d-proton's own
`meson.build`) - the GPU-hang crash-diagnostics feature this rebuild
was actually for (§ "another approach" discussion: `VKD3D_CONFIG=
breadcrumbs`/`breadcrumbs_sync` instruments command lists with real
GPU checkpoints and dumps a digested view of what was executing at
the moment of a device-lost/ring-timeout, directly relevant to §53's
confirmed ring hang).

### The gotcha: `make redist UNSTRIPPED_BUILD=1` silently did nothing, the first time

The first run completed (exit 0) but produced a byte-identical,
untouched `d3d12.dll` (same Sep 14 timestamp as before). Root cause,
found by reading `make/rules-meson.mk`: the meson *configure* step is
gated by a stamp file (`.vkd3d-proton-x86_64-configure`) whose only
tracked dependency is `vkd3d-proton/meson.build` itself - not any
Makefile variable. Since `UNSTRIPPED_BUILD` is a *make variable*, not
a source-file change, Make correctly (from its own dependency-graph
logic) considered the existing stamp up to date and never re-ran
`meson ... -Denable_trace=true` at all - the new flag was silently
never passed anywhere. Fixed by manually deleting the x86_64 stamp
chain (`.vkd3d-proton-x86_64-{configure,build,post-build,dist}`)
before re-running, forcing a real reconfigure. **Real, generally
useful lesson for this build system**: changing any `make`-level build
variable (not just this one) needs its component's stamp files cleared
manually first, or it's silently ignored on a re-run.

### The second gotcha: the freshly-built dll didn't land where the game actually loads it from

After the real rebuild, `dist/files/lib/wine/vkd3d-proton/x86_64-windows/d3d12.dll`
was correctly fresh (new timestamp, correct size change - smaller,
233KB vs 482KB, since `UNSTRIPPED_BUILD` splits debug symbols into
separate `.debug` sidecar files instead of embedding them), but
`dist/files/lib/wine/x86_64-windows/d3d12.dll` - the actual path this
project has deployed from and verified against all along - stayed
stale. No Makefile/script rule anywhere in the source tree merges
`lib/wine/vkd3d-proton/x86_64-windows/` into the main
`lib/wine/x86_64-windows/` path; that copy was evidently done by hand
in an earlier session (not this one) when the patch was first applied,
and `make redist` has no mechanism to redo it automatically. Fixed the
same way this project always handles deployment: copied
`d3d12.dll`/`d3d12core.dll` from the `vkd3d-proton/x86_64-windows/`
build output directly into `redist/files/lib/wine/x86_64-windows/`
(removing the pre-existing read-only files first - build outputs are
installed `-r-xr-xr-x`, no write bit), confirmed via
`strings ... | grep breadcrumb` that the real implementation
(`d3d12core.dll`, not the thin-proxy `d3d12.dll`) now contains
`breadcrumbs`/`breadcrumbs_sync`/`Enabling AMD_buffer_marker
breadcrumbs.` and related strings, then redeployed via md5-verified
copy to `fdtest-11.0-2c`, `proton11-fdtest`, and `Proton 11.0`
(md5 `b902ada34042a389f3dc2aff429f9910` / `18c3c128a301ead4c07b7d947ef27df0`).

### Not yet done

Live-testing `VKD3D_CONFIG=external_memory_fd,breadcrumbs_sync`
(plus `RADV_DEBUG=syncshaders` per the binary's own recommendation) on
a run that hits the known, reliable ring hang (§53) - the actual point
of this rebuild - hasn't happened yet this session. Real next step.

## 60. Breadcrumbs tested live, twice - real negative result, not a build problem

Live-tested `VKD3D_CONFIG=external_memory_fd,breadcrumbs_sync
RADV_DEBUG=syncshaders` against §53's known, reliable ring hang, using
the automated launch/cap/watch infrastructure from §57. Two real runs,
via the automated pipeline's Steam-launch-options edit (see the
sub-note below on a real gotcha hit doing that):

- **Run 1, 60s window**: config confirmed active
  (`VKD3D_CONFIG='external_memory_fd,breadcrumbs_sync,
  force_bindless_texel_buffer'`), same known hang signature (~225-244%
  CPU, frozen logs), but killed at the standard 60s limit before any
  device-lost event had a chance to be detected.
- **Run 2, a one-off 150s window** (explicitly not changed as the
  standing default - single exception, per direct instruction): same
  hang signature, CPU usage began declining around the 120s mark
  (matching §58's self-recovery timing), and this time a real
  `VK_ERROR_DEVICE_LOST` genuinely was logged
  (`d3d12_device_mark_as_removed: Device ... is lost`). The trigger
  condition breadcrumbs needs did happen.

**Still zero breadcrumb output, either run.** More tellingly: the
`"Enabling AMD_buffer_marker breadcrumbs."` /
`"Enabling NV_device_diagnostics_checkpoints breadcrumbs."`
initialization message - which vkd3d-proton's own source prints once,
unconditionally, at device creation whenever breadcrumbs is active -
never appears anywhere in either session's full log, not just around
the hang. That message is a real precondition for the feature working
at all. Its total absence, despite the config flag being read
correctly (confirmed in the `VKD3D_CONFIG=` init line both times),
points at the breadcrumbs *initialization* path itself never
completing on this system - most plausibly this RDNA4/RADV driver
combination lacking both `VK_AMD_buffer_marker` and
`VK_NV_device_checkpoints` support, rather than anything wrong with
§59's rebuild (which is independently confirmed correct: the binary's
own `strings` output has all the right breadcrumbs text, and the
config flag parses correctly at runtime).

**Real conclusion: this diagnostic approach is a dead end on this
specific hardware/driver combination**, not something more rebuild or
launch-option tweaking would fix. Confirming the RADV/Mesa version's
actual extension support (`vulkaninfo | grep -i
"buffer_marker\|checkpoints"`) would settle this definitively, but
given the total absence of even the init log line across two separate
real device-lost events, this is low-priority to chase further.

### A real Steam launch-options gotcha, hit getting here

The first attempt at this test used a direct edit of Steam's
`localconfig.vdf` (at the user's explicit request, after being warned
of exactly this risk) instead of pasting into the Steam UI. **Steam
silently reverted the edit** before the test ran - the live client
overwrote the file back to the old value on its own periodic save,
without any error or indication. Confirmed by re-reading the file
after the "successful" edit and finding the old value back, and
independently confirmed by the actual live `VKD3D_CONFIG=` line in
that run's log missing `breadcrumbs_sync` entirely. Real, empirical
confirmation (not just the theoretical risk raised earlier) that
direct `localconfig.vdf` edits are not reliable while Steam is
running - the UI paste method, used for the second (successful)
attempt, is the only confirmed-reliable way to change this while
Steam is live.

## 61. A more realistic submission-order probe (real code, real result: PASS)

While researching `guentra/dlss5-amd-hip-linux` (found via looking for
prior art in this problem space), read its actual source, not just its
README - and found real, more rigorous D3D12 submission engineering
than this project's own earlier probes used. Its
`native_game_submission.h` does genuine fence-based deferred/ring-buffer
submission (wait only on slot reuse, never after every single
`ExecuteCommandLists` call - matching real game submission cadence far
more closely than either of this project's own earlier probes: §51
always blocked after every submission, §52 never blocked at all). Its
`native_snapshot_gate.h` adds a real producer-ordering check (is the
result we're about to trust genuinely the *last* occurrence of that
list in its batch, recorded on the checking thread). This is MIT
licensed, so - unlike the earlier `bulacha3`/un-licensed-`guentra`-repo
situation - this project can legally adapt the actual code, not just
the idea, with attribution (`THIRD-PARTY.md`).

Built `windows-runtime-bridge/investigations/snapshot_gate.h`/`.c` (a C port of the
producer-ordering check, pure/Wine-independent, 11 real unit tests in
`test_snapshot_gate.c`, all pass) and
`windows-runtime-bridge/investigations/faithful_submission_probe.c` (an 8-slot ring,
40 total submissions/5 full wraps, adapting the real deferred-
submission fence logic).

**Result, run as a standalone synthetic probe (not against the live
game - same safe pattern as this project's other investigations/
probes): PASS.** All 40 submissions, all 40 readbacks correct, zero
gate-check failures, after a real final `Flush()`. A properly fence-
guaranteed, correctly-ordered, realistically-paced (partial, not
per-submission) synchronization pattern does not reproduce any drop or
staleness on this system.

**What this does and doesn't tell us**: this is a real step up from
§51/§52 - closer to actual game submission behavior than either earlier
probe - and it's still clean. Combined with §51 (memory-coherency ruled
out) and §52 (idealized reordering not reproduced), the honest
remaining possibility space for §50e's actual capture-drop pattern
keeps narrowing toward something specific to danielblnc's own wait
mechanism itself (its internal budget/watchdog logic, or its own
polling implementation lacking the kind of real fence guarantee this
probe confirms is available and correctly working on this system) -
not a general vkd3d-proton/Wine/RADV submission-correctness problem,
which three increasingly realistic tests have now failed to find.

Not yet done: running this same logic against the real, live game -
deliberately not attempted without separate, explicit go-ahead first.

## 62. A backend-selection mechanism, prepared - real research, not yet live

Prepared `windows-runtime-bridge/backends/` so this project's tooling can cleanly
switch between two mutually-exclusive injection mechanisms: this
project's own (`version.dll` hijack + `windows-runtime-bridge/hip-unixlib`) and
`guentra/dlss5-amd-hip-linux`'s (a `d3d12.dll`-proxying ReShade add-on
+ his own modified vkd3d-proton + his own `dlss5_hip.dll`).

**Real research, not assumption**: read `guentra`'s actual
`linux/dlssnr/deploy.py` to find the real mechanism, rather than
inferring it from his README. Confirmed his HIP-mode launcher
explicitly sets `WINEDLLOVERRIDES="version=b;..."` (builtin-only) -
a deliberate measure, in his own code, to make Wine ignore any
hijacked `version.dll` like danielblnc's. This independently confirms
the two backends are genuinely meant to be mutually exclusive, not
just an assumption on this project's part.

**Built**: `windows-runtime-bridge/backends/README.md` (why the two can't coexist -
overlapping FFX/D3D12 hook surface, not a literal filename collision),
`danielblnc/README.md` and `guentra/README.md` (exact file sets and
launch options for each, the latter real-researched from his source),
and `switch_backend.sh` (backs up whichever backend's files are
currently present, restores the requested one, prints the correct
launch options - never downloads or runs anything). Dry-run tested
against a sandboxed fake directory (not the real game folder): backup
and restore round-trip both verified correct.

**Real open question, flagged rather than guessed at**: whether
`guentra`'s backend needs this project's own patched Proton
(`fdtest-11.0-2c`) or a stock build. His modified vkd3d-proton targets
a different problem (a "split vkd3d submission boundary" for
CPU-mediated readback/upload) than this project's `external_memory_fd`
patch (zero-copy D3D12<->HIP interop) - his own README says his backend
uses CPU readback/upload, not shared-memory interop, so it very likely
doesn't need this project's patch at all, but this hasn't been
confirmed against his actual build docs yet.

**Status: scaffolding and documentation only.** `guentra`'s actual
release binaries have not been fetched or deployed anywhere - not into
this repo (same standing rule as `windows-runtime-bridge/vendor/`), not into the real
game install. That's a real next step, deliberately gated behind
separate, explicit go-ahead, same standing caution applied to every
other action that touches the live game install this session.

## 63. A real, sustained interactive-play session - the picture looks very different than any automated test

Every automated test all session (§50 onward) ran the game passively -
launch, sit idle, watch, kill at a fixed time limit. Tonight, for the
first time, a real person actually played for several minutes
(`CpuWait=1` active throughout) instead.

**Reliability over a long real session is far better than any short
automated test suggested.** `frames 9600 dispatches 1099 (1.00/frame)
submitted 1060 ready 1059 skipped 39 timeouts 1056 route fsr` - a
99%+ delivery rate (1059/1060 ready) across 1000+ real network jobs.
Every automated test got killed at a fixed 60s limit, squarely inside
the noisy early-startup window, before whatever internal adaptive
budget stabilizes into its steady state (`budget stays 50 ms`, only
visible after roughly job 100). The deterministic "job 2/3/5 always
fail" pattern earlier tests kept finding was real, but it's a startup
transient, not the steady-state behavior.

**But the `timeouts` counter (1056 of 1099 dispatches - 96%) is not
harmless**, and it directly explains a real, felt problem: `worker
wall 20.1 ms` (danielblnc's own measured network time) versus `spin
waiting on us 156.1 ms` (how long the game actually waits) - roughly
130 ms of pure waste per frame, and `present period` bouncing between
~163 ms and over 1000 ms (roughly 1-6 fps). The user's own report
("just really low fps") matches this exactly. The capture-check isn't
failing outright most of the time - it's timing out and retrying
through a real, expensive cycle on nearly every single job before
eventually succeeding, and that retry tax is what's costing the frame
rate, not a separate performance problem.

**One real, isolated new error, tied to the DLSS-NR overlay toggle
itself**: `job 884 GPU errors: operation not supported`, a single
occurrence right in the middle of a real SPIKE cluster
(`SPIKE at 19:48:46.870: job 853 took 420 ms (typical 20 ms)`, several
more following) that coincided with the user opening/using
danielblnc's in-game overlay to toggle DLSS-NR off and on. Plausibly a
transient resource teardown/recreation side-effect of the toggle
itself, not a repeat of the old systemic failure - one event among
1000+ successful jobs.

**The clue that led to §64**: `network job N done in 20 ms (0.0 ms
network on the GPU, ...)` - on every single job, all session. The
network never actually takes 0 ms (dozens of real kernel launches per
job, confirmed in the native log) - danielblnc's runtime measures this
with `hipEventRecord`/`hipEventElapsedTime` on its own real stream.

## 64. Real stream forwarding - `hipLaunchKernel`/`hipMemcpyAsync`/`hipMemsetAsync` were all silently running on the null stream

§63's `0.0 ms network on the GPU` measurement, combined with the 96%
capture-check timeout rate and the GPU ring hang, all have one
plausible, unifying explanation, found by re-reading this shim's own
code: `pe_shim.c`'s `hipLaunchKernel` discarded its `stream` argument
(`(void)stream;`) and `native.c`'s `unix_launch_kernel` always passed
`NULL` to `hipModuleLaunchKernel` regardless of what stream
danielblnc's runtime actually requested. `hipMemcpyAsync`/
`hipMemsetAsync` did the same - executed synchronously on the sync
path, stream ignored, a deliberate simplification documented in this
shim's own comments as "no benefit the pipeline has ever actually
needed" (§50b) - written before this project had any real evidence
either way.

If danielblnc's runtime records a `hipEvent` on its own real stream
around the copies/sets/kernel launches that do the actual network
work, but that work always ran on the null stream instead, the event
completes as soon as the (empty) real stream is checked - before the
real work is even queued, let alone finished. That's a real, complete
explanation for measuring 0.0 ms of GPU time on every job. It's also a
plausible explanation for both open problems tracked all night: the
capture-check waiting on a flag write that could land at a
wildly different real time than the runtime's own (now-shown-wrong)
timing model expects (the 96% timeout rate), and a GPU-side spin-wait
budgeted around a ~20 ms completion time it never actually observes
correctly (a plausible contributor to the ring hang, though this part
remains unconfirmed).

**Fixed**: `args_launch_kernel`, `args_memcpy`, and `args_memset` all
gained a real `stream` field. `unix_launch_kernel` now passes it to
`hipModuleLaunchKernel` instead of `NULL`. New `unix_memcpy_async`/
`unix_memset_async` native functions call the real `hipMemcpyAsync`/
`hipMemsetAsync` (newly `dlsym`'d) with the real stream, replacing the
old synchronous-on-null-stream stand-ins; the synchronous `hipMemcpy`/
`hipMemset` paths are unchanged. New `HIP_CALL_MEMCPY_ASYNC` opcode
(`HIP_CALL_MEMSET_ASYNC` already existed, now genuinely used for async
instead of aliasing the sync handler). `make test` - all 7 pure-module
suites still pass (this change is pure argument-plumbing, no new
branching logic to unit test beyond what already exists). Rebuilt and
redeployed via md5-verified copy to all ten real locations
(`.dll` `8bc7d89b960510e4f039968be94a2ec7`,
`.so` `275a1595cbb06aa91b5af2aa45eb187e`).

**Not yet tested live.** This is a real, plausible, mechanistically-
grounded theory, not a confirmed fix - the next real session should
show whether `network on the GPU` finally reports a real, nonzero
time, whether the 96% timeout rate drops, and whether the ~156 ms
`spin waiting on us` collapses toward the real ~20 ms the work takes.

## 65. §64's theory, tested live and cleanly falsified - the shim was never the bug here

Automated 60s test, standard launch/cap/watch pipeline. Two direct
results:

**`0.0 ms network on the GPU` is completely unchanged**, still showing
on every job through job 1500. **The deterministic capture-drop
pattern is byte-identical to every previous test** - jobs 2, 3, 5 fail
with the exact same `captured word`/`submitted` numbers as before this
fix (`captured word 1, submitted 3` for jobs 2 and 3;
`captured word 4, submitted 6` for job 5).

Before concluding the theory was simply wrong, confirmed the fix
actually deployed and is active - the exact reason `stream=%p` was
added to `unix_launch_kernel`'s own log line in §64: `/tmp/
amdhip64_7_unixlib.log` shows `hipLaunchKernel(function=..., kernel=...,
stream=(nil)) ... -> ret=0 (real)` on every single launch. The new code
is genuinely running (the log field itself didn't exist before §64).

**Direct, clean falsification, not a deployment failure**: `stream=
(nil)` means danielblnc's own runtime passes `NULL` as the kernel-
launch stream *by its own design* - this was never a bug in this
shim discarding a real stream. Forwarding the real stream instead of
discarding it, as §64 did, changes nothing here because the value
being forwarded was already null. Under normal HIP legacy-default-
stream semantics, null-stream work is implicitly ordered against every
other stream anyway, so even if danielblnc's timing events are
recorded on a separate real stream, they should still correctly
observe null-stream kernel completion - meaning §64's whole causal
chain (stream mismatch -> early event completion -> 0.0 ms
measurement) doesn't hold up against what's actually happening.

**What §64's fix is still worth keeping**: real `hipMemcpyAsync`/
`hipMemsetAsync` forwarding and real kernel-stream forwarding are
correct, general improvements over silently-synchronous stand-ins,
independent of whether they explain this specific symptom - matches
this project's own standing practice (e.g. the `alloc_cap`/`CpuWait`
mitigations that stayed in place even after being ruled out as *the*
fix for what they were built to investigate).

**Where this leaves the real, open question**: `0.0 ms network on the
GPU` and the 96% capture-check timeout rate are still unexplained.
Given legacy-stream semantics should already order things correctly
regardless of which stream anything runs on, the most likely remaining
explanation shifts back toward something in danielblnc's own timing/
capture-check implementation itself - not a stream-ordering bug
reachable from this shim's side at all. This may be at the point where
independent investigation from this project's side has run out of
new, cheap angles to try (§51, §52, §58, §61, §64/§65 - five real,
different, well-reasoned attempts, all either ruled-out or falsified)
- see the updated message to danielblnc (which now includes the real
96% timeout-rate finding) for the honest next step.

## 66. `InlineWaitMs` tested - confirmed real, cleanly ruled out as a fix

The remaining untested ini levers from §54 (`InlineWaitMs`, `SpinDraw`,
plus the never-actually-confirmed `single_queue` vkd3d-proton flag)
hadn't been exhausted - `InlineWaitMs` tried first as the most directly
targeted at the actual mechanism §63/§65 established (a timing budget
being exceeded on 96% of jobs).

Set `InlineWaitMs=500` (2.5x the observed default starting budget of
200 ms), `CpuWait=1` kept as the known-working baseline. Standard
automated 60s test.

**Confirmed genuinely active**: the log shows `budget 500 ms` directly
(previous sessions always started at `200 ms`) - this is a real,
correctly-read setting, not a dead/ignored key like `Inline=0` turned
out to be in §55.

**Clean negative result despite that**: the adaptive system still
converges right back down (`budget now 70 ms`, same as every prior
session regardless of starting point), and the capture-drop pattern is
byte-identical to every previous test - jobs 2, 3, 5 fail with the
exact same `captured word`/`submitted` numbers as before this change.

**Read on this**: raising the *starting* budget doesn't change the
*converged* behavior at all, which argues the 96% timeout rate isn't
really about how much time the check is initially given - it's about
something that consistently produces roughly the same small effective
window regardless of the nominal budget, and specific job indices
(2, 3, 5) miss it regardless. `SpinDraw` and confirming `single_queue`
remain the two cheap, untested levers left from §54's list.

## 67. `SpinDraw=0` tested - inconclusive, not a clean rule-out like §66

Isolated test: `SpinDraw=0` only, `CpuWait`/`InlineWaitMs` both
removed (`SpinDraw` most plausibly only matters on the GPU-spin path,
so testing it under `CpuWait=1` would risk not exercising it at all).
Standard automated 60s test.

**No confirmation this setting is actually live in this build.** The
wait-mode log line reads identically to every default session -
`inline wait: predicated spin slices (preemptible between slices)` -
and, unlike §66's `InlineWaitMs` (which produced an explicit,
confirming `budget 500 ms` log line), nothing in this session's log
mentions `SpinDraw` at all. The binary's own strings (§54) describe
`SpinDraw` toggling between "graphics draws" and "compute-dispatch"
spin variants - "predicated spin slices" reads like a third,
apparently newer implementation that may have superseded whatever
`SpinDraw` used to control, in this exact build. Genuinely inconclusive,
not a rule-out - would need confirmation the setting is even parsed at
all (not verifiable without danielblnc's source or a build with more
verbose ini-parsing diagnostics).

Capture-drop pattern: unchanged, byte-identical to every other test
tonight.

**Where this leaves things**: `single_queue` is the one remaining item
from the original list, and it's the only one that's a vkd3d-proton-
level flag rather than a danielblnc ini setting - a genuinely different
kind of lever than the three ini keys tried (§56, §66, §67), none of
which changed the outcome. Worth trying for real, cleanly (a single
launch-options change, verified via the `vkd3d_config_flags_init_once`
log line the way every other flag test tonight has been), before
concluding this project's own cheap, independent levers are fully
exhausted.

## 68. `single_queue` tested for real this time - confirmed active, and it genuinely changed something (for the worse)

`VKD3D_CONFIG=external_memory_fd,single_queue` set via the Steam UI
this time (not a direct `localconfig.vdf` edit), confirmed genuinely
active via the log: `VKD3D_CONFIG='external_memory_fd,single_queue,
force_bindless_texel_buffer'`. Unlike §67's `SpinDraw`, this is a real,
verified test, not an inconclusive one.

**A real behavioral difference, unlike every ini-level test tonight**:
`job 4` failed this session (`captured word 2, submitted 4`) - in
every baseline session all night (§50e onward), job 4 *usually
succeeded*. That's a genuine change, not noise - `single_queue`
forcing every D3D12 queue onto one real Vulkan queue plausibly
increases contention exactly where jobs 2-5 already sit right at the
edge of failing.

**Not a fix - if anything, slightly worse.** The core pattern (jobs 2,
3, 5 failing identically) persisted unchanged, and the log froze
completely for the last ~50s of the session while CPU stayed pegged
around 229% - the same signature as every ring-hang/stuck-spin session
tonight, not a healthier one.

**This closes out §54's original list** (`CpuWait` §56/58,
`InlineWaitMs` §66, `SpinDraw` §67, `single_queue` here) - four real,
independent, cheap levers tried, none of which fixed the capture-drop
pattern. Three came back clean negative results; `single_queue` is the
only one that measurably changed anything, and it made the pattern
marginally worse rather than better. Launch options reverted back to
the plain `VKD3D_CONFIG=external_memory_fd` baseline.

**Where this genuinely leaves the investigation**: every independently-
testable lever available from this project's own side - HIP-shim
correctness (§50), memory coherency (§51), idealized and realistic
submission ordering (§52, §61), stream forwarding (§64/65), and now
all four of danielblnc's own exposed config levers (§56/58, §66, §67,
§68) - has been tried. None fixed the capture-drop pattern. The
updated message to danielblnc (with the real 96% timeout-rate finding
and this session's evidence that his own config levers don't change
the underlying pattern either) is the honest next step from here,
not another synthetic probe.
