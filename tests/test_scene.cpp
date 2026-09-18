// ---------------------------------------------------------------------------
// Unit tests for buildSceneAlert: obstacle warning vs. description precedence.
// Pure logic, fully deterministic (no threads/hardware).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "app/scene.hpp"

#include "alerts/alert.hpp"
#include "core/config.hpp"
#include "core/detection.hpp"
#include "core/time.hpp"

using lumina::alerts::Priority;
using lumina::alerts::Source;
using lumina::app::buildSceneAlert;
using lumina::core::BoundingBox;
using lumina::core::Config;
using lumina::core::defaultConfig;
using lumina::core::Detection;
using lumina::core::TimePoint;

namespace {

Detection makeDetection(int classId, float x, float y, float width, float height)
{
    Detection detection;
    detection.classId = classId;
    detection.score = 0.9F;
    detection.box = BoundingBox{x, y, width, height};
    return detection;
}

const TimePoint kCaptured = TimePoint{} + std::chrono::milliseconds(1234);

} // namespace

TEST_CASE("scene: a near in-path obstacle becomes an urgent Warning")
{
    const Config config = defaultConfig();
    // 40x60 on 100x100 = 0.24 area (Near), centred at x=50 => in path.
    const auto alert = buildSceneAlert(
        {makeDetection(0, 30.0F, 0.0F, 40.0F, 60.0F)}, config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    CHECK(alert->priority == Priority::Warning);
    CHECK(alert->source == Source::Obstacle);
    CHECK(alert->text == "cuidado, obstáculo cerca.");
    CHECK(alert->dedupKey == alert->text);
    CHECK(alert->detectedAt == kCaptured);
}

TEST_CASE("scene: a mid in-path obstacle uses the gentler, non-preempting phrase")
{
    const Config config = defaultConfig();
    // 30x30 on 100x100 = 0.09 (Mid), centred at x=50.
    const auto alert = buildSceneAlert(
        {makeDetection(0, 35.0F, 0.0F, 30.0F, 30.0F)}, config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    // Non-preempting: Description priority, but still tagged as an obstacle.
    CHECK(alert->priority == Priority::Description);
    CHECK(alert->source == Source::Obstacle);
    CHECK(alert->text == "obstáculo cerca.");
}

TEST_CASE("scene: a near but off-path obstacle is only described")
{
    const Config config = defaultConfig();
    // Same size as the Near case but centred at 86 (|86-50| = 36 > 35) => off path.
    const auto alert = buildSceneAlert(
        {makeDetection(0, 66.0F, 0.0F, 40.0F, 60.0F)}, config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    CHECK(alert->priority == Priority::Description);
    CHECK(alert->source == Source::Description);
    CHECK(alert->text == "una persona enfrente.");
}

TEST_CASE("scene: a small in-path object is only described")
{
    const Config config = defaultConfig();
    // 10x10 on 100x100 = 0.01 (Far), centred.
    const auto alert = buildSceneAlert(
        {makeDetection(0, 45.0F, 0.0F, 10.0F, 10.0F)}, config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    CHECK(alert->priority == Priority::Description);
    CHECK(alert->text == "una persona enfrente.");
}

TEST_CASE("scene: a non-obstacle class is not upgraded to a Warning")
{
    const Config config = defaultConfig();
    // Cat (15) is narrated but not in obstacleClassIds, even if large and centred.
    const auto alert = buildSceneAlert(
        {makeDetection(15, 30.0F, 0.0F, 40.0F, 60.0F)}, config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    CHECK(alert->priority == Priority::Description);
    CHECK(alert->text == "un gato enfrente.");
}

TEST_CASE("scene: a deferred class is ignored entirely")
{
    const Config config = defaultConfig();
    // Motorcycle (3) was deferred; it is not in classIds so nothing is said.
    const auto alert = buildSceneAlert(
        {makeDetection(3, 30.0F, 0.0F, 40.0F, 60.0F)}, config, 100, 100, kCaptured);
    CHECK_FALSE(alert.has_value());
}

TEST_CASE("scene: an empty frame produces no alert")
{
    const Config config = defaultConfig();
    CHECK_FALSE(buildSceneAlert({}, config, 100, 100, kCaptured).has_value());
}

TEST_CASE("scene: maxNarratedItems caps the description to two classes")
{
    Config config = defaultConfig();
    config.maxNarratedItems = 2;
    // Three small/far classes (person, dog, cat) => a Description naming at most 2.
    const auto alert = buildSceneAlert(
        {makeDetection(0, 0.0F, 0.0F, 5.0F, 5.0F),    // person
         makeDetection(16, 10.0F, 0.0F, 5.0F, 5.0F),  // dog
         makeDetection(15, 20.0F, 0.0F, 5.0F, 5.0F)}, // cat
        config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    CHECK(alert->priority == Priority::Description);
    CHECK(alert->text.find(" y ") != std::string::npos); // two items joined by "y"
    CHECK(alert->text.find(',') == std::string::npos);   // no three-item comma list
}

TEST_CASE("scene: a near obstacle outranks a co-present description")
{
    const Config config = defaultConfig();
    // A small far person plus a big near car; the obstacle warning must win.
    const auto alert = buildSceneAlert(
        {makeDetection(0, 45.0F, 0.0F, 10.0F, 10.0F),      // person, Far
         makeDetection(2, 30.0F, 0.0F, 40.0F, 60.0F)},     // car, Near in path
        config, 100, 100, kCaptured);
    REQUIRE(alert.has_value());
    CHECK(alert->priority == Priority::Warning);
    CHECK(alert->text == "cuidado, obstáculo cerca.");
}
