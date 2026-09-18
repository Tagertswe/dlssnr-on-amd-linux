# `guentra` backend (alternate, open-source reimplementation)

Real-researched from `guentra/dlss5-amd-hip-linux`'s actual source
(`linux/dlssnr/deploy.py`), not guessed. Not yet live-tested against
the real game - see this directory's parent `README.md`'s Status
section.

**Confirms the mutual-exclusivity design decision directly**: in HIP
mode, his own launcher wrapper explicitly sets `version=b`
(builtin-only) - a deliberate measure to make Wine ignore any hijacked
`version.dll` (like danielblnc's), precisely to avoid the double-hook
conflict this project's `../README.md` describes. This wasn't inferred
- it's literally in his `wrapper_bytes()` function.

## Files, in the game's directory (game-local, not Proton-level)

| File | Real source in his repo | Role |
| --- | --- | --- |
| `d3d12.dll` | `ReShade64.dll`, renamed | Loads first, proxies to the next file |
| `lmxxf-d3d12.dll` | his own modified vkd3d-proton build (`build-vkd3d/libs/d3d12/d3d12.dll`, renamed) | The real, patched D3D12 implementation ReShade proxies to |
| `d3d12core.dll` | his own vkd3d-proton build's core lib | vkd3d-proton's actual implementation |
| `dlss5_hip.dll` | `hip/dlss5_hip.dll` | The Windows-side HIP bridge/trampoline for his own kernels |
| his ReShade add-on binary | `package.ADDON` (exact name not yet confirmed - deploy.py references it via `package.py`, not read yet) | The actual ReShade add-on doing the D3D12 hook work |
| `ReShade.ini` | generated | `[PROXY] ProxyLibrary=.\lmxxf-d3d12.dll` - tells ReShade where to proxy real D3D12 calls |
| `DLSS5-AMD/native-game-tiled-assets/` | generated from the user's own `nvngx_dlssnr.dll` via his weight-conversion tooling | Converted network weights, resident in VRAM at runtime - never NVIDIA's actual DLL/weights redistributed |

**Not vendored here** - same standing rule as `windows-runtime-bridge/vendor/` for
danielblnc's runtime. A real install needs the user's own copy of
guentra's release archive
(`https://github.com/guentra/dlss5-amd-hip-linux/releases`), extracted
locally, gitignored.

## Launch (his own generated wrapper, not a plain `%command%` line)

His installer generates a `launch.sh` wrapper script (not just an
env-var launch-options string like this project uses) that:

1. Strips any pre-existing `WINEDLLOVERRIDES` entries for
   `d3d12`/`d3d12core`/`dxgi`/`dlss5_hip` (and, in HIP mode, `version`
   too) before setting its own - real, careful handling to avoid
   duplicate/conflicting override entries if something else (like
   this project's own `danielblnc` backend) already set them.
2. Sets, for the HIP-inference mode specifically:
   `WINEDLLOVERRIDES="version=b;dlss5_hip=n;d3d12=n,b;d3d12core=n,b"`
   (plus `dxgi=n,b` too if targeting a Magpie-hooked title, not
   relevant to Cyberpunk 2077).
3. Adds `STEAM_COMPAT_MOUNTS` pointing at the game's own directory.

Compatibility tool: **not yet determined whether this needs this
project's own patched Proton (`fdtest-11.0-2c`) or a stock Proton
build** - his own modified vkd3d-proton is a *different* patch than
this project's `external_memory_fd` one (his targets a "split vkd3d
submission boundary" for CPU-mediated readback/upload, not zero-copy
D3D12↔HIP interop - his own README says this backend uses "CPU
readback/upload," not shared-memory interop, so it very likely doesn't
need `external_memory_fd` at all). Real open question before any live
test: does his backend need a stock, unpatched Proton install instead
of this project's patched one? Worth confirming from his own
`docs/BUILD.md`/`BUILD-POC.md` before attempting a real test.

## Switching back to the `danielblnc` backend afterward

`switch_backend.sh` handles this - backs up whichever of this
directory's files are present, restores `../danielblnc/`'s file set.
Manual equivalent: remove `d3d12.dll`, `lmxxf-d3d12.dll`,
`d3d12core.dll`, `dlss5_hip.dll`, `ReShade.ini`, restore
`version.dll`/`amdhip64_7.dll`/`dlssnr_on_amd.ini`, and go back to the
`danielblnc` backend's own launch options and compatibility tool.
