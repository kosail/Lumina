# ---------------------------------------------------------------------------
# Lúmina beta runtime — top-level build configuration.
#
# Target: Raspberry Pi Zero 2 W (aarch64, glibc) — see INVARIANTS.md (INV-010, INV-024).
# Language: C++23 (GCC 14.2.1), no compiler extensions — see INVARIANTS.md (INV-020, INV-025).
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

# C++23 is mandatory and compiler-specific extensions are disabled (INV-020).
set(CMAKE_CXX_STANDARD 23)
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
# The libcamera capture path needs the target's libcamera. OFF by default so the
# host/mock build links no camera libraries; enable for the on-device probe/runtime.
option(LUMINA_ENABLE_LIBCAMERA "Build the libcamera capture path" OFF)         # INV-011
# The NCNN detector path needs the cross-built static libncnn.a in third_party/ncnn.
option(LUMINA_ENABLE_NCNN "Build the NCNN detector path" OFF)                  # INV-022
# The Piper TTS + ALSA/bluealsa audio path needs libpiper (third_party/libpiper)
# and libasound from the sysroot; OFF by default so host builds link neither.
option(LUMINA_ENABLE_AUDIO "Build the Piper TTS + ALSA/bluealsa audio path" OFF) # INV-022, INV-060
# The OpenCV face path (YuNet + SFace) needs OpenCV 4.10 from the target sysroot;
# OFF by default so the host/mock build links no OpenCV (INV-025, FR-03).
option(LUMINA_ENABLE_FACE "Build the OpenCV face recognition path" OFF)

# Tests default ON so the laptop build exercises pure logic (AGENTS.md §9); the
# aarch64 preset turns them OFF. Benchmarks run on the Pi and default OFF.
option(LUMINA_BUILD_TESTS "Build host unit tests (doctest)" ON)
option(LUMINA_BUILD_BENCH "Build on-device benchmark targets" OFF)

set(LUMINA_FACE_EMBEDDER   "sface" CACHE STRING "Face embedder: sface | mobilefacenet")
set(LUMINA_INFER_PRECISION "fp16"  CACHE STRING "Inference precision: fp16 | int8")
set(LUMINA_INFER_WIDTH     "320"   CACHE STRING "Detector input width in pixels")
set(LUMINA_INFER_HEIGHT    "256"   CACHE STRING "Detector input height in pixels")

# ---------------------------------------------------------------------------
# Core library. Pipeline logic lives here so the executable and the host tests
# share exactly the same code; tests link this and need no hardware (INV-030).
# Add new .cpp files here as modules are implemented.
# ---------------------------------------------------------------------------
add_library(lumina_core STATIC
    src/core/logging.cpp
    src/i18n/es.cpp
    src/vision/yolo11_decode.cpp
    src/vision/face_store.cpp
    src/vision/image_list.cpp
    src/processing/distance.cpp
    src/alerts/arbiter.cpp
    src/app/scene.cpp
    src/app/describer.cpp
    src/app/face_greeter.cpp
    src/app/pipeline.cpp
    src/audio/caching_tts.cpp)  # describer/pipeline/caching_tts use interfaces only -> host-buildable

# Let sources include project headers as "core/..." rather than relative paths.
# PUBLIC so anything linking lumina_core inherits the same include path.
target_include_directories(lumina_core PUBLIC src)

# Warn aggressively while the codebase is small; warnings are signals, not noise.
target_compile_options(lumina_core PUBLIC -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Optional libcamera capture source (INV-011). libcamera comes from the target
# sysroot via pkg-config; when cross-compiling, CMakePresets points pkg-config at
# cmake/rpi-sysroot (see the aarch64 preset's `environment`).
# ---------------------------------------------------------------------------
if(LUMINA_ENABLE_LIBCAMERA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)
    target_sources(lumina_core PRIVATE src/capture/libcamera_source.cpp)
    target_link_libraries(lumina_core PUBLIC PkgConfig::LIBCAMERA)
    target_compile_definitions(lumina_core PUBLIC LUMINA_HAS_LIBCAMERA=1)
endif()

# ---------------------------------------------------------------------------
# Optional NCNN detector path (INV-022). Links the static libncnn.a produced by
# scripts/build_ncnn.sh; the imported target `ncnn` also carries Threads/pthread.
# ---------------------------------------------------------------------------
if(LUMINA_ENABLE_NCNN)
    # Default to the normal ncnn install. Point at third_party/ncnn-bench (built by
    # scripts/2-build_ncnn.sh --layer-benchmark) for per-layer profiling.
    set(LUMINA_NCNN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ncnn" CACHE PATH
        "ncnn install prefix (e.g. third_party/ncnn-bench for the instrumented build)")
    # NO_CMAKE_FIND_ROOT_PATH is required: cmake/toolchain-aarch64.cmake sets
    # CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY, which re-roots every package search
    # into the sysroot. Without it, this in-tree path (outside the sysroot) is
    # silently ignored and ncnn is reported missing.
    find_package(ncnn REQUIRED
        PATHS "${LUMINA_NCNN_ROOT}/lib/cmake/ncnn"
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH)
    target_sources(lumina_core PRIVATE src/vision/ncnn_detector.cpp)
    target_link_libraries(lumina_core PUBLIC ncnn)
    target_compile_definitions(lumina_core PUBLIC LUMINA_HAS_NCNN=1)
endif()

# ---------------------------------------------------------------------------
# Optional Piper + ALSA/bluealsa audio path (INV-022, INV-060). libpiper is
# GPL-3.0 and is linked as a SHARED library behind ITtsEngine: the boundary that
# INV-060 requires. libasound comes from the target sysroot via pkg-config.
# ---------------------------------------------------------------------------
if(LUMINA_ENABLE_AUDIO)
    set(LUMINA_LIBPIPER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpiper" CACHE PATH
        "libpiper install prefix (built by scripts/5-build_libpiper.sh)")
    # NO_CMAKE_FIND_ROOT_PATH: our toolchain restricts library/package searches to
    # the sysroot, which would hide this in-tree path (same reason as ncnn above).
    find_path(LIBPIPER_INCLUDE_DIR piper.h
        PATHS "${LUMINA_LIBPIPER_ROOT}/include" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(LIBPIPER_LIBRARY piper
        PATHS "${LUMINA_LIBPIPER_ROOT}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT LIBPIPER_INCLUDE_DIR OR NOT LIBPIPER_LIBRARY)
        message(FATAL_ERROR
            "libpiper not found under ${LUMINA_LIBPIPER_ROOT}. Run scripts/5-build_libpiper.sh.")
    endif()
    add_library(libpiper::libpiper UNKNOWN IMPORTED)
    set_target_properties(libpiper::libpiper PROPERTIES
        IMPORTED_LOCATION "${LIBPIPER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBPIPER_INCLUDE_DIR}")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

    target_sources(lumina_core PRIVATE
        src/audio/piper_tts.cpp
        src/audio/bluealsa_sink.cpp)
    target_link_libraries(lumina_core PUBLIC libpiper::libpiper PkgConfig::ALSA)
    # The shared libpiper needs libonnxruntime.so at runtime; help the linker
    # resolve its NEEDED entry during our own link.
    target_link_options(lumina_core PUBLIC "-Wl,-rpath-link,${LUMINA_LIBPIPER_ROOT}/lib")
    target_compile_definitions(lumina_core PUBLIC LUMINA_HAS_AUDIO=1)
endif()

# ---------------------------------------------------------------------------
# Optional OpenCV face recognition path (FR-03, INV-025). OpenCV 4.10 comes from
# the target sysroot; the Debian OpenCV config self-locates relative to its
# cmake/opencv4 directory, so pointing OpenCV_DIR at it keeps the whole
# resolution inside cmake/rpi-sysroot and never touches a host OpenCV install.
# ---------------------------------------------------------------------------
if(LUMINA_ENABLE_FACE)
    if(NOT DEFINED OpenCV_DIR AND DEFINED LUMINA_SYSROOT AND NOT LUMINA_SYSROOT STREQUAL "")
        set(OpenCV_DIR "${LUMINA_SYSROOT}/usr/lib/aarch64-linux-gnu/cmake/opencv4"
            CACHE PATH "OpenCV CMake config directory (inside the sysroot)")
    endif()
    # Only the modules the runtime needs. imgcodecs (photo decoding) is linked
    # only by lumina_enroll below, so the runtime never pulls its heavy GDAL
    # dependency (which would also waste RAM, INV-052).
    find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc objdetect imgcodecs)
    target_sources(lumina_core PRIVATE src/vision/face.cpp)
    target_link_libraries(lumina_core PUBLIC opencv_core opencv_imgproc opencv_objdetect)
    target_compile_definitions(lumina_core PUBLIC LUMINA_HAS_FACE=1)
    # Debian's libopencv_core needs liblapack.so.3/libblas.so.3. In the synced
    # sysroot the top-level names are dangling /etc/alternatives symlinks; the real
    # files live under blas/ and lapack/, so point the linker at them. The synced
    # sysroot is also missing a few transitive libraries (e.g. libarmadillo pulled
    # by GDAL); --allow-shlib-undefined defers those to runtime, where the Pi has
    # the full set. This affects every face target, so it lives on lumina_core.
    if(DEFINED LUMINA_SYSROOT AND NOT LUMINA_SYSROOT STREQUAL "")
        target_link_options(lumina_core PUBLIC
            "-Wl,-rpath-link,${LUMINA_SYSROOT}/usr/lib/aarch64-linux-gnu/blas"
            "-Wl,-rpath-link,${LUMINA_SYSROOT}/usr/lib/aarch64-linux-gnu/lapack"
            "-Wl,--allow-shlib-undefined")
    endif()
endif()

# ---------------------------------------------------------------------------
# Executable. The binary is named `lumina` to match the documented output path
# build/<preset>/lumina (AGENTS.md §9, RAW_PLAN.md §8).
# ---------------------------------------------------------------------------
add_executable(lumina src/main.cpp)
target_link_libraries(lumina PRIVATE lumina_core)

# ---------------------------------------------------------------------------
# On-device capture probe (headless; INV-021). Built only with the capture path.
# ---------------------------------------------------------------------------
if(LUMINA_ENABLE_LIBCAMERA)
    add_executable(lumina_capture_test src/tools/capture_probe.cpp)
    target_link_libraries(lumina_capture_test PRIVATE lumina_core)
endif()

# Face enrollment tool (FR-04). Needs the face path; camera capture inside the
# tool is optional (compiled only when LIBCAMERA is also enabled), so it can
# enroll from photos on any FACE-enabled build.
if(LUMINA_ENABLE_FACE)
    add_executable(lumina_enroll src/tools/enroll_face.cpp)
    # Photo decoding needs imgcodecs; the runtime itself stays codec-free (the
    # GDAL/armadillo link quirk is handled by lumina_core's link options above).
    target_link_libraries(lumina_enroll PRIVATE lumina_core opencv_imgcodecs)
endif()

# ---------------------------------------------------------------------------
# Tests + benchmarks (host unit tests use doctest and mocks, AGENTS.md §9).
# ---------------------------------------------------------------------------
if(LUMINA_BUILD_TESTS OR LUMINA_BUILD_BENCH)
    enable_testing()
    add_subdirectory(tests)
endif()
