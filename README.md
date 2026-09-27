# Lúmina — Beta Runtime

> **Archived. Built for the Innovatec 2026 (InnovaTecNM) contest.**
> Lúmina was created for the **Innovatec 2026 (InnovaTecNM)** student innovation contest.
> This repository is **archived** (read-only) and preserved as the final state;
> no further development is planned. Code, docs, and tests remain as a
> complete reference.

Lúmina is a modular processing unit that describes the world out loud. It is attached to any pair of glasses.
A small camera watches the environment, on-device AI makes sense of it, and the result is spoken through bone-conduction
audio, so someone who is blind or has low vision can *hear* what is in front of them. Everything
runs on the device itself: no cloud, no account, no internet connection required.

This repository is the **runtime** that runs on the **Raspberry Pi Zero 2 W**, written from scratch
in **C++23**. The earlier Python proof-of-concept we built (the "nightly") is a separate project and
is **not** part of this codebase.

![The prototype mounted on a pair of glasses](/.github/img/prototype.webp)

---

## What it does

- **Names what it sees, in Spanish** — people, animals and everyday objects, phrased naturally:
  "una persona enfrente.", "una silla enfrente."
- **Warns about obstacles ahead**, and lets a safety warning cut in mid-sentence when something is
  close.
- **Recognizes a few enrolled people** and greets them by name: "María está enfrente."
- **Runs entirely on the device**, in Spanish (`es_MX`). Nothing ever leaves the glasses.

All of the above was verified on a real device. A few numbers we measured: about **4.3 frames per
second** of detection, a spoken alert starting **~250–300 ms** after detection (before the Bluetooth
hop), and around **289 MB** of memory in use with face recognition running.

**What it does not do:** currency recognition, offline navigation, indigenous-language voices,
cloud processing, a desktop GUI, or more than one Bluetooth device at a time.

## How it works

```
[ camera ] --latest frame--> [ object detector ]
     |                             |
     |                       [ alert arbiter ]   <-- decides what to say first
     +--> [ face recognition ] --->|
                                   |
                  [ speech (Piper) ] -> [ Bluetooth earbuds ]
[ front distance sensor ]  (short-range safety alert)
```

A few ideas hold the pipeline together. Hardware is reached only through small interfaces
(`ICamera`, `ITtsEngine`, `IAudioSink`, `IProximitySensor`, …), so the logic can run on a laptop
with fakes. Frames move through bounded queues that drop stale ones instead of piling up. And a
single **alert arbiter** owns the speech queue, so a safety alert can always interrupt a
description.

## Hardware

| Part | What we used |
|------|--------------|
| Board | Raspberry Pi Zero 2 W — quad-core Arm Cortex-A53 @ 1 GHz, 512 MB RAM |
| Camera | OV5647 5 MP, ~135° field of view, auto IR-CUT |
| Audio | Bluetooth A2DP bone-conduction earbuds (a single paired device) |
| Distance sensor | One front VL53L0X time-of-flight sensor (I²C1) |
| Case | Solid aluminum |
| OS | Raspberry Pi OS Lite 64-bit (Debian 13 "trixie"), headless |

## Built with

| Layer | Choice |
|-------|--------|
| Language / build | C++23 (GCC 14), CMake |
| Object detection | [NCNN](https://github.com/Tencent/ncnn) + YOLO11n |
| Camera | libcamera |
| Faces | OpenCV (YuNet + SFace) |
| Speech | [libpiper](https://github.com/OHF-Voice/piper1-gpl) + espeak-ng, Spanish voice |
| Audio out | ALSA → `bluealsa` → BlueZ |
| Distance | VL53L0X over the Linux `i2c-dev` interface |

## Project structure

```
src/
  core/         queues, frame types, config, time, logging
  capture/      the libcamera camera source
  vision/       object detector, face recognition, enrollment store
  processing/   the distance heuristic
  sensors/      the front proximity sensor
  alerts/       the alert arbiter (priority, cooldown, preemption)
  audio/        speech (Piper) and the BlueALSA audio sink
  app/          the pipeline and the Spanish describer
  status/       the local status snapshot the companion agent reads
  agent/        lumina_agent — telemetry + control for the companion app
  i18n/         the Spanish message catalog
scripts/        setup and build helpers (sysroot, NCNN, models, voices, services)
tests/          unit tests and on-device benchmarks
cmake/          the aarch64 cross toolchain
```

## Getting started

### Build on your laptop (no hardware needed)

```bash
cmake --preset host
cmake --build --preset host
ctest --preset host
```

### Cross-build for the Raspberry Pi

Build on a laptop, not on the Pi, as a 512 MB board is slow and can run out of memory. You need a
GCC 14 aarch64 cross toolchain. First pull the target sysroot from the Pi (one time, ~1.4 GB, kept
out of git):

```bash
scripts/1-sync_sysroot.sh --host pi@<pi-host>   # -> cmake/rpi-sysroot/
cmake --preset aarch64                          # configure with the cross toolchain
cmake --build --preset aarch64                  # -> build/aarch64/lumina
```

The full walkthrough — sysroot, symlinks, verification, deployment, troubleshooting, and the Arch
`--sysroot` gotcha — is in [`docs/CROSS_COMPILE.md`](docs/CROSS_COMPILE.md).

### Models and voice

Model files are not committed. Fetch and export them with:

```bash
scripts/3-export_models.sh      # YOLO11n -> NCNN, at the sizes we benchmarked
scripts/4-fetch_onnxruntime.sh  # the aarch64 ONNX Runtime that Piper needs
scripts/6-fetch_voices.sh       # the Spanish (es_MX) Piper voice
scripts/8-fetch_face_models.sh  # the YuNet + SFace face models
```

## Running Lúmina

```bash
./build/aarch64/lumina
```

Audio is routed to the paired earbuds automatically. At startup the runtime looks for the earbuds
and waits for them (up to a few minutes): if they never appear it powers the device off, so the
device never sits there pretending to work, and when power-off isn't permitted, it simply stops.
This was tested with the earbuds both connected and disconnected. See
[`docs/BLUETOOTH.md`](docs/BLUETOOTH.md).

### Start automatically at boot

```bash
scripts/bt_setup.sh              # pair and trust the single earbud
scripts/9-setup_autostart.sh     # install and enable lumina.service
```

A cold boot with the service enabled is ready in about a minute. Most of that is loading the speech
model (~21 s); if the earbuds aren't connected yet you can add ~21 s of waiting, so it's worth
turning them on before (or at) the Pi.

## Configuration

Everything below is optional. The defaults are what the code uses when nothing is set; the systemd
units override a couple of them for the demo.

### Build flags

Passed to CMake at configure time, for example `cmake --preset aarch64 -DLUMINA_ENABLE_FACE=ON`.

| Flag | Default | Description |
|------|---------|-------------|
| `LUMINA_ENABLE_LIBCAMERA` | `OFF` | Build the camera capture path (libcamera). |
| `LUMINA_ENABLE_NCNN` | `OFF` | Build the object detector (NCNN + YOLO11n). |
| `LUMINA_ENABLE_AUDIO` | `OFF` | Build speech (Piper + espeak-ng) and the ALSA/bluealsa output. |
| `LUMINA_ENABLE_FACE` | `OFF` | Build face recognition (OpenCV YuNet + SFace). |
| `LUMINA_ENABLE_PROXIMITY` | `OFF` | Build the front VL53L0X distance sensor. |
| `LUMINA_ENABLE_STATUS` | `ON` | Write the local status snapshot (`/run/lumina/status`). |
| `LUMINA_BUILD_AGENT` | `ON` | Build the companion agent (`lumina_agent`). |
| `LUMINA_ENABLE_TELEMETRY` | `OFF` | Build the older runtime-side UDP telemetry (superseded by the agent). |
| `LUMINA_BUILD_TESTS` | `ON` | Build the host unit tests. The `aarch64` preset sets it `OFF`. |
| `LUMINA_BUILD_BENCH` | `OFF` | Build the on-device benchmark targets. |
| `LUMINA_FACE_EMBEDDER` | `sface` | Face model to use: `sface` or `mobilefacenet`. |
| `LUMINA_INFER_PRECISION` | `fp16` | Inference precision: `fp16` or `int8`. |
| `LUMINA_INFER_WIDTH` / `LUMINA_INFER_HEIGHT` | `320` / `256` | Object-detector input size, in pixels. |
| `LUMINA_NCNN_ROOT` | `third_party/ncnn` | Where the prebuilt NCNN install lives (advanced). |
| `LUMINA_LIBPIPER_ROOT` | `third_party/libpiper` | Where the prebuilt libpiper install lives (advanced). |

> The full runtime needs **`LUMINA_ENABLE_LIBCAMERA`**, **`LUMINA_ENABLE_NCNN`** and
> **`LUMINA_ENABLE_AUDIO`** together . Without them, `lumina` builds a stub that just prints a notice.

### Runtime environment variables

Read when `lumina` starts. Unset variables keep their defaults, and a value that can't be parsed is
ignored (the runtime logs a warning and keeps the default).

| Variable | Default | Description |
|----------|---------|-------------|
| `LUMINA_LOG_LEVEL` | `info` | Log verbosity: `trace`, `debug`, `info`, `warn`, `error` or `off`. |
| `PIPER_NUM_THREADS` | `3` (set by the service) | Threads for speech synthesis / ONNX Runtime. |
| `LUMINA_PHRASE_CACHE_DIR` | `$HOME/.cache/lumina/phrase-cache` | Where the pre-rendered speech cache lives. |
| `LUMINA_STATUS_PATH` | `/run/lumina/status` | Where the runtime writes its status snapshot. |
| `LUMINA_WARM_ONLY` | *(off)* | `1` loads the models, renders any missing phrases, then exits. A quick pre-show cache check. |
| `LUMINA_CAMERA_ROTATION` | `90` | Rotate frames upright: `0`, `90`, `180` or `270` degrees. |
| `LUMINA_PROXIMITY_ENABLED` | `true` | `0`/`false` disables the distance sensor. |
| `LUMINA_AUDIO_SHUTDOWN_ON_FAILURE` | `true` | Power off when the earbuds never appear; `0` just stops the runtime. |
| `LUMINA_AUDIO_SINK_MAX_RETRIES` | `60` | How many times to look for the audio sink (the demo unit sets `10000`). |
| `LUMINA_AUDIO_SINK_RETRY_MS` | `3000` | Delay between those attempts, in ms. |
| `LUMINA_PROXIMITY_THRESHOLD_M` | `0.8` | Distance (m) at or below which the safety alert fires. |
| `LUMINA_PROXIMITY_RELEASE_M` | `1.2` | Distance (m) at which the alert clears (hysteresis). |
| `LUMINA_NEAR_AREA_FRACTION` | `0.20` | Box-size fraction that makes a centered object an imminent obstacle. |
| `LUMINA_MID_AREA_FRACTION` | `0.06` | Mid-distance band; kept for tuning, no longer raises alerts on its own. |
| `LUMINA_PATH_CENTER_TOLERANCE` | `0.25` | How centered an object must be to count as "in the path". |
| `LUMINA_OBSTACLE_CLASSES` | `1,2,5,56,57,60` | COCO class ids treated as obstacles; people (`0`) are always narrated. |
| `ORT_DISABLE_TELEMETRY` | set in-process | Silences ONNX Runtime telemetry; the runtime already sets it itself. |

The `lumina_agent` variables (network addresses, ports and token) are listed in
[The companion app](#the-companion-app).

## The companion app

Lúmina has a phone and desktop app that talks to it over the device's own Wi-Fi hotspot. The
runtime stays off the network: a separate, small program, **`lumina_agent`**, owns every socket. It
reports status once a second, controls volume, lists and enrolls people, and starts or stops the
runtime. The app lives in
[`kosail/Lumina-Companion`](https://github.com/kosail/Lumina-Companion).

```bash
# On the Pi, after deploying lumina and lumina_agent to $LUMINA_HOME:
scripts/10-setup_agent.sh --start
```

The agent's settings are normally set by the setup script:

| Variable | Meaning |
|----------|---------|
| `LUMINA_HOME` | Folder holding `lumina`, `lumina_agent`, `models/`, `third_party/` |
| `LUMINA_AGENT_BIND_ADDR` | Address the control channel listens on (the hotspot gateway, e.g. `10.42.0.1`) |
| `LUMINA_AGENT_BROADCAST` | Where telemetry is broadcast (the hotspot subnet, e.g. `10.42.0.255`) |
| `LUMINA_AGENT_ENROLL_STOP_RUNTIME` | `1` stops the runtime during photo enrollment to free memory |
| `LUMINA_AGENT_TOKEN` | Overrides the token file (`$LUMINA_HOME/agent.token`) |
| `LUMINA_AGENT_BIND_RETRIES` / `LUMINA_AGENT_BIND_RETRY_MS` | How long to keep retrying the control bind while the hotspot comes up |

## Privacy

Lúmina makes **no network or cloud calls**. The one thing that would have broken that is the
prebuilt ONNX Runtime inside Piper, which phones home by default (telemetry to Microsoft Azure servers); the runtime disables it before it
starts, and can be checked with a socket watch:

```bash
strace -f -e trace=connect ./lumina models/yolo11n_ncnn_320x256 \
  models/voices/es_MX-claude-high.onnx espeak-ng-data   # expect no internet connections
```

## Performance and memory

The Pi Zero 2 W is tight on memory — about 447 MB usable with `gpu_mem=32`, and the full runtime
with face recognition sits around **289 MB**. A few things help:

- Set `gpu_mem=32` in `/boot/firmware/config.txt`.
- Use **zram** instead of SD swap (`zram-tools`, `PERCENTAGE=50`).
- Add `/etc/sysctl.d/99-lumina.conf` with `vm.swappiness=10`, `vm.page-cluster=0`,
  `vm.watermark_boost_factor=0`, then `sudo sysctl --system`.
- Disable services you don't need (`avahi-daemon`, `cups`, `triggerhappy`, `ModemManager`), but keep
  `bluetooth` for the earbuds.

Measured numbers and the design trade-offs behind them live in
[`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).

## Enrolling people

Enroll each person on the Pi, one named person per run, from photos (recommended) or the live
camera:

```bash
# Cross-build with the face path (add LIBCAMERA only if you want --camera)
cmake --preset aarch64 -DLUMINA_ENABLE_FACE=ON -DLUMINA_ENABLE_NCNN=ON \
      -DLUMINA_ENABLE_AUDIO=ON -DLUMINA_ENABLE_LIBCAMERA=ON
cmake --build --preset aarch64

# On the Pi, from the deploy dir (~/lumina):
scripts/enroll_face.sh "María" photos/Maria         # every photo in a folder
scripts/enroll_face.sh "Juan"  juan1.jpg juan2.jpg  # explicit photo files
scripts/enroll_face.sh "Ana"   --camera             # live camera capture
```

Enrollment writes `models/face/embeddings.bin` (up to 10 embeddings per person) and reloads on the
next boot. Embeddings and photos stay on the device. For best results, give each person **3–5
varied, front-facing, well-lit photos**. The runtime then greets a recognized person with
"<nombre> está enfrente". The live-camera path needs the runtime stopped first (it holds the
camera); full details are in [`docs/FACE.md`](docs/FACE.md).

## Known limitations

- **A brand-new, uncached sentence can't be interrupted.** The speech engine has no cancellation and
  can return a whole utterance at once, so a sentence we didn't pre-render may briefly delay a
  safety alert. We mitigate it by pre-rendering the fixed vocabulary and keeping descriptions short.
- **Only the front distance sensor** is present; the rear one was deferred.
- **No physical volume buttons** (deferred on purpose).
- **One Bluetooth audio device** at a time.
- **Face recognition** targets a small set of people (~3–4).
- **The camera is physically mounted rotated**; the software rotates the frames back upright.
- **The device is modest** — about 4.3 FPS of detection, by design for this board.
- **Models, the cross sysroot, and `third_party/` builds are not committed** — the scripts and
  [`docs/CROSS_COMPILE.md`](docs/CROSS_COMPILE.md) rebuild them.

## The contest

Lúmina was built for the **Innovatec 2026 (InnovaTecNM)** contest. We reached to the **regional
stage**, but sadly we were not chosen and the project **did not advance**. This repository is archived as the final state and as a
reference; the companion client is archived separately in [Lumina Companion](https://github.com/kosail/Lumina-Companion).

Likely, I will never work on this repository again.

## Documentation

If you want the deeper detail, these are the places to look:

- [`docs/PI_RUNBOOK.md`](docs/PI_RUNBOOK.md) — bring up a real device from scratch.
- [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) — measured numbers and the reasoning behind them.
- [`docs/COMPANION.md`](docs/COMPANION.md) and [`docs/API_CONTRACT.md`](docs/API_CONTRACT.md) — the
  companion app and the protocol it speaks.
- [`docs/CROSS_COMPILE.md`](docs/CROSS_COMPILE.md) — the cross-build walkthrough.
- [`docs/FACE.md`](docs/FACE.md) — enrollment details.
- [`docs/PROXIMITY.md`](docs/PROXIMITY.md) and [`docs/BLUETOOTH.md`](docs/BLUETOOTH.md) — sensors
  and audio.
- [`docs/DEFERRED.md`](docs/DEFERRED.md) — what we deliberately chose not to build.
- [`INVARIANTS.md`](INVARIANTS.md), [`SPECS.md`](SPECS.md), [`RAW_PLAN.md`](RAW_PLAN.md) and
  [`AGENTS.md`](AGENTS.md) — the engineering notes and rules we worked under.

## Contributing

This project is **archived and no longer maintained**, so pull requests are unlikely to be
reviewed. Even so, if you're here to learn from it or to reuse parts of it, this is how we worked
on it:

1. **Read the docs first.** Start with [`docs/PI_RUNBOOK.md`](docs/PI_RUNBOOK.md) to bring up a
   device, and [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) for the measured numbers and the
   trade-offs behind the design.
2. **Build and test locally before anything else.** The host build needs no hardware:

   ```bash
   cmake --preset host && cmake --build --preset host && ctest --preset host
   ```

3. **Keep the architecture.** Hardware is reached only through interfaces (`ICamera`, `ITtsEngine`,
   `IAudioSink`, `IProximitySensor`, …) that are injected at startup, so everything stays testable
   with fakes.
4. **Comment the *why*, not the *what*.** This codebase is written to be readable by someone coming
   back to C++ after a long time.
5. **No cloud, no network in the core path.** Lúmina is meant to work fully offline.
6. **If you fork it, keep it GPLv3.**

## License

The **Lúmina projects are licensed under the GNU General Public License version 3 (GPLv3)**. See
[`LICENSE`](LICENSE).

Lúmina itself is GPLv3; **third-party components keep their own licenses** and are used under their
own terms:

- **Piper / `piper1-gpl`** and **espeak-ng** — GPL-3.0 (consistent with Lúmina's own GPLv3).
- **NCNN** — BSD-3-Clause.
- **OpenCV** — Apache-2.0; the **YuNet** (MIT) and **SFace** (Apache-2.0) face models.
- **YOLO11n** weights — Ultralytics license (not committed; verify before redistribution).
- The companion app's dependencies keep their own licenses (see `https://github.com/kosail/Lumina-Companion`).

---

## Copyleft notice

© 2026 kosail, from the Lúmina Team. Lúmina is free software, licensed under the **GNU GPL v3.0 (copyleft)** — see
[`LICENSE`](LICENSE). Third-party components keep their own licenses.

With love, from Honduras. Mi país cinco estrellas.
