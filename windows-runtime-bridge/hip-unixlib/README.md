# dlssnr-proton-bridge (working title)

Get danielblnc/DLSS-NR-on-AMD's standalone runtime loading and hooking
correctly under Linux/Proton, on AMD GPUs, via a real HIP backend.

**Status: transport layer complete and verified against real hardware.
Real neural-network inference does run through this path, but is not
yet usable for normal play** - see Status below and the root
`README.md`'s Status section for the current, honest picture.

## What this is

A clean-room, from-scratch `amdhip64_7.dll` replacement (a Wine unixlib
module - a Windows-side shim paired with native Linux code) that lets
danielblnc's proprietary DLSS-NR-on-AMD runtime run under Proton by
answering its HIP calls for real, using your system's actual ROCm
install. Contains no proprietary code, weights, or logic from NVIDIA or
danielblnc.

## What this is not

- Not a redistribution of danielblnc's runtime or weights - you obtain
  those yourself (see below).
- Not (yet) a way to get real DLSS5 Neural Rendering output. Kernel
  launches succeed against real hardware, but real inference is gated on
  a licensing conversation with danielblnc (his compiled kernels are
  proprietary) - see docs/linux-support-spec.md.

## Requirements

- An AMD GPU with ROCm support (tested: RX 9070 XT / gfx1201)
- ROCm installed (`rocminfo`, `libamdhip64`)
- Proton (tested: Valve's official Proton 11.0)
- `wine64-tools`, `clang`, `lld` (build-time only)

## Install

1. Build (`make` in this directory)
2. Install the module into your Proton version's own directory:
   ```
   cp x86_64-windows/amdhip64_7.dll "<Proton>/files/lib/wine/x86_64-windows/"
   cp x86_64-unix/amdhip64_7.so "<Proton>/files/lib/wine/x86_64-unix/"
   winebuild --builtin "<Proton>/files/lib/wine/x86_64-windows/amdhip64_7.dll"
   ```
3. Obtain danielblnc's runtime yourself (see zmodelerlover/dlss5-neural-amd's
   `tools/extract_runtime.py` approach - never redistributed here) and
   place it as `version.dll` next to your game's .exe, along with the
   weights file.
4. Steam launch options: `WINEDLLOVERRIDES="version=n,b" %command%`

## Testing

`make test` runs the unit tests for the pure registry/parsing logic
(no Wine, HIP, or GPU required).

## Status

- [x] Danielblnc's runtime loads and hooks correctly under Proton
- [x] Real HIP device/memory calls (malloc, memcpy, device properties)
- [x] Real fat-binary loading and kernel resolution
- [x] Real kernel launches execute on hardware (158/158 in testing)
- [ ] Real inference output - blocked on licensing, not engineering
- [ ] Skyrim SE / other DX11 games (Cyberpunk 2077 validated so far)

## Licensing

This project's own original code is MIT-licensed, **except** the files
noted in their own headers as derived from brainrom/winedll-example
(LGPL-2.1+) and the vendored `wine/unixlib.h` (Wine's own LGPL-2.1+
header) - those remain under their original license, per LGPL's terms
on distributing modified versions of a covered work. See
docs/linux-support-spec.md for the full analysis.

Danielblnc's runtime and weights remain his, under his own license -
never bundled or redistributed here.

## Credits

Builds on public techniques from zmodelerlover/dlss5-neural-amd (MIT) and
brainrom/winedll-example (LGPL-2.1+) - see individual file headers.
