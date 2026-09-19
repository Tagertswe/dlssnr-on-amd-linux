#!/bin/bash
# Switches the live game directory between the 'daniel' (danielblnc's
# proprietary runtime) and 'guentra' (open-source HIP reimplementation)
# backends, and prints the correct Steam launch options for whichever
# is now active.
#
# Rewritten once guentra's own installer (dlssnr/cli.py, vendored under
# windows-runtime-bridge/backends/guentra/vendor/) actually existed and
# turned out to have its own real, tested backup/restore system
# (.dlssnr-linux/backups/) - this script now defers to it for the
# guentra side instead of re-implementing file-list bookkeeping by
# hand. See docs/linux-support-spec.md for the real install/uninstall
# session this was built from.
#
# The two backends do NOT actually collide on disk: daniel's entry
# point is version.dll (picked up via WINEDLLOVERRIDES="version=n,b"),
# guentra's is a generated launch.sh wrapper that forces
# WINEDLLOVERRIDES="version=b;...;d3d12=n,b;d3d12core=n,b" (builtin
# version.dll, native d3d12/d3d12core from guentra's own files).
# Switching is therefore mostly a launch-options change - this script
# still adds/removes guentra's own files via its installer so a
# leftover d3d12.dll/d3d12core.dll in the game directory can never be
# picked up by Wine's normal next-to-exe search order while daniel's
# backend is meant to be active (a real risk if left in place; Wine
# does not strictly honor WINEDLLOVERRIDES over a same-named file
# sitting beside the game exe for every DLL name).
#
# Never runs anything against Steam's own config (localconfig.vdf) -
# see docs/linux-support-spec.md's confirmed finding that editing it
# directly while Steam is running is unreliable. This script only
# tells you what to paste into the Steam UI.
#
# Usage:
#   switch_backend.sh daniel  --exe /path/to/Game.exe
#   switch_backend.sh guentra --exe /path/to/Game.exe --package /path/to/nvngx_dlssnr.dll --runner /path/to/proton-dir
#
# Optional: --installer-dir /path/to/vendor/dlss5-amd-hip-linux-vX.Y.Z/dlss5-amd-hip-linux
#           (defaults to the newest windows-runtime-bridge/backends/guentra/vendor/dlss5-amd-hip-linux-v*/dlss5-amd-hip-linux)
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

# Persistent (not /tmp-scratchpad) location for the small patchelf tool this
# fix needs. Bootstrapped once via a venv + pip (patchelf's PyPI package
# ships a prebuilt binary wheel, no compiler/sudo needed) - real fix found
# and validated live 2026-09-19, see docs/linux-support-spec.md §74c.
PATCHELF_VENV="$HOME/.local/share/dlssnr-linux-tools/patchelf-venv"
NOSPACE_DIR="$HOME/.local/share/dlssnr-linux-tools/nospace"

ensure_patchelf() {
    if [ -x "$PATCHELF_VENV/bin/patchelf" ]; then
        return
    fi
    echo "  (one-time setup: installing patchelf into $PATCHELF_VENV)" >&2
    python3 -m venv "$PATCHELF_VENV"
    "$PATCHELF_VENV/bin/pip" install --quiet patchelf
}

# Real, live-validated fix for the global-LD_PRELOAD-vs-Steam-sandbox
# problem documented in docs/linux-support-spec.md §74c: guentra's own
# generated launch.sh sets LD_PRELOAD=libdlss5_hip.so globally, which Steam's
# own launch chain (a) splits on whitespace in the game's install path
# (breaks under any path containing a space) and (b) inherits into its own
# early sandbox-bootstrap helpers, which run before /run/host is fully
# populated, so libamdhip64.so.7's transitive dependency closure can't
# resolve there even with correct paths. Both fixed by: a space-free real
# copy of the .so, with its entire ROCm dependency closure bundled locally
# and $ORIGIN-relative RPATH baked in via patchelf - sidesteps both problems
# without touching Steam or Proton itself.
#
# NOT durable across a guentra reinstall on its own - install.sh regenerates
# launch.sh every time, wiping the so= line this edits. That's exactly why
# this function exists: call it after every `install.sh install`, not just
# once, so `switch_backend.sh guentra` always re-applies it automatically.
# This local-environment fix touches exactly one generated file
# (.dlssnr-linux/launch.sh's `so=` line) and never touches guentra's actual
# checked-in source. To keep that fully decoupled and reviewable - so a
# real upstream bug report can show "here's his tool's real, untouched
# output" vs. "here's the one-line local environment shim needed on this
# specific sandboxed-Proton setup" - a pristine copy of what install.sh
# actually generated is kept the first time this runs, never overwritten
# after that, and restored before every uninstall so his own installer's
# checksum verification always sees exactly what it originally wrote.
patch_guentra_sandbox_fix() {
    local gamedir="$1"
    local wrapper="$gamedir/.dlssnr-linux/launch.sh"
    local pristine="$gamedir/.dlssnr-linux/launch.sh.upstream-pristine"
    local real_so="$gamedir/.dlssnr-linux/lib/libdlss5_hip.so"
    [ -f "$wrapper" ] && [ -f "$real_so" ] || {
        echo "  WARNING: patch_guentra_sandbox_fix: $wrapper or $real_so missing, skipping fix" >&2
        return 0
    }
    if [ ! -f "$pristine" ]; then
        cp -f "$wrapper" "$pristine"
        echo "  Saved guentra's real, untouched generated launch.sh -> $pristine"
        echo "  (for upstream comparison/reporting - never edited by this script)"
    fi

    echo "=== Applying real launch-sandbox fix (docs/linux-support-spec.md §74c) ==="
    ensure_patchelf
    local patchelf="$PATCHELF_VENV/bin/patchelf"

    mkdir -p "$NOSPACE_DIR/rocm-bundle"
    cp -f "$real_so" "$NOSPACE_DIR/libdlss5_hip.so"

    # Find the real libamdhip64.so.7 the same way windows-runtime-bridge/hip-unixlib/native.c
    # does, then bundle its whole real transitive dependency closure (via a
    # clean-env ldd so no stale LD_LIBRARY_PATH skews the result), skipping
    # only the base loader/libc - those two must stay the sandbox's own.
    local hip_lib=""
    for c in /opt/rocm-*/lib/libamdhip64.so.7 /opt/rocm/lib/libamdhip64.so.7 \
             /usr/lib/x86_64-linux-gnu/libamdhip64.so.7; do
        [ -f "$c" ] && { hip_lib="$c"; break; }
    done
    [ -n "$hip_lib" ] || { echo "  FATAL: no libamdhip64.so.7 found on this host" >&2; return 1; }

    cp -f "$hip_lib" "$NOSPACE_DIR/rocm-bundle/"
    local dep
    while read -r dep; do
        case "$dep" in
            */ld-linux-x86-64.so.2|*/libc.so.6|linux-vdso.so.1) continue ;;
        esac
        [ -f "$dep" ] && cp -f -L "$dep" "$NOSPACE_DIR/rocm-bundle/" 2>/dev/null
    done < <(env -i ldd "$hip_lib" 2>/dev/null | grep -oE '/[^ ]+\.so[0-9.]*')

    for f in "$NOSPACE_DIR"/rocm-bundle/*.so*; do
        "$patchelf" --force-rpath --set-rpath '$ORIGIN' "$f"
    done
    "$patchelf" --force-rpath --set-rpath '$ORIGIN/rocm-bundle' "$NOSPACE_DIR/libdlss5_hip.so"

    # Verify before touching the wrapper - a bad bundle should fail loudly
    # here, not silently at the next game launch.
    if env -i ldd "$NOSPACE_DIR/libdlss5_hip.so" 2>/dev/null | grep -q "not found"; then
        echo "  FATAL: unresolved dependencies remain after bundling - not patching launch.sh" >&2
        env -i ldd "$NOSPACE_DIR/libdlss5_hip.so" 2>/dev/null | grep "not found" >&2
        return 1
    fi

    sed -i "s|^so='.*'$|so='$NOSPACE_DIR/libdlss5_hip.so'|" "$wrapper"
    echo "  Fixed: $wrapper now loads $NOSPACE_DIR/libdlss5_hip.so (self-contained, space-free)"

    # Keep his own installer's change-tracking (.dlssnr-linux/manifest.json)
    # self-consistent with this intentional local edit, so `install.sh
    # status`/`uninstall` keep working normally instead of refusing on a
    # "changed deployed file" - confirmed live this is otherwise a real,
    # hard failure (docs/linux-support-spec.md §74), not a cosmetic one.
    sync_manifest_hash "$gamedir" ".dlssnr-linux/launch.sh"
}

# Updates one file's recorded sha256 in guentra's own manifest.json to match
# its current real content - used only for files this script itself
# intentionally changed (the local-environment fix), never to paper over an
# unexplained drift. ReShade.ini legitimately gets rewritten by ReShade
# itself during normal play; that drift is real and reported to the user
# separately below, not silently resynced here.
sync_manifest_hash() {
    local gamedir="$1" relpath="$2"
    local manifest="$gamedir/.dlssnr-linux/manifest.json"
    [ -f "$manifest" ] || return 0
    python3 - "$manifest" "$gamedir/$relpath" "$relpath" <<'PYEOF'
import hashlib, json, sys
manifest_path, file_path, relpath = sys.argv[1], sys.argv[2], sys.argv[3]
with open(manifest_path) as f:
    data = json.load(f)
if relpath not in data.get('files', {}):
    sys.exit(0)
with open(file_path, 'rb') as f:
    digest = hashlib.sha256(f.read()).hexdigest()
data['files'][relpath]['sha256'] = digest
with open(manifest_path, 'w') as f:
    json.dump(data, f, indent=2)
PYEOF
}

usage() {
    cat >&2 <<'EOF'
Usage:
  switch_backend.sh daniel  --exe /path/to/Game.exe
  switch_backend.sh guentra --exe /path/to/Game.exe --package /path/to/nvngx_dlssnr.dll --runner /path/to/proton-dir [--installer-dir DIR]
EOF
    exit 2
}

[ $# -ge 1 ] || usage
TARGET="$1"; shift
case "$TARGET" in daniel|guentra) ;; *) usage ;; esac

EXE=""
PACKAGE=""
RUNNER=""
INSTALLER_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --exe) EXE="$2"; shift 2 ;;
        --package) PACKAGE="$2"; shift 2 ;;
        --runner) RUNNER="$2"; shift 2 ;;
        --installer-dir) INSTALLER_DIR="$2"; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$EXE" ] || usage
GAMEDIR="$(dirname -- "$EXE")"

if [ -z "$INSTALLER_DIR" ]; then
    INSTALLER_DIR=$(find "$SCRIPT_DIR/guentra/vendor" -maxdepth 2 -type d -name 'dlss5-amd-hip-linux' 2>/dev/null | sort -V | tail -1 || true)
fi

if [ "$TARGET" = guentra ]; then
    [ -n "$PACKAGE" ] || { echo "FATAL: --package /path/to/nvngx_dlssnr.dll required for guentra" >&2; exit 1; }
    [ -n "$RUNNER" ] || { echo "FATAL: --runner /path/to/proton-dir required for guentra" >&2; exit 1; }
    [ -n "$INSTALLER_DIR" ] && [ -x "$INSTALLER_DIR/install.sh" ] || {
        echo "FATAL: guentra installer not found (checked $SCRIPT_DIR/guentra/vendor/dlss5-amd-hip-linux-v*/dlss5-amd-hip-linux/install.sh)" >&2
        exit 1
    }
    echo "=== Installing guentra backend (this also cleanly backs up/restores any real file collisions) ==="
    "$INSTALLER_DIR/install.sh" install --exe "$EXE" --package "$PACKAGE" --runner "$RUNNER" \
        --allow-derived-layouts --confirm-runner --accept-risk --json
    echo
    patch_guentra_sandbox_fix "$GAMEDIR"
    echo
    echo "=== Launch options for the 'guentra' backend ==="
    echo "(printed above by install.sh as 'launch_options' - paste that exact string into Steam)"
    exit 0
fi

# TARGET = daniel
if [ -n "$INSTALLER_DIR" ] && [ -x "$INSTALLER_DIR/install.sh" ] && [ -d "$GAMEDIR/.dlssnr-linux" ]; then
    PRISTINE_WRAPPER="$GAMEDIR/.dlssnr-linux/launch.sh.upstream-pristine"
    if [ -f "$PRISTINE_WRAPPER" ]; then
        cp -f "$PRISTINE_WRAPPER" "$GAMEDIR/.dlssnr-linux/launch.sh"
        sync_manifest_hash "$GAMEDIR" ".dlssnr-linux/launch.sh"
        echo "=== Restored his real, untouched launch.sh before uninstalling (undid the local sandbox fix) ==="
    fi
    STATUS_JSON=$("$INSTALLER_DIR/install.sh" status --exe "$EXE" --json 2>/dev/null || true)
    if echo "$STATUS_JSON" | grep -q '"valid": false'; then
        echo "NOTE: guentra's installer reports other deployed files changed since install"
        echo "(commonly ReShade.ini, which ReShade itself legitimately rewrites during play -"
        echo "not something this script touched). Its own status --json above/below names them."
        echo "$STATUS_JSON"
    fi
    echo "=== guentra backend detected - uninstalling via its own managed backup/restore ==="
    "$INSTALLER_DIR/install.sh" uninstall --exe "$EXE" --yes --json
else
    echo "=== No active guentra installation detected under $GAMEDIR/.dlssnr-linux - nothing to remove ==="
fi

echo
if [ ! -e "$GAMEDIR/version.dll" ]; then
    echo "WARNING: version.dll (danielblnc's runtime) is not present in $GAMEDIR."
    echo "See windows-runtime-bridge/backends/danielblnc/README.md for where to obtain it (never committed to this repo)."
fi

echo "=== Launch options for the 'daniel' backend ==="
cat <<'EOF'
PRESSURE_VESSEL_FILESYSTEMS_RO=/opt:/etc/alternatives VKD3D_CONFIG=external_memory_fd WINEDLLOVERRIDES="version=n,b;amdhip64_7=b" PROTON_LOG=1 %command%

Compatibility tool: this project's patched Proton (fdtest-11.0-2c).
EOF
echo
echo "Reminder: paste this into the Steam UI (Properties -> Launch Options)."
echo "Do not edit localconfig.vdf directly while Steam is running - confirmed"
echo "unreliable earlier this session (docs/linux-support-spec.md \xc2\xa759-\xc2\xa760 era)."
