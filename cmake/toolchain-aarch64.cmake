# ---------------------------------------------------------------------------
# Cross-compilation toolchain for the Raspberry Pi Zero 2 W (aarch64, glibc).
#
# Used by CMakePresets.json -> "aarch64". Build on the laptop, deploy to the Pi:
#   cmake --preset aarch64 && cmake --build --preset aarch64
#
# Prerequisites (verify on your machine; INV-001 — do not assume):
#   - aarch64 cross toolchain, e.g. on Debian/Ubuntu:
#       sudo apt install g++-aarch64-linux-gnu
#   - Point LUMINA_SYSROOT at a Raspberry Pi OS / Debian 13 "trixie" aarch64 sysroot so
#     libcamera/OpenCV headers and libraries resolve. A sysroot is rsynced from the Pi
#     (e.g. /usr/include, /usr/lib). When `cmake/rpi-sysroot/` exists it is used by default.
#   - Host compiler must match the sysroot ABI (GCC 14.2.1) — see INV-023 / INV-025.
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LUMINA_CROSS_PREFIX "aarch64-linux-gnu" CACHE STRING "Cross-toolchain prefix")

# Prefer an explicit LUMINA_SYSROOT; otherwise auto-detect the in-tree sysroot if present.
if(NOT DEFINED LUMINA_SYSROOT OR LUMINA_SYSROOT STREQUAL "")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/rpi-sysroot/usr")
        set(LUMINA_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/rpi-sysroot" CACHE PATH
            "aarch64 sysroot (Raspberry Pi OS / Debian 13 trixie)")
    else()
        set(LUMINA_SYSROOT "" CACHE PATH
            "aarch64 sysroot (Raspberry Pi OS / Debian 13 trixie)")
    endif()
endif()

set(CMAKE_C_COMPILER   "${LUMINA_CROSS_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${LUMINA_CROSS_PREFIX}-g++")

if(LUMINA_SYSROOT)
    set(CMAKE_SYSROOT "${LUMINA_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${LUMINA_SYSROOT}")
    # Look for headers/libraries only inside the sysroot; find build tools on the host.
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

    # VERIFIED GOTCHA (Arch's aarch64-linux-gnu-gcc): plain --sysroot does NOT redirect
    # the toolchain's library search. Its built-in sysroot is /usr/aarch64-linux-gnu with a
    # `lib64` layout, so -lm/-lc fall back to the host's /usr/lib64 and fail with
    # "file in wrong format". We therefore force the Debian multiarch directories and pass
    # --sysroot straight to ld. See docs/CROSS_COMPILE.md and CHG-0008.
    set(_lumina_link_flags
        "-L${LUMINA_SYSROOT}/usr/lib/aarch64-linux-gnu"
        "-L${LUMINA_SYSROOT}/lib/aarch64-linux-gnu"
        "-Wl,--sysroot,${LUMINA_SYSROOT}"
        "-Wl,-rpath-link,${LUMINA_SYSROOT}/usr/lib/aarch64-linux-gnu")
    string(REPLACE ";" " " _lumina_link_flags "${_lumina_link_flags}")

    set(CMAKE_EXE_LINKER_FLAGS_INIT    "${CMAKE_EXE_LINKER_FLAGS_INIT} ${_lumina_link_flags}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT} ${_lumina_link_flags}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_MODULE_LINKER_FLAGS_INIT} ${_lumina_link_flags}")
    # Also feed the compiler flags so CMake's own compiler/ABI link probe succeeds
    # (the -L/-Wl arguments are harmless during compile-only steps).
    set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT} ${_lumina_link_flags}")
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_lumina_link_flags}")
endif()
