// ---------------------------------------------------------------------------
// Per-frame speech decision: obstacle warning vs. scene description.
//
// Pure, hardware-free logic (AGENTS §9): given the detector output for one frame,
// decide what Lúmina should say. A near, in-path obstacle yields a Warning alert
// (FR-02); otherwise the frame becomes a Description alert. Kept out of Pipeline
// so the policy is unit-tested without threads or hardware.
// ---------------------------------------------------------------------------

#pragma once

#include <optional>
#include <vector>

#include "alerts/alert.hpp"
#include "core/config.hpp"
#include "core/detection.hpp"
#include "core/time.hpp"

namespace lumina::app {

// Build the alert for one frame, or nullopt when there is nothing worth saying.
//
// `capturedAt` is carried into the alert so the speech worker can report
// event->audible latency (INV-051). Precedence: nearest in-path obstacle (Near,
// then Mid), else the multi-class Spanish description.
[[nodiscard]] std::optional<alerts::Alert> buildSceneAlert(
    const std::vector<core::Detection>& detections,
    const core::Config& config,
    int frameWidth,
    int frameHeight,
    core::TimePoint capturedAt);

} // namespace lumina::app
