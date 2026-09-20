# PERFORMANCE.md — Lúmina performance record

> Measured-on-device evidence for the detection stage, the reasoning behind the
> `320×256` target, the fallback plan, and the end-to-end pipeline run at the Day‑2
> gate. Facts here are **measurements**, not estimates; each was produced on the
> real Raspberry Pi Zero 2 W (the benchmark via `build/aarch64/tests/lumina_bench_fps`,
> the pipeline via the `lumina` binary). Read with `INVARIANTS.md` (INV‑050, INV‑051,
> INV‑052, INV‑071).

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
COCO classes at any resolution — that gap is addressed by the front VL53L0X proximity
sensor now on the hardware (INV‑013/INV‑075; see `docs/PROXIMITY.md`).

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

---

## 10. End-to-end pipeline (Day‑2 gate, CHG‑0035)

Measured on the Pi Zero 2 W with the full runtime (libcamera capture + YOLO11n/NCNN +
Piper/espeak‑ng + ALSA→bluealsa), 320×256 inference, 4 NCNN threads, `PIPER_NUM_THREADS=3`.
The gate run used `es_MX-ald-x_low`; the shipping default is `es_MX-claude-high`
(INV‑042 / CHG‑0037), whose re-measurement follows.

```bash
LD_LIBRARY_PATH=$PWD/third_party/libpiper/lib LUMINA_LOG_LEVEL=debug ./lumina \
  models/yolo11n_ncnn_320x256 models/voices/es_MX-ald-x_low.onnx espeak-ng-data
```

### Shipping voice comparison (same 320×256 / 4-thread config)

| Metric | `es_MX-ald-x_low` (gate) | **`es_MX-claude-high`** (shipping) |
|---|---|---|
| RSS (idle) | 157 MB | **187 MB** (model 63 MB vs 21 MB) |
| Detection throughput | 4.0–4.3 FPS | **4.2–4.3 FPS** |
| Alert latency, cached phrase | `event→speech-start` 247–271 ms | **252–298 ms** |
| `event→end` | ≈ 2.3 s | **≈ 2.16–2.20 s** |
| Phrase cache warm | 36 phrases ≈ 60 s (first run) | **0 new** (already warmed; persisted) |
| Synthesis/warm time | ≈ 1.1–1.6 s per phrase (from warm) | not captured (cache already warm) |

> *Warm-count note:* the "36 phrases" above are historical (12 narrated classes × 3). After
> CHG‑0047 the narrated set is 10 classes; after CHG‑0055 a fresh cache warms **212 phrases**
> (30 single‑class + 180 two‑class + 2 alerts). The first run renders them (minutes, one‑time) and
> later runs are cache hits.

### Gate run detail (`es_MX-ald-x_low`)

| Metric | Measured | Notes |
|---|---|---|
| Detection throughput (co-load) | **4.0–4.3 FPS** | below the ≥ 5 target — temporary, see below |
| Detection latency (capture→description) | **247–288 ms** | from the per-frame DEBUG line |
| RSS (idle) | **157 MB** | transient 189 MB during live synthesis (INV‑052 budget ~450 MB) |
| Alert latency, cached phrase | **event→speech-start 247–271 ms** | plus Bluetooth (~150–300 ms) for INV‑051 |
| Utterance playback | **~1.75 s** (38 656 samples @ 22 050 Hz) | `event→end` ≈ 2.3 s |
| Phrase cache warm | **36 phrases ≈ 60 s** (one-time) | persisted at `$HOME/.cache/lumina/phrase-cache` |
| First-heard multi-class phrase | **first audio 6.7 s**, then cached | lazy miss (not pre-warmed) |

### Gate outcome
Camera → NCNN → Piper → Bluetooth works end-to-end and is audible in the paired buds, so the
**Day‑2 GO/NO‑GO gate (INV‑071) is PASS** and the hardened Python‑nightly fallback is not required.

### INV‑050 caveat (temporary)
Under full co-load the pipeline runs at **~4.3 FPS**, below the ≥ 5 FPS target (the isolated
inference benchmark still measures 5.27 FPS). This gap is **temporarily accepted** to protect the
6-day schedule (CHG‑0036). The target is unchanged; if time remains after the core MVP we escalate
in order: **decimation** (infer every N frames) → **256×256** → NanoDet‑Plus.

### Known limitations
- YOLO11n at 320×256 can confuse similar classes (e.g. a person reported as a motorcycle). The
  model's behaviour is out of scope for this stage; recall validation is §7. (Motorcycle and truck
  are no longer narrated in the beta, so that particular confusion no longer reaches the user.)
- Repetition is governed by the Day‑3 alert arbiter (`src/alerts/arbiter.*`): an unchanged scene is
  not repeated more than once per 4 s per phrase; a changed scene can speak again immediately; a
  Near in‑path obstacle preempts narration (a Mid one does not).

> **WARNING — live synthesis is not interruptible (later stage).** libpiper (pinned `251fdb9d`)
> exposes **no cancellation** (`piper.h` has no stop/cancel call), and `piper_synthesize_next` can
> return an entire utterance as a single chunk. A live (uncached) synthesis therefore **cannot be
> preempted by any priority**, including the planned IR/Safety alert; preemption is guaranteed only
> for **cached** playback (between ~47 ms chunks) and tail‑write aborts. Mitigations now: pre‑warmed
> single‑ and two‑class descriptions, descriptions capped at 2 items, and smaller cache/write
> chunks. Consequences: **Mid stays non‑preempting**, and **IR/proximity preemption of a live
> synthesis is not guaranteed**. Revisit at a later stage — options: render descriptions off the
> speech thread so the speech path is cache‑only; vendor cancellation into libpiper; or add an
> IR‑only immediate tone that bypasses TTS. (The IR sensor, when implemented in Phase C, is
> `Priority::Safety`, the highest.)

---

## 11. Day‑3 alert behavior (arbiter)

The alert pipeline is now: inference → `app::buildSceneAlert` → `AlertArbiter` → `CachingTts` →
`AlsaSink`. Policy: priority ordering, 2‑frame stability for descriptions (1 for warnings), 4 s
per‑phrase cooldown, 600 ms global gap (bypassed by warnings), strictly‑higher preemption.

**Latency benchmark** (`tests/bench_latency.cpp`, on the Pi; needs `LUMINA_ENABLE_AUDIO=ON` +
`LUMINA_BUILD_BENCH=ON`) measures `event → first audio` for a cached alert phrase:

```bash
LD_LIBRARY_PATH=$PWD/third_party/libpiper/lib \
  ./build/aarch64/tests/lumina_bench_latency \
  models/voices/es_MX-claude-high.onnx third_party/libpiper/share/espeak-ng-data 10
```

It exercises the real `CachingTts` + `AlsaSink` (falls back to a discard sink if bluealsa is not
open). It excludes inference and the Bluetooth radio hop (~150–300 ms), and aborts each utterance
after the first chunk so it stays fast.

**Results (on‑device, 2026‑09‑18):**

| Metric | Value | Notes |
|---|---|---|
| cached alert `event→first audio` | **claude: 0.13–0.16 ms; ald‑xlow: 0.13–0.19 ms** | `lumina_bench_latency`, 10 iterations, bluealsa |
| cached alert `event→speech-start` | **251–311 ms** steady (433 ms cold) | pipeline log; +~150–300 ms BT ⇒ < 600 ms |
| preempted alert `event→speech-start` | **311 ms** | was 738 ms before CHG‑0054; measured after the abort fixes |
| live multi‑class synthesis | **6667–9217 ms** (claude) to first audio | lazy cache miss; eliminated on the warmed path by CHG‑0055 |
| FPS / RSS | **4.1–4.3** / **189 MB (claude), 148 MB (ald‑xlow)** | well under INV‑052 |
| static‑scene repeat | warnings ~9–24 s apart | no ~2 s churn |
| Mid alert | `obstáculo cerca.` detected **and spoken** | non‑preempting; may queue behind live synthesis |

**Crash found and fixed (CHG‑0056):** the first post‑change run segfaulted on Ctrl‑C with
libcamera's "Camera in Stopping state trying queueRequest()". Root cause was a teardown race between
`onRequestCompleted()` (libcamera thread) and `stop()`; it is now serialized (see the file header in
`src/capture/libcamera_source.cpp`). **Verified on‑device (CHG‑0058):** 11 clean start/Ctrl‑C cycles
including a ~4.5‑minute soak, no libcamera error, no segfault.

**Voices:** cached playback is equally fast for both (`0.13–0.19 ms`). Live synthesis is ~**3.4×**
faster with `ald‑xlow` (~25.6k samples/s) than `claude‑high` (~7.5k samples/s), at ~40 MB less RSS.
A two‑voice "claude for warmed phrases, ald for misses" fallback was measured as viable but is
**deferred** (CHG‑0057) until the later‑stage live‑synthesis work.

---

## 12. Day‑4 face recognition (design; measurements pending)

**Stack.** OpenCV 4.10 from the sysroot: YuNet (`face_detection_yunet_2023mar.onnx`, ~0.23 MB) for
detection + SFace (`face_recognition_sface_2021dec.onnx`, ~37 MB) for embeddings, behind
`IFaceRecognizer`. Models fetched by `scripts/8-fetch_face_models.sh` (SHA‑256 pinned, CHG‑0059).
Build with `LUMINA_ENABLE_FACE=ON` (CHG‑0060).

**Policy (approved, CHG‑0061/0063).**
- Matching: cosine similarity, threshold **0.363** (OpenCV Zoo reference) + best‑vs‑second
  **margin 0.05** to reject ambiguous near‑ties between enrolled people.
- Greeting: **3 stable observations** + **30 s per‑person cooldown**; phrase
  `"<nombre> está enfrente"`; Description priority (non‑preempting); pre‑warmed per enrolled name.
- Store: `models/face/embeddings.bin`, up to **10 embeddings/person**, versioned + atomic (CHG‑0061).
- Enrollment: `lumina_enroll` from **photos** (`--image`/`--images-dir`, recommended) or the live
  **camera** (`--camera`), up to 10 samples/person (CHG‑0067).
- Runtime: dedicated **low-rate face worker thread** (single-slot queues, `faceIntervalMs=500`),
  `cv::setNumThreads(1)` so OpenCV does not oversubscribe the 4 cores NCNN uses (CHG‑0062/0065).
- Detection input: the 640×480 frame is downscaled to **`faceDetectionSide` (default 320)** before
  YuNet, because at full size YuNet's DNN workspace churned tens of MB per inference and forced SD
  swap (CHG‑0070).

**Memory pressure on the 415 MB board (CHG‑0070).** The Zero 2 W exposes only ~415 MB usable RAM
(GPU/firmware reserve ~97 MB). Idle ≈124 MB; lumina without face inference ≈201 MB RSS. Running
YuNet at the full 640×480 pushed the system to ~388 MB used, **26 MB available, ~149 MB swap**, and
the SD‑card swap stall made libcamera miss its 1 s V4L2 dequeue (`Camera frontend has timed out`).
Mitigation: downscale the face input to `faceDetectionSide`. If headroom is still tight, also:

- **Lower the GPU split** (headless): set `gpu_mem=64` (or lower) in `/boot/firmware/config.txt`
  and reboot — frees tens of MB for the CPU side.
- **Use zram instead of SD swap** (`zram-tools` / `dtoverlay`/systemd‑zram): compressed RAM swap is
  far faster than SD, so a brief spike cannot stall the camera.
- Raise `faceIntervalMs`, lower `faceDetectionSide` to 256, or reduce NCNN threads 4 → 3.
- Larger change: smaller embedder (int8 SFace or MobileFaceNet‑on‑NCNN ~4 MB).

**To measure on‑device (Phase I, pending):**
| Metric | Baseline (Day 3) | Day‑4 target |
|--------|------------------|--------------|
| Inference FPS (faces on) | 4.1–4.8 | stay within the INV‑050 caveat (no worse than ~4.0) |
| RSS (faces on) | 184–189 MB | steady, and no swap growth during a run |
| `face: identify` cost | — | < ~150 ms at 320 px |
| Enroll (per person) | — | up to 10 embeddings captured in one run |
| Recognition | — | enrolled greeted by name; non‑enrolled not named |
| Persistence | — | `embeddings.bin` reloads after reboot |

**Fallbacks if RAM/FPS regress:** int8bq SFace (verify OpenCV 4.10 support first) → MobileFaceNet on
NCNN (deferred). Record results here and append a CHANGELOG entry once measured.
