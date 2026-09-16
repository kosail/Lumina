# CHANGELOG.md — Lúmina Beta Runtime

> **AUDIENCE: AI AGENTS (machine-oriented). Not written for humans.**
>
> This file is an **append-only** record of every meaningful change and design decision made while
> building the beta runtime. Its purpose is to let a future agent reconstruct the *why* behind the
> current state **without re-reading the whole repository or re-deriving decisions from scratch**.

---

## Format specification (do not change without a changelog entry)

- Entries are **YAML mappings** in a single top-level sequence.
- **Append only.** Never edit or delete a past entry. To reverse/supersede a decision, add a new
  entry and set `supersedes`.
- Newest entries are appended at the **end** of the list.
- Required fields:

```yaml
- id: CHG-NNNN              # zero-padded, strictly increasing, never reused
  date: YYYY-MM-DD
  agent: <model-or-tool>/<variant>   # e.g. opencode/deepseek-flash
  type: docs|decision|impl|fix|refactor|test|chore|revert
  status: proposed|applied|superseded|reverted
  invariants: [INV-xxx, ...]  # invariants affected/referenced; [] if none
  supersedes: CHG-NNNN | null # the entry this one replaces, if any
  summary: >-                # one or two sentences, what changed
    ...
  rationale: >-              # WHY; the reasoning a future agent must not re-derive
    ...
  files: [path, ...]         # files added/modified/removed
  approvals: [user]          # who authorized it; [user] unless purely mechanical
  follow_up: >-              # nullable; what remains to do / what to watch
    ...
```

- `invariants` must list every invariant the entry touches. If a change **would** alter an
  invariant, it may only be applied after explicit user approval (see `INVARIANTS.md` change
  protocol).
- Keep `summary`/`rationale` factual and terse. No marketing language.

---

## Entries

```yaml
# ---------------------------------------------------------------------------
# CHG-0001 — Initial plan for the beta runtime
# ---------------------------------------------------------------------------
- id: CHG-0001
  date: 2026-09-15
  agent: opencode/deepseek-flash
  type: docs
  status: applied
  invariants: []
  supersedes: null
  summary: >-
    Created RAW_PLAN.md defining the beta runtime stack, architecture, repository layout, 6-day
    schedule with a Day-2 GO/NO-GO gate, risk table, and the modular IProximitySensor
    future-proofing.
  rationale: >-
    User approved a plan to build a from-scratch C++ runtime for the Raspberry Pi Zero 2 W.
    NCNN was chosen for inference (pure C++, best ARM performance, Ultralytics-recommended over
    ONNX/TFLite), Piper/libpiper for Spanish TTS, libcamera for the OV5647 sensor, and
    ALSA->bluealsa for the single Bluetooth A2DP earbuds. The A53 has no INT8 dot-product
    (ARMv8.0-A), so FP16 is the default and INT8 is benchmark-gated. Face recognition (not
    currency) was selected as the single extra feature because it needs no custom dataset and is
    tolerant of the IR/color handling. Proximity is absent from the hardware, so it is scaffolded
    behind a null interface.
  files:
    - Lumina-BETA-RPI-2W/RAW_PLAN.md
  approvals: [user]
  follow_up: >-
    Build the AI-assisted environment (AGENTS/INVARIANTS/SPECS/CHANGELOG) and begin Day 0-1
    (scaffold + libcamera capture + YOLO11n->NCNN export/benchmark).

# ---------------------------------------------------------------------------
# CHG-0002 — Bootstrap the AI-assisted project environment
# ---------------------------------------------------------------------------
- id: CHG-0002
  date: 2026-09-15
  agent: opencode/deepseek-flash
  type: docs
  status: applied
  invariants: [INV-002, INV-030, INV-070, INV-072]
  supersedes: null
  summary: >-
    Added the four-file operating environment in Lumina-BETA-RPI-2W: AGENTS.md (operating manual),
    INVARIANTS.md (rank-1 foundation), SPECS.md (rank-2 requirements), and CHANGELOG.md (this
    append-only, machine-readable log).
  rationale: >-
    The user defined an explicit documentation architecture: AGENTS.md holds working/style rules,
    INVARIANTS.md holds the immutable decisions and outranks every other source, SPECS.md holds the
    structured business requirements, and CHANGELOG.md is an AI-only append-only history so future
    agents can reconstruct intent. The source-of-truth hierarchy is enforced as
    INVARIANTS > SPECS > RAW_PLAN > AGENTS > code. Global engineering rules captured: modular,
    testable, decoupled, fully commented (the maintainer is a Java developer returning to C++),
    and C++20 (not C++23).
  files:
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/SPECS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    None. Subsequent changes append new entries here.

# ---------------------------------------------------------------------------
# CHG-0003 — Add CORE invariant: NEVER ASSUME (INV-001)
# ---------------------------------------------------------------------------
- id: CHG-0003
  date: 2026-09-15
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-001, INV-002]
  supersedes: null
  summary: >-
    Added INV-001 "NEVER ASSUME" as the highest-priority CORE invariant: always consult the latest
    official documentation online; if the lookup fails or confidence is below 0.80, stop and ask
    the user; never assume.
  rationale: >-
    Direct user instruction. On constrained and fast-moving hardware/software (RPi Zero 2W, NCNN,
    libcamera, OpenCV, Piper), unverified assumptions are the largest source of wasted time and
    silent breakage. Making "ask" the default below the confidence threshold converts a potential
    silent error into an explicit, cheap interaction. Operationalized via the documentation-fetching
    rules in AGENTS.md §8 (cite URL + access date) and the workflow rules in §10.
  files:
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/AGENTS.md
  approvals: [user]
  follow_up: >-
    Enforce in every future task; any external claim in code must carry a source citation.

# ---------------------------------------------------------------------------
# CHG-0004 — Target OS decision (Pi OS Lite 64-bit; DietPi post-gate; no Alpine)
# ---------------------------------------------------------------------------
- id: CHG-0004
  date: 2026-09-15
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-021, INV-023, INV-024]
  supersedes: null
  summary: >-
    Recorded the target OS decision: Raspberry Pi OS Lite 64-bit is the platform; DietPi 64-bit is
    permitted only as a post-gate optimization; Alpine/musl is rejected for the beta. Added INV-024
    and a cross-reference from INV-021.
  rationale: >-
    The OV5647 ISP/libcamera stack and the glibc-only onnxruntime that libpiper links are the
    reference-supported path on Pi OS; Alpine/musl would jeopardize camera and TTS bring-up and
    force dependency rebuilds inside a 6-day budget (INV-071). DietPi is Debian/glibc over the RPi
    kernel, so it remains compatible while trimming background services (supports INV-052). The
    cross-compile sysroot stays Debian Bookworm aarch64 (INV-023).
  files:
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Reconsider DietPi only if the memory budget (NFR-03/INV-052) is exceeded after cheap fixes
    (disable unused services, zram, MobileFaceNet over SFace).

# ---------------------------------------------------------------------------
# CHG-0005 — Project README
# ---------------------------------------------------------------------------
- id: CHG-0005
  date: 2026-09-15
  agent: opencode/deepseek-flash
  type: docs
  status: applied
  invariants: [INV-070, INV-072]
  supersedes: null
  summary: >-
    Replaced the placeholder README.md with a project README (174 lines): scope, target hardware,
    stack, architecture, repository layout, documentation map, build/test/model commands, CMake
    options, roadmap, third-party licensing, and contribution rules.
  rationale: >-
    The repository had only a one-line placeholder. A README is required for the public GitHub
    project and to onboard contributors. It links to the rank-1/2 documents rather than duplicating
    decisions, states the beta is under active development (no overclaiming), and surfaces the
    Piper/espeak-ng GPL-3.0 productization note (INV-060). Kept under the 200-line constraint.
  files:
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Keep the README's "intended shape" wording until the Day-2 vertical slice lands, then update
    commands/examples to reflect the actual CLI.

# ---------------------------------------------------------------------------
# CHG-0006 — Correct the initialized project skeleton (init hygiene)
# ---------------------------------------------------------------------------
- id: CHG-0006
  date: 2026-09-15
  agent: opencode/deepseek-flash
  type: chore
  status: applied
  invariants: [INV-020, INV-021, INV-022, INV-023, INV-024, INV-070]
  supersedes: null
  summary: >-
    Corrected the CLion-generated initialization: fixed cmake_minimum_required from 4.3 to 3.20,
    added strict C++20 settings, LUMINA_* feature flags and warning flags; renamed the executable
    to `lumina` and moved main.cpp from the repo root to src/main.cpp (commented placeholder);
    added CMakePresets.json (host/aarch64), cmake/toolchain-aarch64.cmake, a root .gitignore, and
    .clang-format; added .gitkeep placeholders so the empty module tree survives a clone; added
    models/README.md and third_party/.gitkeep.
  rationale: >-
    cmake_minimum_required(VERSION 4.3) would fail to configure on Raspberry Pi OS Bookworm
    (CMake 3.25) and most CI, contradicting INV-023; the CLion sample main.cpp and root location
    contradicted the intended layout and the mandatory-comment policy. Empty directories are not
    tracked by git, so the module structure would vanish on clone. No root .gitignore risked
    committing build output, IDE files, and model binaries. CMake presets schema version 3 was
    verified against the official CMake documentation because the `toolchainFile` field requires
    schema v3 (CMake >= 3.21); build/test presets exist since schema v2 (CMake 3.20). JetBrains
    .idea/ is ignored in whole per user instruction. No commit was made.
  files:
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CMakePresets.json
    - Lumina-BETA-RPI-2W/cmake/toolchain-aarch64.cmake
    - Lumina-BETA-RPI-2W/.gitignore
    - Lumina-BETA-RPI-2W/.clang-format
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/models/README.md
    - Lumina-BETA-RPI-2W/third_party/.gitkeep
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Day 0-1 still pending (interfaces, bounded_queue, Result<T>, ICamera mock, export_models.sh).
    Verify the host build with `cmake --preset host && cmake --build --preset host`; the aarch64
    preset requires an aarch64 cross toolchain and (ideally) a Raspberry Pi OS sysroot.

# ---------------------------------------------------------------------------
# CHG-0007 — Verified target environment; migrate to Debian 13 (trixie) + C++23
# ---------------------------------------------------------------------------
- id: CHG-0007
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-010, INV-012, INV-020, INV-023, INV-024, INV-025]
  supersedes: null
  summary: >-
    Corrected all environment facts to the user-verified target and moved the language to C++23.
    Added INV-025 (verified target environment) and amended INV-010, INV-012, INV-020, INV-023,
    INV-024. Updated AGENTS, SPECS, RAW_PLAN, README, CMakeLists, .clang-format, the aarch64
    toolchain/preset, .gitignore, the root analysis doc, and RPI_2W_SPECSHEET.txt.
  rationale: >-
    The device reports Debian GNU/Linux 13 "trixie" aarch64 (Raspberry Pi OS Lite 64-bit), kernel
    6.18.50+rpt-rpi-v8, CPU BCM2837 (4x Cortex-A53 @1GHz), GPU bcm2835-vc4, not Bookworm/BCM2710A1
    as previously assumed. The host cross-compiler is GCC 14.2.1, matching the sysroot GCC 14, so
    the bookworm/GCC-12 rationale for staying on C++20 was wrong; the user chose C++23. INV-012 was
    re-verified against sources: the A53 is ARMv8.0-A and lacks SDOT/UDOT (added in later profiles)
    and FP16 arithmetic (ARMv8.2), so INT8 gains are bandwidth/size, not arithmetic. This entry
    supersedes the Bookworm/GCC-12 statements in CHG-0004 and CHG-0006 without editing them. The
    C++20 -> C++23 switch was a user correction and is recorded here rather than as its own entry.
  files:
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/SPECS.md
    - Lumina-BETA-RPI-2W/RAW_PLAN.md
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/.clang-format
    - Lumina-BETA-RPI-2W/.gitignore
    - Lumina-BETA-RPI-2W/cmake/toolchain-aarch64.cmake
    - Lumina-BETA-RPI-2W/CMakePresets.json
    - Lumina-BETA-RPI-2W/RPI_2W_SPECSHEET.txt
    - Lumina-BETA-RPI-2W/CHANGELOG.md
    - FIRST_IMPRESSIONS_ON_NIGHTLY_VERSION.md
  approvals: [user]
  follow_up: >-
    After the clean sysroot is in place, verify it exposes CMake (OpenCVConfig.cmake, libcamera)
    and pkg-config metadata so find_package works; otherwise set include/lib paths manually. Then
    run `cmake --preset host` and, once the sysroot is ready, `cmake --preset aarch64`.

# ---------------------------------------------------------------------------
# CHG-0008 — Cross-compile works: fix Arch --sysroot gotcha + document the process
# ---------------------------------------------------------------------------
- id: CHG-0008
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-023, INV-025]
  supersedes: null
  summary: >-
    Made the CMake cross build actually work and documented cross-compilation. Added explicit
    sysroot link flags to cmake/toolchain-aarch64.cmake, added docs/CROSS_COMPILE.md, linked it
    from the README and AGENTS, noted the host toolchain gotcha in INV-025, and appended this entry.
  rationale: >-
    A manual cross-compile initially failed with "cannot find /usr/lib64/libm.so.6: file in wrong
    format". Diagnostics showed the host cross toolchain (Arch's aarch64-linux-gnu-gcc, built-in
    sysroot /usr/aarch64-linux-gnu with a lib64 layout) does NOT apply --sysroot to its library
    search, so -lm fell back to the host's x86-64 /usr/lib64. Passing explicit -L<sysroot>
    /usr/lib/aarch64-linux-gnu (+ /lib/aarch64-linux-gnu), -Wl,--sysroot, and -Wl,-rpath-link made
    the link succeed; the resulting aarch64 ELF ran correctly on the Pi. The same flags are now
    baked into the toolchain file (linker AND compiler flags, so CMake's compiler/ABI probe also
    succeeds), and the sysroot's merged-/usr symlinks (lib -> usr/lib, loader alias) are documented.
  files:
    - Lumina-BETA-RPI-2W/cmake/toolchain-aarch64.cmake
    - Lumina-BETA-RPI-2W/docs/CROSS_COMPILE.md
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run `cmake --preset aarch64 && cmake --build --preset aarch64` on the host to confirm the
    preset path works end to end; then proceed to Day 0-1. A separate OpenCV/libcamera smoke target
    was intentionally NOT added in this change (deferred).

# ---------------------------------------------------------------------------
# CHG-0009 — Redesign cross-compile guide; add sync_sysroot.sh
# ---------------------------------------------------------------------------
- id: CHG-0009
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: docs
  status: applied
  invariants: [INV-023, INV-025]
  supersedes: null
  summary: >-
    Rewrote docs/CROSS_COMPILE.md as an end-to-end Setup -> Build -> Maintain guide (extraction from
    the Pi, placement, merged-/usr symlinks, a verification gate, deployment, refresh, troubleshooting)
    and added scripts/sync_sysroot.sh, an idempotent helper that mkdirs, rsyncs the Pi's /usr into
    cmake/rpi-sysroot/usr, re-creates the symlinks, and verifies sysroot anchors. Updated README and
    AGENTS command/layout references and added a sysroot-provenance row to INV-025.
  rationale: >-
    The first version of the guide assumed a sysroot already existed: it never explained how to pull
    /usr off the Pi (tool, flags, excludes, ownership), never said to create cmake/rpi-sysroot and
    rsync into .../usr, listed the symlinks before any sysroot existed and without noting they must be
    re-created on refresh, and had no verification step (a broken sysroot failed deep in CMake).
    The redesign makes the one-time setup explicit and adds a script so the manual steps are
    repeatable. Decisions (user-approved): include the helper script; document installing the -dev
    packages on the Pi; use rsync without --delete and re-add symlinks on every refresh; keep example
    commands generic (pi@<pi-host>).
  files:
    - Lumina-BETA-RPI-2W/docs/CROSS_COMPILE.md
    - Lumina-BETA-RPI-2W/scripts/sync_sysroot.sh
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run scripts/sync_sysroot.sh --check on the host to confirm the anchors pass, and confirm the
    script exists on the machine with bash available (it uses bash arrays). The script itself was
    not executed in the agent container (no bash/rsync/ssh/network there).

# ---------------------------------------------------------------------------
# CHG-0010 — Host-testable foundation: core types, interfaces, tests
# ---------------------------------------------------------------------------
- id: CHG-0010
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-020, INV-030, INV-031, INV-072]
  supersedes: null
  summary: >-
    Added the host-testable foundation: core value types (frame, detection, result, config, time),
    the bounded drop-oldest queue (INV-031), the project logger, the ICamera and IDetector
    interfaces, mock implementations, doctest unit tests, and a lumina_core static library that the
    executable and the tests share.
  rationale: >-
    Capture and detection both depend on shared types and interface-first seams, and AGENTS §9
    requires pure logic to be unit-tested on the host with mocks (INV-030, INV-072). Wiring tests
    before hardware keeps feedback fast. doctest is fetched by FetchContent; the pinned v2.4.11 had
    to be bumped to v2.5.3 because v2.4.11 declares cmake_minimum_required(VERSION 3.0) and CMake 4
    removed compatibility with projects requiring < 3.5.
  files:
    - Lumina-BETA-RPI-2W/src/core/frame.hpp
    - Lumina-BETA-RPI-2W/src/core/detection.hpp
    - Lumina-BETA-RPI-2W/src/core/result.hpp
    - Lumina-BETA-RPI-2W/src/core/time.hpp
    - Lumina-BETA-RPI-2W/src/core/config.hpp
    - Lumina-BETA-RPI-2W/src/core/bounded_queue.hpp
    - Lumina-BETA-RPI-2W/src/core/logging.hpp
    - Lumina-BETA-RPI-2W/src/core/logging.cpp
    - Lumina-BETA-RPI-2W/src/capture/camera.hpp
    - Lumina-BETA-RPI-2W/src/vision/detector.hpp
    - Lumina-BETA-RPI-2W/tests/CMakeLists.txt
    - Lumina-BETA-RPI-2W/tests/test_main.cpp
    - Lumina-BETA-RPI-2W/tests/test_bounded_queue.cpp
    - Lumina-BETA-RPI-2W/tests/test_config.cpp
    - Lumina-BETA-RPI-2W/tests/mocks/mock_camera.hpp
    - Lumina-BETA-RPI-2W/tests/mocks/mock_detector.hpp
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CMakePresets.json
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Verified by the user: host tests pass (10 doctest cases) and the aarch64 build is green. The
    detector implementation lands in a later entry.

# ---------------------------------------------------------------------------
# CHG-0011 — libcamera OV5647 capture source + on-device probe
# ---------------------------------------------------------------------------
- id: CHG-0011
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-011, INV-021, INV-023, INV-031, INV-050, INV-070]
  supersedes: null
  summary: >-
    Implemented LibcameraSource (ICamera over libcamera 0.7.2) with a manual mmap frame path, added
    the headless lumina_capture_test probe and pkg-config-based CMake wiring, and fixed a shutdown
    race that re-queued requests while the camera was Stopping.
  rationale: >-
    Day 0-1 requires a libcamera OV5647 capture path validated on real hardware. The API was
    verified against the installed libcamera 0.7.2 headers and the upstream application-developer
    guide (INV-001). MappedFrameBuffer is declared only in the internal, non-installed
    libcamera/internal/mapped_framebuffer.h, so the source maps FrameBuffer planes itself with
    mmap(). Completion callbacks run on libcamera's internal thread and copy the newest frame into
    a single-slot hand-off (stale frames dropped, INV-031). The first hardware run exposed a
    shutdown bug: the completion signal was detached AFTER Camera::stop(), so requests cancelled
    during stop were re-queued ("Camera in Stopping state trying queueRequest()"); stop() now clears
    the running flag and disconnects the signal before stopping the camera, and cancelled requests
    are never re-queued.
  files:
    - Lumina-BETA-RPI-2W/src/capture/libcamera_source.hpp
    - Lumina-BETA-RPI-2W/src/capture/libcamera_source.cpp
    - Lumina-BETA-RPI-2W/src/tools/capture_probe.cpp
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CMakePresets.json
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Verified on the Pi: libcamera v0.7.2+rpt20260817, camera /base/soc/i2c0mux/i2c@1/ov5647@36,
    pipeline rpi/vc4, 640x480-RGB888/sRGB, stride 1920, single plane (921600 B), 4 buffers; 30
    frames in 1669.4 ms (~17.97 FPS capture-only, no inference). Next: NCNN cross-build + YOLO11n
    export + on-device benchmark (Increment 3).

# ---------------------------------------------------------------------------
# CHG-0012 — NCNN detector path: YOLO11n decode, NcnnDetector, benchmark
# ---------------------------------------------------------------------------
- id: CHG-0012
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-020, INV-021, INV-022, INV-030, INV-042, INV-050, INV-072]
  supersedes: null
  summary: >-
    Added the NCNN YOLO11n detection path: a pure, host-tested output decoder
    (yolo11_decode), a pimpl NcnnDetector implementing IDetector, a Spanish COCO
    class catalog (i18n/es), an on-device FPS/RSS/temperature benchmark, and the
    LUMINA_ENABLE_NCNN CMake wiring against third_party/ncnn.
  rationale: >-
    Day 0-1 requires the inference half of the pipeline and INV-050 evidence. NCNN is
    cross-built by scripts/build_ncnn.sh into third_party/ncnn (no official Linux
    aarch64 prebuilt exists) and linked via its CMake package (imported target `ncnn`,
    which also carries Threads/pthread). The model is exported by scripts/export_models.sh
    with `yolo export format=ncnn imgsz=320|416 quantize=16`. Inspection of the generated
    .param (2026-09-16) showed the export ALREADY applies the DFL box decode and class
    sigmoid and concatenates to 4+numClasses channels per anchor (84 for COCO); the anchor
    count is baked per export (Reshape 0=2100 at 320, 0=3549 at 416), so the input must be
    the matching square. The decoder is split from NCNN so the box/NMS math is unit-tested
    on the host with no model. Blob names are read from the net (in0/out0) rather than
    hardcoded. Blob dimensions are probed at runtime (whichever axis equals 4+numClasses is
    the channel axis) so the code is robust to pnnx tensor lowering. The detector also forces
    fp32 blob storage (ncnn defaults to fp16 storage, which Mat::row() would silently misread)
    and validates the baked anchor count against the configured input size.
  files:
    - Lumina-BETA-RPI-2W/src/vision/yolo11_decode.hpp
    - Lumina-BETA-RPI-2W/src/vision/yolo11_decode.cpp
    - Lumina-BETA-RPI-2W/src/vision/ncnn_detector.hpp
    - Lumina-BETA-RPI-2W/src/vision/ncnn_detector.cpp
    - Lumina-BETA-RPI-2W/src/i18n/es.hpp
    - Lumina-BETA-RPI-2W/src/i18n/es.cpp
    - Lumina-BETA-RPI-2W/tests/test_yolo11_decode.cpp
    - Lumina-BETA-RPI-2W/tests/bench_fps.cpp
    - Lumina-BETA-RPI-2W/tests/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/.gitignore
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run `cmake --preset host && cmake --build --preset host && ctest --preset host` (decoder
    tests), then the aarch64 build with -DLUMINA_ENABLE_NCNN=ON -DLUMINA_BUILD_BENCH=ON, deploy
    build/aarch64/tests/lumina_bench_fps with models/yolo11n_ncnn_320 and _416 to the Pi, and
    record FPS/RSS/temp. Face recognition (YuNet/SFace) is deferred to Day 4.

# ---------------------------------------------------------------------------
# CHG-0013 — Performance tooling: bench --threads + instrumented NCNN build
# ---------------------------------------------------------------------------
- id: CHG-0013
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: chore
  status: applied
  invariants: [INV-001, INV-050, INV-070]
  supersedes: null
  summary: >-
    Added a --threads N option to lumina_bench_fps, a --layer-benchmark mode to
    scripts/2-build_ncnn.sh that installs an NCNN_BENCHMARK=ON build into a separate prefix
    (third_party/ncnn-bench), and a LUMINA_NCNN_ROOT CMake cache variable to select the ncnn
    prefix. Fixed a "$SRC" typo in the script's extraction branch and updated the AGENTS §9
    command list to the numbered script names.
  rationale: >-
    On-device: 320px fp16 went 406 ms (single-threaded, because ncnn was built with
    NCNN_OPENMP=OFF so -fopenmp was never emitted) -> 231 ms (~1.8x, 4.32 FPS) after
    rebuilding with NCNN_OPENMP=ON + NCNN_SIMPLEOMP=ON; 416px ~367 ms (2.73 FPS). Setting
    the CPU governor to performance and re-enabling fp16 storage both changed nothing, so the
    remaining bottleneck is either SIMPLEOMP per-region synchronization overhead or a
    compute-bound workload that scales poorly across the 4 A53 cores. A thread sweep
    (--threads 1..4) quantifies scaling, and the instrumented build prints per-layer timings
    to find hotspots. Results decide whether real libgomp, a smaller input, or INT8 is
    warranted, or whether INV-050 must be restated with user approval.
  files:
    - Lumina-BETA-RPI-2W/tests/bench_fps.cpp
    - Lumina-BETA-RPI-2W/scripts/2-build_ncnn.sh
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run the thread sweep and the per-layer profile on the Pi and record results. Scripts were
    renamed with numeric prefixes (1-sync_sysroot.sh, 2-build_ncnn.sh, 3-export_models.sh);
    README, docs/CROSS_COMPILE.md and INVARIANTS still reference the old names and need a
    follow-up pass.

# ---------------------------------------------------------------------------
# CHG-0014 — Detection input 320x256 (non-square) + performance record
# ---------------------------------------------------------------------------
- id: CHG-0014
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-001, INV-020, INV-022, INV-050, INV-070]
  supersedes: null
  summary: >-
    Made the detector support a non-square input (width x height) and set the approved
    target to 320x256; converted core::Config.inferSize into inferWidth/inferHeight; added a
    docs/PERFORMANCE.md record of every on-device measurement; updated the export script to
    emit 320x256 plus 256x256/320x320/416x416; reverted the ncnn build to SIMPLEOMP; and swept
    the numbered script names through the docs. Amended INV-050 with the measured evidence.
  rationale: >-
    On-device tests (docs/PERFORMANCE.md) show YOLO11n/NCNN at 4.33 FPS (320x320) and 2.73 FPS
    (416x416), below INV-050. The board (4x A53 @1GHz, single-channel LPDDR2) is the wall:
    real libgomp vs SIMPLEOMP, OMP_WAIT_POLICY, governor=performance, and fp16 storage all
    changed nothing, and a thread sweep showed only 1.75x on 4 cores (~40% serial). A 640x480
    frame letterboxes to 320x240 regardless, so 320x320 wastes 25% of compute on padding;
    exporting 320x256 keeps the exact 320x240 effective resolution while removing that waste
    (~20% less compute, expected ~5.4 FPS). This satisfies INV-050 without lowering the input
    resolution perceptibly, unlike 256x256 (which drops to 256x192).
  files:
    - Lumina-BETA-RPI-2W/src/vision/ncnn_detector.hpp
    - Lumina-BETA-RPI-2W/src/vision/ncnn_detector.cpp
    - Lumina-BETA-RPI-2W/src/core/config.hpp
    - Lumina-BETA-RPI-2W/tests/test_config.cpp
    - Lumina-BETA-RPI-2W/tests/bench_fps.cpp
    - Lumina-BETA-RPI-2W/scripts/2-build_ncnn.sh
    - Lumina-BETA-RPI-2W/scripts/3-export_models.sh
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/docs/CROSS_COMPILE.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Confirm 320x256 measures >= 5 FPS on the Pi and record it in docs/PERFORMANCE.md. Run the
    recall validation (256 vs 320x256 vs 416) before treating 320x256 as validated. If it
    measures below 5 FPS, escalate per CHG-0015. The libgomp.so symlink the user added to the
    sysroot is no longer needed and can be removed.

# ---------------------------------------------------------------------------
# CHG-0015 — Model-swap contingency decision (NanoDet post-gate; others rejected)
# ---------------------------------------------------------------------------
- id: CHG-0015
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-001, INV-022, INV-050, INV-071]
  supersedes: null
  summary: >-
    Decided the detection-model strategy: keep YOLO11n at 320x256 as the target; keep a
    documented, gated fallback to NanoDet-Plus (post-Day-2 gate only, and only if recall or
    latency is insufficient, or headroom is needed); and reject YOLOv5, YOLO-FastestV2 and
    MobileNet-SSD. INV-050 is NOT relaxed. Escalation order: 320x256 -> 256x256 -> NanoDet.
  rationale: >-
    The bottleneck is the Raspberry Pi Zero 2 W (bandwidth/compute), not the model family, so
    a swap is at best ~1.5x, not a leap. YOLOv5n is less accurate than YOLO11n (28.4 vs 39.5
    mAP50-95 @640) for ~30% fewer FLOPs; YOLO-FastestV2 is far weaker (COCO mAP@0.5 ~24%);
    MobileNet-SSD has weaker small-object recall. NanoDet-Plus-m (27.0 mAP@320, 0.9 GFLOPs;
    ncnn C++ demo exists) is the only credible alternative, but it needs ONNX->ncnn plus a new
    GFL+FCOS decoder and a new dependency (INV-022), so it must not be attempted before the
    Day-2 gate. The detector is already isolated behind IDetector, so the swap stays a drop-in.
    Sources (accessed 2026-09-16): Ultralytics YOLO11 docs; NanoDet-Plus README; YOLO-FastestV2
    README. Details and measured results: docs/PERFORMANCE.md.
  files:
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Revisit only at/after the Day-2 gate. If NanoDet is pursued, it requires an INV-022
    dependency approval and its own CHG entry documenting the decoder and validation.

# ---------------------------------------------------------------------------
# CHG-0016 — Verified: 320x256 meets INV-050 (5.27 FPS)
# ---------------------------------------------------------------------------
- id: CHG-0016
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: test
  status: applied
  invariants: [INV-050, INV-070]
  supersedes: null
  summary: >-
    Recorded the verified on-device result for the approved 320x256 detection input:
    189.83 ms / 5.27 FPS / 54.1 MB peak RSS / 48.9 C at 4 threads on the Pi Zero 2 W, which
    satisfies INV-050 (>= 5 FPS at 320 px). Updated docs/PERFORMANCE.md and the INV-050
    amendment accordingly, and queued a --threads 3 headroom check.
  rationale: >-
    Prevents relying on a predicted number. 320x256 keeps the same effective 320x240
    resolution as 320x320 (the letterbox of a 640x480 frame) while removing ~20 % padding
    compute; measured 5.27 FPS vs 4.33 FPS at 320x320. The margin over 5.0 FPS is only ~5 %,
    and the bench is pure inference using all four cores, so a --threads 3 headroom check
    (one core reserved for capture + audio) is required before the Day-2 gate; decimation and
    the 256x256 fallback remain the mitigations if end-to-end throughput falls below target.
  files:
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run `lumina_bench_fps ~/yolo11n_ncnn_320x256 320 256 200 --threads 3` and record whether
    it still clears 5 FPS, then complete the recall validation (256 vs 320x256 vs 416) from
    docs/PERFORMANCE.md section 7.

# ---------------------------------------------------------------------------
# CHG-0017 — Decision: ship 320x256 with 4 inference threads + decimation
# ---------------------------------------------------------------------------
- id: CHG-0017
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-031, INV-050, INV-070]
  supersedes: null
  summary: >-
    Resolved the CPU headroom question: the shipping detection config is 320x256 with 4
    NCNN inference threads, CPU headroom supplied by decimation, with 256x256 as the ready
    fallback. Recorded the measured 3-thread result (205.4 ms / 4.87 FPS, below INV-050) in
    docs/PERFORMANCE.md and the INV-050 amendment.
  rationale: >-
    On-device: 320x256 at 4 threads = 5.27 FPS (189.8 ms, INV-050 met); at 3 threads = 4.87
    FPS (205.4 ms), which is below the >= 5 FPS target because the A53 is already
    bandwidth/compute-bound and cannot spare a whole core. Rather than relax INV-050 or drop
    resolution, we keep 4 inference threads and rely on the architecture's decimation
    (inference is bursty, one ~190 ms region per N frames, so capture and Piper run in the
    gaps; capture is light - libcamera uses its own thread and our per-frame copy is ~1.2 MB).
    If the end-to-end vertical slice misses 5 FPS under co-load, the approved escalation is
    256x256 (6.33 FPS @4t, ~5.85 @3t), which clears the target with a core to spare.
  files:
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Measure the end-to-end vertical slice (capture -> NCNN -> Piper -> bluealsa) FPS at the
    Day-2 gate; if it stays >= 5 FPS, 320x256 is final, otherwise escalate to 256x256. Recall
    validation (docs/PERFORMANCE.md section 7) is still outstanding.

# ---------------------------------------------------------------------------
# CHG-0018 — Decision: INT8 quantization formally deferred
# ---------------------------------------------------------------------------
- id: CHG-0018
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-012, INV-050, INV-070]
  supersedes: null
  summary: >-
    Formally deferred INT8 quantization for the beta. The FP16 YOLO11n path already meets INV-050 at
    the approved 320x256 input (5.27 FPS), and INV-012 establishes that INT8 gives no arithmetic
    speedup on the Cortex-A53. Updated docs/PERFORMANCE.md section 8 and added a deferral note to
    INV-012.
  rationale: >-
    RAW_PLAN Day 0-1 asked to benchmark fp16 vs int8, but INV-012 (no SDOT/UDOT on ARMv8.0-A) makes
    int8 a size/bandwidth play only, and fp16 storage already showed no gain. Running it would spend
    schedule on the critical path for no expected benefit, so this is a recorded deferral rather than
    a silent omission. Policy is unchanged: INT8 stays benchmark-gated.
  files:
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Revisit after the Day-2 gate only if memory/size pressure appears (e.g., RSS approaching
    INV-052). No action on the Day 0-1 / Day 1-2 critical path.

# ---------------------------------------------------------------------------
# CHG-0019 — Decision: use the official ONNX Runtime aarch64 prebuilt
# ---------------------------------------------------------------------------
- id: CHG-0019
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-022, INV-023, INV-070]
  supersedes: null
  summary: >-
    Decided to use Microsoft's official prebuilt aarch64 ONNX Runtime tarball instead of
    cross-building it. Pinned candidate: onnxruntime-linux-aarch64-1.30.0.tgz (GitHub release v1.30.0,
    published 2026-09-10, 9.79 MB, sha256 e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da).
    To be fetched into third_party/onnxruntime by a script and verified on the Pi before use.
  rationale: >-
    onnxruntime is the heaviest build in the plan and the main Day 1-2 schedule risk; the official
    aarch64 prebuilt removes that risk (INV-023 prefers no heavy builds). onnxruntime is already part
    of the INV-022 fixed stack as a libpiper dependency, so no new runtime dependency is introduced.
    Status stays 'proposed' until the artifact is downloaded, sha256-verified, and libonnxruntime.so
    loads on the Pi (glibc 2.41 / aarch64). Sources (accessed 2026-09-16): onnxruntime.ai/docs/install
    (C/C++ CPU install = official *.tgz from GitHub releases); github.com/microsoft/onnxruntime
    releases/tag/v1.30.0 (asset listing shows onnxruntime-linux-aarch64-1.30.0.tgz).
  files:
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Add scripts/4-fetch_onnxruntime.sh (download + sha256 check + extract to third_party/onnxruntime),
    confirm the tarball layout (include/ + lib/libonnxruntime.so), and verify libonnxruntime.so loads
    on the Pi before wiring libpiper. Fall back to a lower ORT tag or a source build only if the
    prebuilt fails on the A53.

# ---------------------------------------------------------------------------
# CHG-0020 — Vertical-slice audio: interfaces, PiperTts, AlsaSink, pipeline
# ---------------------------------------------------------------------------
- id: CHG-0020
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-030, INV-031, INV-032, INV-051, INV-052, INV-060, INV-070]
  supersedes: null
  summary: >-
    Implemented the Day 1-2 vertical slice (camera -> NCNN -> Piper ES -> bluealsa) in code:
    IAudioSink/ITtsEngine interfaces, PiperTts (libpiper C API, streaming, interruptible),
    AlsaSink (libasound -> bluealsa PCM), pure Spanish describer, a threaded Pipeline
    (one jthread per stage, bounded queues), main.cpp wiring, mocks + host tests, a new
    LUMINA_ENABLE_AUDIO CMake option, and scripts/5-build_libpiper.sh with a vendored
    toolchain-forwarding patch.
  rationale: >-
    Keeps hardware behind interfaces (INV-030) so describer/pipeline logic is host-testable, and
    isolates GPL-3.0 Piper behind ITtsEngine as a SHARED library (INV-060). The libpiper build needs
    two workarounds, both encoded: (1) its espeak-ng ExternalProject does not inherit our toolchain,
    so the vendored patch forwards CMAKE_TOOLCHAIN_FILE/SYSROOT/compilers (else an x86-64
    libespeak-ng.a); (2) our toolchain forces CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY, so the script
    pre-sets ONNXRUNTIME_LIB to stop libpiper's find_library from being re-rooted into the sysroot.
    The pipeline uses a simple newest-wins/interrupt policy for the slice; the full arbiter is Day 3.
  files:
    - Lumina-BETA-RPI-2W/src/audio/audio_sink.hpp
    - Lumina-BETA-RPI-2W/src/audio/tts_engine.hpp
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.hpp
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.cpp
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.hpp
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.cpp
    - Lumina-BETA-RPI-2W/src/app/describer.hpp
    - Lumina-BETA-RPI-2W/src/app/describer.cpp
    - Lumina-BETA-RPI-2W/src/app/pipeline.hpp
    - Lumina-BETA-RPI-2W/src/app/pipeline.cpp
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/tests/mocks/mock_audio_sink.hpp
    - Lumina-BETA-RPI-2W/tests/mocks/mock_tts_engine.hpp
    - Lumina-BETA-RPI-2W/tests/test_describer.cpp
    - Lumina-BETA-RPI-2W/tests/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/scripts/5-build_libpiper.sh
    - Lumina-BETA-RPI-2W/third_party/patches/libpiper-toolchain.patch
    - Lumina-BETA-RPI-2W/.gitignore
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run scripts/5-build_libpiper.sh (spike first): confirm espeak-ng cross-builds (watch for it
    running target binaries for data), that libpiper.so is aarch64, and whether the piper CLI
    subdirectory must be disabled. Then host build+tests (ctest --preset host) and the aarch64 build
    with LUMINA_ENABLE_LIBCAMERA/NCNN/AUDIO=ON. Deploy libpiper.so*, libonnxruntime.so.1.30.0, and
    espeak-ng-data to the Pi, run the slice over bluealsa, and measure INV-051 latency / INV-052 RSS
    / under-co-load FPS at the Day-2 gate. Not yet compiled on any platform.

# ---------------------------------------------------------------------------
# CHG-0021 — Decision: es_ES voice as a temporary stand-in for the slice
# ---------------------------------------------------------------------------
- id: CHG-0021
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-042, INV-052, INV-070]
  supersedes: null
  summary: >-
    Accepted models/voices/es_ES-sharvard-medium.onnx (Spain Spanish, 22050 Hz, 76.7 MB) as a
    temporary stand-in to bring up and gate the vertical slice, while a real es_MX voice is fetched
    by the user.
  rationale: >-
    INV-042 requires Spanish es_MX. es_ES is the same language and the same Pipeline/libpiper path,
    so it is sufficient to prove the Day-2 gate; it is explicitly temporary. The 76.7 MB fp32 model
    is a memory risk against INV-052, so a smaller es_MX voice (medium/low) should also be evaluated
    for RSS.
  files:
    - Lumina-BETA-RPI-2W/models/voices/es_ES-sharvard-medium.onnx
    - Lumina-BETA-RPI-2W/models/voices/es_ES-sharvard-medium.onnx.json
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Replace with a pinned es_MX voice before the demo/final, verify its sample rate and license, and
    record the swap plus RSS impact in a later entry.

# ---------------------------------------------------------------------------
# CHG-0022 — Fix: generate espeak-ng data with a native host build
# ---------------------------------------------------------------------------
- id: CHG-0022
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-023, INV-060, INV-070]
  supersedes: null
  summary: >-
    Fixed scripts/5-build_libpiper.sh so the cross build completes. The first run built an aarch64
    libpiper.so and installed the ONNX Runtime libs, then failed at the libpiper install step because
    espeak-ng's compiled data directory was never produced. The script now builds the compiled
    espeak-ng-data with a NATIVE host build of the same espeak-ng tag and stages it into the cross
    install tree before libpiper's install; the completion check also requires the data marker.
  rationale: >-
    espeak-ng includes cmake/data.cmake only under `if (COMPILE_INTONATIONS AND NOT
    CMAKE_CROSSCOMPILING)` (espeak-ng/CMakeLists.txt:18). That file both generates the compiled data
    (running the espeak-ng binary: --compile-intonations/--compile-phonemes/--compile=<lang>) and
    installs it. Cross-compiling skips it, so libpiper's `install(DIRECTORY
    ${ESPEAKNG_DATA_SRC})` had no source. The data compiler cannot run as an aarch64 binary during
    the cross build, and the compiled data is little-endian/platform-independent (x86-64 and aarch64
    agree), so a native build of the pinned tag produces correct data for the Pi. This adds no new
    runtime dependency (INV-022) and honors INV-023 (the native build runs on the laptop, not the Pi).
  files:
    - Lumina-BETA-RPI-2W/scripts/5-build_libpiper.sh
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Re-run scripts/5-build_libpiper.sh (no --force needed; the data marker now gates completion).
    Confirm third_party/libpiper/share/espeak-ng-data/phondata exists and libpiper.so is aarch64.
    If host data ever mismatches, build/verify espeak-ng data against the pinned tag 212928b (1.52.0.1).

# ---------------------------------------------------------------------------
# CHG-0023 — Fix: correct Spanish plurals via an explicit i18n catalog
# ---------------------------------------------------------------------------
- id: CHG-0023
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-042, INV-070]
  supersedes: null
  summary: >-
    Fixed describeDetections producing an incorrect plural ("camiónes") for accent-shifting nouns.
    Added spanishPlural(ObjectClass) to the i18n catalog (all 12 spoken classes) and switched the
    describer from a rule-based pluraliser to that table. Host tests now cover camión->camiones,
    autobús->autobuses and sofá->sofás.
  rationale: >-
    Spanish plurals are not rule-based: "camión"->"camiones" and "autobús"->"autobuses" drop the
    accent (the plural becomes llana), while "sofá"->"sofás" keeps it. A suffix rule (+s/+es) cannot
    express this, and the beta's noun set is small and fixed, so an explicit table is both correct and
    simpler. It also keeps all Spanish strings in the i18n module (INV-042). The bug surfaced because
    test_describer's "consonant-ending noun" case had asserted the wrong form; the test was corrected
    alongside the code.
  files:
    - Lumina-BETA-RPI-2W/src/i18n/es.hpp
    - Lumina-BETA-RPI-2W/src/i18n/es.cpp
    - Lumina-BETA-RPI-2W/src/app/describer.cpp
    - Lumina-BETA-RPI-2W/tests/test_describer.cpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Re-run host tests (expect all green) and the aarch64 build. When the es_MX voice replaces the
    es_ES stand-in (CHG-0021), revisit only if labels/plurals need regional wording.

# ---------------------------------------------------------------------------
# CHG-0024 — Fix: aarch64 compile errors in the audio path and main.cpp
# ---------------------------------------------------------------------------
- id: CHG-0024
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-030, INV-070]
  supersedes: null
  summary: >-
    Fixed the first aarch64 compile of the audio path. PiperTts and AlsaSink now define their real
    nested pimpl types (PiperTts::Impl / AlsaSink::Impl) instead of differently-named anonymous
    classes, and main.cpp fully qualifies the lumina:: namespaces.
  rationale: >-
    The headers forward-declare `class Impl;` and hold `std::unique_ptr<Impl>`, but the .cpp files
    defined `PiperTtsImpl`/`AlsaSinkImpl` in an anonymous namespace, leaving Impl incomplete (the
    errors were "invalid use of incomplete type" and the unique_ptr conversion/SFINAE failures).
    main.cpp used bare `core::`/`capture::`/`vision::`/`audio::`/`app::`, which only resolve inside
    namespace lumina; the host build did not catch either because LUMINA_ENABLE_AUDIO is off there.
  files:
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.cpp
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.cpp
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Re-run the aarch64 build. Watch for further first-compile issues in the ALSA calls (the rest of
    the file compiled only up to the pimpl error). The "Could NOT find OpenMP" line during configure
    is benign (ncnn is built with SIMPLEOMP; the detector already linked this way).

# ---------------------------------------------------------------------------
# CHG-0025 — Fix: silence (aborted utterances) + slice instrumentation
# ---------------------------------------------------------------------------
- id: CHG-0025
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-031, INV-032, INV-051, INV-052, INV-070]
  supersedes: null
  summary: >-
    Fixed "person detected but zero audio" on the Pi. inferenceLoop no longer sets m_stopSpeech on
    every enqueue (it aborted each utterance before the first chunk was written); m_stopSpeech is now
    only a shutdown/preemption signal. speechLoop drains after each phrase instead of dropping the
    buffer, phrases must persist for a couple of frames (hysteresis) before being spoken, and the
    slice now logs speech start, event->audible latency, sample counts, a 5-second FPS/RSS line, and
    honours a LUMINA_LOG_LEVEL environment variable.
  rationale: >-
    On-device evidence: aplay -D bluealsa works and the app triggered the ALSA path only when a person
    appeared, yet nothing was audible while AlsaSink::stop() (snd_pcm_drop + prepare) cycled
    repeatedly. Root cause was the interrupt design: pipeline.cpp set m_stopSpeech=true on every
    enqueue, and PiperTts::synthesize checks it at the top of every chunk, so any new detection during
    synthesis (flickering person counts bypass the exact-string de-dup) aborted the utterance before a
    single sample was written, producing the drop/prepare spam and silence. Letting utterances finish
    (drain instead of drop, capacity-1 queue keeps the newest phrase) restores audio and is the
    correct slice behaviour; the real arbiter on Day 3 will set m_stopSpeech only for safety
    preemption. The new INFO instrumentation is required to verify the fix and to measure INV-051
    (event->audible) and INV-052 (RSS) at the Day-2 gate.
  files:
    - Lumina-BETA-RPI-2W/src/app/pipeline.hpp
    - Lumina-BETA-RPI-2W/src/app/pipeline.cpp
    - Lumina-BETA-RPI-2W/src/audio/audio_sink.hpp
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.hpp
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.cpp
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.cpp
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/tests/mocks/mock_audio_sink.hpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Rebuild host (tests) and aarch64, redeploy the binary, and run with LUMINA_LOG_LEVEL=debug.
    Expect: "speech: 'Veo 1 persona.'", "Piper: wrote N samples", and "spoken: event->audible X ms",
    plus audible speech in the buds and a 5s FPS/RSS line. If audio is still silent, writes will now
    surface as ALSA errors; fallback is to convert Piper's float32 to S16_LE in AlsaSink (the format
    aplay -D bluealsa uses). Record the gate numbers in docs/PERFORMANCE.md.

# ---------------------------------------------------------------------------
# CHG-0026 — Fix: dropped final Piper chunk (still no audio)
# ---------------------------------------------------------------------------
- id: CHG-0026
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-032, INV-051, INV-070]
  supersedes: null
  summary: >-
    Fixed PiperTts::synthesize discarding the final audio chunk. The loop now processes each chunk
    before honoring PIPER_DONE, so short one-clause phrases actually reach the sink. Added a Debug
    line with samples/phoneme ids/last flag per chunk.
  rationale: >-
    On-device evidence: synthesis took ~3.5 s but reported "wrote 0 samples", and BlueALSA showed
    drain/start with no data. libpiper's piper_synthesize_next() returns the final chunk together with
    PIPER_DONE (libpiper/src/piper.cpp:742 `return chunk->is_last ? PIPER_DONE : PIPER_OK`), and our
    loop broke on PIPER_DONE before writing, so the whole utterance (a single chunk for one clause)
    was dropped. This was not the ALSA path (aplay -D bluealsa works) nor phonemization (start
    succeeded). The library README example has the same pitfall, hence the explicit note in the code.
  files:
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.cpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Rebuild host+aarch64, redeploy the binary, and confirm audible speech. Then address INV-051: the
    observed ~3.5 s synthesis for a short phrase exceeds the < 600 ms alert budget; candidates are a
    low/x_low es_MX voice and/or pre-rendering our small fixed phrase set.

# ---------------------------------------------------------------------------
# CHG-0027 — Fix: repeated utterances failed with EBADFD after drain
# ---------------------------------------------------------------------------
- id: CHG-0027
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-032, INV-051, INV-070]
  supersedes: null
  summary: >-
    Fixed the second and later utterances failing with "ALSA: write failed: File descriptor in bad
    state" (EBADFD). AlsaSink now (re)prepares the PCM before writing when its state is not
    RUNNING/PREPARED, prepares again after snd_pcm_drain(), and re-prepares once as a last-resort
    recovery on a failed write.
  rationale: >-
    On-device evidence: the first phrase played, then every later phrase logged EBADFD. snd_pcm_drain()
    leaves the stream in SND_PCM_STATE_SETUP, and the previous per-utterance snd_pcm_drop()+prepare()
    that used to reset it was removed in CHG-0025; snd_pcm_recover() does not handle EBADFD. Preparing
    on demand fixes it and is robust to xrun/suspend states too.
  files:
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.hpp
    - Lumina-BETA-RPI-2W/src/audio/bluealsa_sink.cpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Confirm repeated person detections are all audible after the first.

# ---------------------------------------------------------------------------
# CHG-0028 — Perf: make Piper's ONNX Runtime intra-op threads configurable
# ---------------------------------------------------------------------------
- id: CHG-0028
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-051, INV-070]
  supersedes: null
  summary: >-
    Added third_party/patches/libpiper-threads.patch so libpiper reads PIPER_NUM_THREADS (default 3)
    for ONNX Runtime intra-op threads instead of hard-coding 1; updated scripts/5-build_libpiper.sh to
    apply both vendored patches.
  rationale: >-
    libpiper/src/piper.cpp hard-codes SetIntraOpNumThreads(1)/SetInterOpNumThreads(1), leaving Piper
    single-threaded on a 4-core A53 — the main cause of the ~3.5 s synthesis (INV-051). Default 3
    leaves one core for capture/other work; the env var allows tuning without rebuilding. Inter-op
    stays 1 because the graph is sequential.
  files:
    - Lumina-BETA-RPI-2W/third_party/patches/libpiper-threads.patch
    - Lumina-BETA-RPI-2W/scripts/5-build_libpiper.sh
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Re-run scripts/5-build_libpiper.sh and redeploy libpiper.so; measure synthesis time and tune
    PIPER_NUM_THREADS (2/3/4) against detection under co-load.

# ---------------------------------------------------------------------------
# CHG-0029 — es_MX voice fetch script + default voice (x_low)
# ---------------------------------------------------------------------------
- id: CHG-0029
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-042, INV-051, INV-052, INV-070]
  supersedes: null
  summary: >-
    Added scripts/6-fetch_voices.sh (pinned, SHA-256-verified) and switched the default voice to
    models/voices/es_MX-ald-x_low.onnx. This satisfies INV-042 (es_MX) and supersedes the temporary
    es_ES stand-in accepted in CHG-0021.
  rationale: >-
    There is no "low" for this voice; es_MX-ald-x_low (20.9 MB) is the smallest es_MX Piper model,
    which cuts synthesis time and memory (INV-051/INV-052) versus the 76.7 MB es_ES medium stand-in.
    The .onnx is verified by sha256 d8aae54a...; source is the official rhasspy/piper-voices repo.
  files:
    - Lumina-BETA-RPI-2W/scripts/6-fetch_voices.sh
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/AGENTS.md
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Run scripts/6-fetch_voices.sh, deploy the voice, and re-check labels/quality and RSS.

# ---------------------------------------------------------------------------
# CHG-0030 — On-disk phrase cache + latency metrics
# ---------------------------------------------------------------------------
- id: CHG-0030
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-051, INV-052, INV-070]
  supersedes: null
  summary: >-
    Added an on-disk phrase cache (CachingTts, an ITtsEngine decorator) that pre-renders our fixed
    narration phrases and replays them instantly; PCM lives on disk (default /tmp/lumina-phrase-cache,
    override LUMINA_PHRASE_CACHE_DIR) to respect the RAM budget. Added phraseCatalog()/formatCount()
    to the describer, wired warming into main, and refined latency logging (event->speech-start,
    "Piper: first audio after X ms", event->end).
  rationale: >-
    Even with more threads, live Piper synthesis is seconds, so the < 600 ms alert target (INV-051)
    needs pre-rendered audio for our small, fixed vocabulary. Keeping samples on disk avoids holding
    every phrase in RAM (INV-052); entries are one file per phrase keyed by an FNV-1a hash, validated
    by a header + the phrase text, and written atomically. Uncached text falls through to live
    synthesis, so behaviour is always correct.
  files:
    - Lumina-BETA-RPI-2W/src/audio/caching_tts.hpp
    - Lumina-BETA-RPI-2W/src/audio/caching_tts.cpp
    - Lumina-BETA-RPI-2W/src/app/describer.hpp
    - Lumina-BETA-RPI-2W/src/app/describer.cpp
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.cpp
    - Lumina-BETA-RPI-2W/src/app/pipeline.cpp
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/tests/test_caching_tts.cpp
    - Lumina-BETA-RPI-2W/tests/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CMakeLists.txt
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    On the Pi, confirm the first run renders the phrases (~seconds) and later runs skip rendering;
    measure event->speech-start (should be ~0 on cache hits) and record the Day-2 gate numbers.

# ---------------------------------------------------------------------------
# CHG-0031 — Narration wording: articles, spelled numbers, noun-phrase style
# ---------------------------------------------------------------------------
- id: CHG-0031
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-042, INV-070]
  supersedes: null
  summary: >-
    Reworked the Spanish narration to be a noun phrase about the environment, not a sentence about
    the device: dropped "Veo", use the gendered article for one item ("una persona", "un carro"),
    spell out numbers two..ten ("dos personas"), use "más de diez" above ten, and end with
    "enfrente". Multi-item lists join as "a, b y c". Also switched labels coche->carro and
    motocicleta->moto (es_MX).
  rationale: >-
    Lúmina narrates the environment for a blind user; "una persona enfrente" is the intended framing
    rather than "Veo una persona". Spanish uses the article instead of the numeral one, and the user
    asked to avoid digits so the TTS engine never normalizes numbers. Words live in the i18n catalog
    (INV-042/FR-08).
  files:
    - Lumina-BETA-RPI-2W/src/i18n/es.hpp
    - Lumina-BETA-RPI-2W/src/i18n/es.cpp
    - Lumina-BETA-RPI-2W/src/app/describer.hpp
    - Lumina-BETA-RPI-2W/src/app/describer.cpp
    - Lumina-BETA-RPI-2W/src/audio/piper_tts.cpp
    - Lumina-BETA-RPI-2W/tests/test_describer.cpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Listen on-device and confirm wording/pronunciation; revisit the "enfrente" direction once bbox
    left/right/center and distance are available.

# ---------------------------------------------------------------------------
# CHG-0032 — Expand the default narration class subset
# ---------------------------------------------------------------------------
- id: CHG-0032
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-040, INV-042, INV-070]
  supersedes: null
  summary: >-
    Expanded core::Config::classIds from {person} to all narrated classes
    {0,1,2,3,5,7,15,16,24,56,57,60} (person, bicycle, car, motorcycle, bus, truck, cat, dog,
    backpack, chair, couch, dining table).
  rationale: >-
    FR-01 requires chair/table/backpack/dog/cat (plus person) in the live demo, and the user asked to
    narrate the full labeled set. More classes means more chatter; the Day-3 alert arbiter will add
    priority/cooldown, and the list stays config-driven (FR-08) so it can be trimmed without code.
  files:
    - Lumina-BETA-RPI-2W/src/core/config.hpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Evaluate narration churn indoors (chairs/tables) and trim classIds if needed.

# ---------------------------------------------------------------------------
# CHG-0033 — Persistent cache dir + lazy caching; warm range 1..3
# ---------------------------------------------------------------------------
- id: CHG-0033
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: impl
  status: applied
  invariants: [INV-051, INV-052, INV-070]
  supersedes: null
  summary: >-
    Moved the TTS phrase cache default to a persistent path (main() computes
    $HOME/.cache/lumina/phrase-cache; override LUMINA_PHRASE_CACHE_DIR) because /tmp is a tmpfs on
    the target OS, and added lazy caching: on a miss, CachingTts synthesizes through a TeeSink and
    stores the audio for next time. phraseCatalog now warms counts 1..3 per class.
  rationale: >-
    /tmp being tmpfs means the cache would consume RAM and be lost on reboot (INV-052), so it must be
    disk-backed and persistent. Eager warming is limited to counts 1..3 (classes x3) to keep the
    one-time render short; counts >= 4 and multi-class phrases are cached lazily on first use, so the
    runtime still converges to instant playback (INV-051) without a long first run.
  files:
    - Lumina-BETA-RPI-2W/src/audio/caching_tts.hpp
    - Lumina-BETA-RPI-2W/src/audio/caching_tts.cpp
    - Lumina-BETA-RPI-2W/src/app/describer.hpp
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/tests/test_caching_tts.cpp
    - Lumina-BETA-RPI-2W/README.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Verify the cache persists across reboots and that a second run logs "0 new phrase(s)".

# ---------------------------------------------------------------------------
# CHG-0034 — Fix: lazy cache never created its directory
# ---------------------------------------------------------------------------
- id: CHG-0034
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: fix
  status: applied
  invariants: [INV-051, INV-070]
  supersedes: null
  summary: >-
    Fixed CachingTts's lazy path: it now ensures the cache directory exists before storing, so a
    first-heard phrase (no prior warm()) is written to disk and served from cache next time. Also
    initialized the new PhraseCacheConfig::tag in the caching tests to silence
    -Wmissing-field-initializers.
  rationale: >-
    Only warm() called std::filesystem::create_directories; the lazy miss path (synthesize -> store)
    did not, so store() could not create its .tmp file and silently failed, making the same phrase
    synthesize live every time. Found by the "a live miss is stored and then served from cache" test
    (inner.calls() == 2).
  files:
    - Lumina-BETA-RPI-2W/src/audio/caching_tts.cpp
    - Lumina-BETA-RPI-2W/tests/test_caching_tts.cpp
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Re-run host tests; confirm the lazy-store test passes and no missing-field warnings remain.

# ---------------------------------------------------------------------------
# CHG-0035 — Day-2 gate verified end-to-end (vertical slice works)
# ---------------------------------------------------------------------------
- id: CHG-0035
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: test
  status: applied
  invariants: [INV-050, INV-051, INV-052, INV-070, INV-071]
  supersedes: null
  summary: >-
    Recorded the Day-2 gate run: camera -> YOLO11n/NCNN -> Spanish -> Piper/es_MX -> bluealsa works
    end-to-end and is audible in the paired buds. Full numbers in docs/PERFORMANCE.md section 10.
  rationale: >-
    Gate evidence (Pi Zero 2 W, voice es_MX-ald-x_low, 320x256, 4 NCNN threads, PIPER_NUM_THREADS=3):
    phrase cache warmed 36 phrases once (~60 s) into $HOME/.cache/lumina/phrase-cache; RSS 157 MB
    idle (189 MB transient during live synthesis); detection 4.0-4.3 FPS under co-load; cached alert
    event->speech-start 247-271 ms and event->end ~2.3 s (utterance ~1.75 s); first multi-class phrase
    (lazy miss) first audio 6.7 s then cached. Misclassification observed (person<->motorcycle) is a
    model limitation, out of scope. Gate outcome: PASS, so the Python-nightly fallback is not needed.
  files:
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Repetition churn and the INV-050 co-load gap are tracked separately (CHG-0036 and the Day-3
    arbiter). Recall validation (section 7) remains outstanding.

# ---------------------------------------------------------------------------
# CHG-0036 — Decision: temporarily accept end-to-end detection below 5 FPS
# ---------------------------------------------------------------------------
- id: CHG-0036
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-050, INV-071, INV-070]
  supersedes: null
  summary: >-
    Temporarily accept the full-pipeline detection rate (~4.0-4.3 FPS under co-load) as below the
    >= 5 FPS INV-050 target, to protect the 6-day schedule. The target is NOT relaxed; this is an
    explicit, temporary gap to be revisited only if time remains after the core MVP (Days 3-5).
  rationale: >-
    The isolated inference benchmark still meets the target (5.27 FPS at 320x256), so the gap is
    integration overhead, not a detector regression. The user prioritizes finishing the project in
    the remaining 5 days; experimentation (decimation, 256x256, NanoDet) is deferred. Documented in
    INVARIANTS.md (INV-050 amendment) and docs/PERFORMANCE.md section 10 so the gap is never mistaken
    for a met target.
  files:
    - Lumina-BETA-RPI-2W/INVARIANTS.md
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Revisit only after the MVP is complete and time remains. Escalation unchanged: decimation
    (infer every N frames) -> 256x256 -> NanoDet-Plus.

# ---------------------------------------------------------------------------
# CHG-0037 — Default voice: es_MX-claude-high (ald kept as fallback)
# ---------------------------------------------------------------------------
- id: CHG-0037
  date: 2026-09-16
  agent: opencode/deepseek-flash
  type: decision
  status: applied
  invariants: [INV-042, INV-051, INV-052, INV-070]
  supersedes: null
  summary: >-
    Switched the default Piper voice to es_MX-claude-high (63 MB, Mexican Spanish, "high"
    quality), which the user auditioned and approved. scripts/6-fetch_voices.sh now downloads
    claude-high plus the two ald fallbacks (medium, x_low), all SHA-256 verified.
  rationale: >-
    The es_MX-ald voices sounded poor (ald is a finetune of the Spain davefx voice on a small
    community dataset). Enumeration of the official piper-voices repo confirmed there is no other
    Mexican/Latin American voice at low/medium/x_low (es_AR only ships high; the rest are es_ES).
    claude-high is the same ~63 MB as ald-medium, so no compression is needed; the phrase cache
    absorbs its (re-measure TBD) synthesis cost and INV-051 at runtime. ald is retained as a
    smaller fallback resource.
  files:
    - Lumina-BETA-RPI-2W/scripts/6-fetch_voices.sh
    - Lumina-BETA-RPI-2W/src/main.cpp
    - Lumina-BETA-RPI-2W/docs/PERFORMANCE.md
    - Lumina-BETA-RPI-2W/CHANGELOG.md
  approvals: [user]
  follow_up: >-
    Deploy es_MX-claude-high.onnx + .json to the Pi and re-measure synthesis time and RSS
    (docs/PERFORMANCE.md section 10); the phrase cache re-renders automatically because the cache
    tag includes the voice path.
```
