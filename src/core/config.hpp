#pragma once

#include <algorithm>
#include <vector>

namespace lumina::core {

// Weight precision the detector loads. FP16 is the default because the Cortex-A53
// has no INT8 dot-product unit (INV-012); INT8 is opt-in only after a measured win.
enum class InferPrecision {
    Fp16,
    Int8,
};

// Runtime-tunable settings for the pipeline. Keeping them in one value type (no
// globals) lets tests construct arbitrary variants freely (INV-030).
// C++ note (for Java readers): the `= value` after each member is a default member
// initializer, so `Config{}` produces a fully-populated, valid configuration.
struct Config {
    int inferWidth = 320;                             // model input width in px (INV-050)
    int inferHeight = 256;                            // model input height in px (INV-050)
    InferPrecision precision = InferPrecision::Fp16;  // INV-012
    float scoreThreshold = 0.25F;                     // minimum detection confidence
    float nmsThreshold = 0.45F;                       // non-max-suppression IoU cutoff
    // Distance heuristic (FR-02). An object that covers a large share of the frame
    // is treated as "near" because the beta has no depth camera in the vision path
    // yet (the VL53L0X sensor arrives in Phase C). Fractions are box area / frame
    // area in [0, 1]; `pathCenterTolerance` is the horizontal half-band around the
    // frame centre, as a fraction of frame width, that counts as "in the path".
    // Invariant: midAreaFraction <= nearAreaFraction (enforced by clampConfig).
    float nearAreaFraction = 0.20F;                   // >= this fraction => Near
    float midAreaFraction = 0.06F;                    // >= this fraction => Mid, else Far
    float pathCenterTolerance = 0.35F;                // |centerX - W/2| <= tol*W => in path
    // Maximum number of classes named in one description sentence. Capped at 2 so
    // sentences match the pre-warmed two-class phrase catalog (fewer live Piper
    // syntheses, which cannot be preempted). FR-01/FR-08.
    int maxNarratedItems = 2;
    // COCO class ids we narrate (all classes with a Spanish label; FR-01): person,
    // bicycle, car, bus, cat, dog, backpack, chair, couch, dining table.
    // Motorcycle (3) and truck (7) were removed from the beta and deferred as a
    // nice-to-have (INV-040); their i18n entries stay dormant so re-enabling them
    // is a config-only change (FR-08).
    std::vector<int> classIds{0, 1, 2, 5, 15, 16, 24, 56, 57, 60};
    // Subset of classIds that counts as an obstacle for proximity alerts (FR-02):
    // person, bicycle, car, bus, chair, couch, dining table. Small classes are
    // narrated but do not raise obstacle alerts.
    std::vector<int> obstacleClassIds{0, 1, 2, 5, 56, 57, 60};
    int faceStableFrames = 3;                         // frames before announcing a name
    float faceMatchThreshold = 0.50F;                 // embedding cosine similarity cutoff
};

// The approved detection input sizes (width x height), in preference order. The
// target is 320x256; the others exist for recall validation and as fallbacks
// (INV-050). All are multiples of 32, as the YOLO11 NCNN export requires.
[[nodiscard]] inline bool isSupportedInferSize(int width, int height) noexcept {
    return (width == 320 && height == 256) || (width == 256 && height == 256) ||
           (width == 320 && height == 320) || (width == 416 && height == 416);
}

// The documented defaults; tests use this as the known-valid baseline.
[[nodiscard]] inline Config defaultConfig() { return Config{}; }

// Return a copy with every field forced into its legal range. Apply this to
// untrusted input (files/CLI) so the rest of the code can trust the values.
[[nodiscard]] inline Config clampConfig(Config config) {
    if (!isSupportedInferSize(config.inferWidth, config.inferHeight)) {
        config.inferWidth = 320;
        config.inferHeight = 256;
    }
    config.scoreThreshold = std::clamp(config.scoreThreshold, 0.0F, 1.0F);
    config.nmsThreshold = std::clamp(config.nmsThreshold, 0.0F, 1.0F);
    config.nearAreaFraction = std::clamp(config.nearAreaFraction, 0.0F, 1.0F);
    // Clamp mid AFTER near so the ordering invariant holds even for bad input.
    config.midAreaFraction = std::clamp(config.midAreaFraction, 0.0F, config.nearAreaFraction);
    config.pathCenterTolerance = std::clamp(config.pathCenterTolerance, 0.0F, 1.0F);
    config.maxNarratedItems = std::clamp(config.maxNarratedItems, 1, 3);
    config.faceStableFrames = std::clamp(config.faceStableFrames, 1, 30);
    config.faceMatchThreshold = std::clamp(config.faceMatchThreshold, 0.0F, 1.0F);
    return config;
}

// True when every field is already inside its legal range. Complements
// clampConfig for validating values we produced ourselves.
[[nodiscard]] inline bool isValid(const Config& config) noexcept {
    return isSupportedInferSize(config.inferWidth, config.inferHeight) &&
           config.scoreThreshold >= 0.0F && config.scoreThreshold <= 1.0F &&
           config.nmsThreshold >= 0.0F && config.nmsThreshold <= 1.0F &&
           config.nearAreaFraction >= 0.0F && config.nearAreaFraction <= 1.0F &&
           config.midAreaFraction >= 0.0F && config.midAreaFraction <= config.nearAreaFraction &&
           config.pathCenterTolerance >= 0.0F && config.pathCenterTolerance <= 1.0F &&
           config.maxNarratedItems >= 1 && config.maxNarratedItems <= 3 &&
           config.faceStableFrames >= 1 && config.faceStableFrames <= 30 &&
           config.faceMatchThreshold >= 0.0F && config.faceMatchThreshold <= 1.0F;
}

}  // namespace lumina::core
