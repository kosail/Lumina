// ---------------------------------------------------------------------------
// Frame geometry helpers: rotate a captured frame and serialize it for diagnostics.
//
// The camera in the glasses is physically mounted rotated, so libcamera delivers
// sideways frames. Rotating them back to upright before inference restores object
// and face recognition. The transform is pure (no libcamera, no clock) so it runs
// on the host and is unit-tested (AGENTS §9).
//
// C++ note (for Java readers): these are free functions, not methods, because they
// only transform a value; `[[nodiscard]]` makes the compiler warn if the returned
// frame is accidentally ignored.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <vector>

#include "core/frame.hpp"

namespace lumina::core {

// Rotate an RGB888 frame clockwise by `degrees` (0/90/180/270). 90/270 swap the
// output dimensions; 0 returns an unchanged copy. A non-RGB888 frame or an angle
// that is not a multiple of 90 also returns an unchanged copy (the caller logs and
// keeps running). `stride` is honoured on both input and output, so a padded
// capture row never leaks into the image.
[[nodiscard]] Frame rotateFrame(const Frame& frame, int degrees);

// Serialize an RGB888 frame as a binary PPM (P6) so the on-device angle check can
// `scp` one corrected frame and view it. Pure: returns the exact bytes; the caller
// writes the file. Returns an empty vector for a non-RGB888 or empty frame.
[[nodiscard]] std::vector<std::uint8_t> encodePpm(const Frame& frame);

}  // namespace lumina::core
