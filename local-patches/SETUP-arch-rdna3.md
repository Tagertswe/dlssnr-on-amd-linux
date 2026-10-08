# Arch Linux + RX 7700 XT (RDNA3, gfx1101) setup, 2026-10-07

Working end to end: danielblnc DLSS-NR-on-AMD runtime v0.6.0 running real
network jobs under Proton, no GPU resets, in:

- Halo: Campaign Evolved (Steam)
- GTA V Enhanced (Rockstar Games Launcher, Story Mode, outside Steam; see
  "Non-Steam games" below). The player reported a clearly better picture
  with the network on.

## Stack

- ROCm 7.2.4 from Arch (`rocm-hip-runtime`), installed under `/opt/rocm`.
- Proton `proton-11.0-2c`, built with Valve's container pipeline as `dlss5-fd`:
  - wine: `proton-patches/wine-winevulkan-external-memory-fd.patch` (applies as-is)
  - vkd3d-proton (pinned 212991f): `local-patches/vkd3d-proton-external-memory-fd-11.0-2c.patch`
    (port of the upstream patch to the old config-flag scheme, bit 46)
- hip-unixlib (this branch) built against the Proton build's own Wine tree:

  ```
  W=<proton build dir>/dst-wine-x86_64
  PATH=$W/bin:$PATH make clean test all WINELIB=$W/lib/wine WINEINC=$W/include/wine
  ```

  then copied into `dlss5-fd/files/lib/wine/x86_64-{windows,unix}/` and marked
  with `winebuild --builtin`.

## Branch changes (runtime v0.6.0 compatibility)

- 7 HIP exports v0.6.0 delay-imports that the shim lacked:
  hipGetDevice, hipStreamCreateWithPriority, hipStreamDestroy,
  hipDeviceGetStreamPriorityRange, hipHostMalloc, hipHostGetDevicePointer,
  hipOccupancyMaxActiveBlocksPerMultiprocessor.
- hipLaunchKernel forwards the full kernel-params array (was args[0] only).
- Logging is startup-only (first 5000 lines) unless `HIP_SHIM_VERBOSE` is set;
  per-call logging cost ~160 flushes per network job and grew /tmp by ~500 MB
  per session.

## Game requirements

The runtime hooks FSR only through FidelityFX DLLs (`amd_fidelityfx_*_dx12.dll`,
`ffx_fsr3*.dll`) in a D3D12 game. FSR compiled into the executable is not
hooked (Palworld, Dead Island 2 ruled out on that basis).

## Steam launch options

```
PRESSURE_VESSEL_FILESYSTEMS_RO=/opt/rocm VKD3D_CONFIG=external_memory_fd WINEDLLOVERRIDES="version=n,b;wineopenxr=d" LD_PRELOAD="$LD_PRELOAD:/path/to/hostlibs/libgflags.so.2.2:/path/to/hostlibs/libglog.so.2:/path/to/hostlibs/libfmt.so.12:/path/to/hostlibs/libnuma.so.1" %command%
```

- `LD_PRELOAD`: Arch's libamdhip64 needs libfmt, libglog (+libgflags) and libnuma,
  which the steamrt4 container lacks; dependents other than libamdhip64 have no
  RUNPATH, so copying them into /opt/rocm/lib does not help. Copy the host's
  `libgflags.so.2.2`, `libglog.so.2`, `libfmt.so.12` and `libnuma.so.1` from
  `/usr/lib` into a `hostlibs` folder and preload them from there.
- Add `PROTON_LOG=1` only while debugging a launch: with the Rockstar
  launcher running, the log grew to 3.7 GB in one evening.
- `wineopenxr=d`: with WiVRn running, vkd3d-proton's OpenXR probe deadlocked
  the game at startup.
- The compatibility tool must be force-selected (dlss5-fd) after a full Steam
  restart; otherwise Steam silently uses Proton Experimental and the runtime
  fails with c06d007e (amdhip64_7.dll not found).

## Game folder

`version.dll` (runtime, extracted unmodified from the v0.6.0 installer),
`nvngx_dlssnr.dll` 310.8.0.0 (sha256 e16bcf15...1fc8e, Authenticode valid),
`dlssnr_on_amd.ini` with `CpuWait=1`.

## Measured (Halo, 1080p output)

- Render 1130x636: ~20 ms per network job, 1 job per frame.
- Render ~1080p: ~52 ms per job, 1 job every other frame, ~28 fps.
- Stream priority already maxed (priority -1 via hipStreamCreateWithPriority).
- Untested lever: `Inline=0` (async; the runtime expects HIP 7.2.60201, Arch
  ships 7.2.53211, but the inline flag check passes here).

## Measured (GTA V Enhanced, 1080p output, FSR 3 Quality = 1280x720 render)

- ~52 ms per job with `Quality=reference`, ~47 ms with `Quality=fast`; inline
  mode, 1 job per frame, ~19 fps. Ray tracing on made this worse; turning the
  game's RT off is the biggest lever.
- With VSync on a 60 Hz display without VRR, frames land on 50 or 67 ms and
  alternate, which reads as small stutters.
- Occasional 0.3-0.75 s stalls (network job 5-14x its usual time on the GPU,
  not waiting on the game); no amdgpu resets in `journalctl -k`. Cause not
  pinned down.

## Non-Steam games (Rockstar Games Launcher / GTA V Enhanced)

`rockstar-launcher.example.sh` runs the launcher on the patched Proton inside
SteamLinuxRuntime_4 with the same environment as the Steam launch options.

- The game must be D3D12 with FidelityFX DLLs: GTA V Enhanced ships
  `amd_fidelityfx_dx12.dll` (FFX API, FSR 3.1.4), which the runtime hooks.
  Select AMD FSR 3 as the upscaler in the game.
- Story Mode only, BattlEye off in the launcher. Never use this online.
- Keep the prefix outside `steamapps/compatdata`: Steam deleted it once along
  with a removed non-Steam shortcut.
- A fresh prefix has no `amdhip64_7.dll` in `system32`, and the game dies
  before opening a window with `c06d007e` (`VERSION.dll failed to
  initialize`). The script copies the shim in.
- The launcher rejects an install folder inside its own folder, and Wine's
  folder picker cannot select a bare drive: map a second drive letter to the
  game directory and pick a subfolder.

## Known runtime issues (closed-source runtime, not fixable here)

- Startup race while hooking, on roughly half of GTA launches:
  - `detour of ID3D12CommandQueue::ExecuteCommandLists failed (5)` in
    `dlssnr_on_amd.log`: the game runs, but captures never land (`submitted 3`
    and every later frame skipped), so the picture is plain FSR.
  - Or a deadlock: the log stops after `hooked IDXGISwapChain::Present`, the
    game sits at 0% CPU, and Wine logs a timed-out wait on the main process
    heap lock. Kill the game and launch again.
- The in-game overlay (End) writes the ini when closed; it once saved
  `Enabled=0`, which turns the network off at the next start.
