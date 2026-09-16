#!/usr/bin/env bash
# Cross-build NCNN (static) for the Raspberry Pi Zero 2 W. Run on your pc, not on the Pi.
#
# Usage:
#   scripts/2-build_ncnn.sh                     # normal build -> third_party/ncnn
#   scripts/2-build_ncnn.sh --layer-benchmark   # instrumented -> third_party/ncnn-bench
#
# --layer-benchmark compiles ncnn with -DNCNN_BENCHMARK=ON, which makes the
# library print per-layer timings on every inference (diagnostic only; noisy).
# It installs to a SEPARATE prefix so the normal third_party/ncnn is untouched.
# Point the Lumina build at it with -DLUMINA_NCNN_ROOT=<repo>/third_party/ncnn-bench.
set -euo pipefail

NCNN_VERSION="20260526"                 # pinned release tag (matches ultralytics pnnx pin)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SYSROOT="$REPO_ROOT/cmake/rpi-sysroot"
SRC="$REPO_ROOT/third_party/ncnn-src"
ZIP="ncnn-$NCNN_VERSION-full-source.zip"
URL="https://github.com/Tencent/ncnn/releases/download/$NCNN_VERSION/$ZIP"

LAYER_BENCHMARK=0
for arg in "$@"; do
  case "$arg" in
    --layer-benchmark) LAYER_BENCHMARK=1 ;;
    -h|--help)
      echo "usage: $0 [--layer-benchmark]"
      echo "  --layer-benchmark  build with NCNN_BENCHMARK=ON into third_party/ncnn-bench"
      exit 0 ;;
    *) echo "error: unknown argument: $arg" >&2; exit 2 ;;
  esac
done

if [ "$LAYER_BENCHMARK" -eq 1 ]; then
  BUILD="$REPO_ROOT/third_party/ncnn-bench-build"
  DEST="$REPO_ROOT/third_party/ncnn-bench"
  EXTRA_CMAKE=(-DNCNN_BENCHMARK=ON)
  echo "==> instrumented build (per-layer timing) -> third_party/ncnn-bench"
else
  BUILD="$REPO_ROOT/third_party/ncnn-build"
  DEST="$REPO_ROOT/third_party/ncnn"
  EXTRA_CMAKE=(-DNCNN_BENCHMARK=OFF)
fi

# 0) Preconditions: cross compiler present, sysroot present.
command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
[ -d "$SYSROOT/usr" ] || { echo "error: sysroot missing; run scripts/1-sync_sysroot.sh"; exit 1; }

# 1) Download + extract the pinned 'full-source' zip (self-contained, no git submodules).
mkdir -p "$SRC"
if [ ! -f "$SRC/CMakeLists.txt" ]; then
  echo "==> downloading $ZIP"
  curl -fL "$URL" -o "$REPO_ROOT/third_party/$ZIP"
  ( cd "$SRC" && unzip -q "../$ZIP" )
fi

# 2) The Arch cross-toolchain linker workaround (same reason as cmake/toolchain-aarch64.cmake):
#    plain --sysroot does not redirect library search on this host.
ARCH_FLAGS="-L$SYSROOT/usr/lib/aarch64-linux-gnu -L$SYSROOT/lib/aarch64-linux-gnu \
-Wl,--sysroot,$SYSROOT -Wl,-rpath-link,$SYSROOT/usr/lib/aarch64-linux-gnu"

# 3) Configure.
cmake -S "$SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
  -DCMAKE_SYSROOT="$SYSROOT" \
  -DCMAKE_FIND_ROOT_PATH="$SYSROOT" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
  -DCMAKE_EXE_LINKER_FLAGS="$ARCH_FLAGS" \
  -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_FLAGS" \
  -DCMAKE_INSTALL_PREFIX="$DEST" \
  -DNCNN_SHARED_LIB=OFF \
  -DNCNN_VULKAN=OFF \
  -DNCNN_OPENMP=ON -DNCNN_SIMPLEOMP=ON \
  -DCMAKE_EXE_LINKER_FLAGS="$ARCH_FLAGS" \
  -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF \
  -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BUILD_TESTS=OFF \
  -DNCNN_INSTALL_SDK=ON \
  "${EXTRA_CMAKE[@]}"

# 4) Build and install.
cmake --build "$BUILD" --target install -j"$(nproc)"

# 5) Verify the installed artifacts are aarch64.
file "$DEST/lib/libncnn.a"
ls "$DEST/include/ncnn/net.h" "$DEST/lib/cmake/ncnn/ncnnConfig.cmake"
echo "==> installed to $DEST"
