# INVARIANTS.md — Lúmina Beta Runtime

> **Rank: 1 (HIGHEST).** This file outranks `SPECS.md`, `RAW_PLAN.md`, `AGENTS.md`,
> any code comment, and any suggestion made by an AI agent or a human contributor.
>
> The entries below are facts and decisions that **MUST NOT be violated or silently changed**.
> They are the foundation of the project architecture.

---

## 0. How to read this file

Each invariant has:

- **ID** — stable identifier, never reused.
- **Severity** — one of `CORE`, `HARD`, `SCOPE`, `TARGET`, `PROCESS` (see legend).
- **Statement** — the rule itself.
- **Rationale** — *why* it exists (so a future agent does not "optimize it away").
- **Source** — where it came from (user instruction, hardware datasheet, RAW_PLAN, research).
- **Changeability** — what it would take to change it.

### Severity legend

| Severity  | Meaning |
|-----------|---------|
| `CORE`    | Meta-rule that governs **all** work and every other invariant. |
| `HARD`    | Platform/hardware/stack fact. Changing it means changing hardware or the runtime stack. |
| `SCOPE`   | Beta deliverable decision. Changing it changes what is shipped on deadline. |
| `TARGET`  | Measurable goal. Deviation is allowed only if reported and approved. |
| `PROCESS` | Workflow rule for agents. |

### Change protocol (mandatory)

1. **Never silently change an invariant.** If a task appears to require it, **STOP and ask the user**.
2. Any accepted change requires: explicit human approval **and** an append-only entry in `CHANGELOG.md`.
3. Changed invariants are never rewritten in place without leaving a trace — supersede with a new
   ID or an explicit "amended by CHG-XXXX" note.
4. If two invariants appear to conflict, **stop and ask the user**; do not pick one.

---

## 1. CORE invariants (meta-rules)

### INV-001 — NEVER ASSUME  🟥 `CORE`

**Statement.** Never assume anything.
- Always consult the **latest official documentation online** before using an API, library,
  file format, model format, or hardware behavior.
- If the documentation lookup **fails**, or confidence in an answer/decision is **below `0.80`**,
  **STOP and ask the user**. Do not guess, do not "try it and see", do not proceed on memory.

**Rationale.** This project targets constrained, unusual hardware (RPi Zero 2W) with fast-moving
libraries (NCNN, libcamera, OpenCV, Piper). Assumptions here are the single largest source of
wasted days and silent breakage. A confidently wrong assumption is worse than an explicit question.

**Operationalization.**
- External facts must be backed by a source: **URL + access date**, recorded in comments/docs.
- Self-assessed confidence must be explicit when it is not obvious from a cited source.
- Asking is always allowed and never counts as failure.

**Source.** Direct user instruction (2026-09-15).

**Changeability.** Not changeable without the user explicitly revoking the instruction.

---

### INV-002 — Invariants outrank everything  🟥 `CORE`

**Statement.** This file is the highest source of truth. No agent, plan, requirement, or code
may contradict it. Where sources conflict, this file wins.

**Rationale.** Without a single immutable foundation, architecture drifts silently between agents.

**Source.** User instruction defining this file's role.

**Changeability.** Only via the change protocol above.

---

### INV-003 — On-device and offline by default  🟥 `CORE`

**Statement.** All core functionality runs locally on the device with **no network dependency**.
No cloud calls, no external services, no telemetry unless `LUMINA_ENABLE_TELEMETRY=ON` and the
user has explicitly requested it. Privacy of the user's camera stream is preserved by design.

**Rationale.** Core product promise (edge AI, privacy, works without connectivity) and the
dossier's central differentiator.

**Source.** `AGENTS.md` project brief / `RAW_PLAN.md`.

**Changeability.** Requires user approval; contradicts the product's core value proposition.

---

## 2. HARDWARE invariants

### INV-010 — Target hardware is the Raspberry Pi Zero 2 W  `HARD`

**Statement.** The runtime must run well on the **Raspberry Pi Zero 2 W Rev 1.0**: SoC reported by
the OS as **Broadcom BCM2837** (vendor SiP **RP3A0 / BCM2710A1** die; both are **quad-core 64-bit
Arm Cortex-A53 @ 1 GHz**), **512 MB LPDDR2**, GPU **VideoCore IV** (`bcm2835-vc4`; no general-purpose
GPU compute), CSI-2 camera, single USB OTG, microSD, no analog audio.

**Rationale.** This is the physical device already owned and demonstrated. Full verified details in
`INV-025`.

**Source.** User-verified hardware report (2026-09-16) + `RPI_2W_SPECSHEET.txt`.

**Changeability.** Not without different hardware.

---

### INV-011 — Camera is an OV5647 5 MP, 135°, IR-CUT  `HARD`

**Statement.** Capture uses the **OV5647** sensor via **libcamera**: 5 MP, 1/4", 130–135°
diagonal, **auto IR-CUT (75/175)**, **fixed manually-adjustable focus**. Wide-angle barrel
distortion is expected; detection/face work on a **central ROI** unless undistortion is added.

**Rationale.** This is the purchased camera; it is the only camera.

**Source.** User-provided seller specification (2026-09-15).

**Changeability.** Not without different hardware.

---

### INV-012 — Cortex-A53 has NO INT8 dot-product acceleration  `HARD`

**Statement.** The A53 implements **ARMv8.0-A**: NEON is present, but the integer dot-product
instructions **SDOT/UDOT** are **not** (they were added in later profiles, ARMv8.2-A/8.4-A), and
full **FP16 arithmetic** is likewise absent (added as an optional extension in ARMv8.2-A; on A53
FP16 is a *storage* format only). Consequences:
- INT8 quantization must **not** be assumed to give large arithmetic speedups; the benefit is
  mainly a smaller model and reduced memory bandwidth.
- "FP16" on this CPU means FP16 **storage** with fp32 arithmetic.
- **Default precision is FP16**, and INT8 is adopted only if on-device benchmarks prove a win.

**Rationale.** Prevents repeating the dossier's speculative "4× INT8" claim as if it were fact.
The dot-product and FP16-processing extensions post-date ARMv8.0-A and are not present on the A53.

**Source.** Wikipedia "ARM Cortex-A53" (ARMv8-A; BCM2837 and Raspberry Pi Zero 2 W both listed
under Cortex-A53) and "AArch64" (ARMv8.4-A adds SDOT/UDOT; ARMv8.2-A adds optional half-precision
*processing*); `RAW_PLAN.md` §2; user-verified CPU report (2026-09-16).

**Deferred (CHG-0018).** INT8 was formally deferred by user decision (2026-09-16): the FP16 path
already meets INV-050 at the approved input, and the A53 offers no arithmetic benefit, so the
benchmark is off the beta critical path. This does not change the policy above — INT8 remains
benchmark-gated and may be revisited after the Day-2 gate if memory/size pressure appears.

**Changeability.** Not changeable (hardware); the precision choice is measured, not assumed.

---

### INV-013 — One VL53L0X proximity sensor is present (front)  `SCOPE`

**Statement.** The hardware bundle includes **one VL53L0X time-of-flight proximity sensor**,
mounted facing **forward** and connected over **I²C1** at its default address `0x29`. A **second
VL53L0X is held as a spare** for a future **rear** sensor; that rear sensor is **fully deferred**
until every pending task, nice-to-have, and telemetry option is done. Proximity **remains
modular** behind `IProximitySensor` (with `NullProximitySensor` as the no-hardware fallback). The
chip is **confirmed VL53L0X on-device** (I²C model ID register `0xC0` → `0xEE`, verified
2026-09-18); the breakout silkscreen reads "VL53L0/1XV2" but the part is VL53L0X (INV-001).

**Rationale.** The IR module above the camera is only an illuminator + IR-CUT filter and cannot
measure distance; even a single front ToF sensor closes the "curbs/steps/poles are not COCO
classes" gap identified in `docs/PERFORMANCE.md` §5 and enables a low-latency, vision-independent
safety alert. The rear sensor is deferred to protect the schedule. See `RAW_PLAN.md` §5 and
`docs/PROXIMITY.md`.

**Source.** User confirmation (2026-09-18): two VL53L0X (8-pin) acquired, all confirmed VL53L0X
by model ID; user then reduced scope to **front only** and deferred the rear to the very end.

**Changeability.** Hardware scope; by user approval. Implementation is toggled by
`LUMINA_ENABLE_PROXIMITY` (see INV-033); the pin/address contract is INV-075.

---

### INV-014 — Exactly one Bluetooth audio device  `HARD`

**Statement.** The **only** Bluetooth device ever connected is the bone-conduction earbuds,
already paired, output via **A2DP**. The runtime must assume a single sink and must not manage
other Bluetooth devices.

**Rationale.** Simplifies audio routing and matches the physical setup.

**Source.** User instruction (invariant 6).

**Changeability.** By user approval.

---

### INV-015 — Thermal headroom is not a concern  `HARD`

**Statement.** The Pi is in a solid aluminum case and observed ~30 °C under normal load. Thermal
throttling is **not** the primary constraint; **memory bandwidth and CPU** are. Do not spend time
on exotic cooling; still avoid needless sustained load.

**Rationale.** Directs engineering effort to the real bottlenecks.

**Source.** User instruction (invariant 7).

**Changeability.** By user approval.

---

## 3. PLATFORM / BUILD invariants

### INV-020 — Language is C++23  `HARD`

**Statement.** Production code is **C++23**, built with the target toolchain (GCC **14.2.1** on
Debian 13 "trixie"; see `INV-025`). CMake ≥ 3.20.

**Rationale.** The target and the host cross-compiler are GCC 14.2.1, which supports C++23; the user
selected C++23. Some individual C++23 library facilities may still be incomplete in GCC 14.2 —
verify availability before depending on a specific one (INV-001). See `RAW_PLAN.md` §7.

**Source.** User instruction (2026-09-16) + `INV-025`.

**Changeability.** Requires user approval.

---

### INV-021 — Headless operation  `HARD`

**Statement.** The runtime is **headless**: no desktop, no GUI, no OpenCV HighGUI window.
Debug rendering, if any, is behind a compile-time flag and must not run in the shipped path.

**Rationale.** Frees RAM/CPU on a 512 MB device; the nightly's dual-panel UI is a pitch artifact.

**Source.** `RAW_PLAN.md`; `FIRST_IMPRESSIONS_ON_NIGHTLY_VERSION.md`.

**OS specifics.** Defined in `INV-024` (added by CHG-0004).

**Changeability.** By user approval.

---

### INV-022 — Fixed runtime stack  `HARD`

**Statement.** The runtime uses only: **NCNN** (inference), **libcamera** (capture), **OpenCV**
(`objdetect`: YuNet + SFace; image ops), **libpiper + espeak-ng** (TTS), **ALSA → bluealsa**
(audio), **BlueZ** (Bluetooth). Adding any **new runtime dependency** requires explicit approval.
Proximity reads the kernel **`i2c-dev`** interface directly, which is not a new library (INV-033).

**Rationale.** Each new dependency costs build time, RAM, and licensing review on a 6-day budget.

**Source.** `RAW_PLAN.md` §1.

**Changeability.** By user approval.

---

### INV-023 — Cross-compile; no heavy native builds  `PROCESS`

**Statement.** Build on the laptop with an **aarch64 sysroot** (Raspberry Pi OS / Debian 13
"trixie", GCC 14). Do **not** run heavy native builds on the Zero 2W (512 MB causes OOM and takes
hours); native build is a last-resort fallback only. The host cross-compiler must match the sysroot
ABI (**GCC 14.2.1** confirmed: `aarch64-linux-gnu-g++`).

**Rationale.** Build time is the project's #1 schedule risk. A compiler/sysroot ABI mismatch (e.g.
linking the wrong `libstdc++.so.6`) is a common cross-compile failure.

**Source.** `RAW_PLAN.md` §7 / §11; user-verified toolchain report (2026-09-16).

**Changeability.** By user approval.

---

### INV-024 — Target OS is Raspberry Pi OS Lite 64-bit  `HARD`

**Statement.** The runtime target is **Raspberry Pi OS Lite 64-bit based on Debian 13 "trixie"**
(glibc, aarch64; kernel **6.18.x+rpt-rpi-v8**). **DietPi 64-bit** is the only permitted
alternative, and only as a **post-gate optimization** (after the end-of-Day-2 GO/NO-GO gate) if
memory pressure requires it. **Alpine/musl is explicitly rejected** for the beta.

**Rationale.** The OV5647 ISP/libcamera stack and the glibc-only `onnxruntime` that `libpiper`
links are the reference-supported path on Pi OS; Alpine/musl risks camera and TTS bring-up and
would require rebuilding dependencies — unacceptable on the 6-day budget (INV-071). DietPi is
Debian/glibc over the Raspberry Pi kernel, so it stays compatible while trimming background
services (helps INV-052). The verified target environment is recorded in `INV-025`. See CHG-0004
and CHG-0007.

**Source.** User decision (2026-09-15).

**Changeability.** Requires user approval; changes the cross-compile sysroot and CHANGELOG.

---

### INV-025 — Verified target environment (ground truth)  `HARD`

**Statement.** The following is the verified environment for the beta target and must be treated as
ground truth when making build/performance decisions:

| Item | Verified value |
|------|----------------|
| Board | Raspberry Pi Zero 2 W **Rev 1.0** |
| OS | Raspberry Pi OS (reports as **Debian GNU/Linux 13 "trixie"**), **aarch64** |
| Kernel | **6.18.50+rpt-rpi-v8** (64-bit) |
| CPU | reported **Broadcom BCM2837**, **4 cores @ 1.00 GHz** (Arm **Cortex-A53**, ARMv8.0-A) |
| GPU | **Broadcom bcm2835-vc4** (VideoCore IV; no general-purpose GPU compute) |
| RAM | 512 MB (vendor spec sheet) |
| Proximity | **1× VL53L0X** ToF on I²C1 (front, `0x29`); model ID `0xEE` **confirmed**; second unit spare (rear deferred) — INV-013, INV-075 |
| Host cross-compiler | `aarch64-linux-gnu-g++` **GCC 14.2.1 20250405** (Arch package; built-in sysroot `/usr/aarch64-linux-gnu`, `lib64` layout) |
| Host gotcha | The Arch cross toolchain **ignores `--sysroot` for library search**; the toolchain file must pass explicit `-L`/`-Wl,--sysroot`/`-rpath-link` (see `CHG-0008`, `docs/CROSS_COMPILE.md`) |
| Sysroot provenance | rsync `pi:/usr` -> `cmake/rpi-sysroot/usr`, plus merged-`/usr` symlinks; built/refreshed by `scripts/1-sync_sysroot.sh` (gitignored, ~1.4 GB) |
| Target toolchain (sysroot) | GCC **14** (`usr/include/c++/14`) |
| OpenCV | **4.10.0**; `objdetect/face.hpp` provides `FaceDetectorYN` + `FaceRecognizerSF` |
| libcamera | **0.7** (`libcamera.so.0.7`) |

**Rationale.** Centralizes the facts several other invariants depend on (INV-010, INV-012, INV-020,
INV-023, INV-024) so a future agent does not re-derive or assume them (INV-001). Note the CPU-label
discrepancy: the OS reports **BCM2837** while the vendor spec sheet names the SiP **RP3A0 /
BCM2710A1**; both are quad Arm Cortex-A53, so the ISA conclusions are identical.

**Source.** User-provided observations from the actual device (2026-09-16) plus inspection of the
cross sysroot.

**Changeability.** Update only with new device/sysroot observations (human-approved).

---

## 4. ARCHITECTURE invariants

### INV-030 — Interface-first and dependency injection  `HARD`

**Statement.** Hardware and external systems are accessed only through interfaces
(`ICamera`, `IDetector`, `IFaceRecognizer`, `ITtsEngine`, `IAudioSink`, `IProximitySensor`).
Concrete implementations are injected. Logic must be unit-testable with mocks and **no hardware**.

**Rationale.** Testability, decoupling, and host-side development without the Pi.

**Source.** User instruction (modular/testable/decoupled).

**Changeability.** By user approval.

---

### INV-031 — Bounded queues; never block capture  `HARD`

**Statement.** Inter-thread communication uses **bounded** queues. Capture must never block on
inference; stale frames are dropped (depth 1 for the inference handoff).

**Rationale.** Real-time behavior on a slow CPU; mirrors and fixes the nightly's architecture.

**Source.** `RAW_PLAN.md` §3.

**Changeability.** By user approval.

---

### INV-032 — Safety alerts preempt descriptions  `HARD`

**Statement.** The alert arbiter orders by safety priority; an obstacle/collision alert
preempts an in-progress spoken description. Speech is interruptible.

**Rationale.** Latency of a safety warning is a physical-safety concern, not a UX nicety.
Fixes the nightly's unbounded-queue / non-interruptible-TTS defect.

**Source.** `FIRST_IMPRESSIONS_ON_NIGHTLY_VERSION.md` §9; `RAW_PLAN.md` §3.

**Changeability.** By user approval.

---

### INV-033 — Proximity is modular and enabled on target  `HARD`

**Statement.** Proximity is delivered through `IProximitySensor` (+ `NullProximitySensor` and a
factory) with a **VL53L0X implementation**, behind `LUMINA_ENABLE_PROXIMITY` (**ON** for the
aarch64 preset, **OFF** for the host test build). With the null sensor the runtime behaves exactly
as the no-proximity beta. The driver uses the Linux **`i2c-dev`** interface directly — **no
third-party library** (INV-022) — and reads the single **front** sensor at its default address
`0x29`; `XSHUT` (GPIO17) is used only to reset it. The pin contract is INV-075. The rear sensor
and any front/rear distinction are **deferred** (INV-013).

**Rationale.** Forward compatibility (INV-013) with a real safety feature, without adding a
dependency or scope creep. See `RAW_PLAN.md` §5 and `docs/PROXIMITY.md`.

**Source.** User instruction (2026-09-18); `RAW_PLAN.md` §5.

**Changeability.** Toggle the CMake option; by user approval.

**Implementation status.** Planned (Phase C, after Day 4) — tracked in `CHANGELOG.md`. The
`src/sensors/` scaffolding mentioned by earlier revisions did not exist in code as of
2026-09-18; it is created in that phase.

---

### INV-034 — Telemetry is deferred, optional, and off the core path  `HARD`

**Statement.** Telemetry is `OFF` by default (`LUMINA_ENABLE_TELEMETRY=OFF`). If enabled, it runs
in a low-priority thread using stateless UDP datagrams, never blocking detection, alerts, or
audio. It is implemented only after all core features pass (Day 6).

**Rationale.** Must not regress core runtime performance; user's explicit condition.

**Source.** User instruction (invariant 9); `RAW_PLAN.md` §10.

**Changeability.** By user approval.

**Amendment (CHG-0084, 2026-09-21).** The deferred telemetry is realized as an optional **companion
API** (FR-11). The runtime core stays network-free: it only writes a local status snapshot
(`LUMINA_ENABLE_STATUS`, default ON, ~1 Hz, low priority; a local file is not telemetry). A separate,
opt-in **`lumina_agent`** process owns the entire network surface — a 1 Hz UDP status broadcast and a
**token-gated, LAN-only TCP control channel** (volume/mute, people list, start/stop, enrollment). The
inbound control channel is **new scope beyond one-way telemetry** and was explicitly approved by the
user. It must never block the core path and must not be reachable off the local hotspot. Because the
runtime itself still issues no network calls, **INV-003 is unaffected**. Implementation plan and
protocol: `docs/APP_PROTOCOL.md`, `docs/COMPANION.md`.

---

### INV-075 — Front proximity I²C/GPIO contract  `HARD`

**Statement.** The single **front** VL53L0X uses **I²C1** at its default address `0x29` and is
**polled** (not interrupt-driven). The fixed map is:

| Signal | Front sensor | Raspberry Pi Zero 2 W |
|--------|--------------|------------------------|
| SDA | SDA | GPIO2 = physical pin 3 |
| SCL | SCL | GPIO3 = physical pin 5 |
| XSHUT | XSHUT | GPIO17 = physical pin 11 (reset line) |
| VIN | VIN | 3.3 V = pin 1 (verify board rating) |
| GND | GND | pin 6 |
| GPIO1 | unused | (optional interrupt, later) |
| I²C address | `0x29` (default) | — |

`XSHUT` is **active-low** reset; the driver may pulse it to recover a hung sensor. No address
reassignment is needed for one sensor. A future **rear** sensor has a reserved entry (`XSHUT`
GPIO27 = pin 13, address `0x30` after an `XSHUT`-sequenced reassignment) but is deferred
(INV-013) and not wired now. The camera (CSI ribbon) does not use these pins.

**Rationale.** Removes wiring guesswork for the one sensor deployed now; the rear reservation
avoids re-deriving the dual-sensor scheme later. See `docs/PROXIMITY.md`.

**Source.** User decision (2026-09-18); VL53L0X datasheet (ST) and Raspberry Pi GPIO pinout
(docs consulted per INV-001).

**Changeability.** By user approval; changing it updates `docs/PROXIMITY.md` and `CHANGELOG.md`.

---

## 5. FEATURE SCOPE invariants

### INV-040 — Beta features are fixed  `SCOPE`

**Statement.** The beta delivers: (1) object/animal/person detection narrated in Spanish;
(2) prioritized obstacle alerts; (3) **face recognition of 3–4 enrolled people**. A fourth feature
— **(4) a front VL53L0X proximity alert** (INV-013, INV-033, INV-075) — is in scope but strictly
**after** the core three and only if time remains (planned after Day 4). A **rear** proximity
sensor (and any front/rear distinction) is **out of scope** and deferred until after every pending
task, nice-to-have, and the telemetry option. Nothing else is in scope for Day 6.

**Rationale.** 6-day deadline with a mandatory GO/NO-GO gate; scope control is survival.

**Source.** User instruction (invariant 3); `RAW_PLAN.md` §8.

**Changeability.** By user approval.

---

### INV-041 — Face recognition, NOT currency; no navigation  `SCOPE`

**Statement.** The single extra feature is **face recognition**. Currency recognition and offline
navigation are explicitly **out of scope** for the beta. Indigenous-language support is post-beta.

**Rationale.** Currency needs a custom dataset and is degraded by IR/color handling; navigation
and indigenous-language NLP do not fit the deadline. See `RAW_PLAN.md` §5 / research.

**Source.** User decision (2026-09-15).

**Changeability.** By user approval.

---

### INV-042 — Spanish only  `SCOPE`

**Statement.** All speech output and commands are **Spanish (es_MX)** using the existing Piper
voice family. No other locale is shipped in the beta.

**Rationale.** The product is Spanish-first by design; matches the existing assets.

**Source.** User instruction (invariant 12).

**Changeability.** By user approval.

---

## 6. PERFORMANCE / LATENCY invariants

### INV-050 — Inference throughput  `TARGET`

**Statement.** Detection runs at **≥ 5 FPS** at 320 px (or 416 px) input on the target device,
with inference decimated (not every frame).

**Rationale.** Minimum viable guidance rate for walking; derived from `RAW_PLAN.md` §2 estimates.

**Source.** `RAW_PLAN.md` §2/§8.

**Changeability.** Report and get approval if the measured best is lower.

**Amended by CHG-0014 (2026-09-16).** Verified on the Pi Zero 2 W: YOLO11n/NCNN
measures **4.33 FPS at 320×320** and **2.73 FPS at 416×416** (4 threads), below the
target. The approved detection input is therefore **320×256** (320 px wide), which
keeps the same effective 320×240 resolution while removing letterbox padding (≈20 %
less compute). The **≥ 5 FPS rule is unchanged**, and 320×256 **meets it: 5.27 FPS**
(189.8 ms, 4 threads, measured 2026‑09‑16). Escalation order if the end-to-end pipeline
drops below it: `256×256`, then NanoDet‑Plus (post‑gate, INV‑022 approval). The 3‑thread
configuration measures **4.87 FPS (< 5)**, so the shipping inference thread count is **4**,
with **decimation** providing CPU headroom for capture and audio; `256×256` (~5.85 FPS at
3 threads) is the fallback. Full evidence and analysis: `docs/PERFORMANCE.md`.

**End-to-end measurement (CHG-0035/0036, 2026‑09‑16).** The full pipeline (libcamera + inference +
Piper + bluealsa, 320×256, 4 threads) runs at **~4.0–4.3 FPS under co-load**, below the ≥ 5 FPS
target, while the isolated inference benchmark still measures 5.27 FPS. This gap is **temporarily
accepted by user decision (CHG-0036)** to protect the 6-day schedule; the **≥ 5 FPS rule is
unchanged** and is revisited only if time remains after the core MVP (Days 3–5). Escalation is
unchanged: **decimation** → `256×256` → NanoDet‑Plus. Evidence: `docs/PERFORMANCE.md` §10.

---

### INV-051 — End-to-end spoken-alert latency  `TARGET`

**Statement.** Target **< 600 ms** from event to audible alert (camera → inference → arbiter →
TTS → Bluetooth). Bluetooth alone is expected to add ~150–300 ms.

**Rationale.** Safety usability; `RAW_PLAN.md` §3/§9.

**Source.** `RAW_PLAN.md`.

**Changeability.** Report and get approval.

---

### INV-052 — Memory budget  `TARGET`

**Statement.** Resident set stays **under ~450 MB** (512 MB total). Prefer the low-RAM face
embedder (MobileFaceNet) over SFace if pressure appears.

**Rationale.** 512 MB is the binding constraint.

**Source.** `RAW_PLAN.md` §1/§11.

**Changeability.** Report and get approval.

---

### INV-053 — Startup readiness  `TARGET`

**Statement.** From power-on to "ready" (models loaded, Bluetooth audio connected) within
**~60 s** cold (~40 s when the earbuds are already connected at boot).

**Rationale.** Demo and field usability.

**Source.** `RAW_PLAN.md` (boot-time BT autoconnect requirement).

**Changeability.** Report and get approval.

**Amendment.** Amended by **CHG-0082** (2026-09-21). The original **~30 s** was an initial estimate;
the first measured cold boot with autostart is **~62 s**, dominated by Piper model load (~14.5 s) and
the earbuds' own reconnect wait (~21.5 s). The user accepted ~62 s as good for the beta and no
further boot optimization is planned. See `docs/PERFORMANCE.md` section 14.

---

## 7. PROCESS invariants

### INV-070 — Every meaningful change is logged  `PROCESS`

**Statement.** Every design decision and meaningful code change appends an entry to
`CHANGELOG.md` in the mandated YAML-block format. The log is **append-only**; history is never
rewritten, only superseded.

**Rationale.** Lets future agents reconstruct *why* without re-reading the repo.

**Source.** User instruction defining `CHANGELOG.md`.

**Changeability.** By user approval.

---

### INV-071 — Deadline and gate  `PROCESS`

**Statement.** The beta has a **6-day** budget (competition in 7). There is a mandatory
**GO/NO-GO gate at the end of Day 2**: camera → NCNN → Piper → Bluetooth must work end-to-end.
If it fails, the fallback is the hardened Python nightly for the demo.

**Rationale.** Prevents betting the demo on an unfinished runtime.

**Source.** User instruction (invariant 5); `RAW_PLAN.md` §8.

**Changeability.** By user approval.

---

### INV-072 — Modular, testable, decoupled, fully commented  `PROCESS`

**Statement.** Code is modular, easy to test, and easy to decouple. **Everything is commented.**
The primary maintainer is a Java developer returning to C++ after ~2 years, so comments must also
explain C++-specific constructs (pointers, RAII/ownership, move semantics, header/source split)
where they differ from Java.

**Rationale.** Explicit user requirement; maintainability by a small team on a deadline.

**Source.** User instruction (2026-09-15).

**Changeability.** By user approval.

---

## 8. Index

| ID       | Severity | Title |
|----------|----------|-------|
| INV-001  | CORE     | NEVER ASSUME |
| INV-002  | CORE     | Invariants outrank everything |
| INV-003  | CORE     | On-device and offline by default |
| INV-010  | HARD     | Target hardware is the RPi Zero 2 W |
| INV-011  | HARD     | Camera is OV5647 5 MP, 135°, IR-CUT |
| INV-012  | HARD     | Cortex-A53 has no INT8 dot-product |
| INV-013  | SCOPE    | One VL53L0X proximity sensor present (front; rear deferred) |
| INV-014  | HARD     | Exactly one Bluetooth audio device |
| INV-015  | HARD     | Thermal headroom is not a concern |
| INV-020  | HARD     | Language is C++23 |
| INV-021  | HARD     | Headless operation |
| INV-022  | HARD     | Fixed runtime stack |
| INV-023  | PROCESS  | Cross-compile; no heavy native builds |
| INV-024  | HARD     | Target OS is Raspberry Pi OS Lite 64-bit (trixie) |
| INV-025  | HARD     | Verified target environment (ground truth) |
| INV-030  | HARD     | Interface-first and dependency injection |
| INV-031  | HARD     | Bounded queues; never block capture |
| INV-032  | HARD     | Safety alerts preempt descriptions |
| INV-033  | HARD     | Proximity is modular and enabled on target |
| INV-034  | HARD     | Telemetry optional/off core path; companion API amended CHG-0084 |
| INV-040  | SCOPE    | Beta features are fixed |
| INV-041  | SCOPE    | Face recognition, NOT currency; no navigation |
| INV-042  | SCOPE    | Spanish only |
| INV-050  | TARGET   | Inference throughput ≥ 5 FPS @320/416 |
| INV-051  | TARGET   | Spoken-alert latency < 600 ms |
| INV-052  | TARGET   | Memory < ~450 MB |
| INV-053  | TARGET   | Startup ready in ~60 s (amended CHG-0082) |
| INV-070  | PROCESS  | Every meaningful change is logged |
| INV-071  | PROCESS  | 6-day deadline and Day-2 gate |
| INV-072  | PROCESS  | Modular, testable, decoupled, commented |
| INV-075  | HARD     | Front proximity I²C/GPIO contract |
