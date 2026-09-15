# models/

Model artifacts are **not committed** (see the root `.gitignore`). Populate this
directory with `scripts/export_models.sh` (run on the laptop with Python tooling).

Expected layout:

```
models/
  yolo11n_ncnn/   model.ncnn.param, model.ncnn.bin    # object detection (NCNN)
  face/           yunet.onnx, sface.onnx              # or mobilefacenet.ncnn.*
  voices/         es_MX-*.onnx, es_MX-*.onnx.json     # Piper Spanish TTS
```

See `RAW_PLAN.md` §6 and `AGENTS.md` §3.
