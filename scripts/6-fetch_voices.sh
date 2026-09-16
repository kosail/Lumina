#!/usr/bin/env bash
# Fetch the Piper Spanish (es_MX) voices used by the runtime. Run on your pc; needs network.
#
# Primary voice: es_MX-claude-high — Mexican Spanish, "high" quality (the ald voices
# sounded poor in testing; ald is a finetune of the Spain davefx voice). Fallbacks:
# es_MX-ald-medium and es_MX-ald-x_low. Every .onnx is verified by SHA-256 before it
# is installed, and all are es_MX so INV-042 holds. See CHG-0037.
#
# Usage:
#   scripts/6-fetch_voices.sh            # download + verify -> models/voices/
#   scripts/6-fetch_voices.sh --force    # re-download even if present
#
# Source: https://huggingface.co/rhasspy/piper-voices (piper's official voice repo).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="$REPO_ROOT/models/voices"

# Pinned to the repository's default revision; the SHA-256 values below are the real
# integrity check. (Pin a commit hash here instead of "main" for bit-for-bit restarts.)
HF_REV="main"
HF_BASE="https://huggingface.co/rhasspy/piper-voices/resolve/$HF_REV"

# "name|relative dir|sha256 of the .onnx". The first entry is the runtime default.
VOICES=(
  "es_MX-claude-high|es/es_MX/claude/high|3ef40a71ea63852cd8ab7e6fa7d2ecdcfa67a0b47c9c48e3f10e02ee02083ea0"
  "es_MX-ald-medium|es/es_MX/ald/medium|019b3803293c93e34a206dd2e53a3889209a514e786fd7144f7b70196c579b63"
  "es_MX-ald-x_low|es/es_MX/ald/x_low|d8aae54aee9eafb37fb85d6da676facafca31c2d84d11acdca1a5b6dd82b1df6"
)

FORCE=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    -h|--help) echo "usage: $0 [--force]"; exit 0 ;;
    *) echo "error: unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# Downloader.
if command -v curl >/dev/null 2>&1; then
  download() { curl -fL --retry 3 -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
  download() { wget -q -O "$2" "$1"; }
else
  echo "error: need curl or wget" >&2
  exit 1
fi

mkdir -p "$DEST"

for entry in "${VOICES[@]}"; do
  IFS='|' read -r name dir sha <<< "$entry"
  echo "==> voice: $name"

  # ONNX model (verified).
  onnx="$DEST/$name.onnx"
  if [ "$FORCE" -eq 1 ] || [ ! -f "$onnx" ]; then
    tmp="$onnx.tmp.$$"
    download "$HF_BASE/$dir/$name.onnx" "$tmp"
    echo "$sha  $tmp" | sha256sum -c -
    mv "$tmp" "$onnx"
    echo "    installed $onnx ($(stat -c %s "$onnx") bytes)"
  else
    echo "    $onnx already present (use --force to refresh)"
  fi

  # Voice config JSON (small; not LFS, so we print its hash for provenance).
  json="$DEST/$name.onnx.json"
  if [ "$FORCE" -eq 1 ] || [ ! -f "$json" ]; then
    download "$HF_BASE/$dir/$name.onnx.json" "$json"
  fi
  echo "    $name.onnx.json sha256: $(sha256sum "$json" | awk '{print $1}')"

  # Model card (license/provenance).
  card="$DEST/$name.MODEL_CARD"
  if [ "$FORCE" -eq 1 ] || [ ! -f "$card" ]; then
    download "$HF_BASE/$dir/MODEL_CARD" "$card"
  fi
done

echo
echo "Default voice: $DEST/es_MX-claude-high.onnx"
echo "Run with:      ./lumina models/yolo11n_ncnn_320x256 $DEST/es_MX-claude-high.onnx <espeak-ng-data-dir>"
