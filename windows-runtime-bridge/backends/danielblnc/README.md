# `danielblnc` backend (this project's original design)

The backend this whole project has used since its start. Nothing new
here - this file exists so `switch_backend.sh` has an exact, documented
file set to back up/restore when switching away from it, matching
`guentra/README.md`'s equivalent documentation for the other backend.

## Files, in the game's `bin/x64/` directory

| File | Source | Role |
| --- | --- | --- |
| `version.dll` | danielblnc's real runtime (user-supplied, see `windows-runtime-bridge/vendor/`) | DLL-hijack entry point |
| `amdhip64_7.dll` | this project, `windows-runtime-bridge/hip-unixlib` | PE-side HIP shim, forwards to `native.c` |
| `dlssnr_on_amd.ini` | danielblnc's runtime's own config | `Enabled=1`, `CpuWait=1` (this project's real, `dmesg`-verified ring-hang mitigation - see `docs/linux-support-spec.md` §56) |

## Files, at the Proton/system level (shared across every game using that build)

| File | Source |
| --- | --- |
| `files/lib/wine/x86_64-windows/d3d12.dll`, `d3d12core.dll` | this project's patched vkd3d-proton (`external_memory_fd`) |
| `files/lib/wine/x86_64-unix/amdhip64_7.so` | this project, `windows-runtime-bridge/hip-unixlib` native side |

## Launch options

```
PRESSURE_VESSEL_FILESYSTEMS_RO=/opt:/etc/alternatives VKD3D_CONFIG=external_memory_fd WINEDLLOVERRIDES="version=n,b;amdhip64_7=b" PROTON_LOG=1 %command%
```

Compatibility tool: this project's patched Proton build
(`fdtest-11.0-2c` in this system's real deployment).
