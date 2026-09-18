// ---------------------------------------------------------------------------
// Distance heuristic: estimate how close an object is from its bounding box.
//
// The beta has no depth camera in the vision path yet (the front VL53L0X sensor
// arrives in Phase C), so we approximate: an object that fills a large fraction
// of the frame is "near". This is deliberately pure, hardware-free logic
// (AGENTS §9) so it runs on the host under mocks and is unit-tested exhaustively.
//
// C++ note (for Java readers): these are free functions in a namespace rather
// than static methods of a utility class. Return values are marked [[nodiscard]]
// so the compiler warns if a caller accidentally ignores them.
// ---------------------------------------------------------------------------

#pragma once

#include "core/detection.hpp"

namespace lumina::processing {

// Coarse distance classification for an obstacle.
enum class DistanceBand {
    Near, // large in-frame: imminent
    Mid,  // moderate: worth a warning
    Far,  // small: ignore for alerts
};

// Thresholds for the heuristic. The runtime values live in core::Config, but the
// pure functions take this small value type so tests can call them directly
// without constructing an entire Config.
struct DistanceThresholds {
    // Box area / frame area at or above which the object is Near / Mid. Fractions
    // are in [0, 1]; `midAreaFraction` is expected to be <= `nearAreaFraction`
    // (classifyDistance normalises the order defensively so a swapped pair still
    // classifies sanely).
    float nearAreaFraction = 0.20F;
    float midAreaFraction = 0.06F;
    // Horizontal half-band around the frame centre that counts as "in the path",
    // as a fraction of frame width. 0 => only the exact centre; 1 => the whole row.
    float pathCenterTolerance = 0.35F;
};

// Fraction of the frame area covered by `box`, clamped to [0, 1]. Returns 0 for a
// non-positive frame size or an empty/negative box, so it never divides by zero.
[[nodiscard]] float boxAreaFraction(const core::BoundingBox& box,
                                    int frameWidth,
                                    int frameHeight) noexcept;

// Classify `box` as Near/Mid/Far. A non-positive frame size or an empty box is
// Far. NaN thresholds degrade safely to Far because every comparison is false.
[[nodiscard]] DistanceBand classifyDistance(const core::BoundingBox& box,
                                            int frameWidth,
                                            int frameHeight,
                                            const DistanceThresholds& thresholds) noexcept;

// True when the box is horizontally centred within `pathCenterTolerance` of the
// frame centre, i.e. the user is walking toward it (FR-02). False for a
// non-positive frame width or a non-finite centre.
[[nodiscard]] bool isInPath(const core::BoundingBox& box,
                            int frameWidth,
                            const DistanceThresholds& thresholds) noexcept;

// Convenience combining the two: a near obstacle directly in the user's path,
// which is what should raise a proximity alert (FR-02).
[[nodiscard]] bool isNearObstacleInPath(const core::BoundingBox& box,
                                        int frameWidth,
                                        int frameHeight,
                                        const DistanceThresholds& thresholds) noexcept;

} // namespace lumina::processing
