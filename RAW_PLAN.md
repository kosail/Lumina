# RAW_PLAN.md — Lúmina Beta Runtime (RPi Zero 2W)

doc_id: lumina.beta.raw_plan
status: approved_pending_execution
target_hw: Raspberry Pi Zero 2 W Rev 1.0 (reported BCM2837, 4x Cortex-A53 @1GHz, 512MB LPDDR2)
target_os: Raspberry Pi OS Lite 64-bit (Debian 13 "trixie", aarch64), kernel 6.18.x+rpt-rpi-v8
camera: OV5647 5MP, 135° diagonal, IR-CUT 75/175 auto, fixed manual focus
audio_out: Bluetooth A2DP bone-conduction earbuds (single device, already paired)
language_target: C++23 (GCC 14.2.1)
deadline: 6 working days (competition in 7)
repo: Lumina-BETA-RPI-2W
objective: functional beta that MUST run well on RPi Zero 2W
derived_from:
  - FIRST_IMPRESSIONS_ON_NIGHTLY_VERSION.md
  - RPI_2W_SPECSHEET.txt
  - AGENTS.md (invariants)

---

## 0. INVARIANT COMPLIANCE

- INV1 runs on RPi 2W: enforced by 320/416px inference, decimation, bounded queues, headless OS.
- INV2 pure C++ preferred: yes for vision/core; audio uses ALSA->bluealsa (still C++). No MicroPython.
- INV3 one extra feature: FACE RECOGNITION (3-4 people). Currency rejected (color/IR risk, no model).
- INV4 camera OV5647 135° IR-CUT: libcamera OV5647; risk of NoIR/orange tint in night mode -> central ROI.
- INV5 6 days: hard GO/NO-GO gate at end of Day 2.
- INV6 BT audio only device: A2DP via bluealsa; trusted + autoconnect; single sink.
- INV7 solid aluminum case, ~30C: thermal non-issue; still rate-limit and pass-through cooling.
- INV8 no offline navigation: excluded.
- INV9 telemetry deferred to Day 6; UDP fire-and-forget; must not touch core path.
- INV10 runtime chosen by performance + ease: NCNN for inference; libpiper for TTS.
- INV11 IR proximity: NOT present in purchased hardware. Dropped now, but implemented
         MODULARLY behind IProximitySensor so a VL53L0X (or E18/TCRT5000) can be added later.
- INV12 keep PiperTTS; Spanish only (reuse existing es_MX voice family).

---

## 1. LOCKED STACK

| Layer        | Choice                                        | Rationale |
|--------------|-----------------------------------------------|-----------|
| Capture      | libcamera (OV5647)                            | Native Pi OS; no v4l2 legacy |
| Inference    | NCNN + YOLO11n (exported)                     | Pure C++, zero deps, best ARM; Ultralytics-recommended |
| Face detect  | OpenCV FaceDetectorYN (YuNet)                | Turnkey C++ API in objdetect |
| Face embed   | OpenCV FaceRecognizerSF (SFace) DEFAULT; MobileFaceNet(NCNN) option | SFace turnkey; MobileFaceNet saves RAM |
| Distance     | bbox relative-size heuristic (from nightly)   | Cheap; fused with optional proximity later |
| Alerts       | priority arbiter, preemptible queue           | Safety > description ordering |
| TTS          | libpiper (C API) + espeak-ng                  | Keep Piper (INV12); streamed PCM 22.05kHz mono float |
| Audio out    | ALSA PCM -> bluealsa -> BT A2DP buds          | C++ path; avoids shelling out |
| Telemetry    | raw UDP datagram (optional, deferred)         | Zero dependency, no core impact |
| Config/build | CMake + cross-compile on laptop (aarch64)     | Avoid slow/OOM native builds on device |

Model artifacts:
  yolo11n -> model.ncnn.param + model.ncnn.bin   (fp16 and int8 variants, benchmarked)
  yunet.onnx                                     (~0.34 MB)
  sface.onnx                                     (~37 MB)  OR mobilefacenet ncnn (~4 MB)
  voices/es_MX-<medium>.onnx + .json             (prefer low/medium over claude-high)

CMake options:
  LUMINA_FACE_EMBEDDER = sface | mobilefacenet     (default sface)
  LUMINA_ENABLE_PROXIMITY = OFF                    (default OFF; future ON)
  LUMINA_ENABLE_TELEMETRY = OFF                    (default OFF; Day6)
  LUMINA_INFER_PRECISION = fp16 | int8             (default fp16; benchmark int8)
  LUMINA_INFER_SIZE = 320 | 416                    (default 320)

---

## 2. HARDWARE NOTES / CONSTRAINTS

- OV5647 supported via libcamera (camera_auto_detect or dtoverlay=ov5647).
- Max 1080p30, rolling shutter, 1/4" sensor, 130-135° diagonal -> barrel distortion.
  => central-crop ROI for detection/face; optional undistort (stretch).
- Fixed focus: set lens manually for ~0.5-3 m working range.
- IR-CUT auto: color by day, B&W by night. Ensure IR LED board is off/auto in daylight.
- NO proximity/distance sensor in bundle. 4 screw holes are for IR LED board (illuminator).
- Verified environment (INV-025): Raspberry Pi Zero 2 W Rev 1.0; Raspberry Pi OS Lite 64-bit
  Debian 13 "trixie" aarch64; kernel 6.18.x+rpt-rpi-v8; CPU reported BCM2837 = 4x Cortex-A53
  @1GHz; GPU bcm2835-vc4; GCC 14.2.1.
- Cortex-A53 = ARMv8.0-A: NEON yes, NO int8 dot-product (SDOT/UDOT; added ARMv8.2/8.4) and NO
  FP16 arithmetic (added ARMv8.2). int8 gains are mainly bandwidth/size, not arithmetic.
  => benchmark fp16 vs int8; do not assume the dossier's 4x int8 claim.
- 512MB RAM is the binding constraint. Prefer MobileFaceNet if SFace pressures memory.
- BT + WiFi share 2.4GHz antenna -> glitches under interference.

Expected perf (extrapolated from Pi5 NCNN 67ms @640):
  640px ~2-3 FPS ; 416px ~4-6 FPS ; 320px ~6-12 FPS  (adequate with decimation)

---

## 3. RUNTIME ARCHITECTURE

```
[libcamera capture thread] --latest frame (bounded depth 1)--> [NCNN worker @320/416, 5-10 Hz]
        |                                                              |
        |                                                      [fusion + alert arbiter]
        |                                                       (preemptible priority queue)
        +--> [face worker, on-demand / low rate] -------------------->|
                                                                      |
                                        [Piper TTS worker] -> [ALSA/bluealsa sink] -> BT buds
[telemetry UDP thread] (optional, deferred)
[IProximitySensor] (null today; optional future source feeding arbiter: instant obstacle tone + wake gate)
```

Threading rules:
- Bounded queues only; drop stale frames; never block capture on inference.
- Collision/obstacle alerts preempt long descriptions (fix nightly unbounded-queue defect).
- Single sink device assumption (INV6): one bluealsa PCM.

---

## 4. REPO LAYOUT (Lumina-BETA-RPI-2W)

```
CMakeLists.txt
cmake/                     toolchain-aarch64.cmake, FindBlueALSA.cmake
third_party/               ncnn (static), libpiper + onnxruntime (prebuilt/sysroot)
models/
  yolo11n_ncnn/            model.ncnn.param, model.ncnn.bin
  face/                    yunet.onnx, sface.onnx | mobilefacenet.ncnn.*
  voices/                  es_MX-*.onnx, es_MX-*.onnx.json
src/
  main.cpp
  core/                    bounded_queue.hpp, frame.hpp, event.hpp, config.hpp, time.hpp
  capture/camera.{hpp,cpp}                # libcamera OV5647
  vision/detector.{hpp,cpp}               # NCNN YOLO + class subset + ES labels
  vision/face.{hpp,cpp}                   # YuNet + embedder + enroll store
  vision/face_store.{hpp,cpp}             # 3-4 embeddings persisted
  processing/distance.{hpp,cpp}           # bbox heuristic (+ fuse proximity)
  sensors/proximity.hpp                   # IProximitySensor, NullProximitySensor, factory
  sensors/vl53l0x_proximity.{hpp,cpp}     # FUTURE (behind LUMINA_ENABLE_PROXIMITY)
  alerts/arbiter.{hpp,cpp}                # priority, cooldown, preemption
  audio/piper_tts.{hpp,cpp}               # libpiper wrapper
  audio/bluealsa_sink.{hpp,cpp}           # ALSA write to bluealsa
  i18n/es.{hpp,cpp}                       # message catalog
  telemetry/udp.{hpp,cpp}                 # optional
scripts/
  cross_build.sh, export_models.sh, enroll_face.sh, bt_setup.sh
tests/
  bench_fps.cpp, bench_latency.cpp
docs/
  RAW_PLAN.md (this file)
```

---

## 5. MODULAR PROXIMITY (FUTURE-PROOFING, INV11)

Interface (compiles today, used by arbiter; null by default):

```cpp
struct ProximityReading { float meters; bool valid; };

class IProximitySensor {
public:
  virtual ~IProximitySensor() = default;
  virtual bool init() = 0;
  virtual std::optional<ProximityReading> read() = 0;  // nullopt if no data
};

class NullProximitySensor final : public IProximitySensor {
  bool init() override { return true; }
  std::optional<ProximityReading> read() override { return std::nullopt; }
};

std::unique_ptr<IProximitySensor> make_proximity_sensor(const Config&);
```

Uses when a sensor is later added (VL53L0X I2C ~3 USD):
  - instant "obstaculo cerca" tone bypassing vision pipeline (low latency)
  - wake-gate: run NCNN only when something is within range (power/thermal saving)
  - fuse distance: proximity true-meters + bbox heuristic
Behavior when NullProximitySensor: exactly current beta behavior (no gating).

---

## 6. MODEL EXPORT (run on laptop, not Pi)

  pip install ultralytics
  yolo export model=yolo11n.pt format=ncnn imgsz=320
  # optional: ncnnoptimize / int8 quantize using calibration frames
  # download YuNet + SFace from opencv_zoo to models/face/
  # copy es_MX voice .onnx + .json (prefer medium/low) to models/voices/

Class subset (from nightly) + additions: person, dog, cat, chair, table, backpack,
car, bicycle, motorcycle, bus, truck, stairs(optional custom). ES i18n map reused.

---

## 7. BUILD / TOOLCHAIN

- Cross-compile on the laptop with a Debian 13 "trixie" aarch64 sysroot (host and target GCC
  **14.2.1**, matching ABI).
- OpenCV **4.10.0** from the sysroot (objdetect provides FaceDetectorYN + SFace — verified,
  INV-025). A clean sysroot must expose its CMake/pkg-config metadata for `find_package` to work.
- NCNN: official aarch64 prebuilt or cross-build; statically link.
- libpiper: build once, cache binary + espeak-ng-data; link onnxruntime.
- Language baseline is **C++23** (GCC 14.2.1); verify individual C++23 library facilities before use.
- Provide native fallback build for on-device last resort (low -j, zram).

---

## 8. 6-DAY PLAN (hard gate end of Day 2)

Day 0-1: cross toolchain; libcamera capture OV5647; export YOLO11n->NCNN;
         benchmark fp16 vs int8 @320/416 (FPS, RAM, temp).
Day 1-2: vertical slice camera -> NCNN -> Piper ES -> bluealsa buds.
         *** GO/NO-GO GATE ***  (if fail: fallback = hardened Python nightly for demo)
Day 3:   alert arbiter (priority/preemption/cooldown) + i18n + class labels.
Day 4:   face enroll + recognition (YuNet + embedder), ROI crop, threshold tuning.
Day 5:   BT autoconnect at boot, thermal/power soak, end-to-end demo script.
Day 6:   optional UDP telemetry ONLY if all green; else buffer/fallback.

Fallback at any point: demo hardened Python nightly; keep C++ core as WIP.

---

## 9. BLUETOOTH AUDIO SETUP

- Pair + `trust` buds; enable autoconnect (module-switch-on-connect / WirePlumber).
- Disable module-suspend-on-idle (prevents first-word delay/stutter).
- Route Piper PCM to bluealsa PCM; optionally tune `pactl set-port-latency-offset`.
- Expected added latency 150-300ms; acceptable for description, marginal for safety
  (future proximity tone mitigates).

---

## 10. TELEMETRY (DEFERRED, INV9)

- Low-priority thread; stateless UDP datagrams to laptop server.
- No MQTT/extra libs. Never blocks core; disabled unless LUMINA_ENABLE_TELEMETRY=ON.
- Implement only after Day 5.

---

## 11. RISKS

| Risk | Impact | Mitigation |
|------|--------|------------|
| OpenCV/libpiper cross-build time | schedule | sysroot provides OpenCV 4.10; build libpiper once |
| Host/sysroot ABI mismatch | compile | host cross-compiler GCC 14.2.1 matches sysroot (INV-023/025) |
| Sysroot missing CMake/pkg-config metadata | build | verify after clean copy; else set include/lib paths |
| C++23 library gaps in GCC 14.2 | compile | verify a facility before using it (INV-020) |
| A53 int8 underperforms | perf | benchmark fp16; default fp16 |
| 512MB RAM pressure | runtime | MobileFaceNet option; zram; one model resident |
| 135° distortion + fixed focus | accuracy | central ROI; set focus 0.5-3m; optional undistort |
| IR tint at night (B&W/orange) | color features | face path tolerant; no color feature; LED auto/off by day |
| BT latency/glitches | UX/safety | latency tuning; future proximity tone |
| GPL-3.0 (Piper/espeak-ng) | legal (product) | OK for contest; relicense/alternate TTS for product |
| Face mis-ID false accept | trust | conservative cosine threshold; require stable multi-frame match |

---

## 12. OPEN ITEMS

1. Face embedder final: SFace (turnkey) vs MobileFaceNet (low RAM). Default SFace;
   switch if RAM > ~430MB under load.
2. Verify IR LED board switching (GPIO/photoresistor) to control tint/power.
3. Optional undistortion if face accuracy degrades at image edges.
4. Proximity sensor procurement (3-4 weeks) -> flip LUMINA_ENABLE_PROXIMITY.
