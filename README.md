# Lúmina — Beta Runtime

Lúmina is an edge-AI assistive device for blind and low-vision users. A camera observes the
environment; on-device AI interprets it; the result is spoken through bone-conduction audio.
Everything runs locally — no cloud, no internet connection required.

This repository is the **real runtime** for the **Raspberry Pi Zero 2 W**, written in **C++23**.
It is a from-scratch build; the original Python proof-of-concept ("nightly") is a separate
prototype and is **not** part of this codebase.

> Status: **active beta development.** The runtime is not production-ready. Interfaces, options,
> and commands in this README describe the intended shape and may change as implementation lands.

---

## Current scope (beta)

- Detect people, animals, and objects and narrate them in Spanish.
- Prioritized obstacle alerts; safety alerts preempt spoken descriptions.
- Face recognition for **3–4 enrolled people**, announced by name.
- Fully on-device and Spanish-first (`es_MX`).

**Out of scope for the beta:** currency recognition, offline navigation, indigenous-language
support, cloud processing, desktop/GUI, and multi-device Bluetooth.

## Target hardware

| Component | Specification |
|-----------|---------------|
| Board | Raspberry Pi Zero 2 W Rev 1.0 — reported BCM2837, quad-core Arm Cortex-A53 @ 1 GHz, 512 MB LPDDR2 |
| Camera | OV5647 5 MP, ~135° diagonal, auto IR-CUT (75/175), fixed manual focus (via libcamera) |
| Audio | Bluetooth A2DP bone-conduction earbuds (single paired device) |
| Proximity | 1× front VL53L0X time-of-flight (I²C1 `0x29`, XSHUT on GPIO17); second unit spare, rear deferred — see `docs/PROXIMITY.md` |
| Enclosure | Solid aluminum case (thermal headroom is comfortable) |
| OS | Raspberry Pi OS Lite 64-bit based on Debian 13 "trixie" (glibc, aarch64; kernel 6.18.x), headless |

## Stack

| Layer | Choice | Notes |
|-------|--------|-------|
| Language / build | C++23 (GCC 14.2.1), CMake >= 3.20 | Some C++23 library facilities may lag; verify before use |
| Inference | [NCNN](https://github.com/Tencent/ncnn) + YOLO11n | Pure C++, best ARM CPU performance |
| Capture | libcamera | OV5647 sensor |
| Face | OpenCV `objdetect` (YuNet + SFace) | MobileFaceNet/NCNN is the low-RAM alternative |
| TTS | [libpiper](https://github.com/OHF-Voice/piper1-gpl) + espeak-ng | Spanish (`es_MX`) voice, streamed PCM |
| Audio out | ALSA -> `bluealsa` -> BlueZ | Single A2DP sink |
| Proximity | VL53L0X over Linux `i2c-dev` | No third-party library; front zone alert |
| Telemetry | UDP (optional, off by default) | Deferred; never touches the core path |

## Architecture

```
[libcamera capture] --latest frame--> [NCNN detector @320/416, 5-10 Hz]
        |                                        |
        |                                  [alert arbiter]      <-- priorities + preemption
        +--> [face worker, on demand] ------>|
                                             |
                       [Piper TTS] -> [ALSA/bluealsa sink] -> Bluetooth earbuds
[telemetry UDP]  (optional, disabled by default)
[IProximitySensor] (1x front VL53L0X; short-range alert; see docs/PROXIMITY.md)
```

Design rules: interface-first with dependency injection, bounded queues that drop stale frames,
and a single interruptible speech queue. See `AGENTS.md` and `INVARIANTS.md`.

## Repository layout

```
src/
  core/         bounded queue, frame/event types, config, time, result, logging
  capture/      libcamera camera source (ICamera)
  vision/       NCNN object detector, face detection/recognition, enrollment store
  processing/   distance heuristic
  sensors/      IProximitySensor + NullProximitySensor + VL53L0X (front, I2C1)
  alerts/       alert arbiter (priority, cooldown, preemption)
  audio/        Piper TTS wrapper (ITtsEngine), BlueALSA audio sink (IAudioSink)
  app/          pipeline orchestrator + Spanish describer
  status/       local status snapshot hook (FR-11; runtime side, no network)
  agent/        companion telemetry/control agent (FR-11; owns the network)
  i18n/         Spanish message catalog
  telemetry/    optional UDP telemetry (superseded by agent/; kept for history)
models/         yolo11n_ncnn/, face/, voices/
scripts/        1-sync_sysroot.sh, 2-build_ncnn.sh, 3-export_models.sh, 4-fetch_onnxruntime.sh, 5-build_libpiper.sh, 6-fetch_voices.sh, 7-setup_i2c.sh, 8-fetch_face_models.sh, enroll_face.sh, bt_setup.sh, 9-setup_autostart.sh, 10-setup_agent.sh (+ lumina.service, lumina-agent.service)
tests/          unit tests (doctest) and on-device benchmarks
cmake/          aarch64 toolchain and find-modules
```

## Documentation map

| File | Role |
|------|------|
| [`INVARIANTS.md`](INVARIANTS.md) | Highest source of truth — non-negotiable facts and decisions |
| [`SPECS.md`](SPECS.md) | Structured business requirements |
| [`RAW_PLAN.md`](RAW_PLAN.md) | Execution schedule and stack rationale |
| [`AGENTS.md`](AGENTS.md) | Operating manual: style, workflow, documentation rules |
| [`CHANGELOG.md`](CHANGELOG.md) | Append-only, machine-readable change log |
| [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) | Measured detection performance, resolution decision, fallback plan |
| [`docs/COMPANION.md`](docs/COMPANION.md) | Companion app ecosystem: architecture, agent, probe results |
| [`docs/APP_PROTOCOL.md`](docs/APP_PROTOCOL.md) | Frozen wire protocol for the Android/desktop client (FR-11) |

Source-of-truth order: `INVARIANTS.md` > `SPECS.md` > `RAW_PLAN.md` > `AGENTS.md` > code.

## Build

### Host build (fast feedback, no hardware required)

```bash
cmake --preset host
cmake --build --preset host
ctest --preset host
```

### Cross build for the Raspberry Pi

First time only, build the target sysroot from the Pi (needs `rsync` + `ssh`; ~1.4 GB, gitignored):

```bash
scripts/1-sync_sysroot.sh --host pi@<pi-host>   # -> cmake/rpi-sysroot/
```

Then, on the laptop:

```bash
cmake --preset aarch64          # configure with cmake/toolchain-aarch64.cmake
cmake --build --preset aarch64  # produces build/aarch64/lumina
```

Requires a GCC 14 aarch64 cross toolchain. Build on the laptop, not on the device: native builds on
a 512 MB board are slow and can run out of memory. Full walkthrough (extraction, placement,
symlinks, verification, deployment, troubleshooting, and the Arch `--sysroot` gotcha):
[`docs/CROSS_COMPILE.md`](docs/CROSS_COMPILE.md).

### Models

Model files are not committed. Export/download them with:

```bash
scripts/3-export_models.sh      # YOLO11n -> NCNN (320x256 + comparison sizes)
scripts/4-fetch_onnxruntime.sh  # official prebuilt ONNX Runtime (aarch64) for libpiper
scripts/6-fetch_voices.sh       # pinned es_MX Piper voice (Spanish TTS)
scripts/8-fetch_face_models.sh  # YuNet + SFace face models (OpenCV, SHA-256 pinned)
```

## Configuration

Compile-time options (defaults shown):

| Option | Default | Meaning |
|--------|---------|---------|
| `LUMINA_FACE_EMBEDDER` | `sface` | Face embedding model (`sface` or `mobilefacenet`) |
| `LUMINA_INFER_PRECISION` | `fp16` | Inference precision (`fp16` or `int8`, benchmark-gated) |
| `LUMINA_INFER_WIDTH` / `LUMINA_INFER_HEIGHT` | `320` / `256` | Detector input size (see `docs/PERFORMANCE.md`) |
| `LUMINA_ENABLE_LIBCAMERA` | `OFF` | Build the libcamera capture path |
| `LUMINA_ENABLE_NCNN` | `OFF` | Build the NCNN detector path |
| `LUMINA_ENABLE_AUDIO` | `OFF` | Build the Piper TTS + ALSA/bluealsa path |
| `LUMINA_ENABLE_FACE` | `OFF` | Build the OpenCV face recognition path (YuNet + SFace) |
| `LUMINA_ENABLE_PROXIMITY` | `OFF` | Build the VL53L0X proximity path (`ON` in the aarch64 preset) |
| `LUMINA_ENABLE_TELEMETRY` | `OFF` | Build the optional UDP telemetry path (superseded by `LUMINA_BUILD_AGENT`) |
| `LUMINA_ENABLE_STATUS` | `ON` | Runtime writes the local status snapshot `/run/lumina/status` (~1 Hz; no network) |
| `LUMINA_BUILD_AGENT` | `ON` | Build `lumina_agent` (companion telemetry + control; FR-11) |

## Run

```bash
./build/aarch64/lumina
```

Audio is routed to the paired bone-conduction earbuds automatically at startup. The runtime waits
up to 3 minutes for the BlueALSA sink before giving up (FR-06.1): if it never appears it powers the
device off, or just stops the runtime when power-off is not permitted. Verified on-device
(CHG-0081) with the earbuds both connected and disconnected. See `docs/BLUETOOTH.md`.

### Autostart at boot

Install and enable the systemd service (also adds the narrow power-off sudoers rule):

```bash
scripts/bt_setup.sh              # pair/trust the single earbud (system layer)
scripts/9-setup_autostart.sh     # install + enable lumina.service
```

First cold boot with the service enabled: ready in **~60 s**. Roughly 18 s is boot-to-start, ~21 s is
model load (Piper dominates), and — if the earbuds are not connected yet — up to ~21 s more is the
watchdog waiting for the BlueALSA sink. Power the earbuds on before (or at) the Pi to avoid that wait
(~40 s total). Measured 2026-09-20; see `docs/PERFORMANCE.md` §14.

Runtime environment variables (all optional):

| Variable | Meaning |
|----------|---------|
| `LUMINA_LOG_LEVEL` | `trace`/`debug`/`info`/`warn`/`error`/`off` (default `info`) |
| `PIPER_NUM_THREADS` | Piper/ONNX Runtime intra-op threads (default 3) |
| `LUMINA_PHRASE_CACHE_DIR` | On-disk TTS phrase cache directory (default `$HOME/.cache/lumina/phrase-cache`) |
| `ORT_DISABLE_TELEMETRY` | Set to `1` to silence ONNX Runtime telemetry. **Privacy: the runtime sets this in-process before ONNX Runtime initializes (CHG-0080); you do not need to export it, and Lúmina never phones home (INV-003/INV-034).** |

### Companion app (FR-11)

The runtime reports its status to an Android/desktop companion app over the Pi's own Wi-Fi hotspot.
The runtime stays **network-free**: it only writes `/run/lumina/status` at ~1 Hz. A separate,
opt-in **`lumina_agent`** process owns every socket — a 1 Hz UDP status broadcast (`:47600`) and a
token-gated TCP control channel (`:47601`) for volume/mute, the enrolled-people list, runtime
start/stop, and enrollment (Pi camera or phone photos). Wire contract: `docs/APP_PROTOCOL.md`;
architecture and operations: `docs/COMPANION.md`.

```bash
# On the Pi (after deploying `lumina` and `lumina_agent` to $LUMINA_HOME):
scripts/10-setup_agent.sh --start   # /run/lumina + token + sudoers + lumina-agent.service
```

Agent environment variables (set by the setup script / the unit):

| Variable | Meaning |
|----------|---------|
| `LUMINA_HOME` | Directory holding `lumina`, `lumina_agent`, `models/`, `third_party/` |
| `LUMINA_AGENT_BIND_ADDR` | Local address for TCP control (the hotspot gateway, e.g. `10.42.0.1`) |
| `LUMINA_AGENT_BROADCAST` | Telemetry broadcast target (the hotspot subnet, e.g. `10.42.0.255`) |
| `LUMINA_AGENT_ENROLL_STOP_RUNTIME` | `1` stops the runtime during image enrollment (frees RAM) |
| `LUMINA_AGENT_TOKEN` | Overrides the token file (`$LUMINA_HOME/agent.token`) |

### Privacy / no network

Lúmina performs **no cloud/network calls** (INV-003, INV-034). The one thing that would have
violated this is the official ONNX Runtime prebuilt behind libpiper, which ships telemetry that
POSTs to `mobile.events.data.microsoft.com/OneCollector/1.0/`. The runtime disables it by setting
`ORT_DISABLE_TELEMETRY=1` before ONNX Runtime initializes (`src/audio/piper_tts.cpp`), and
`lumina.service` also sets it explicitly. Verify with a packet/socket watch:

```bash
strace -f -e trace=connect ./lumina models/yolo11n_ncnn_320x256 \
  models/voices/es_MX-claude-high.onnx espeak-ng-data   # expect no AF_INET connects
```

**Verified on-device (CHG-0081):** with only the in-process `setenv` (no launcher export),
`strace -e trace=connect` showed **no AF_INET connects**, so the code-level disable is sufficient.
The `lumina.service` `Environment=` line is kept as belt-and-braces.

### Memory on the Pi Zero 2 W

The board is memory-bound: with `gpu_mem=32` it has only ~447 MB usable RAM, and the runtime with
face recognition settles around ~289 MB RSS. Face detection is downscaled to `faceDetectionSide`
(default 320) and face inference backs off while a person is on greeting cooldown; see
`docs/PERFORMANCE.md` §12.

Recommended system tuning:
- Set `gpu_mem=32` in `/boot/firmware/config.txt` (frees RAM for the CPU side).
- Use **zram** instead of SD swap (`zram-tools`, `PERCENTAGE=50`); confirm `swapon --show` lists only
  `/dev/zram0`.
- Add `/etc/sysctl.d/99-lumina.conf` with `vm.swappiness=10`, `vm.page-cluster=0`,
  `vm.watermark_boost_factor=0`, then `sudo sysctl --system`.
- Disable unneeded services (`avahi-daemon`, `cups`, `triggerhappy`, `ModemManager`); keep
  `bluetooth` for the earbuds.

## Testing and benchmarks

- Unit tests (host, mocks only): `ctest --preset host`
- On-device performance: `tests/bench_fps`, `tests/bench_latency`
- Record FPS, latency, and resident memory in `CHANGELOG.md`.

## Roadmap

1. Runtime skeleton, capture, detector, TTS-to-Bluetooth vertical slice (Day-2 gate). — **done**
2. Alert arbiter + Spanish alert catalog (Day 3). — **done** (see `docs/PERFORMANCE.md` §11)
3. Face enrollment and recognition (Day 4). — **implemented; on-device verification pending**
4. Boot-time Bluetooth autoconnect, soak testing, demo hardening. — **BT autoconnect + autostart
   implemented** (`scripts/bt_setup.sh`, `scripts/9-setup_autostart.sh`, runtime sink watchdog;
   see `docs/BLUETOOTH.md`, CHG-0077). The sink-watchdog `running`/`stop` polarity was fixed in
   CHG-0079 (it previously skipped the wait and exited 0 immediately).
5. Optional UDP telemetry (only after everything else passes).
6. Front VL53L0X proximity alert — **implemented** (FR-10, `-DLUMINA_ENABLE_PROXIMITY=ON`, CHG-0074); rear sensor deferred to the very end (`docs/PROXIMITY.md`).

## Face enrollment (FR-04)

Build with the face path and enroll each person on the Pi (one named person per run).
Enrollment accepts **photos** (recommended) or the **live camera**:

```bash
# Cross-build with the face path (add LIBCAMERA only if you want --camera)
cmake --preset aarch64 -DLUMINA_ENABLE_FACE=ON -DLUMINA_ENABLE_NCNN=ON \
      -DLUMINA_ENABLE_AUDIO=ON -DLUMINA_ENABLE_LIBCAMERA=ON
cmake --build --preset aarch64

# On the Pi, from the deploy dir (~/lumina), one run per person:
scripts/enroll_face.sh "María" photos/Maria        # all photos in a folder
scripts/enroll_face.sh "Juan"  juan1.jpg juan2.jpg # explicit photo files
scripts/enroll_face.sh "Ana"   --camera            # live camera capture
```

Enrollment writes `models/face/embeddings.bin` (up to 10 embeddings per person) and
reloads automatically on the next boot. Embeddings and photos stay on the device
(FR-03.4). For best results give each person **3–5 varied, front-facing, well-lit
photos**. The runtime greets a recognized person with "<nombre> está enfrente".

The **live `--camera` path was verified on-device (2026-09-21, CHG-0083)**: stop the runtime first
(it holds the camera), enroll, then restart so the greeting is pre-warmed. Full runbook,
verification steps and troubleshooting: **`docs/FACE.md`**.

## License and third-party notices

Project source in this repository is provided for the InnovaTecNM contest beta.
Third-party components keep their own licenses:

- **Piper / `piper1-gpl`** and **espeak-ng** are **GPL-3.0**. Acceptable for the beta, but this is
  a productization consideration (see `INVARIANTS.md` → INV-060).
- **NCNN** is BSD-3-Clause; **OpenCV** is Apache-2.0; **YOLO11n** weights follow the Ultralytics
  license (verify before commercial use).

## Contributing

Before contributing, read `AGENTS.md` and `INVARIANTS.md`.

- **Never assume.** Consult the latest official documentation before using an API or hardware
  behavior. If a lookup fails, or confidence is below 0.80, ask. (INV-001)
- **Never silently change an invariant.** Ask first; log the outcome.
- Keep changes modular, decoupled, testable, and **fully commented**.
- Append an entry to `CHANGELOG.md` for every meaningful change.
