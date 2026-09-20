#!/usr/bin/env bash
# Fetch the face-detection (YuNet) and face-recognition (SFace) ONNX models used
# by the Day-4 face path. Run on your pc; needs network.
#
# Both models come from the official OpenCV Zoo. They are stored with Git LFS in
# that repository, so the downloader MUST follow redirects (`curl -L`): a plain
# raw.githubusercontent.com request returns a ~130-byte LFS *pointer*, not the
# model. Every file is verified by SHA-256 before it is installed (INV-001).
#
# YuNet variant: use the **2023mar** export, which has a fixed input shape and is
# the correct model for OpenCV **4.x** (our sysroot is 4.10.0, INV-025). The newer
# `face_detection_yunet_2026may.onnx` has dynamic dims and targets the OpenCV 5.x
# ONNX-Runtime engine — do NOT substitute it (opencv_zoo README, 2026-09-18).
#
# Licenses: YuNet is MIT; SFace is Apache-2.0. Both are copied next to the model
# so the provenance travels with the deployment.
#
# Usage:
#   scripts/8-fetch_face_models.sh            # download + verify -> models/face/
#   scripts/8-fetch_face_models.sh --force    # re-download even if present
#
# Source: https://github.com/opencv/opencv_zoo (paths below).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="$REPO_ROOT/models/face"

# Pinned to the repository's default revision; the SHA-256 values below are the
# real integrity check (they are the Git-LFS object ids). Pin a commit hash here
# instead of "main" for bit-for-bit restarts.
ZOO_REV="main"
ZOO_RAW="https://github.com/opencv/opencv_zoo/raw/$ZOO_REV"
ZOO_LICENSE="https://raw.githubusercontent.com/opencv/opencv_zoo/$ZOO_REV"

# "filename|relative dir|sha256|bytes". The install name is the first field's
# basename, rewritten to the short names the runtime expects (yunet.onnx /
# sface.onnx) so the code does not care which dated export is current.
MODELS=(
  "face_detection_yunet_2023mar.onnx|models/face_detection_yunet|8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4|232589"
  "face_recognition_sface_2021dec.onnx|models/face_recognition_sface|0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79|38696353"
)
# Short install names, parallel to MODELS (yunet, sface).
SHORT_NAMES=("yunet" "sface")

FORCE=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    -h|--help) echo "usage: $0 [--force]"; exit 0 ;;
    *) echo "error: unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# Downloader. Always follow redirects: OpenCV Zoo serves the binaries via LFS.
if command -v curl >/dev/null 2>&1; then
  download() { curl -fL --retry 3 -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
  download() { wget -q -O "$2" "$1"; }
else
  echo "error: need curl or wget" >&2
  exit 1
fi

mkdir -p "$DEST"

for idx in "${!MODELS[@]}"; do
  IFS='|' read -r file dir sha bytes <<< "${MODELS[$idx]}"
  short="${SHORT_NAMES[$idx]}"
  echo "==> model: $short ($file)"

  onnx="$DEST/$short.onnx"
  if [ "$FORCE" -eq 1 ] || [ ! -f "$onnx" ]; then
    tmp="$onnx.tmp.$$"
    download "$ZOO_RAW/$dir/$file" "$tmp"
    # Verify both size and hash: the pointer-vs-binary mistake yields the right
    # line count but the wrong bytes, so the hash check is the real guard.
    actual_size="$(stat -c %s "$tmp")"
    if [ "$actual_size" != "$bytes" ]; then
      echo "error: $short.onnx is $actual_size bytes, expected $bytes" >&2
      echo "       (did the download return a Git-LFS pointer instead?)" >&2
      rm -f "$tmp"
      exit 1
    fi
    echo "$sha  $tmp" | sha256sum -c -
    mv "$tmp" "$onnx"
    echo "    installed $onnx ($actual_size bytes)"
  else
    echo "    $onnx already present (use --force to refresh)"
  fi

  # License text (small; travels with the model for provenance).
  license="$DEST/$short.LICENSE"
  if [ "$FORCE" -eq 1 ] || [ ! -f "$license" ]; then
    download "$ZOO_LICENSE/$dir/LICENSE" "$license"
  fi
  echo "    $short.LICENSE sha256: $(sha256sum "$license" | awk '{print $1}')"
done

echo
echo "Face models installed under: $DEST"
echo "Runtime expects:            $DEST/yunet.onnx  and  $DEST/sface.onnx"
