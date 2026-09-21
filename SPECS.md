# SPECS.md — Lúmina Beta Runtime Requirements

> **Rank: 2.** Requirements are subordinate to `INVARIANTS.md` (rank 1) and must never contradict
> it. If a requirement here conflicts with an invariant, the invariant wins and this file is wrong.
>
> Scope of this document: the **beta runtime** that runs on the Raspberry Pi Zero 2 W. It does not
> cover the higher-level contest deliverables (full technical report, full business model) except
> where they impose a requirement on the runtime.

---

## 0. Conventions

- **Requirement IDs** are stable and never reused: `FR-xx` (functional), `NFR-xx`
  (non-functional), `CON-xx` (constraint), `OOS-xx` (out of scope).
- **Priority** uses MoSCoW: `MUST`, `SHOULD`, `COULD`, `WON'T (this beta)`.
- **Acceptance criteria** are written so they can be tested objectively.
- **Traceability** maps each requirement to the invariant(s) it derives from.

### Status legend (to be updated as work progresses)

`Proposed` → `Approved` → `In progress` → `Implemented` → `Verified` / `Blocked`.
All requirements start as `Approved` (the plan was approved by the user on 2026-09-15).

---

## 1. Functional requirements

### FR-01 — Detect people, animals, and objects, and narrate them in Spanish
- **Priority:** MUST · **Status:** Approved · **Trace:** INV-040, INV-042
- **Description:** Using the CSI camera, run on-device object detection (YOLO11n on NCNN) over a
  configured subset of classes. Produce Spanish descriptions of relevant detections.
- **Acceptance criteria:**
  1. On the target device, detection runs at ≥ 5 FPS at the configured input size (INV-050).
  2. Person, and at least the nightly's classes (chair, table, backpack, dog, cat) are recognized
     in a live demonstration.
  3. Descriptions are spoken in Spanish via Piper.
  4. No network access is required at any point.

### FR-02 — Prioritized obstacle proximity alerts
- **Priority:** MUST · **Status:** Approved · **Trace:** INV-032, INV-051
- **Description:** When a detected obstacle is classified as `cerca` (near) and centered in the
  user's path, emit a Spanish warning through the alert arbiter, respecting a cooldown to avoid
  repetition.
- **Acceptance criteria:**
  1. A near, path-centered obstacle produces a spoken alert within the latency target (INV-051).
  2. Repeated frames do not spam: a per-alert cooldown is enforced.
  3. A safety alert preempts a longer descriptive utterance in progress.
  4. Distance is fused from the true front VL53L0X proximity reading (INV-013) and the
     bbox-size heuristic, with proximity taking precedence at short range.

### FR-03 — Face recognition of 3–4 enrolled people
- **Priority:** MUST · **Status:** Approved · **Trace:** INV-040, INV-041
- **Description:** Detect faces (YuNet) and recognize a small set of enrolled people using
  on-device embeddings (SFace by default, or MobileFaceNet), speaking the person's name in Spanish.
- **Verified:** OpenCV **4.10.0** in the target sysroot provides `FaceDetectorYN` and
  `FaceRecognizerSF` in `opencv2/objdetect/face.hpp` (INV-025).
- **Acceptance criteria:**
  1. Supports enrolling between 3 and 4 people.
  2. Recognizes an enrolled person in a live demo with a conservative false-accept threshold; a
     stable multi-frame match is required before announcing a name.
  3. A non-enrolled person is not confidently announced as a known person.
  4. All embeddings are stored locally; no biometric data leaves the device.

### FR-04 — Face enrollment flow
- **Priority:** MUST · **Status:** Approved · **Trace:** INV-040
- **Description:** Provide a simple, scriptable enrollment procedure that captures several frames
  per person, computes embeddings, and persists them under a name.
- **Acceptance criteria:**
  1. `scripts/enroll_face.sh` (or equivalent) enrolls one named person in a single run.
  2. Enrollment data is persisted and reloaded on subsequent boots.
  3. Enrolling N people does not require recompilation.
- **Verified on-device (2026-09-21, CHG-0083):** `scripts/enroll_face.sh "<name>" --camera` captured
  and persisted embeddings; the runtime reloaded the store on restart and greeted the enrolled person
  by name. Runbook: `docs/FACE.md`.

### FR-05 — Spanish text-to-speech (Piper)
- **Priority:** MUST · **Status:** Approved · **Trace:** INV-042, INV-022
- **Description:** Use libpiper with an es_MX voice to synthesize speech on-device and stream PCM
  to the audio sink.
- **Acceptance criteria:**
  1. Speech is intelligible Spanish (es_MX voice).
  2. Synthesis is streamed (chunked), not fully buffered before playback.
  3. A safety alert can interrupt ongoing speech (see FR-02).

### FR-06 — Bluetooth A2DP audio output to the single paired device
- **Priority:** MUST · **Status:** Implemented · **Trace:** INV-014
- **Description:** Route all audio to the bone-conduction earbuds over A2DP via bluealsa, using the
  already-paired device.
- **Acceptance criteria:**
  1. Audio plays through the earbuds after boot without manual intervention.
  2. The runtime does not attempt to manage any other Bluetooth device.
  3. First-word latency after idle is acceptable (suspend-on-idle disabled — see `RAW_PLAN.md` §9).
- **Implementation (CHG-0077):** `scripts/bt_setup.sh` pairs/trusts the single device;
  `scripts/lumina.service` autostarts the runtime; `src/audio/sink_watchdog.*` retries the
  `bluealsa` sink every 3 s (60 attempts = 3 min) and then powers the device off (or stops the
  runtime if power-off is not permitted). See `docs/BLUETOOTH.md`.

### FR-07 — Preemptible, priority-ordered speech queue
- **Priority:** MUST · **Status:** Approved · **Trace:** INV-032
- **Description:** A single arbiter owns all speech; alerts have priorities and the queue is
  bounded. Descriptions yield to safety alerts.
- **Acceptance criteria:**
  1. Higher-priority alert starts within the latency target even if lower-priority speech is active.
  2. The queue never grows without bound.
  3. Cooldown/dedup prevents repeating identical alerts too often.

### FR-08 — Configurable detection classes and Spanish labels
- **Priority:** SHOULD · **Status:** Approved · **Trace:** INV-040, INV-042
- **Description:** Class subset and their Spanish labels are data/config-driven, not hard-coded in
  logic (reuse the nightly's i18n vocabulary).
- **Acceptance criteria:**
  1. Adding/removing a class requires only a config/catalog edit.
  2. Labels are externalized in `src/i18n/es.*` (no literals in pipeline logic).

### FR-09 — Optional device status telemetry to a laptop
- **Priority:** COULD (deferred) · **Status:** Proposed · **Trace:** INV-034, INV-003
- **Description:** If time remains after core features pass (Day 6), send lightweight device status
  to a local laptop over UDP; the laptop relays to a mobile app.
- **Acceptance criteria:**
  1. Compiled out by default (`LUMINA_ENABLE_TELEMETRY=OFF`).
  2. Never blocks the core path; runs in a low-priority thread.
  3. Zero new runtime dependencies beyond the OS socket API.
  4. Enabling it does not measurably change FPS or alert latency.

### FR-10 — Front proximity obstacle alert (rear deferred)
- **Priority:** SHOULD (after the core three; Phase C, planned post-Day-4) · **Status:** Implemented
  (CHG-0074; on-device verification pending) ·
  **Trace:** INV-013, INV-033, INV-075
- **Description:** Read the single **front** VL53L0X time-of-flight sensor over I²C1 and speak a
  short Spanish alert when an obstacle is closer than a configured threshold. Routed through the
  alert arbiter (priority/cooldown/preemption, FR-02/FR-07). A **rear** sensor and any front/rear
  distinction are **deferred** to the very end of the project (INV-013/INV-040).
- **Acceptance criteria:**
  1. With `LUMINA_ENABLE_PROXIMITY=OFF` or no hardware, behavior is identical to the current beta
     (`NullProximitySensor`).
  2. The front sensor is read at `0x29` (INV-075) and reports plausible distances (roughly
     30–2000 mm); `XSHUT` (GPIO17) can reset a hung sensor.
  3. A front obstacle within threshold speaks a short Spanish proximity phrase.
  4. Proximity alerts are not spammed (cooldown) and can preempt a description (INV-032).
  5. No proximity assumption leaks into modules that should not depend on it (interface-only).

---

## 2. Non-functional requirements

### NFR-01 — Performance
- **Priority:** MUST · **Trace:** INV-050
- Detection throughput ≥ 5 FPS at 320/416 px on the target device, with inference decimated
  (not every captured frame). Measured with `tests/bench_fps`.

### NFR-02 — Latency
- **Priority:** MUST · **Trace:** INV-051
- Event-to-audible-alert target < 600 ms (including ~150–300 ms Bluetooth). Measured with
  `tests/bench_latency`.

### NFR-03 — Memory
- **Priority:** MUST · **Trace:** INV-052
- Resident memory < ~450 MB during a 10-minute run. If exceeded, switch the face embedder to
  MobileFaceNet.

### NFR-04 — Startup
- **Priority:** SHOULD · **Trace:** INV-053
- Ready (models loaded, Bluetooth audio connected) within ~60 s of power-on; ~40 s when the earbuds
  are already connected at boot. *(Amended by CHG-0082: the initial ~30 s was an estimate; the first
  measured cold boot is ~62 s — user accepted.)*

### NFR-05 — Reliability
- **Priority:** MUST · **Trace:** INV-031, INV-032
- No crash and no unbounded memory growth during a 1-hour soak at load. A failed camera/sensor
  must degrade gracefully (logs + defined behavior), not crash the runtime.

### NFR-06 — Privacy
- **Priority:** MUST · **Trace:** INV-003, INV-034
- No camera data, face embeddings, or audio leave the device by default. Telemetry (if enabled)
  contains no raw media or biometric identifiers.

### NFR-07 — Maintainability / modularity / testability
- **Priority:** MUST · **Trace:** INV-030, INV-072
- Interface-first design with DI; pure logic unit-tested on the host with mocks; no global mutable
  state; fully commented for a Java developer. Enforced by `AGENTS.md` §5–§9.

### NFR-08 — Compatibility
- **Priority:** MUST · **Trace:** INV-020, INV-021, INV-022, INV-023
- Builds as C++23 (GCC 14.2.1) with CMake ≥ 3.20; runs headless on Raspberry Pi OS Lite 64-bit
  (Debian 13 "trixie"); cross-compiled from the laptop; uses only the fixed runtime stack.

---

## 3. Constraints

| ID | Constraint | Trace |
|----|------------|-------|
| CON-01 | 6 working days; competition in 7; hard GO/NO-GO gate at end of Day 2. | INV-071 |
| CON-02 | Must be C++23 on the Raspberry Pi Zero 2 W (512 MB, Cortex-A53). | INV-010, INV-020 |
| CON-03 | No cloud, no network dependency in the core path. | INV-003 |
| CON-04 | Exactly one Bluetooth output device (bone-conduction earbuds). | INV-014 |
| CON-05 | Camera is the OV5647 135° IR-CUT module via libcamera. | INV-011 |
| CON-06 | One front VL53L0X ToF on I²C1 (model ID `0xEE` confirmed); `XSHUT` on GPIO17; rear sensor deferred. | INV-013, INV-075 |
| CON-07 | Piper/espeak-ng are GPL-3.0 (accepted for beta). | INV-060 |

---

## 4. Out of scope (this beta)

| ID | Item | Why | Trace |
|----|------|-----|-------|
| OOS-01 | Currency/banknote recognition | Needs custom dataset; degraded by IR/color handling; higher 6-day risk. | INV-041 |
| OOS-02 | Offline navigation / GPS | Explicitly excluded by user. | INV-008→INV-041 |
| OOS-03 | Indigenous-language support (Náhuatl, etc.) | Post-beta. | INV-041 |
| OOS-04 | Cloud processing or remote inference | Violates offline/edge premise. | INV-003 |
| OOS-05 | Desktop GUI / OpenCV HighGUI | Headless only. | INV-021 |
| OOS-06 | Multi-device Bluetooth management | Single sink only. | INV-014 |
| OOS-07 | Subscription/B2B/B2G backend | Business layer, not the runtime beta. | — |
| OOS-08 | Mobile app and laptop relay server | Vibe-coded separately; only the UDP hook is in scope (FR-09). | INV-034 |
| OOS-09 | Motorcycle and truck narration | Removed from the beta class set and deferred as a nice-to-have; re-enabling is config-only (FR-08). | INV-040, INV-041 |

---

## 5. Beta acceptance scenario (demo)

A single, repeatable end-to-end demonstration that proves the beta:

1. **Boot:** power on the device; within ~30 s it is ready and audio is routed to the earbuds.
2. **Obstacle:** place a person/object ~1–2 m ahead and slightly centered; the system announces it
   in Spanish within the latency target, and does not repeat continuously.
3. **Description:** present several known classes (e.g., dog, chair, backpack); the system narrates
   them in Spanish.
4. **Face recognition:** present an enrolled person; the system greets them by name after a stable
   multi-frame match. Present a non-enrolled person; no false name is announced.
5. **Preemption:** while a long description is playing, trigger an obstacle; the safety alert
   interrupts the description.
6. **Offline:** repeat the above with Wi-Fi/network disabled to prove on-device operation.

The demo passes if steps 2, 4, and 5 succeed, steps 1 and 3 behave as specified, and step 6
confirms no network dependency.

---

## 6. Traceability summary

| Requirement | Invariants |
|-------------|------------|
| FR-01 | INV-040, INV-042, INV-050 |
| FR-02 | INV-032, INV-051 |
| FR-03 | INV-040, INV-041 |
| FR-04 | INV-040 |
| FR-05 | INV-042, INV-022 |
| FR-06 | INV-014 |
| FR-07 | INV-032 |
| FR-08 | INV-040, INV-042 |
| FR-09 | INV-034, INV-003 |
| FR-10 | INV-013, INV-033, INV-075 |
| NFR-01..05 | INV-050..053, INV-031, INV-032 |
| NFR-06 | INV-003, INV-034 |
| NFR-07 | INV-030, INV-072 |
| NFR-08 | INV-020..023 |
| CON-01..07 | INV-071, INV-010, INV-003, INV-014, INV-011, INV-013/INV-075, INV-060 |
