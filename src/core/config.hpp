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
    // COCO class ids we narrate (all classes with a Spanish label; FR-01). Person
    // plus bicycle, car, motorcycle, bus, truck, cat, dog, backpack, chair, couch,
    // dining table. Trim this if the narration is too chatty.
    std::vector<int> classIds{0, 1, 2, 3, 5, 7, 15, 16, 24, 56, 57, 60};
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
           config.faceStableFrames >= 1 && config.faceStableFrames <= 30 &&
           config.faceMatchThreshold >= 0.0F && config.faceMatchThreshold <= 1.0F;
}

}  // namespace lumina::core
