#!/bin/bash
# Rockstar Games Launcher (and GTA V Enhanced, which it starts) on the
# patched Proton (external_memory_fd + hip-unixlib), outside Steam, inside
# SteamLinuxRuntime_4. Same environment as the Steam launch options in
# SETUP-arch-rdna3.md, so the DLSS-NR runtime works in games it launches.
#
# Edit the paths below. Install the launcher into LAUNCHER_DIR (run once with
# ROCKSTAR_EXE=/path/to/Rockstar-Games-Launcher.exe), then install the game
# from inside the launcher to a folder under H: (GAME_DIR); the launcher
# refuses installs inside its own folder, and Wine's folder picker can't
# select a bare drive, so pick a subfolder such as H:\Games.

STEAM="$HOME/.local/share/Steam"
PROTON="$HOME/.steam/root/compatibilitytools.d/dlss5-fd"    # patched Proton
HOSTLIBS="$HOME/dlss5-hostlibs"   # libgflags/libglog/libfmt/libnuma copies
# Keep the prefix out of steamapps/compatdata: Steam removes prefixes there
# along with deleted non-Steam shortcuts.
PREFIX="$HOME/.local/share/rockstar-prefix"
LAUNCHER_DIR="$HOME/RockstarLauncher"   # G:
GAME_DIR="$HOME/RockstarGames"          # H:
EXE="${ROCKSTAR_EXE:-$LAUNCHER_DIR/Launcher.exe}"

mkdir -p "$PREFIX" "$LAUNCHER_DIR" "$GAME_DIR"
# Real-disk drive letters; Z: is the container's small root filesystem, so
# the launcher's free-space check fails there.
if [ -d "$PREFIX/pfx/dosdevices" ]; then
    ln -sfn "$LAUNCHER_DIR" "$PREFIX/pfx/dosdevices/g:"
    ln -sfn "$GAME_DIR" "$PREFIX/pfx/dosdevices/h:"
    # The runtime (version.dll) needs the HIP shim in system32; a new prefix
    # doesn't get it, and the game then dies with c06d007e.
    cp -u "$PROTON/files/lib/wine/x86_64-windows/amdhip64_7.dll" \
        "$PREFIX/pfx/drive_c/windows/system32/"
fi
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM"
export STEAM_COMPAT_DATA_PATH="$PREFIX"
export SteamAppId=0
export SteamGameId=rockstar

export PRESSURE_VESSEL_FILESYSTEMS_RO=/opt/rocm
export VKD3D_CONFIG=external_memory_fd
export WINEDLLOVERRIDES="version=n,b;wineopenxr=d"
# export PROTON_LOG=1   # debugging only; grows by gigabytes per evening
export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$HOSTLIBS/libgflags.so.2.2:$HOSTLIBS/libglog.so.2:$HOSTLIBS/libfmt.so.12:$HOSTLIBS/libnuma.so.1"

exec "$STEAM/steamapps/common/SteamLinuxRuntime_4/run" -- \
    "$PROTON/proton" waitforexitandrun "$EXE" "$@"
