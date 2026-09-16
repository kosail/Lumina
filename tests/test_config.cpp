// ---------------------------------------------------------------------------
// Unit tests for core/config.hpp — range validation and clamping of runtime
// settings. Pure logic, so it needs no hardware and runs on the host (AGENTS.md §9).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

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
