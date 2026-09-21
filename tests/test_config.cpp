// ---------------------------------------------------------------------------
// Unit tests for core/config.hpp — range validation and clamping of runtime
// settings. Pure logic, so it needs no hardware and runs on the host (AGENTS.md §9).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <algorithm>

#include "core/config.hpp"

using lumina::core::clampConfig;
using lumina::core::defaultConfig;
using lumina::core::isSupportedInferSize;
using lumina::core::isValid;

TEST_CASE("defaultConfig is valid") { CHECK(isValid(defaultConfig())); }

TEST_CASE("only the approved input sizes are supported") {
    CHECK(isSupportedInferSize(320, 256));  // the approved target
    CHECK(isSupportedInferSize(256, 256));  // low-res fallback
    CHECK(isSupportedInferSize(320, 320));
    CHECK(isSupportedInferSize(416, 416));
    CHECK_FALSE(isSupportedInferSize(640, 480));
    CHECK_FALSE(isSupportedInferSize(0, 0));
}

TEST_CASE("clampConfig repairs out-of-range values") {
    auto config = defaultConfig();
    config.inferWidth = 999;
    config.inferHeight = 999;
    config.scoreThreshold = 2.0F;
    config.nmsThreshold = -1.0F;
    config.faceStableFrames = 0;
    config.faceMatchThreshold = 5.0F;

    const auto fixed = clampConfig(config);
    CHECK(fixed.inferWidth == 320);   // unsupported size falls back to the target
    CHECK(fixed.inferHeight == 256);
    CHECK(fixed.scoreThreshold == doctest::Approx(1.0F));
    CHECK(fixed.nmsThreshold == doctest::Approx(0.0F));
    CHECK(fixed.faceStableFrames == 1);
    CHECK(fixed.faceMatchThreshold == doctest::Approx(1.0F));
    CHECK(isValid(fixed));
}

TEST_CASE("clampConfig leaves valid values untouched") {
    auto config = defaultConfig();
    config.inferWidth = 416;
    config.inferHeight = 416;

    const auto fixed = clampConfig(config);
    CHECK(fixed.inferWidth == 416);
    CHECK(fixed.inferHeight == 416);
    CHECK(isValid(fixed));
}

TEST_CASE("clampConfig repairs distance thresholds and keeps their order") {
    auto config = defaultConfig();
    config.nearAreaFraction = 2.0F;   // above range
    config.midAreaFraction = 5.0F;    // above range AND above near
    config.pathCenterTolerance = -1.0F;

    const auto fixed = clampConfig(config);
    CHECK(fixed.nearAreaFraction == doctest::Approx(1.0F));
    CHECK(fixed.midAreaFraction == doctest::Approx(1.0F)); // clamped to near
    CHECK(fixed.pathCenterTolerance == doctest::Approx(0.0F));
    CHECK(isValid(fixed));

    // mid above near is pulled down to near (order invariant).
    auto ordered = defaultConfig();
    ordered.nearAreaFraction = 0.10F;
    ordered.midAreaFraction = 0.50F;
    CHECK(clampConfig(ordered).midAreaFraction == doctest::Approx(0.10F));
}

TEST_CASE("isValid rejects a swapped distance threshold pair") {
    auto config = defaultConfig();
    config.nearAreaFraction = 0.05F;
    config.midAreaFraction = 0.30F; // mid must not exceed near
    CHECK_FALSE(isValid(config));
}

TEST_CASE("clampConfig keeps maxNarratedItems in [1, 3]") {
    auto config = defaultConfig();
    config.maxNarratedItems = 0;
    CHECK(clampConfig(config).maxNarratedItems == 1);

    config.maxNarratedItems = 9;
    CHECK(clampConfig(config).maxNarratedItems == 3);

    CHECK(isValid(defaultConfig())); // default (2) is valid
}

TEST_CASE("defaultConfig narrates the beta set and defers motorcycle/truck") {    const auto config = defaultConfig();
    const auto& ids = config.classIds;
    const auto has = [&ids](int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };

    CHECK(has(0));   // person
    CHECK(has(1));   // bicycle
    CHECK(has(2));   // car
    CHECK(has(5));   // bus
    CHECK(has(15));  // cat
    CHECK(has(16));  // dog
    CHECK(has(24));  // backpack
    CHECK(has(56));  // chair
    CHECK(has(57));  // couch
    CHECK(has(60));  // dining table
    CHECK_FALSE(has(3)); // motorcycle deferred
    CHECK_FALSE(has(7)); // truck deferred

    // Obstacle subset: excludes small classes; also cannot contain a deferred class.
    const auto& obstacles = config.obstacleClassIds;
    const auto isObstacle = [&obstacles](int id) {
        return std::find(obstacles.begin(), obstacles.end(), id) != obstacles.end();
    };
    CHECK(isObstacle(0));
    CHECK(isObstacle(56));
    CHECK_FALSE(isObstacle(15)); // cat is not an obstacle
    CHECK_FALSE(isObstacle(24)); // backpack is not an obstacle
    CHECK_FALSE(isObstacle(3));  // motorcycle is deferred
}

TEST_CASE("face defaults match the approved Day-4 values") {
    const auto config = defaultConfig();
    // SFace cosine cutoff per the OpenCV Zoo reference (sface.py, 2026-09-18).
    CHECK(config.faceMatchThreshold == doctest::Approx(0.363F));
    CHECK(config.faceMatchMargin == doctest::Approx(0.05F));
    CHECK(config.faceStableFrames == 3);
    CHECK(config.faceEnabled);
    CHECK(config.faceIntervalMs == 500);
    CHECK(config.faceGreetingCooldownMs == 30000);
    CHECK(config.faceDetectionSide == 320);
    CHECK(config.faceCooldownBackoffFactor == 6);
    CHECK(isValid(config));
}

TEST_CASE("clampConfig repairs out-of-range face fields") {
    auto config = defaultConfig();
    config.faceMatchThreshold = 5.0F;   // above [0,1]
    config.faceMatchMargin = -1.0F;     // below [0,1]
    config.faceRoiFraction = 0.0F;      // below [0.1,1]
    config.faceIntervalMs = 0;          // below the 100 ms floor (INV-050)
    config.faceGreetingCooldownMs = 0;  // below the 1 s floor
    config.faceDetectionSide = 10;      // below the 160 px floor
    config.faceCooldownBackoffFactor = 0;  // below the factor-1 floor

    const auto fixed = clampConfig(config);
    CHECK(fixed.faceMatchThreshold == doctest::Approx(1.0F));
    CHECK(fixed.faceMatchMargin == doctest::Approx(0.0F));
    CHECK(fixed.faceRoiFraction == doctest::Approx(0.1F));
    CHECK(fixed.faceIntervalMs == 100);
    CHECK(fixed.faceGreetingCooldownMs == 1000);
    CHECK(fixed.faceDetectionSide == 160);
    CHECK(fixed.faceCooldownBackoffFactor == 1);
    CHECK(isValid(fixed));
}

TEST_CASE("proximity defaults match the approved FR-10 values") {
    const auto config = defaultConfig();
    CHECK(config.proximityEnabled);
    CHECK(config.proximityThresholdM == doctest::Approx(0.8F));
    CHECK(config.proximityReleaseM == doctest::Approx(1.2F));
    CHECK(config.proximityPollMs == 200);
    CHECK(isValid(config));
}

TEST_CASE("clampConfig repairs out-of-range proximity fields") {
    auto config = defaultConfig();
    config.proximityThresholdM = 0.0F;   // below the 0.05 m floor
    config.proximityReleaseM = 0.0F;     // must stay >= threshold
    config.proximityPollMs = 0;          // below the 20 ms floor

    const auto fixed = clampConfig(config);
    CHECK(fixed.proximityThresholdM == doctest::Approx(0.05F));
    CHECK(fixed.proximityReleaseM == doctest::Approx(0.05F));  // clamped up to threshold
    CHECK(fixed.proximityPollMs == 20);
    CHECK(isValid(fixed));
}
