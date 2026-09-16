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
```

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
```
