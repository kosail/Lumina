# AGENTS.md — Lúmina Beta Runtime (Operating Manual for AI Agents)

> This file is the **operating manual** for AI agents and contributors working on the Lúmina
> beta runtime. It defines **how** we work (style, workflow, tooling, rules).
>
> It does **not** define the project's core decisions. Those live in `INVARIANTS.md` and are
> ranked above this file. Business requirements live in `SPECS.md`.

---

## 1. Project identity

- **Project:** Lúmina — an edge-AI assistive device that narrates the environment through
  bone-conduction audio for blind and low-vision users. Spanish-first, fully on-device.
- **This module:** the **real beta runtime** for the **Raspberry Pi Zero 2 W**, written in C++.
  It is a from-scratch build, not a port of the Python nightly.
- **Stage:** contest beta (InnovaTecNM). Technical report + business model + prototype already
  exist at a higher level. This repo is the working runtime.
- **Feature scope for the beta:** object/animal/person detection narrated in Spanish, prioritized
  obstacle alerts, and **face recognition of 3–4 enrolled people**.
- **Deadline:** 6 working days, with a hard GO/NO-GO gate at the end of Day 2.
- **Human context:** the maintainer is a Java developer who has not written C++ for ~2 years.
  Everything must be commented with that in mind (see §6).

### Related documents (do not duplicate; link instead)

| File | Role |
|------|------|
| `INVARIANTS.md` | Highest source of truth — non-negotiable facts/decisions |
| `SPECS.md` | Structured business requirements |
| `RAW_PLAN.md` | Execution schedule + stack rationale |
| `CHANGELOG.md` | Append-only machine-readable history |
| `RPI_2W_SPECSHEET.txt` | Target hardware facts |

---

## 2. Source-of-truth hierarchy (READ THIS FIRST)

```
INVARIANTS.md            ← highest; never violate or silently change
  > SPECS.md             ← business requirements
    > RAW_PLAN.md        ← schedule + stack rationale
      > AGENTS.md        ← this file (workflow/style rules)
        > code comments / any other file
CHANGELOG.md             ← record of every meaningful change to any of the above
```

**Conflict rule.** If anything conflicts with an invariant, the invariant wins. If a task seems to
require changing or violating an invariant, **STOP and ask the user**. Never resolve the conflict
yourself.

**INV-001 applies to you at all times: NEVER ASSUME.** Consult the latest official documentation
online before using any API/library/hardware behavior. If the lookup fails, or your confidence is
below `0.80`, stop and ask the user. See `INVARIANTS.md` → INV-001.

---

## 3. Stack and platforms

- **Language:** C++23 (GCC 14.2.1). See INV-020.
- **Build:** CMake ≥ 3.20, CMake Presets, out-of-source builds.
- **Inference:** NCNN (static link). Model: YOLO11n exported to NCNN (`model.ncnn.param`/`.bin`).
- **Capture:** libcamera (OV5647 sensor). See INV-011.
- **Face:** OpenCV `objdetect` — `FaceDetectorYN` (YuNet) + `FaceRecognizerSF` (SFace) by
  default; MobileFaceNet on NCNN is the low-RAM alternative.
- **TTS:** libpiper (C API) + espeak-ng, Spanish (es_MX) voice.
- **Audio out:** ALSA PCM routed to `bluealsa` → Bluetooth A2DP earbuds.
- **Bluetooth:** BlueZ (single trusted device).
- **Target OS:** Raspberry Pi OS **Lite 64-bit, Debian 13 "trixie"** (glibc, aarch64; kernel
  6.18.x+rpt-rpi-v8), headless (INV-021, INV-024). DietPi 64-bit is the only permitted alternative
  and only as a **post-gate optimization**; Alpine/musl is rejected for the beta.
- **Toolchain:** GCC **14.2.1** on the device and on the host cross-compiler
  (`aarch64-linux-gnu-g++`); compile against an aarch64 **trixie sysroot** (INV-023, INV-025).
  Target facts (board, kernel, GPU, library versions) are recorded in `INV-025`.
  **Known host gotcha:** the Arch cross toolchain ignores `--sysroot` for library search, so
  `cmake/toolchain-aarch64.cmake` adds explicit `-L`/`-Wl,--sysroot`/`-rpath-link` flags.
  Do not remove them. Full walkthrough: `docs/CROSS_COMPILE.md`.
- **Python:** **tooling only** (model export, benchmark scripts). Not part of the runtime.
- **Host for builds:** the developer laptop (cross-compile). No heavy native builds on the Pi
  (INV-023).

---

## 4. Repository layout

```
Lumina-BETA-RPI-2W/
  AGENTS.md                  # this file
  INVARIANTS.md              # foundation (rank 1)
  SPECS.md                   # requirements
  RAW_PLAN.md                # plan/schedule
  CHANGELOG.md               # append-only history (AI-oriented)
  CMakeLists.txt
  CMakePresets.json
  cmake/                     # toolchain-aarch64.cmake, FindBlueALSA.cmake, rpi-sysroot/ (gitignored)
  third_party/               # ncnn (static) + ncnn-src, libpiper + onnxruntime
  models/                    # yolo11n_ncnn/, face/, voices/
  src/
    main.cpp
    core/                    # bounded_queue, frame, event, config, time, result, logging
    capture/                 # camera (libcamera), ICamera
    vision/                  # detector (NCNN YOLO), face, face_store
    processing/              # distance heuristic
    sensors/                 # proximity.hpp (IProximitySensor + Null), future vl53l0x
    alerts/                  # arbiter (priority, cooldown, preemption)
    audio/                   # piper_tts, bluealsa_sink
    i18n/                    # es message catalog
    telemetry/               # udp (optional, off by default)
  scripts/                   # 1-sync_sysroot.sh, 2-build_ncnn.sh, 3-export_models.sh, 4-fetch_onnxruntime.sh, enroll_face.sh, bt_setup.sh
  tests/                     # doctest unit tests + bench targets
  docs/                      # design notes (CROSS_COMPILE.md, PERFORMANCE.md)
```

Keep new code inside the matching module. Do not create new top-level directories without asking.

---

## 5. Architecture rules (non-negotiable, see INVARIANTS)

1. **Interface-first + dependency injection.** Hardware is reachable only via interfaces
   (`ICamera`, `IDetector`, `IFaceRecognizer`, `ITtsEngine`, `IAudioSink`, `IProximitySensor`).
   Concrete implementations are injected at startup. Logic must run on the host with **mocks**.
2. **Bounded queues; drop stale frames.** Capture never blocks on inference.
3. **Alert arbiter:** safety alerts preempt descriptions; speech is interruptible.
4. **Threading:** one thread per pipeline stage, connected by bounded queues. Prefer
   `std::jthread` (auto-joining). No detached threads.
5. **No global mutable state.** Pass dependencies explicitly.
6. **Proximity and telemetry are compiled out by default** (CMake options).
7. **Single responsibility per module.** One class per file where practical. Keep headers thin.

### Standard interfaces (sketch — exact signatures evolve, names are stable)

```cpp
// All interfaces are pure abstractions so the pipeline can be tested without hardware.
struct ICamera {
    virtual ~ICamera() = default;
    virtual bool start() = 0;
    virtual bool getLatest(Frame& out) = 0;  // non-blocking; may drop stale frames
    virtual void stop() = 0;
};

struct IDetector { virtual ~IDetector() = default;
    virtual std::vector<Detection> detect(const Frame&) = 0; };

struct IFaceRecognizer { virtual ~IFaceRecognizer() = default;
    virtual std::optional<std::string> identify(const Frame&, const Detection&) = 0; };

struct ITtsEngine { virtual ~ITtsEngine() = default;
    virtual void synthesize(const std::string& text, PcmSink&) = 0; };

struct IAudioSink { virtual ~IAudioSink() = default;
    virtual bool open() = 0; virtual void write(const float* samples, size_t n) = 0; };

struct IProximitySensor { virtual ~IProximitySensor() = default;
    virtual bool init() = 0;
    virtual std::optional<ProximityReading> read() = 0; };
```

---

## 6. Coding style (written for a Java developer returning to C++)

Formatting is enforced by `.clang-format`; run it before committing. The conventions below are
chosen to feel familiar to a Java developer.

### Naming

| Element              | Convention              | Java analogue |
|----------------------|-------------------------|---------------|
| Types (class/struct/enum) | `PascalCase`       | same |
| Methods / functions  | `camelCase`             | same |
| Local variables / parameters | `camelCase`      | same |
| Member variables     | `m_camelCase`           | like `this.x` but explicit |
| Compile-time constants | `kPascalCase`         | replaces `UPPER_SNAKE` |
| Namespaces           | `lumina::<module>`      | like Java packages |
| Files                | `lower_snake_case.{hpp,cpp}` | (Java uses one public class per file; we keep one primary type per file) |

- Use `#pragma once` in headers.
- Namespaces: `lumina::core`, `lumina::capture`, `lumina::vision`, `lumina::audio`, etc.

### C++ constructs you must know (and comment about)

Because the maintainer is returning from Java, **explain C++-specific constructs in comments**
(see §7). Key points for authors:

- **RAII (Resource Acquisition Is Initialization).** Resources (files, sockets, ALSA handles)
  are owned by objects and released in their destructors. Prefer this over manual open/close.
- **Ownership.** Use `std::unique_ptr<T>` for exclusive ownership (default), `std::shared_ptr<T>`
  only when ownership is genuinely shared, and raw pointers/references **only as non-owning
  views**. Never use naked `new`/`delete`.
- **Rule of 0/5.** Prefer the Rule of 0 (let members manage themselves). If you must define a
  destructor/copy/move, define all five consistently and comment why.
- **Move semantics.** `std::move` transfers ownership of buffers instead of copying them. Use it
  for large `cv::Mat`/`ncnn::Mat`/buffers. Comment when a move is intentional.
- **References vs pointers.** Prefer `const T&` for read-only parameters; use `T*` only when
  "may be null" is meaningful. Document nullability.
- **Value vs reference.** Small types pass by value; large/owning types by reference or move.
- **`const` correctness.** Mark methods `const` when they do not mutate the object. Mark
  parameters `const` where possible.
- **`[[nodiscard]]`** on functions whose return value must not be ignored (factories, `init`,
  `read`).
- **`enum class`** instead of plain enums (scoped, type-safe — closest to Java enums).
- **Error handling.** Prefer `std::expected<T, E>` (C++23) or our `Result<T>` helper type
  (`src/core/result.hpp`) / `std::optional<T>`; reserve exceptions for truly exceptional,
  non-recoverable conditions and **never let exceptions cross a thread boundary**.
- **Threads.** `std::jthread` + our bounded queue + `std::mutex`/condition variables. Avoid
  busy-waiting. Use `std::atomic` only for simple flags/counters.
- **Casts.** Use `static_cast` / `dynamic_cast` / `reinterpret_cast`; never C-style casts.
- **Headers.** No `using namespace` in headers. Include what you use; keep includes minimal.
- **Avoid `std::iostream` in hot paths.** Use the project logger (`src/core/logging.*`).

### Allowed C++23 features

Concepts, ranges, `std::span`, `std::jthread`, `std::atomic<std::shared_ptr>`, designated
initializers, `constexpr`/`consteval`, `std::bit_cast`, `std::format`/`std::print`, `std::expected`,
`std::mdspan`. **Verify a specific C++23 library facility exists in GCC 14.2 before depending on it**
(INV-001, INV-020) — some are still incomplete.

---

## 7. Commenting policy (mandatory)

> The base agent tooling defaults to "do not add comments". **That default is overridden here.**
> In this repository, comments are **required** and reviewers will reject uncommented code.

Rules:

1. **Every header** starts with a short block explaining the module's responsibility and how it
   fits the pipeline.
2. **Every public type and function** has a comment: what it does, parameters, return value,
   ownership/nullability, and any threading assumptions.
3. **Every non-obvious block** explains *why*, not just *what*.
4. **Add a "C++ note"** whenever a construct is likely unfamiliar to a Java developer (RAII,
   move, pointer ownership, template). Keep it one or two lines.
5. Do **not** write comments that merely restate the code (`i++; // increment i`).
6. TODOs must include an owner tag and link to an invariant or changelog id, e.g.
   `// TODO(prox): enable VL53L0X — INV-033 / CHG-0007`.

Example:

```cpp
// Owns the libcamera device and delivers the most recent frame.
// Thread-safety: getLatest() is called from the inference thread; start()/stop()
// from the main thread and are synchronized internally.
class LibcameraSource final : public ICamera {
public:
    // `config` is consumed by value because the camera copies tuning data internally.
    explicit LibcameraSource(CameraConfig config);
    [[nodiscard]] bool start() override;   // returns false on failure; logs reason
    // Non-blocking: copies the latest frame if available, otherwise returns false.
    [[nodiscard]] bool getLatest(Frame& out) override;
    void stop() override;
private:
    CameraConfig m_config;                 // m_ prefix = member (see AGENTS §6)
    std::unique_ptr<CameraImpl> m_impl;    // RAII: frees the camera in its destructor
};
```

---

## 8. Documentation-fetching rules

INV-001 requires consulting the latest official documentation. When you need an API or behavior:

1. **Fetch the official source** and match the **pinned version** we use.
2. **Cite URL + access date** in the code comment or `docs/DECISIONS.md`.
3. Prefer these canonical sources (not blog aggregators):

| Topic | Canonical source |
|-------|------------------|
| Raspberry Pi / camera | `https://www.raspberrypi.com/documentation/` |
| libcamera | `https://libcamera.org/` |
| OpenCV (incl. face) | `https://docs.opencv.org/4.x/` |
| NCNN | `https://github.com/Tencent/ncnn` + its wiki |
| Ultralytics export | `https://docs.ultralytics.com/` |
| Piper / libpiper | `https://github.com/OHF-Voice/piper1-gpl` |
| BlueZ / BT audio | `https://www.bluez.org/` + Arch Wiki *Bluetooth headset* |
| ALSA | `https://www.alsa-project.org/` |

4. If a lookup fails or confidence < `0.80`, **ask the user** — do not guess.

---

## 9. Testing and modularity

- **Framework:** doctest (header-only, no heavy dependency).
- **Unit tests run on the host** (x86 laptop) against mocks; no hardware required.
- **Every interface has a mock** in `tests/mocks/`.
- **Pure logic** (distance heuristic, alert arbiter, i18n, face matching thresholds) must be fully
  unit-tested.
- **Integration tests / benchmarks** (`tests/bench_*`) run on the Pi and report FPS, latency,
  and RSS; results feed `CHANGELOG.md`.
- A change is not "done" until it builds and its tests pass.

### Commands

```bash
# Host build + unit tests (fast feedback, mocks only)
cmake --preset host && cmake --build --preset host && ctest --preset host

# One-time / refresh the target sysroot from the Pi (needs rsync + ssh; gitignored output)
scripts/1-sync_sysroot.sh --host pi@<pi-host>       # -> cmake/rpi-sysroot/

# Cross build for the Pi (real deployment; requires cross toolchain + cmake/rpi-sysroot)
cmake --preset aarch64 && cmake --build --preset aarch64   # produces build/aarch64/lumina
# See docs/CROSS_COMPILE.md for the first-timer walkthrough and the Arch --sysroot gotcha.

# One-time: cross-build NCNN (static) into third_party/ncnn (laptop; needs network)
scripts/2-build_ncnn.sh
# Instrumented variant for per-layer profiling -> third_party/ncnn-bench
scripts/2-build_ncnn.sh --layer-benchmark

# Model export (laptop; needs ultralytics/Python tooling)
scripts/3-export_models.sh

# One-time: fetch the official prebuilt ONNX Runtime (aarch64) for libpiper -> third_party/onnxruntime
scripts/4-fetch_onnxruntime.sh

# On-device benchmarks (need LUMINA_ENABLE_NCNN=ON + LUMINA_BUILD_BENCH=ON)
build/aarch64/tests/lumina_bench_fps <modelDir> <inputWidth> <inputHeight> [iterations] [--threads N]
```

---

## 10. Agent workflow rules

1. **Read `INVARIANTS.md` before starting any task.** Reject/ask if the task conflicts with it.
2. **Never assume (INV-001).** Fetch docs or ask.
3. **Small, reviewable changes.** One concern per change; keep diffs focused.
4. **Update `CHANGELOG.md` for every meaningful change or decision** (INV-070), using the exact
   YAML-block format documented at the top of that file. Append only.
5. **Never silently change an invariant.** Stop and ask first.
6. **Never commit secrets/keys.** No credentials in the repo.
7. **Do not add a runtime dependency** without approval (INV-022).
8. **Report measurements**, not impressions (FPS, ms, MB) — cite the command used.
9. **Ask when confidence < 0.80.** Asking is cheap; a wrong assumption costs days.
10. **Respect the gate (INV-071):** if the Day-2 vertical slice is not working, do not start
    optional work (telemetry, extra features); propose the fallback instead.

---

## 11. Definition of Done (checklist)

A task is done when **all** of the following hold:

- [ ] It does not violate any invariant (`INVARIANTS.md`).
- [ ] It builds on the host and on the aarch64 target (or is explicitly host-only tooling).
- [ ] Unit tests exist and pass; pure logic is covered.
- [ ] Code is modular, uses interfaces/DI, and has no global mutable state.
- [ ] Every new type/function/block is commented per §7, including C++ notes for Java readers.
- [ ] External facts are cited (URL + access date) per §8.
- [ ] `CHANGELOG.md` has an appended entry.
- [ ] No new dependency was introduced without approval.
- [ ] Measurements (if performance-related) are recorded.

---

## 12. Prohibited actions

- ❌ Assuming anything instead of consulting docs or asking (INV-001).
- ❌ Changing/violating an invariant without explicit user approval.
- ❌ Using library features not yet available in GCC 14.2 without verifying (INV-020).
- ❌ Adding a GUI, desktop, or OpenCV HighGUI to the runtime (INV-021).
- ❌ Introducing cloud/network calls in the core path (INV-003, INV-034).
- ❌ Naked `new`/`delete`, C-style casts, or `using namespace` in headers.
- ❌ Blocking the capture thread on inference (INV-031).
- ❌ Non-interruptible speech that can delay a safety alert (INV-032).
- ❌ Committing secrets or generated binaries/models to git without approval.
- ❌ Writing code without comments.
