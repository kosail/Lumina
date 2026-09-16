# PERFORMANCE.md — Lúmina detection performance record

> Measured-on-device evidence for the detection stage, the reasoning behind the
> `320×256` target, and the fallback plan. Facts here are **measurements**, not
> estimates; each was produced with `build/aarch64/tests/lumina_bench_fps` on the
> real Raspberry Pi Zero 2 W. Read with `INVARIANTS.md` (INV‑050, INV‑051).

---

## 1. Environment

| Item | Value |
|------|-------|
| Board | Raspberry Pi Zero 2 W Rev 1.0 (4× Cortex‑A53 @ 1.00 GHz) |
| OS | Raspberry Pi OS Lite 64‑bit (Debian 13 trixie), headless |
| Inference | NCNN 20260526, static, `NCNN_OPENMP=ON` + `NCNN_SIMPLEOMP=ON`, 4 threads |
| Model | YOLO11n exported with `yolo export format=ncnn quantize=16` (fp16 weights) |
| Camera frame | 640×480 RGB888, `stride=1920`, 4 buffers |
| Thermal | 36–56 °C observed; `vcgencmd get_throttled = 0x0` (no throttling) |
| Governor | `ondemand` (tested) and `performance` (no measurable difference) |

The benchmark feeds a **synthetic 640×480 frame** so the number is pure inference
cost. NCNN output was verified as `[anchors, 4+80]` (84 channels), boxes in
`cx,cy,w,h` letterboxed pixels, class scores already sigmoid.

---

## 2. Capture baseline

`lumina_capture_test` on the Pi: OV5647 via the `rpi/vc4` pipeline, 640×480 RGB888,
single plane (921,600 B), 4 buffers → **30 frames in 1669.4 ms ≈ 17.97 FPS**
(capture only, no inference).

---

## 3. Inference results (all on-device)

| Config | avg latency | FPS | Peak RSS | Notes |
|---|---:|---:|---:|---|
| 320×320, 1 thread (OpenMP **misconfigured**) | 406.1 ms | 2.46 | 58.8 MB | `-fopenmp` never emitted → single-threaded |
| 320×320, 4 threads (OpenMP fixed) | 231.4 ms | 4.32 | 58.8 MB | baseline |
| 416×416, 1 thread (misconfigured) | 683.8 ms | 1.46 | 79.5 MB | |
| 416×416, 4 threads (fixed) | 366.8 ms | 2.73 | 79.3 MB | |
| 320×320 + fp16 storage re-enabled | 231.6 ms | 4.32 | 58.7 MB | **no change** |
| 320×320 + governor=performance | 231.0 ms | 4.33 | — | **no change** |
| 320×320 + real libgomp (SIMPLEOMP off) | 232.0 ms | 4.31 | 58.8 MB | **no change** |
| 320×320 + `OMP_WAIT_POLICY=ACTIVE` | 231.6 ms | 4.32 | 58.7 MB | **no change** |
| 256×256, 4 threads | 157.9 ms | **6.33** | 49.5 MB | lower resolution (see §5) |
| 320×256, 4 threads | **189.8 ms** | **5.27** | 54.1 MB | **approved target — INV‑050 met** (48.9 °C) |
| 320×256, 3 threads | 205.4 ms | 4.87 | 53.3 MB | **below INV‑050** (47.8 °C) — a full reserved core costs the target |

### Thread sweep at 320×320
| threads | avg latency | FPS | speedup vs 1 |
|---:|---:|---:|---:|
| 1 | 403.1 ms | 2.48 | 1.00× |
| 2 | 279.7 ms | 3.57 | 1.44× |
| 3 | 253.5 ms | 3.95 | 1.59× |
| 4 | 230.7 ms | 4.33 | 1.75× |

Sublinear scaling; fitting Amdahl's law gives a **serial fraction of ~36–43 %**.

### Per-layer profile (NCNN_BENCHMARK build, 4 threads)
The slowest layers are **convolutions**: `conv_9` ≈ 11.4–14.5 ms, `conv_14` ≈
11.3–11.7 ms, `conv_4` ≈ 8.5–13 ms, `conv_3` ≈ 8.7–12.2 ms, `conv_65/66` ≈
6.2–6.4 ms. No activation/elementwise layer dominates (an early "0.8 ms Swish"
reading was an artifact of sorting the log incorrectly).

---

## 4. What this means

- **The board, not the runtime, is the wall.** Real libgomp vs SIMPLEOMP and
  `OMP_WAIT_POLICY` changed nothing; the governor and fp16 storage changed nothing.
  The workload is convolution-bound and limited by the single shared LPDDR2 bus and
  shared L2, so extra cores/backends cannot help much.
- **FPS is scene-independent.** Network compute is fixed for a given input size;
  extra objects only add negligible decode/NMS time. So concern about FPS "dropping
  in busy scenes" is unfounded — but it also means FPS says nothing about *recall*.
- **INV‑051 is the user-facing requirement, not INV‑050.** At 320×320, 231 ms
  frame-to-detection is comfortably inside the < 600 ms alert budget (INV‑051).

---

## 5. Resolution decision (approved)

Letterboxing a 640×480 (4:3) frame into different inputs:

| input | effective image | padding waste | notes |
|---|---|---|---|
| 320×320 | 320×240 | 25 % | current; wasted grey padding |
| **320×256** | **320×240** | **6 %** | **approved target** — same effective resolution as 320×320, ~20 % less compute |
| 256×256 | 256×192 | 25 % | ~36 % less compute, ~20 % lower linear resolution |

**Decision:** detection input is **320×256** (width 320, height 256). It preserves
the exact effective resolution of the 320×320 configuration while removing padding
waste. It **measures 5.27 FPS at 4 threads (189.8 ms, 54.1 MB, 48.9 °C) — INV‑050
met** (≥ 5 FPS at 320 px). A `256×256` model is kept as the low-resolution fallback.

**Margin note (measured).** At **3 threads** (one core reserved for capture + audio)
320×256 drops to **4.87 FPS (205.4 ms) — below INV‑050**. The shipping configuration is
therefore **4 inference threads** (5.27 FPS, INV‑050 met), with CPU headroom supplied by
**decimation** (inference is bursty; capture and Piper run in the gaps, and capture is
light: libcamera runs its own thread and our per-frame copy is ~1.2 MB). If the
end-to-end pipeline still misses 5 FPS at the Day‑2 gate, escalate to **256×256**
(6.33 FPS @4t, ~5.85 @3t), which clears the target with a core to spare. Escalation
order is unchanged (§8).

The safety-critical objects (in-path obstacles) are large and near, so 320×240 is
adequate; note also that the most dangerous hazards (curbs, steps, poles) are not
COCO classes at any resolution — that gap requires a proximity sensor, which the
current hardware lacks (INV‑013).

---

## 6. Runtime/model alternatives considered

| Option | Verdict | Why |
|---|---|---|
| YOLO11n (current) | **keep** | Best accuracy/FLOPs balance; turnkey NCNN export |
| NanoDet‑Plus‑m | **deferred fallback** (§8) | Credible (27.0 mAP@320, 0.9 GFLOPs; ncnn C++ demo), but only ~≤1.5× and needs a new decoder + dependency |
| YOLOv5n | reject | Less accurate than YOLO11n (28.4 vs 39.5 mAP50‑95 @640) for ~30 % fewer FLOPs |
| YOLO‑FastestV2 | reject | COCO mAP@0.5 ≈ 24 % (≈ half of YOLO11n); face/person-grade |
| MobileNet‑SSD | reject | Older anchor-SSD; weaker small-object recall |

Sources (accessed 2026‑09‑16): Ultralytics YOLO11 docs (YOLO11n 39.5 mAP50‑95,
6.5 GFLOPs, 2.6 M @640); NanoDet‑Plus README (NanoDet‑Plus‑m 27.0 mAP@320 /
30.4 @416; 1.5× variant 29.9 / 34.1); YOLO‑FastestV2 README (mAP@0.5 24.1 % @352,
0.212 GFLOPs).

---

## 7. Recall validation protocol (required before trusting a smaller input)

FPS is not the risk; recall is. Because the NCNN export also runs in Python, this
is done on the laptop (no Pi needed):

1. Collect ~30–50 representative frames (indoor + outdoor) covering our classes and
   people/obstacles at ~2 m, ~5 m, ~10 m.
2. Run the exported models at **256×256, 320×256, 416×416** and compare per-class
   detections/confidence against ground truth (hand labels or COCO images with known
   boxes).
3. Record per-class recall and the smallest detectable object size per config.
4. Separately confirm FPS stability: run the on-device bench with synthetic frames
   containing 0 / 5 / 20 objects — latency must stay flat.

Only after this evidence do we treat 320×256 (or any fallback) as validated. If
recall is insufficient, the escalation order is documented in §8.

---

## 8. Fallback / escalation order (approved)

Only at/after the Day‑2 GO/NO‑GO gate (INV‑071), and only if recall/latency is
insufficient or headroom is needed for capture + audio:

1. **Switching the input size.** Verified order: `320×256` (target) → `256×256`
   (already ≥ 5 FPS) if headroom is needed.
2. **Model swap to NanoDet‑Plus‑m / 1.5×.** The detector is isolated behind
   `IDetector`, so this is a drop-in. Expected gain **≤ ~1.5×**, not a leap: the
   board is the wall. Cost: COCO checkpoint → ONNX → ncnn, a **new GFL+FCOS decoder**
   (different output layout than YOLO), and a new runtime dependency — which
   requires explicit approval under INV‑022. Do not undertake this before the gate.
3. **INT8 quantization** — **formally deferred** (user decision, CHG‑0018; see INV‑012). It is
   benchmark-gated and low-value on the A53 (no SDOT/UDOT; int8 is size/bandwidth only), and
   fp16 storage showed no gain. Not pursued for the beta; revisit after the gate only if
   memory/size pressure appears.

YOLOv5, YOLO‑FastestV2 and MobileNet‑SSD are rejected outright (§6).

---

## 9. Reproducing these numbers

```bash
# Host: build the target models (320x256 + comparison set)
scripts/3-export_models.sh

# Cross-build the detector + benchmark
cmake --preset aarch64 -DLUMINA_ENABLE_NCNN=ON -DLUMINA_BUILD_BENCH=ON
cmake --build --preset aarch64

# On the Pi
scp build/aarch64/tests/lumina_bench_fps pi@<pi-host>:~/
scp -r models/yolo11n_ncnn_320x256 models/yolo11n_ncnn_256 pi@<pi-host>:~/
ssh pi@<pi-host> '~/lumina_bench_fps ~/yolo11n_ncnn_320x256 320 256 200 --threads 4'
```

Per-layer profiling (optional): build with `scripts/2-build_ncnn.sh --layer-benchmark`,
configure with `-DLUMINA_NCNN_ROOT=<repo>/third_party/ncnn-bench`, and read the
printed per-layer timings. Keep that instrumented binary off the demo path.
