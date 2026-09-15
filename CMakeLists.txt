# ---------------------------------------------------------------------------
# Lúmina beta runtime — top-level build configuration.
#
# Target: Raspberry Pi Zero 2 W (aarch64, glibc) — see INVARIANTS.md (INV-010, INV-024).
# Language: C++20, no compiler extensions — see INVARIANTS.md (INV-020).
#
# Build presets live in CMakePresets.json:
#   cmake --preset host    && cmake --build --preset host     # laptop, mocks, fast tests
#   cmake --preset aarch64 && cmake --build --preset aarch64  # cross-build for the Pi
# ---------------------------------------------------------------------------

# 3.20 is the supported floor. Do NOT raise this to 4.x: the Raspberry Pi OS
# toolchain is older and must be able to configure the project (INV-023).
cmake_minimum_required(VERSION 3.20)

project(Lumina
    VERSION 0.1.0
    DESCRIPTION "Edge-AI assistive runtime for the Raspberry Pi Zero 2 W"
    LANGUAGES CXX)

# C++20 is mandatory and compiler-specific extensions are disabled (INV-020).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release outside an IDE/multi-config generator; presets override this.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Feature flags (see SPECS.md / RAW_PLAN.md). Optional subsystems default OFF so
# the core runtime stays minimal and within the 512 MB memory budget (INV-052).
# ---------------------------------------------------------------------------
option(LUMINA_ENABLE_PROXIMITY "Build the optional proximity-sensor path" OFF) # INV-013, INV-033
option(LUMINA_ENABLE_TELEMETRY "Build the optional UDP telemetry path" OFF)    # INV-034

set(LUMINA_FACE_EMBEDDER   "sface" CACHE STRING "Face embedder: sface | mobilefacenet")
set(LUMINA_INFER_PRECISION "fp16"  CACHE STRING "Inference precision: fp16 | int8")
set(LUMINA_INFER_SIZE      "320"   CACHE STRING "Detector input size in pixels")

# ---------------------------------------------------------------------------
# Executable. The binary is named `lumina` to match the documented output path
# build/<preset>/lumina (AGENTS.md §9, RAW_PLAN.md §8).
# ---------------------------------------------------------------------------
add_executable(lumina src/main.cpp)

# Let sources include project headers as "core/..." rather than relative paths.
target_include_directories(lumina PRIVATE src)

# Warn aggressively while the codebase is small; warnings are signals, not noise.
target_compile_options(lumina PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Tests. Unit tests use doctest and run on the host with mocks (AGENTS.md §9).
# Enabled once the first test sources and the doctest dependency exist (Day 0-1).
# ---------------------------------------------------------------------------
# enable_testing()
# add_subdirectory(tests)
