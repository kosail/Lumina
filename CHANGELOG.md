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
  status: proposed
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
```
