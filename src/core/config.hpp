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
    // Clockwise rotation applied to every captured frame to make it upright (CHG-0101).
    // The camera is physically mounted rotated in the glasses, so the default is 90.
    // Only the four right angles are valid; clampConfig snaps anything else to 0.
    int cameraRotationDegrees = 90;
    // Distance heuristic (FR-02). An object that covers a large share of the frame
    // is treated as "near" because the beta has no depth camera in the vision path
    // yet (the VL53L0X sensor arrives in Phase C). Fractions are box area / frame
    // area in [0, 1]; `pathCenterTolerance` is the horizontal half-band around the
    // frame centre, as a fraction of frame width, that counts as "in the path".
    // Invariant: midAreaFraction <= nearAreaFraction (enforced by clampConfig).
    float nearAreaFraction = 0.20F;                   // >= this fraction => Near
    float midAreaFraction = 0.06F;                    // >= this fraction => Mid, else Far
    float pathCenterTolerance = 0.25F;                // |centerX - W/2| <= tol*W => in path
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
    // Subset of classIds that counts as an obstacle for the camera distance heuristic
    // (FR-02): bicycle, car, bus, chair, couch, dining table. Small classes are
    // narrated but do not raise obstacle alerts. `person` (0) is deliberately NOT an
    // obstacle class: people are named ("una persona enfrente") and the class-agnostic
    // IR proximity channel covers the imminent case at short range.
    std::vector<int> obstacleClassIds{1, 2, 5, 56, 57, 60};
    int faceStableFrames = 3;                         // frames before announcing a name
    // SFace cosine-similarity cutoff. The OpenCV Zoo reference uses 0.363 (cosine);
    // the old 0.50 was stricter and risked rejecting real users (FR-03.2).
    float faceMatchThreshold = 0.363F;
    float faceMatchMargin = 0.05F;                    // best must beat 2nd-best by this
    // Whether the face path runs at all (independent of whether a recognizer was
    // injected; both must be true for recognition to happen).
    bool faceEnabled = true;
    int faceIntervalMs = 500;                         // min gap between face attempts
    int faceGreetingCooldownMs = 30000;               // per-person re-greet cooldown
    // While any person is inside their greeting cooldown, multiply faceIntervalMs
    // by this factor (CHG-0071). A greeted person does not need the heavy SFace
    // inference at full rate, and slowing it down cuts the OpenCV DNN churn that
    // tips the ~447 MB board into swap. 6 turns the 500 ms base into 3 s.
    int faceCooldownBackoffFactor = 6;
    // A person must cover at least this fraction of the frame (box area / frame
    // area) before we attempt recognition, so distant faces are not probed.
    float faceMinBoxFraction = 0.04F;
    // Central-crop fraction used by the OpenCV face path (INV-011): 1.0 = full
    // frame; smaller values trim the distorted lens edges.
    float faceRoiFraction = 1.0F;
    // Longest side (px) fed to YuNet. The 640x480 frame is downscaled to this
    // before detection: YuNet's native size is 320, and at 640x480 its DNN
    // workspace churns tens of MB per inference, which forces SD swap on the
    // 415 MB Pi and starves the camera (INV-052, CHG-0070). 320 keeps faces in
    // YuNet's reliable range for a person covering >= faceMinBoxFraction.
    int faceDetectionSide = 320;
    // --- Front proximity sensor (VL53L0X, FR-10 / INV-013) -------------------
    // Whether to construct the real sensor at startup. When false, or when the
    // build has no proximity support, the factory returns NullProximitySensor and
    // behavior is identical to the camera-only beta (INV-033).
    bool proximityEnabled = true;
    // A reading at or below this many metres is an imminent obstacle and raises a
    // Safety-priority alert (FR-10). 0.8 m is roughly one walking step.
    float proximityThresholdM = 0.8F;
    // Hysteresis: the "close" state clears only once the distance rises above
    // this, so a reading hovering at the threshold cannot flap the alert.
    float proximityReleaseM = 1.2F;
    // Poll interval for the sensor thread. A VL53L0X single-shot measurement takes
    // ~33 ms, so 200 ms (5 Hz) is well within budget and leaves the CPU idle.
    int proximityPollMs = 200;
    // --- Bluetooth audio sink availability (FR-06, INV-014) ------------------
    // The `bluealsa` PCM only exists while the single paired earbud is connected,
    // so a cold boot may need a few seconds before the sink can be opened. Instead
    // of crashing, the runtime retries open() every `audioSinkRetryIntervalMs` up
    // to `audioSinkMaxRetries` times (default 60 x 3000 ms = 3 minutes). See
    // audio/sink_watchdog.*.
    int audioSinkRetryIntervalMs = 3000;    // pause between open() attempts
    int audioSinkMaxRetries = 60;           // attempts before giving up (FR-06.1)
    // When the budget is exhausted the device cannot be used as intended, so the
    // runtime requests a host power-off; set false to only stop the runtime.
    bool audioSinkShutdownOnFailure = true;
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
    // Only the four right angles are meaningful; snap anything else to "no rotation".
    if (config.cameraRotationDegrees != 0 && config.cameraRotationDegrees != 90 &&
        config.cameraRotationDegrees != 180 && config.cameraRotationDegrees != 270) {
        config.cameraRotationDegrees = 0;
    }
    config.nearAreaFraction = std::clamp(config.nearAreaFraction, 0.0F, 1.0F);
    // Clamp mid AFTER near so the ordering invariant holds even for bad input.
    config.midAreaFraction = std::clamp(config.midAreaFraction, 0.0F, config.nearAreaFraction);
    config.pathCenterTolerance = std::clamp(config.pathCenterTolerance, 0.0F, 1.0F);
    config.maxNarratedItems = std::clamp(config.maxNarratedItems, 1, 3);
    config.faceStableFrames = std::clamp(config.faceStableFrames, 1, 30);
    // A 0 ms interval would attempt face recognition every frame and could starve
    // the detector (INV-050), so enforce a sane floor (default is 500 ms).
    config.faceIntervalMs = std::clamp(config.faceIntervalMs, 100, 60000);
    // A 0 ms cooldown would greet a person continuously; floor at 1 s.
    config.faceGreetingCooldownMs = std::clamp(config.faceGreetingCooldownMs, 1000, 3600000);
    // 1 = no back-off; a huge factor would effectively disable re-recognition.
    config.faceCooldownBackoffFactor = std::clamp(config.faceCooldownBackoffFactor, 1, 60);
    config.faceMinBoxFraction = std::clamp(config.faceMinBoxFraction, 0.0F, 1.0F);
    // A face ROI below 0.1 would crop away almost everything; 1.0 is the full frame.
    config.faceRoiFraction = std::clamp(config.faceRoiFraction, 0.1F, 1.0F);
    // YuNet's usable range is small; clamp the detection side to a sane band.
    config.faceDetectionSide = std::clamp(config.faceDetectionSide, 160, 640);
    config.proximityThresholdM = std::clamp(config.proximityThresholdM, 0.05F, 5.0F);
    // Clamp release AFTER threshold so the hysteresis band is never inverted.
    config.proximityReleaseM = std::clamp(config.proximityReleaseM, config.proximityThresholdM, 5.0F);
    config.proximityPollMs = std::clamp(config.proximityPollMs, 20, 5000);
    // A zero interval would busy-loop open() attempts; floor at 100 ms.
    config.audioSinkRetryIntervalMs = std::clamp(config.audioSinkRetryIntervalMs, 100, 60000);
    config.audioSinkMaxRetries = std::clamp(config.audioSinkMaxRetries, 1, 10000);
    config.faceMatchThreshold = std::clamp(config.faceMatchThreshold, 0.0F, 1.0F);
    config.faceMatchMargin = std::clamp(config.faceMatchMargin, 0.0F, 1.0F);
    return config;
}

// True when every field is already inside its legal range. Complements
// clampConfig for validating values we produced ourselves.
[[nodiscard]] inline bool isValid(const Config& config) noexcept {
    return isSupportedInferSize(config.inferWidth, config.inferHeight) &&
           (config.cameraRotationDegrees == 0 || config.cameraRotationDegrees == 90 ||
            config.cameraRotationDegrees == 180 || config.cameraRotationDegrees == 270) &&
           config.scoreThreshold >= 0.0F && config.scoreThreshold <= 1.0F &&
           config.nmsThreshold >= 0.0F && config.nmsThreshold <= 1.0F &&
           config.nearAreaFraction >= 0.0F && config.nearAreaFraction <= 1.0F &&
           config.midAreaFraction >= 0.0F && config.midAreaFraction <= config.nearAreaFraction &&
           config.pathCenterTolerance >= 0.0F && config.pathCenterTolerance <= 1.0F &&
           config.maxNarratedItems >= 1 && config.maxNarratedItems <= 3 &&
           config.faceStableFrames >= 1 && config.faceStableFrames <= 30 &&
           config.faceIntervalMs >= 100 && config.faceIntervalMs <= 60000 &&
           config.faceGreetingCooldownMs >= 1000 && config.faceGreetingCooldownMs <= 3600000 &&
           config.faceCooldownBackoffFactor >= 1 && config.faceCooldownBackoffFactor <= 60 &&
           config.faceMinBoxFraction >= 0.0F && config.faceMinBoxFraction <= 1.0F &&
           config.faceRoiFraction >= 0.1F && config.faceRoiFraction <= 1.0F &&
           config.faceDetectionSide >= 160 && config.faceDetectionSide <= 640 &&
           config.proximityThresholdM >= 0.05F && config.proximityThresholdM <= 5.0F &&
           config.proximityReleaseM >= config.proximityThresholdM &&
           config.proximityReleaseM <= 5.0F &&
           config.proximityPollMs >= 20 && config.proximityPollMs <= 5000 &&
           config.audioSinkRetryIntervalMs >= 100 && config.audioSinkRetryIntervalMs <= 60000 &&
           config.audioSinkMaxRetries >= 1 && config.audioSinkMaxRetries <= 10000 &&
           config.faceMatchThreshold >= 0.0F && config.faceMatchThreshold <= 1.0F &&
           config.faceMatchMargin >= 0.0F && config.faceMatchMargin <= 1.0F;
}

}  // namespace lumina::core
