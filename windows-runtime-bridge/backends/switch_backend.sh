#!/bin/bash
# Backs up whichever backend's files are currently active in the game
# directory, restores (or reports missing) the other's, and prints the
# correct launch options for whichever backend is now active.
#
# Never runs anything, never downloads anything, never touches the
# Steam launch-options config itself (localconfig.vdf - see
# docs/linux-support-spec.md's real, empirically-confirmed finding that
# editing that file directly while Steam is running is unreliable; this
# script only tells you what to paste into the Steam UI).
#
# Usage: switch_backend.sh <daniel|guentra> <path to game's bin/x64 dir>
#
# File sets - see windows-runtime-bridge/backends/danielblnc/README.md and
# windows-runtime-bridge/backends/guentra/README.md for the real, researched source of
# each list.
set -euo pipefail

DANIEL_FILES=(version.dll amdhip64_7.dll dlssnr_on_amd.ini)
GUENTRA_FILES=(d3d12.dll lmxxf-d3d12.dll d3d12core.dll dlss5_hip.dll ReShade.ini)

usage() {
    echo "Usage: $0 <daniel|guentra> <path to game's bin/x64 dir>" >&2
    exit 2
}

[ $# -eq 2 ] || usage
TARGET="$1"
GAMEDIR="$2"
[ -d "$GAMEDIR" ] || { echo "FATAL: not a directory: $GAMEDIR" >&2; exit 1; }
case "$TARGET" in
    daniel|guentra) ;;
    *) usage ;;
esac

STORE="$GAMEDIR/.backend-switch"
mkdir -p "$STORE/daniel" "$STORE/guentra"

backup_present() {
    local backend="$1"; shift
    local -n files_ref="$1"
    local f
    for f in "${files_ref[@]}"; do
        if [ -e "$GAMEDIR/$f" ]; then
            echo "  backing up $f -> .backend-switch/$backend/"
            mv "$GAMEDIR/$f" "$STORE/$backend/$f"
        fi
    done
}

restore_backend() {
    local backend="$1"; shift
    local -n files_ref="$1"
    local f missing=0
    for f in "${files_ref[@]}"; do
        if [ -e "$STORE/$backend/$f" ]; then
            echo "  restoring $f from .backend-switch/$backend/"
            mv "$STORE/$backend/$f" "$GAMEDIR/$f"
        elif [ ! -e "$GAMEDIR/$f" ]; then
            echo "  MISSING: $f (never backed up - not present in $backend/ or $GAMEDIR)"
            missing=1
        fi
    done
    return $missing
}

echo "=== Backing up currently-present files from both backends ==="
backup_present daniel DANIEL_FILES
backup_present guentra GUENTRA_FILES

echo
echo "=== Restoring the '$TARGET' backend's files ==="
MISSING=0
if [ "$TARGET" = daniel ]; then
    restore_backend daniel DANIEL_FILES || MISSING=1
else
    restore_backend guentra GUENTRA_FILES || MISSING=1
fi

echo
if [ "$MISSING" -ne 0 ]; then
    echo "WARNING: some files for the '$TARGET' backend were never present to restore."
    echo "See windows-runtime-bridge/backends/$TARGET/README.md for the real, complete file list and where each comes from."
    echo
fi

echo "=== Launch options for the '$TARGET' backend ==="
if [ "$TARGET" = daniel ]; then
    cat <<'EOF'
PRESSURE_VESSEL_FILESYSTEMS_RO=/opt:/etc/alternatives VKD3D_CONFIG=external_memory_fd WINEDLLOVERRIDES="version=n,b;amdhip64_7=b" PROTON_LOG=1 %command%

Compatibility tool: this project's patched Proton (fdtest-11.0-2c).
EOF
else
    cat <<'EOF'
WINEDLLOVERRIDES="version=b;dlss5_hip=n;d3d12=n,b;d3d12core=n,b" %command%

Compatibility tool: NOT YET CONFIRMED - see windows-runtime-bridge/backends/guentra/README.md's
open question about whether this backend needs a stock Proton build instead of
this project's patched one. Do not assume fdtest-11.0-2c is correct here without
checking guentra's own build docs first.
EOF
fi

echo
echo "Reminder: paste this into the Steam UI (Properties -> Launch Options)."
echo "Do not edit localconfig.vdf directly while Steam is running - confirmed"
echo "unreliable earlier this session (docs/linux-support-spec.md \xc2\xa759-\xc2\xa760 era)."
