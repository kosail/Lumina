#!/usr/bin/env bash
# Cross-build libpiper (OHF-Voice/piper1-gpl) for the Raspberry Pi Zero 2 W. Run on your pc.
#
# libpiper is the GPL-3.0 C/C++ Piper engine (see INV-060). It links espeak-ng (built for us
# by its CMake) and ONNX Runtime. We point it at the official aarch64 ONNX Runtime fetched by
# scripts/4-fetch_onnxruntime.sh instead of letting libpiper download its own.
#
# What this does:
#   1. clone piper1-gpl at a PINNED commit into third_party/libpiper-src
#   2. apply the vendored toolchain-forwarding patch (third_party/patches/)
#   3. cross-configure ONLY libpiper/ against our toolchain + sysroot + prebuilt ORT
#   4. build, then compile espeak-ng-data with a native host build (espeak-ng skips
#      data generation when cross-compiling), and install into third_party/libpiper
#
# Usage:
#   scripts/5-build_libpiper.sh            # clone/patch/build/install
#   scripts/5-build_libpiper.sh --force    # ignore an existing install and rebuild
#   scripts/5-build_libpiper.sh --clean    # wipe src+build+install first (re-clone)
#
# Requires on the host: git, cmake >= 3.26, ninja, a network connection, the aarch64 cross
# toolchain, and third_party/onnxruntime from scripts/4-fetch_onnxruntime.sh.
set -euo pipefail

PIPER_REPO="https://github.com/OHF-Voice/piper1-gpl.git"
PIPER_COMMIT="251fdb9d33f69a5607e0b3432d999c49c4272b45"   # 2026-09-15 (pinned)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO_ROOT/third_party/libpiper-src"
BUILD="$REPO_ROOT/third_party/libpiper-build"
DEST="$REPO_ROOT/third_party/libpiper"
ORT="$REPO_ROOT/third_party/onnxruntime"
TOOLCHAIN="$REPO_ROOT/cmake/toolchain-aarch64.cmake"
PATCH="$REPO_ROOT/third_party/patches/libpiper-toolchain.patch"
PATCH_THREADS="$REPO_ROOT/third_party/patches/libpiper-threads.patch"
ESPEAK_SRC="$BUILD/espeak_ng/src/espeak_ng_external"  # cloned here by espeak-ng's ExternalProject
ESPEAK_INSTALL="$BUILD/espeak_ng-install"             # espeak-ng install prefix (inside our build tree)
HOST_BUILD="$REPO_ROOT/third_party/espeak-ng-host-build"  # native build used only to compile data

FORCE=0
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    --clean) CLEAN=1 ;;
    -h|--help)
      echo "usage: $0 [--force] [--clean]"
      exit 0 ;;
    *) echo "error: unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# 0) Preconditions.
for tool in git cmake ninja; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' is required" >&2; exit 1; }
done
[ -f "$TOOLCHAIN" ] || { echo "error: missing $TOOLCHAIN" >&2; exit 1; }
[ -f "$PATCH" ]     || { echo "error: missing $PATCH" >&2; exit 1; }
[ -f "$PATCH_THREADS" ] || { echo "error: missing $PATCH_THREADS" >&2; exit 1; }
[ -f "$ORT/lib/libonnxruntime.so" ] || {
  echo "error: $ORT/lib/libonnxruntime.so not found; run scripts/4-fetch_onnxruntime.sh first" >&2
  exit 1
}

if [ "$CLEAN" -eq 1 ]; then
  echo "==> --clean: removing src/build/install"
  rm -rf "$SRC" "$BUILD" "$DEST"
  FORCE=1
fi

# A complete install needs BOTH the library and the compiled espeak-ng data. The
# data marker prevents a half-finished install from being mistaken for success
# (the cross-build data limitation handled in step 4b). Use --force to rebuild.
if [ "$FORCE" -eq 0 ] \
   && [ -f "$DEST/lib/libpiper.so" ] \
   && [ -f "$DEST/share/espeak-ng-data/phondata" ]; then
  echo "==> $DEST already present; nothing to do (use --force to rebuild)"
  exit 0
fi

# 1) Clone the pinned commit (idempotent).
if [ ! -d "$SRC/.git" ]; then
  echo "==> cloning piper1-gpl"
  git clone "$PIPER_REPO" "$SRC"
fi
echo "==> checking out $PIPER_COMMIT"
git -C "$SRC" fetch --quiet origin "$PIPER_COMMIT" 2>/dev/null || true
git -C "$SRC" checkout --quiet "$PIPER_COMMIT"

# 2) Apply the vendored patches (idempotent: each is guarded by a marker string).
#    git apply is tried first; GNU patch (with fuzz) is the fallback.
apply_patch_auto() {
  local patch_file="$1"
  if git -C "$SRC" apply "$patch_file" 2>/dev/null; then
    echo "    git apply: $(basename "$patch_file")"
  elif patch -d "$SRC" -p1 --fuzz=3 -l --forward < "$patch_file"; then
    echo "    patch -p1: $(basename "$patch_file")"
  else
    return 1
  fi
}

if grep -q "CMAKE_TOOLCHAIN_FILE" "$SRC/libpiper/CMakeLists.txt"; then
  echo "==> toolchain patch already applied"
else
  echo "==> applying $(basename "$PATCH")"
  apply_patch_auto "$PATCH" || { echo "error: toolchain patch failed" >&2; exit 1; }
  grep -q "CMAKE_TOOLCHAIN_FILE" "$SRC/libpiper/CMakeLists.txt" || {
    echo "error: failed to apply the toolchain patch" >&2; exit 1;
  }
fi

if grep -q "PIPER_NUM_THREADS" "$SRC/libpiper/src/piper.cpp"; then
  echo "==> threads patch already applied"
else
  echo "==> applying $(basename "$PATCH_THREADS")"
  apply_patch_auto "$PATCH_THREADS" || { echo "error: threads patch failed" >&2; exit 1; }
  grep -q "PIPER_NUM_THREADS" "$SRC/libpiper/src/piper.cpp" || {
    echo "error: failed to apply the threads patch" >&2; exit 1;
  }
fi

# 3) Cross-configure libpiper only. ONNXRUNTIME_DIR points at our prebuilt; ONNXRUNTIME_LIB
#    is pre-set so libpiper's find_library() becomes a no-op and is not re-rooted into the
#    sysroot by CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY.
echo "==> configuring (target: aarch64)"
cmake -S "$SRC/libpiper" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_INSTALL_PREFIX="$DEST" \
  -DONNXRUNTIME_DIR="$ORT" \
  -DONNXRUNTIME_LIB="$ORT/lib/libonnxruntime.so"

# 4) Build libpiper + its cross-built espeak-ng ExternalProject.
echo "==> building"
cmake --build "$BUILD" --parallel

# 4b) Generate the compiled espeak-ng-data with a NATIVE host build.
#     espeak-ng only compiles and installs its data when NOT cross-compiling: its
#     cmake/data.cmake is included under `if (COMPILE_INTONATIONS AND NOT
#     CMAKE_CROSSCOMPILING)`. The data compiler is espeak-ng itself, which we
#     cannot run as an aarch64 binary during the cross build. The compiled data is
#     little-endian and platform-independent (x86-64 and aarch64 agree), so data
#     produced by a native build of the SAME espeak-ng tag is correct for the Pi.
#     libpiper's install then copies this directory into third_party/libpiper.
#     Rationale and history: CHG-0022.
if [ ! -f "$ESPEAK_INSTALL/share/espeak-ng-data/phondata" ]; then
  if [ ! -d "$ESPEAK_SRC" ]; then
    echo "error: espeak-ng source not found at $ESPEAK_SRC" >&2
    echo "       (it should exist after the build step; re-run with --clean)" >&2
    exit 1
  fi
  echo "==> building host espeak-ng data (native; one-time)"
  cmake -S "$ESPEAK_SRC" -B "$HOST_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTS=OFF \
    -DUSE_ASYNC=OFF \
    -DUSE_MBROLA=OFF \
    -DUSE_LIBSONIC=OFF \
    -DUSE_LIBPCAUDIO=OFF \
    -DUSE_KLATT=OFF \
    -DUSE_SPEECHPLAYER=OFF
  cmake --build "$HOST_BUILD" --target data --parallel
  mkdir -p "$ESPEAK_INSTALL/share"
  rm -rf "$ESPEAK_INSTALL/share/espeak-ng-data"
  cp -a "$HOST_BUILD/espeak-ng-data" "$ESPEAK_INSTALL/share/"
  echo "==> staged espeak-ng-data into $ESPEAK_INSTALL/share/"
fi

echo "==> installing -> $DEST"
cmake --install "$BUILD"

# 5) Verify the artifacts.
[ -f "$DEST/lib/libpiper.so" ] || { echo "error: libpiper.so missing after install" >&2; exit 1; }
[ -f "$DEST/share/espeak-ng-data/phondata" ] || {
  echo "error: espeak-ng-data missing after install" >&2; exit 1;
}
echo "==> ok: $DEST/lib/libpiper.so"
if command -v file >/dev/null 2>&1; then
  file "$DEST/lib/libpiper.so" || true
fi
echo
echo "Artifacts:"
echo "  header : $DEST/include/piper.h"
echo "  library: $DEST/lib/libpiper.so"
echo "  espeak : $DEST/share/espeak-ng-data"
echo "Copy libpiper.so* and the espeak-ng-data/ to the Pi for runtime."
