// ---------------------------------------------------------------------------
// Distance heuristic implementation. See distance.hpp for the rationale.
// ---------------------------------------------------------------------------

#include "processing/distance.hpp"

#include <algorithm>
#include <cmath>

namespace lumina::processing {

float boxAreaFraction(const core::BoundingBox& box, int frameWidth, int frameHeight) noexcept
{
    // Guard every divisor and reject degenerate boxes (width/height <= 0) so an
    // empty detection can never produce a division by zero or a NaN.
    if (frameWidth <= 0 || frameHeight <= 0 || !(box.width > 0.0F) || !(box.height > 0.0F)) {
        return 0.0F;
    }

    // Compute in double: box coordinates are floats and a large box could lose
    // precision, while the frame area is at most a few million pixels.
    const double frameArea = static_cast<double>(frameWidth) * static_cast<double>(frameHeight);
    const double boxArea = static_cast<double>(box.width) * static_cast<double>(box.height);
    const double fraction = boxArea / frameArea;

    // A box may extend past the frame; clamp the reported fraction into [0, 1].
    return std::clamp(static_cast<float>(fraction), 0.0F, 1.0F);
}

DistanceBand classifyDistance(const core::BoundingBox& box,
                              int frameWidth,
                              int frameHeight,
                              const DistanceThresholds& thresholds) noexcept
{
    // Tolerate a swapped/misconfigured pair: make Near the stricter threshold so
    // the Near band is always a subset of the Mid band.
    const float nearThreshold = std::max(thresholds.nearAreaFraction, thresholds.midAreaFraction);
    const float midThreshold = std::min(thresholds.nearAreaFraction, thresholds.midAreaFraction);

    const float fraction = boxAreaFraction(box, frameWidth, frameHeight);
    if (fraction >= nearThreshold) {
        return DistanceBand::Near;
    }
    if (fraction >= midThreshold) {
        return DistanceBand::Mid;
    }
    return DistanceBand::Far;
}

bool isInPath(const core::BoundingBox& box,
              int frameWidth,
              const DistanceThresholds& thresholds) noexcept
{
    if (frameWidth <= 0) {
        return false;
    }
    // Reject non-finite geometry (e.g. a NaN box from a bad detector) rather than
    // letting it compare false and silently return a wrong answer.
    if (!std::isfinite(box.x) || !std::isfinite(box.width)) {
        return false;
    }

    const float center = box.centerX();
    if (!std::isfinite(center)) {
        return false;
    }

    const float halfWidth = static_cast<float>(frameWidth) / 2.0F;
    const float tolerance = thresholds.pathCenterTolerance * static_cast<float>(frameWidth);
    return std::fabs(center - halfWidth) <= tolerance;
}

bool isNearObstacleInPath(const core::BoundingBox& box,
                          int frameWidth,
                          int frameHeight,
                          const DistanceThresholds& thresholds) noexcept
{
    return classifyDistance(box, frameWidth, frameHeight, thresholds) == DistanceBand::Near &&
           isInPath(box, frameWidth, thresholds);
}

} // namespace lumina::processing
