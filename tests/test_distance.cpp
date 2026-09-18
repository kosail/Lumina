// ---------------------------------------------------------------------------
// Unit tests for the bbox distance heuristic (pure, hardware-free logic).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <limits>

#include "core/detection.hpp"
#include "processing/distance.hpp"

using lumina::core::BoundingBox;
using lumina::processing::boxAreaFraction;
using lumina::processing::classifyDistance;
using lumina::processing::DistanceBand;
using lumina::processing::DistanceThresholds;
using lumina::processing::isInPath;
using lumina::processing::isNearObstacleInPath;

namespace {

BoundingBox makeBox(float x, float y, float width, float height)
{
    return BoundingBox{x, y, width, height};
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

} // namespace

TEST_CASE("boxAreaFraction: simple ratio")
{
    // 100x100 frame = 10000 px; a 50x50 box is a quarter of it.
    CHECK(boxAreaFraction(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 100, 100) == doctest::Approx(0.25F));
}

TEST_CASE("boxAreaFraction: clamps a box larger than the frame to 1.0")
{
    CHECK(boxAreaFraction(makeBox(-10.0F, -10.0F, 200.0F, 200.0F), 100, 100) ==
          doctest::Approx(1.0F));
}

TEST_CASE("boxAreaFraction: degenerate inputs are 0, never a division by zero")
{
    CHECK(boxAreaFraction(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 0, 100) == doctest::Approx(0.0F));
    CHECK(boxAreaFraction(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 100, 0) == doctest::Approx(0.0F));
    CHECK(boxAreaFraction(makeBox(0.0F, 0.0F, 50.0F, 50.0F), -5, -5) == doctest::Approx(0.0F));
    CHECK(boxAreaFraction(makeBox(0.0F, 0.0F, 0.0F, 50.0F), 100, 100) == doctest::Approx(0.0F));
    CHECK(boxAreaFraction(makeBox(0.0F, 0.0F, 50.0F, -3.0F), 100, 100) == doctest::Approx(0.0F));
}

TEST_CASE("classifyDistance: Near / Mid / Far bands")
{
    const DistanceThresholds thresholds; // defaults: near 0.20, mid 0.06
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 100, 100, thresholds) ==
          DistanceBand::Near); // 0.25
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 30.0F, 30.0F), 100, 100, thresholds) ==
          DistanceBand::Mid); // 0.09
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 20.0F, 20.0F), 100, 100, thresholds) ==
          DistanceBand::Far); // 0.04
}

TEST_CASE("classifyDistance: band boundaries are inclusive")
{
    const DistanceThresholds thresholds; // near 0.20, mid 0.06
    // 20x20 on 100x100 => 0.04 (Far); 24.5x24.5 => 0.060025 (Mid, just over 0.06).
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 20.0F, 20.0F), 100, 100, thresholds) ==
          DistanceBand::Far);
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 50.0F, 40.0F), 100, 100, thresholds) ==
          DistanceBand::Near); // exactly 0.20
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 24.5F, 24.5F), 100, 100, thresholds) ==
          DistanceBand::Mid); // ~0.060 > 0.06
}

TEST_CASE("classifyDistance: a swapped threshold pair is normalised")
{
    // near < mid is a misconfiguration; Near must remain the stricter band and
    // Mid the looser one (max becomes Near, min becomes Mid).
    DistanceThresholds swapped;
    swapped.nearAreaFraction = 0.06F;
    swapped.midAreaFraction = 0.20F;
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 100, 100, swapped) ==
          DistanceBand::Near); // 0.25 >= max(0.06, 0.20)
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 30.0F, 30.0F), 100, 100, swapped) ==
          DistanceBand::Mid); // min(0.06,0.20) <= 0.09 < max
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 20.0F, 20.0F), 100, 100, swapped) ==
          DistanceBand::Far); // 0.04 < min
}

TEST_CASE("classifyDistance: degenerate frames and NaN thresholds degrade to Far")
{
    const DistanceThresholds thresholds;
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 0, 100, thresholds) == DistanceBand::Far);

    DistanceThresholds nan = thresholds;
    nan.nearAreaFraction = kNaN;
    nan.midAreaFraction = kNaN;
    CHECK(classifyDistance(makeBox(0.0F, 0.0F, 50.0F, 50.0F), 100, 100, nan) == DistanceBand::Far);
}

TEST_CASE("isInPath: centred box is in the path")
{
    const DistanceThresholds thresholds; // tolerance 0.35
    CHECK(isInPath(makeBox(30.0F, 0.0F, 40.0F, 40.0F), 100, thresholds)); // centre 50
}

TEST_CASE("isInPath: off-centre box is not in the path")
{
    const DistanceThresholds thresholds;
    CHECK_FALSE(isInPath(makeBox(0.0F, 0.0F, 10.0F, 10.0F), 100, thresholds)); // centre 5, diff 45
}

TEST_CASE("isInPath: tolerance boundary is inclusive")
{
    const DistanceThresholds thresholds; // 0.35 * 100 = 35
    // centre 15 => diff exactly 35 => in path
    CHECK(isInPath(makeBox(0.0F, 0.0F, 30.0F, 10.0F), 100, thresholds));
    // centre 14 => diff 36 => out
    CHECK_FALSE(isInPath(makeBox(0.0F, 0.0F, 28.0F, 10.0F), 100, thresholds));
}

TEST_CASE("isInPath: degenerate geometry is handled safely")
{
    const DistanceThresholds thresholds;
    CHECK_FALSE(isInPath(makeBox(0.0F, 0.0F, 30.0F, 30.0F), 0, thresholds));
    CHECK_FALSE(isInPath(makeBox(kNaN, 0.0F, 30.0F, 30.0F), 100, thresholds));
    CHECK_FALSE(isInPath(makeBox(0.0F, 0.0F, kNaN, 30.0F), 100, thresholds));
}

TEST_CASE("isNearObstacleInPath: requires both Near and centred")
{
    const DistanceThresholds thresholds; // near 0.20, path tolerance 0.35 (35 px on 100)
    // Near (40x60/10000 = 0.24) and centred (centre 50) => alert.
    CHECK(isNearObstacleInPath(makeBox(30.0F, 0.0F, 40.0F, 60.0F), 100, 100, thresholds));
    // Near (0.24) but off to the side (centre 86, |86-50| = 36 > 35) => no alert.
    CHECK_FALSE(isNearObstacleInPath(makeBox(66.0F, 0.0F, 40.0F, 60.0F), 100, 100, thresholds));
    // Centred but far/small (20x20 = 0.04) => no alert.
    CHECK_FALSE(isNearObstacleInPath(makeBox(40.0F, 0.0F, 20.0F, 20.0F), 100, 100, thresholds));
}
