# Backends

This project's whole architecture, until now, assumed exactly one
thing: danielblnc's proprietary runtime, DLL-hijacked via `version.dll`,
with `daniel/hip-unixlib` forwarding its real HIP calls to ROCm.

`guentra/dlss5-amd-hip-linux` (MIT licensed) is a genuinely different,
self-contained alternative - an independent, open-source reimplementation
of the neural network itself, injected an entirely different way (a
ReShade add-on renamed to masquerade as `d3d12.dll`, proxying to its own
modified vkd3d-proton build, with its own `dlss5_hip.dll` doing the HIP
work). See `docs/linux-support-spec.md` §61-§62 for the research behind
this and why the two mechanisms are **not composable** - they're
alternate, mutually exclusive backends, not two modules that combine.

This directory prepares this project's tooling to support both, cleanly
switchable, without ever having both active at once.

## Why they can't just coexist

Both backends want to own overlapping surface:

- **`daniel` backend** (this project's original, fully-working design):
  `version.dll` hijack (danielblnc's runtime) + `WINEDLLOVERRIDES`
  forcing this project's own `amdhip64_7.dll`/`.so` as the HIP shim.
  The D3D12↔HIP interop patch (`external_memory_fd`) lives at the
  **Proton/system level** (patches vkd3d-proton's own `d3d12.dll`,
  shared across every game using that Proton build).
- **`guentra` backend**: a `d3d12.dll` placed **game-locally** (next to
  the game .exe, which Windows/Wine DLL search order prefers over the
  Proton-wide version) that's actually a renamed `ReShade64.dll`,
  proxying to `guentra`'s own modified vkd3d-proton build
  (`lmxxf-d3d12.dll`) and his own `dlss5_hip.dll`. No `version.dll`
  hijack in this path at all - his own README explicitly says not to
  reuse the old `version.dll` mod.

There's no direct filename collision (`version.dll` vs `d3d12.dll`),
but running both simultaneously would mean **two separate hooking
systems both intercepting overlapping FFX/D3D12 dispatch points at
once** - danielblnc's runtime's own detours, and guentra's ReShade
add-on's hooks. That's a real, likely-unstable double-hook scenario,
not a theoretical concern - hence: pick one, cleanly.

## Layout

- `danielblnc/` - the backend this project has used all along. Nothing
  new here yet; documents the exact file set for symmetry with
  `guentra/` and so `switch_backend.sh` has something concrete to back
  up/restore when switching away from it.
- `guentra/` - the new backend. Documents the exact files, `WINEDLLOVERRIDES`,
  and `ReShade.ini` shape his installer produces, real-researched from
  his actual source (`linux/dlssnr/deploy.py`) - not guessed. **Does
  not vendor his binaries** - same standing rule as this project's own
  `daniel/vendor/` for danielblnc's runtime: users get his real release
  from his own GitHub releases page, this project's tooling only
  arranges it correctly once present locally.
- `switch_backend.sh` - backs up whichever backend's files are
  currently active in the game directory, restores (or reports missing)
  the other's, and prints the correct launch options for whichever
  backend is now active. Never runs anything - file operations and
  printed instructions only.

## Status

Scaffolding and documentation only, prepared per real research into
both backends' actual mechanisms. **Not yet live-tested against the
real game** - guentra's actual release binaries haven't been fetched
or deployed anywhere in this repo or the real game install, deliberately,
pending explicit go-ahead (this touches the live game install, and
means running a third party's compiled binaries there for the first
time - same standing caution this project has applied to every other
real-install-affecting step).
