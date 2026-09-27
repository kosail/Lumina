// ---------------------------------------------------------------------------
// Per-frame speech decision implementation. See scene.hpp.
// ---------------------------------------------------------------------------

#include "app/scene.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "app/describer.hpp"
#include "i18n/es.hpp"
#include "processing/distance.hpp"

namespace lumina::app {

std::optional<alerts::Alert> buildSceneAlert(const std::vector<core::Detection>& detections,
                                             const core::Config& config,
                                             int frameWidth,
                                             int frameHeight,
                                             core::TimePoint capturedAt,
                                             std::optional<float> proximityMeters)
{
    const processing::DistanceThresholds thresholds{
        config.nearAreaFraction, config.midAreaFraction, config.pathCenterTolerance};

    // Fusion (FR-02.4): the true front ToF reading takes precedence at short
    // range. While it reports an obstacle at/under the threshold, the proximity
    // thread has already spoken the Safety alert, so say nothing from the camera
    // path this frame (warning or narration) to avoid competing with it.
    if (proximityMeters.has_value() && *proximityMeters <= config.proximityThresholdM) {
        return std::nullopt;
    }

    // Is any obstacle class NEAR and in the user's path? The alert phrase is generic
    // (it does not name the object), so we only need to know whether one is present.
    // A Mid-distance obstacle-class object is NOT an obstacle alert: it falls through
    // to the description so the object is named ("una silla enfrente"). This keeps
    // object narration dominant, while a genuinely large, centered hazard — or the
    // class-agnostic IR channel at short range — still warns.
    bool foundNear = false;

    for (const core::Detection& detection : detections) {
        const bool isObstacleClass =
            std::find(config.obstacleClassIds.begin(), config.obstacleClassIds.end(),
                      detection.classId) != config.obstacleClassIds.end();
        if (!isObstacleClass) {
            continue;
        }
        if (!processing::isInPath(detection.box, frameWidth, thresholds)) {
            continue;
        }
        if (processing::classifyDistance(detection.box, frameWidth, frameHeight, thresholds) ==
            processing::DistanceBand::Near) {
            foundNear = true;
        }
    }

    if (foundNear) {
        alerts::Alert alert;
        alert.priority = alerts::Priority::Warning;  // Near preempts narration
        alert.source = alerts::Source::Obstacle;
        alert.text = i18n::proximityAlertPhrase(true);
        alert.dedupKey = alert.text;  // same wording => same scene
        alert.detectedAt = capturedAt;
        return alert;
    }

    // No obstacle: fall back to narrating the scene. Item count comes from config
    // (capped at 2) to match the pre-warmed two-class phrase catalog.
    std::string description =
        describeDetections(detections, config, static_cast<std::size_t>(config.maxNarratedItems));
    if (description.empty()) {
        return std::nullopt;
    }
    alerts::Alert alert;
    alert.priority = alerts::Priority::Description;
    alert.source = alerts::Source::Description;
    alert.text = std::move(description);
    alert.dedupKey = alert.text; // the text encodes the class set/counts (scene signature)
    alert.detectedAt = capturedAt;
    return alert;
}

} // namespace lumina::app
