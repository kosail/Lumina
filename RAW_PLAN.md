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
- INV11 IR proximity: ACQUIRED (2x VL53L0X; 1 front deployed, 1 spare). Delivered MODULARLY
         behind IProximitySensor with a Null fallback; implemented in Phase C after Day 4.
         Rear sensor deferred to the very end (INV-013/INV-033/INV-075).
- INV12 keep PiperTTS; Spanish only (reuse existing es_MX voice family).

---

## 1. LOCKED STACK

| Layer        | Choice                                        | Rationale |
|--------------|-----------------------------------------------|-----------|
| Capture      | libcamera (OV5647)                            | Native Pi OS; no v4l2 legacy |
| Inference    | NCNN + YOLO11n (exported)                     | Pure C++, zero deps, best ARM; Ultralytics-recommended |
| Face detect  | OpenCV FaceDetectorYN (YuNet)                | Turnkey C++ API in objdetect |
| Face embed   | OpenCV FaceRecognizerSF (SFace) DEFAULT; MobileFaceNet(NCNN) option | SFace turnkey; MobileFaceNet saves RAM |
| Distance     | bbox heuristic + VL53L0X ToF fusion (front) | Cheap; ToF gives true metres at short range (INV-013) |
| Alerts       | priority arbiter, preemptible queue           | Safety > description ordering |
| TTS          | libpiper (C API) + espeak-ng                  | Keep Piper (INV12); streamed PCM 22.05kHz mono float |
| Audio out    | ALSA PCM -> bluealsa -> BT A2DP buds          | C++ path; avoids shelling out |
| Telemetry    | raw UDP datagram (optional, deferred)         | Zero dependency, no core impact |
| Config/build | CMake + cross-compile on laptop (aarch64)     | Avoid slow/OOM native builds on device |

Model artifacts:
  yolo11n -> model.ncnn.param + model.ncnn.bin   (fp16 and int8 variants, benchmarked)
  yunet.onnx                                     (~0.34 MB)
  sface.onnx                                     (~37 MB)  OR mobilefacenet ncnn (~4 MB)
  voices/es_MX-claude-high.onnx + .json          (shipping default; ald-x_low fallback; CHG-0037)

CMake options:
  LUMINA_FACE_EMBEDDER = sface | mobilefacenet     (default sface)
  LUMINA_ENABLE_PROXIMITY = OFF|ON                 (ON for the aarch64 preset; OFF for host)
  LUMINA_ENABLE_TELEMETRY = OFF                    (default OFF; Day6)
  LUMINA_INFER_PRECISION = fp16 | int8             (default fp16; benchmark int8)
  LUMINA_INFER_WIDTH / LUMINA_INFER_HEIGHT = 320 / 256 (default; INV-050)

---

## 2. HARDWARE NOTES / CONSTRAINTS

- OV5647 supported via libcamera (camera_auto_detect or dtoverlay=ov5647).
- Max 1080p30, rolling shutter, 1/4" sensor, 130-135° diagonal -> barrel distortion.
  => central-crop ROI for detection/face; optional undistort (stretch).
- Fixed focus: set lens manually for ~0.5-3 m working range.
- IR-CUT auto: color by day, B&W by night. Ensure IR LED board is off/auto in daylight.
- Proximity: 1x VL53L0X ToF on I2C1 at 0x29 (front), XSHUT on GPIO17 (reset) - see INV-075 and
  docs/PROXIMITY.md. A second VL53L0X is a spare for a future rear sensor (deferred). The 4 screw
  holes are for the IR LED board (illuminator).
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
[IProximitySensor] (1x VL53L0X front; feeds arbiter: instant obstacle alert + wake gate)
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
  core/                    bounded_queue.hpp, frame.hpp, event.hpp, config.hpp, time.hpp, power.{hpp,cpp}
  capture/camera.{hpp,cpp}                # libcamera OV5647
  vision/detector.{hpp,cpp}               # NCNN YOLO + class subset + ES labels
  vision/face.{hpp,cpp}                   # YuNet + embedder + enroll store
  vision/face_store.{hpp,cpp}             # 3-4 embeddings persisted
  processing/distance.{hpp,cpp}           # bbox heuristic + ToF fusion (front)
  sensors/proximity.hpp                   # IProximitySensor, NullProximitySensor, factory
  sensors/vl53l0x_proximity.{hpp,cpp}     # VL53L0X via i2c-dev + XSHUT addresses (Phase C)
  alerts/arbiter.{hpp,cpp}                # priority, cooldown, preemption
  audio/piper_tts.{hpp,cpp}               # libpiper wrapper
  audio/bluealsa_sink.{hpp,cpp}           # ALSA write to bluealsa
  audio/sink_watchdog.{hpp,cpp}           # FR-06.1: wait for the sink, retry, power off
  i18n/es.{hpp,cpp}                       # message catalog
  telemetry/udp.{hpp,cpp}                 # optional
scripts/
  cross_build.sh, export_models.sh, enroll_face.sh, bt_setup.sh, 9-setup_autostart.sh, lumina.service
tests/
  bench_fps.cpp, bench_latency.cpp, test_sink_watchdog.cpp
docs/
  RAW_PLAN.md (this file)
```

---

## 5. MODULAR PROXIMITY (1x VL53L0X front, INV-013/033/075)

Interface (Phase C, planned; Null by default so the host build needs no hardware):

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

Hardware (acquired, wired, verified 2026-09-18; INV-013/INV-075):
  - 1x VL53L0X ToF on I2C1 at 0x29 (front); XSHUT on GPIO17 for reset; model ID 0xEE confirmed.
  - second VL53L0X kept as a spare for a future rear sensor (reserved: XSHUT GPIO27, addr 0x30),
    deferred to the very end (after pending tasks + nice-to-haves + telemetry).
  - read via the kernel i2c-dev interface - no third-party library (INV-022/INV-033).

Uses:
  - instant "obstaculo cerca" alert bypassing the vision pipeline (low latency)
  - wake-gate: run NCNN only when something is within range (power/thermal saving)
  - fuse distance: true ToF metres + bbox heuristic
Behavior when NullProximitySensor: exactly current beta behavior (no gating).

---

## 6. MODEL EXPORT (run on laptop, not Pi)

  pip install ultralytics
  yolo export model=yolo11n.pt format=ncnn imgsz=320
  # optional: ncnnoptimize / int8 quantize using calibration frames
  # download YuNet + SFace from opencv_zoo to models/face/
  # copy es_MX voice .onnx + .json (prefer medium/low) to models/voices/

Class subset (from nightly) + additions: person, dog, cat, chair, table, backpack,
car, bicycle, bus. (Motorcycle and truck are deferred as a nice-to-have; see OOS-09.)
ES i18n map reused.

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
         *** DONE 2026-09-18 (CHG-0044..0058); see docs/PERFORMANCE.md section 11 ***
Day 4:   face enroll + recognition (YuNet + embedder), ROI crop, threshold tuning.
         *** IMPLEMENTED 2026-09-18 (CHG-0059..0065); on-device verification pending ***
Day 5:   BT autoconnect at boot, thermal/power soak, end-to-end demo script.
         Front VL53L0X proximity alert (FR-10) IMPLEMENTED 2026-09-20 (CHG-0074); on-device verify.
Day 6:   optional UDP telemetry ONLY if all green; else buffer/fallback.
Post-Day-4 (optional, Phase C): front VL53L0X proximity alert if the core is green.
Very last (optional): rear VL53L0X sensor + front/rear distinction, after telemetry/nice-to-haves.

Fallback at any point: demo hardened Python nightly; keep C++ core as WIP.

---

## 9. BLUETOOTH AUDIO SETUP  (IMPLEMENTED 2026-09-21, CHG-0077; see docs/BLUETOOTH.md)

- Pair + `trust` buds; enable autoconnect (module-switch-on-connect / WirePlumber).
- Disable module-suspend-on-idle (prevents first-word delay/stutter).
- Route Piper PCM to bluealsa PCM; optionally tune `pactl set-port-latency-offset`.
- Expected added latency 150-300ms; acceptable for description, marginal for safety
  (proximity ToF alert mitigates).
- System layer `scripts/bt_setup.sh`: discover the single paired earbud (no hardcoded
  MAC; `--mac`/`--pair` overrides), trust it, set `[Policy] AutoEnable=true`, optional
  `lumina-bt-connect.service`.
- Runtime: the sink watchdog retries `bluealsa` open() every 3 s, 60 attempts (3 min);
  on exhaustion it powers the device off (or just stops the runtime if not permitted).
  It never crashes on a missing sink (FR-06.1).
- `scripts/9-setup_autostart.sh` + `scripts/lumina.service` autostart the runtime at
  boot (After bluetooth.service; RestartPreventExitStatus=2).

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
| BT latency/glitches | UX/safety | latency tuning; proximity ToF alert (front) |
| GPL-3.0 (Piper/espeak-ng) | legal (product) | OK for contest; relicense/alternate TTS for product |
| Face mis-ID false accept | trust | conservative cosine threshold; require stable multi-frame match |

---

## 12. OPEN ITEMS

1. Face embedder final: SFace (turnkey) vs MobileFaceNet (low RAM). Default SFace;
   switch if RAM > ~430MB under load.
2. Verify IR LED board switching (GPIO/photoresistor) to control tint/power.
3. Optional undistortion if face accuracy degrades at image edges.
4. Proximity: Phase B COMPLETE (front wired, detected at 0x29, model ID 0xEE, XSHUT reset
   verified); Phase C driver IMPLEMENTED (CHG-0074: i2c-dev VL53L0X + Safety alert + FR-02.4
   fusion behind `IProximitySensor`, gated by `LUMINA_ENABLE_PROXIMITY`); on-device verification
   pending. Rear sensor deferred to the very end (after telemetry). See docs/PROXIMITY.md.
5. Nice-to-have (deferred): re-enable motorcycle (COCO 3) and truck (COCO 7) narration — a
   config-only change since their i18n labels are kept dormant (OOS-09, INV-040).
6. WARNING (later stage): live (uncached) Piper synthesis cannot be preempted — libpiper has no
   cancellation and can return a whole utterance as one chunk. Mitigated now by pre-warming
   single-/two-class phrases and capping descriptions at 2 items. Before the IR/proximity path
   (Phase C) can promise immediate feedback, either make the speech path cache-only (render off the
   speech thread), vendor cancellation into libpiper, or add an IR-only immediate tone. See
   docs/PERFORMANCE.md known limitations. A dual-voice fallback (claude-high warmed / ald-xlow
   misses) was measured (~3.4x faster misses, ~40 MB) but is deferred (CHG-0057).
7. Day 4 (face recognition) — IMPLEMENTED (CHG-0059..0067); verified on-device (CHG-0073 photos, CHG-0083 live camera).
   Decisions locked: SFace fp32 + YuNet 2023mar (OpenCV 4.10); cosine threshold 0.363 + best-vs-
   second margin 0.05; 3 stable frames + 30 s/person cooldown; `lumina_enroll` from photos
   (recommended) or live camera, up to K=10 embeddings/person; store `models/face/embeddings.bin`;
   dedicated low-rate face worker thread (single-slot queues) so detection FPS is not regressed
   (INV-031/INV-050); greeting "<nombre> está enfrente" (Description priority, non-preempting,
   pre-warmed per enrolled name).
   - Models: `scripts/8-fetch_face_models.sh` -> `models/face/{yunet,sface}.onnx` (SHA-256 pinned).
   - Code: `vision/face.{hpp,cpp}` (OpenCV) + `vision/face_store.{hpp,cpp}` +
     `vision/image_list.{hpp,cpp}` + `app/face_greeter.*` behind `IFaceRecognizer` (INV-030);
     `Alert.preStabilized` lets a one-shot greeting bypass the arbiter's description stability
     counter.
   - Script: `scripts/enroll_face.sh` (+ `src/tools/enroll_face.cpp`); `--image`/`--images-dir`
     for photos, `--camera` for live capture (needs LUMINA_ENABLE_LIBCAMERA).
   - Verify on-device (2026-09-20, CHG-0073): enrolled David Solís (4 photos) and greeted by name;
     a 7-min soak held RSS 287-288 MB flat, swap 29 MB flat, FPS 3.0-4.3, peak 58.5 °C, with no
     camera timeout. Live **--camera** enrollment verified on-device (CHG-0083, runbook `docs/FACE.md`).
Still open: enroll the remaining people.
   - Deferred fallback if RAM pressure (INV-052): int8bq SFace or MobileFaceNet-on-NCNN.
   - Memory hardening (CHG-0070..0072): downscale YuNet input to `faceDetectionSide` (320); system
     `gpu_mem=32` + zram + `vm.swappiness=10`/`page-cluster=0`/`watermark_boost_factor=0`; and in the
     runtime, back off face inference during the greeting cooldown (`faceCooldownBackoffFactor`),
     commit the cooldown only after the arbiter accepts the greeting, run NCNN on 3 threads, and warm
     both DNNs at load. True working set ~289 MB vs 447 MB usable — keep this margin in mind.
