# ---------------------------------------------------------------------------
# Cross-compilation toolchain for the Raspberry Pi Zero 2 W (aarch64, glibc).
#
# Used by CMakePresets.json -> "aarch64". Build on the laptop, deploy to the Pi:
#   cmake --preset aarch64 && cmake --build --preset aarch64
#
# Prerequisites (verify on your machine; INV-001 — do not assume):
#   - aarch64 cross toolchain, e.g. on Debian/Ubuntu:
#       sudo apt install g++-aarch64-linux-gnu
#   - Ideally point LUMINA_SYSROOT at a Raspberry Pi OS (Bookworm) aarch64 sysroot
#     so libcamera/OpenCV headers and libraries resolve. A sysroot can be rsynced
#     from the Pi (e.g. /usr/include, /usr/lib, /lib).
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LUMINA_CROSS_PREFIX "aarch64-linux-gnu" CACHE STRING "Cross-toolchain prefix")
set(LUMINA_SYSROOT "" CACHE PATH "Optional aarch64 sysroot (Raspberry Pi OS Bookworm)")

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
endif()
