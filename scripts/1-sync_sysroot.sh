#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# sync_sysroot.sh — build or refresh the aarch64 cross-compile sysroot from the Pi.
#
# WHAT THIS DOES (in order):
#   1. Creates the destination directory (default: cmake/rpi-sysroot).
#   2. rsyncs the Raspberry Pi's /usr into <dest>/usr (headers + libraries).
#   3. Re-creates the two merged-/usr symlinks the sysroot needs.
#   4. Runs "anchor" checks so a broken sysroot fails HERE, not 20 minutes into
#      a CMake configure.
#
# WHY: the cross-compiler must link against the TARGET's libraries, not the
# host's. The sysroot is a copy of the Pi's /usr for exactly that purpose. It is
# large (~1.4 GB) and gitignored — regenerate it, never commit it.
# Full explanation: docs/CROSS_COMPILE.md
#
# EXAMPLES
#   scripts/sync_sysroot.sh --host pi@raspberrypi.local
#   scripts/sync_sysroot.sh --host pi@192.168.1.42 --dry-run
#   scripts/sync_sysroot.sh --host pi@raspberrypi.local --dest /tmp/rpi-sysroot
#   scripts/sync_sysroot.sh --check          # verify an existing sysroot, no rsync
#
# C++/shell note for Java readers: this is bash, not C++. `set -euo pipefail`
# means "abort on the first failing command (-e), on unset variables (-u), and
# on any failure inside a pipeline (pipefail)" — fail-fast, like an uncaught
# exception. Bash needs to be told to do this; there is no implicit equivalent.
# ---------------------------------------------------------------------------

set -euo pipefail

# --- Defaults ----------------------------------------------------------------
HOST=""        # ssh target, e.g. pi@raspberrypi.local (required unless --check)
DEST=""        # destination; empty => <repo>/cmake/rpi-sysroot
DRY_RUN=0      # 1 => show what rsync WOULD do, change nothing
CHECK_ONLY=0   # 1 => skip the rsync, only re-link + verify an existing sysroot

# Directories that are useless for compiling and waste space. Patterns are
# relative to the rsync source root (/usr), so they carry no leading slash.
EXCLUDES=(
    "share/doc"
    "share/man"
    "share/locale"
    "share/info"
)

usage() {
    cat <<'EOF'
Usage: scripts/sync_sysroot.sh [--host USER@HOST] [--dest DIR] [--dry-run] [--check]

  --host USER@HOST  SSH target of the Raspberry Pi (required unless --check).
  --dest DIR        Where to build the sysroot (default: cmake/rpi-sysroot).
  --dry-run         Run rsync with --dry-run; do not copy anything.
  --check           Do not rsync; just re-create symlinks and verify an
                    existing sysroot. Use this to test after a Pi update.
  -h, --help        Show this help.
EOF
}

# --- Parse arguments ---------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)   HOST="${2:-}"; shift 2 ;;
        --dest)   DEST="${2:-}"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        --check)  CHECK_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# Resolve the repository root from this script's location, so the script works
# no matter which directory you call it from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default destination lives inside the (gitignored) cmake/ directory.
if [[ -z "$DEST" ]]; then
    DEST="$REPO_ROOT/cmake/rpi-sysroot"
elif [[ "$DEST" != /* ]]; then
    DEST="$REPO_ROOT/$DEST"   # make a relative --dest absolute against the repo
fi

if [[ "$CHECK_ONLY" -eq 0 && -z "$HOST" ]]; then
    echo "error: --host is required (e.g. --host pi@raspberrypi.local)" >&2
    usage >&2
    exit 2
fi

# --- Helpers -----------------------------------------------------------------

# require <path> — print ok/MISS for one expected sysroot file; set FAIL on miss.
FAIL=0
require() {
    local path="$1"
    if [[ -e "$path" ]]; then
        printf '  ok    %s\n' "${path#"$DEST"/}"
    else
        printf '  MISS  %s\n' "${path#"$DEST"/}"
        FAIL=1
    fi
}

# --- 1) Extract the sysroot --------------------------------------------------
if [[ "$CHECK_ONLY" -eq 0 ]]; then
    echo "==> Syncing ${HOST}:/usr/  ->  ${DEST}/usr/"
    mkdir -p "$DEST/usr"

    # rsync flag notes (see the rsync(1) man page):
    #   -r  recurse           -l  copy symlinks as symlinks   -p  preserve perms
    #   -t  preserve times    -D  preserve device/special files
    #   --no-owner --no-group : do NOT try to chown the files. /usr on the Pi is
    #       owned by root; preserving ownership would require running locally as
    #       root. We only need to read these files, so we skip ownership.
    #   --rsync-path="sudo rsync" : read the remote files through sudo, in case
    #       some are not world-readable.
    #   --exclude <dir> : skip docs/man/locales to save space.
    #   NO --delete : never remove files from the sysroot (that would delete the
    #       symlinks we add in step 3).
    RSYNC_ARGS=(
        -rlptD
        --no-owner
        --no-group
        --info=progress2
        --rsync-path="sudo rsync"
    )
    for ex in "${EXCLUDES[@]}"; do
        RSYNC_ARGS+=(--exclude "$ex")
    done
    if [[ "$DRY_RUN" -eq 1 ]]; then
        RSYNC_ARGS+=(--dry-run)
        echo "    (dry run: nothing will be changed)"
    fi

    rsync "${RSYNC_ARGS[@]}" "$HOST:/usr/" "$DEST/usr/"
else
    echo "==> --check: skipping rsync, verifying existing sysroot at ${DEST}"
fi

# --- 2) Re-create the merged-/usr symlinks -----------------------------------
# Debian makes /lib a symlink into /usr. If the sysroot only holds usr/, then
# absolute paths like /lib/aarch64-linux-gnu/libc.so.6 (used by glibc's linker
# scripts) cannot resolve. These two links make the sysroot self-consistent.
# `ln -sfn` is idempotent: safe to run on every refresh.
if [[ -d "$DEST/usr/lib" ]]; then
    echo "==> Creating merged-/usr symlinks"
    ln -sfn usr/lib "$DEST/lib"
    ln -sfn aarch64-linux-gnu/ld-linux-aarch64.so.1 "$DEST/usr/lib/ld-linux-aarch64.so.1"
    ln -sfn libgomp.so.1 "$DEST/usr/lib/aarch64-linux-gnu/libgomp.so"
else
    echo "warning: ${DEST}/usr/lib is missing; skipping symlinks" >&2
fi

# --- 3) Verify ---------------------------------------------------------------
echo "==> Verifying sysroot anchors"

# The root-level lib -> usr/lib link is a symlink; compare its target text.
if [[ "$(readlink "$DEST/lib" 2>/dev/null)" == "usr/lib" ]]; then
    echo "  ok    lib -> usr/lib"
else
    echo "  MISS  lib -> usr/lib (symlink)"
    FAIL=1
fi

LIBDIR="$DEST/usr/lib/aarch64-linux-gnu"
require "$LIBDIR/ld-linux-aarch64.so.1"          # program loader (ELF interpreter)
require "$LIBDIR/libc.so.6"                      # glibc
require "$LIBDIR/libstdc++.so.6"                 # C++ runtime
require "$LIBDIR/crt1.o"                         # startup object (links every exe)
require "$LIBDIR/pkgconfig/libcamera.pc"         # camera capture
require "$LIBDIR/pkgconfig/opencv4.pc"           # OpenCV (face, dnn, objdetect)
require "$LIBDIR/cmake/opencv4/OpenCVConfig.cmake"  # find_package(OpenCV)
require "$DEST/usr/include/libcamera/libcamera/camera.h"

if [[ "$FAIL" -ne 0 ]]; then
    echo "" >&2
    echo "error: sysroot verification FAILED (see MISS lines above)." >&2
    echo "       Troubleshooting: docs/CROSS_COMPILE.md -> Part C." >&2
    exit 1
fi

printf '  ok    %s files, %s\n' \
    "$(find "$DEST" -type f | wc -l)" \
    "$(du -sh "$DEST" | cut -f1)"

echo ""
echo "Sysroot ready at: $DEST"
echo "Next:"
echo "  cmake --preset aarch64"
echo "  cmake --build --preset aarch64"
