#!/usr/bin/env bash
# Export YOLO11n to NCNN (fp16). Run on the laptop. Needs network.
#
# The approved detection input is 320x256 (width x height): a 4:3 frame
# letterboxes to 320x240 with only 16 px of padding, ~20% less compute than
# 320x320 at the same effective resolution. We also export 256x256 (the low-res
# fallback) and 320x320 / 416x416 (references for the recall comparison).
# See docs/PERFORMANCE.md.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

python -m venv .venv-models
# shellcheck disable=SC1091
source .venv-models/bin/activate
pip install -U pip
pip install ultralytics   # pulls the pinned pnnx and ncnn automatically

# (dir_suffix, height, width) — ultralytics `imgsz` is (height, width).
EXPORTS=(
  "320x256 256 320"
  "256 256 256"
  "320 320 320"
  "416 416 416"
)

for entry in "${EXPORTS[@]}"; do
  read -r name h w <<< "$entry"
  echo "==> exporting yolo11n ${name} (h=${h} w=${w}, fp16)"
  # Python API (not the CLI) so a non-square imgsz tuple is unambiguous.
  python - "$h" "$w" <<'PY'
import sys

from ultralytics import YOLO

height, width = int(sys.argv[1]), int(sys.argv[2])
YOLO("yolo11n.pt").export(format="ncnn", imgsz=(height, width), quantize=16)
PY
  mkdir -p "models/yolo11n_ncnn_${name}"
  cp yolo11n_ncnn_model/model.ncnn.param yolo11n_ncnn_model/model.ncnn.bin \
     "models/yolo11n_ncnn_${name}/"
done

ls -l models/yolo11n_ncnn_320x256 models/yolo11n_ncnn_256 models/yolo11n_ncnn_320 models/yolo11n_ncnn_416
