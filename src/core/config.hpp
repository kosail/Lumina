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
    int inferSize = 320;                              // square model input in px (INV-050)
    InferPrecision precision = InferPrecision::Fp16;  // INV-012
    float scoreThreshold = 0.25F;                     // minimum detection confidence
    float nmsThreshold = 0.45F;                       // non-max-suppression IoU cutoff
    std::vector<int> classIds{0};                     // COCO class subset; 0 = person
    int faceStableFrames = 3;                         // frames before announcing a name
    float faceMatchThreshold = 0.50F;                 // embedding cosine similarity cutoff
};

// Only 320 and 416 pixel inputs are supported (RAW_PLAN.md §1).
[[nodiscard]] inline bool isSupportedInferSize(int size) noexcept {
    return size == 320 || size == 416;
}

// The documented defaults; tests use this as the known-valid baseline.
[[nodiscard]] inline Config defaultConfig() { return Config{}; }

// Return a copy with every field forced into its legal range. Apply this to
// untrusted input (files/CLI) so the rest of the code can trust the values.
[[nodiscard]] inline Config clampConfig(Config config) {
    if (!isSupportedInferSize(config.inferSize)) {
        config.inferSize = 320;
    }
    config.scoreThreshold = std::clamp(config.scoreThreshold, 0.0F, 1.0F);
    config.nmsThreshold = std::clamp(config.nmsThreshold, 0.0F, 1.0F);
    config.faceStableFrames = std::clamp(config.faceStableFrames, 1, 30);
    config.faceMatchThreshold = std::clamp(config.faceMatchThreshold, 0.0F, 1.0F);
    return config;
}

// True when every field is already inside its legal range. Complements
// clampConfig for validating values we produced ourselves.
[[nodiscard]] inline bool isValid(const Config& config) noexcept {
    return isSupportedInferSize(config.inferSize) && config.scoreThreshold >= 0.0F &&
           config.scoreThreshold <= 1.0F && config.nmsThreshold >= 0.0F &&
           config.nmsThreshold <= 1.0F && config.faceStableFrames >= 1 &&
           config.faceStableFrames <= 30 && config.faceMatchThreshold >= 0.0F &&
           config.faceMatchThreshold <= 1.0F;
}

}  // namespace lumina::core
