#!/usr/bin/env bash
# Fetch the OFFICIAL prebuilt ONNX Runtime (aarch64) used by libpiper. Run on your pc, not on the Pi.
#
# We deliberately do NOT cross-build onnxruntime: it is the heaviest build in the plan and the
# main Day 1-2 schedule risk, and Microsoft ships an official aarch64 prebuilt (INV-023 prefers
# no heavy builds). onnxruntime is already part of the INV-022 fixed stack as a libpiper
# dependency, so nothing new is introduced. See CHG-0019.
#
# Usage:
#   scripts/4-fetch_onnxruntime.sh           # download + verify + extract -> third_party/onnxruntime
#   scripts/4-fetch_onnxruntime.sh --force   # refresh even if already present
#
# Resulting layout (consumed by the libpiper build / Lumina CMake):
#   third_party/onnxruntime/include/...           C/C++ headers
#   third_party/onnxruntime/lib/libonnxruntime.so  aarch64 shared library
#
# The tarball is verified by SHA-256 BEFORE anything is extracted (supply-chain safety).
set -euo pipefail

ORT_VERSION="1.30.0"   # pinned official release tag (matches the asset published 2026-09-10)
ORT_NAME="onnxruntime-linux-aarch64-$ORT_VERSION"
ORT_SHA256="e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da"
URL="https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VERSION/$ORT_NAME.tgz"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="$REPO_ROOT/third_party/onnxruntime"
TGZ="$REPO_ROOT/third_party/$ORT_NAME.tgz"

FORCE=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    -h|--help)
      echo "usage: $0 [--force]"
      exit 0 ;;
    *) echo "error: unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# Idempotent: nothing to do if the library is already unpacked (unless --force).
if [ "$FORCE" -eq 0 ] && [ -f "$DEST/lib/libonnxruntime.so" ]; then
  echo "==> $DEST already present; nothing to do (use --force to refresh)"
  exit 0
fi

# 0) Need a downloader. Prefer curl; fall back to wget.
if command -v curl >/dev/null 2>&1; then
  download() { curl -fL --retry 3 -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
  download() { wget -O "$2" "$1"; }
else
  echo "error: need curl or wget to download ONNX Runtime" >&2
  exit 1
fi

# 1) Download the pinned tarball.
mkdir -p "$REPO_ROOT/third_party"
echo "==> downloading $URL"
download "$URL" "$TGZ"

# 2) Verify the checksum before extracting anything.
echo "==> verifying sha256"
echo "$ORT_SHA256  $TGZ" | sha256sum -c -

# 3) Extract include/ and lib/ into third_party/onnxruntime (drop the version directory).
echo "==> extracting -> $DEST"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
tar -xzf "$TGZ" -C "$STAGE"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -a "$STAGE/$ORT_NAME/include" "$DEST/"
cp -a "$STAGE/$ORT_NAME/lib" "$DEST/"

# 4) Sanity check: the shared library must exist. (Confirm the arch on your host before use.)
test -f "$DEST/lib/libonnxruntime.so" || { echo "error: libonnxruntime.so missing after extract" >&2; exit 1; }
echo "==> ok: $DEST/lib/libonnxruntime.so"
if command -v file >/dev/null 2>&1; then
  file "$DEST/lib/libonnxruntime.so" || true
fi

echo
echo "Next steps:"
echo "  1. Confirm the file above reports 'ARM aarch64'."
echo "  2. Point the libpiper build at $DEST (headers + lib)."
echo "  3. Copy '$ORT_NAME/lib/libonnxruntime.so*' to the Pi (e.g. /usr/local/lib) for runtime."
