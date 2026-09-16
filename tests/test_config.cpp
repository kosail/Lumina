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

TEST_CASE("only 320 and 416 are supported input sizes") {
    CHECK(isSupportedInferSize(320));
    CHECK(isSupportedInferSize(416));
    CHECK_FALSE(isSupportedInferSize(640));
    CHECK_FALSE(isSupportedInferSize(0));
}

TEST_CASE("clampConfig repairs out-of-range values") {
    auto config = defaultConfig();
    config.inferSize = 999;
    config.scoreThreshold = 2.0F;
    config.nmsThreshold = -1.0F;
    config.faceStableFrames = 0;
    config.faceMatchThreshold = 5.0F;

    const auto fixed = clampConfig(config);
    CHECK(fixed.inferSize == 320);  // unsupported size falls back to the default
    CHECK(fixed.scoreThreshold == doctest::Approx(1.0F));
    CHECK(fixed.nmsThreshold == doctest::Approx(0.0F));
    CHECK(fixed.faceStableFrames == 1);
    CHECK(fixed.faceMatchThreshold == doctest::Approx(1.0F));
    CHECK(isValid(fixed));
}

TEST_CASE("clampConfig leaves valid values untouched") {
    auto config = defaultConfig();
    config.inferSize = 416;

    const auto fixed = clampConfig(config);
    CHECK(fixed.inferSize == 416);
    CHECK(isValid(fixed));
}
