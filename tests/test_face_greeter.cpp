// ---------------------------------------------------------------------------
// Unit tests for FaceGreeter: stability (consecutive frames) and per-person
// cooldown. Pure logic, deterministic time (no real clock).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <string>

#include "app/face_greeter.hpp"
#include "core/time.hpp"

using lumina::app::FaceGreeter;
using lumina::app::FaceGreeterConfig;
using lumina::core::TimePoint;

namespace {

TimePoint at(long long ms)
{
    return TimePoint{} + std::chrono::milliseconds(ms);
}

// Short cooldown so the cooldown test runs instantly.
FaceGreeterConfig testConfig()
{
    FaceGreeterConfig config;
    config.stableFrames = 3;
    config.cooldown = std::chrono::milliseconds(100);
    return config;
}

}  // namespace

TEST_CASE("greeter: a name is announced only after stableFrames consecutive observations")
{
    FaceGreeter greeter(testConfig());

    CHECK_FALSE(greeter.observe("Ana", at(0)).has_value());
    CHECK_FALSE(greeter.observe("Ana", at(100)).has_value());
    const auto announced = greeter.observe("Ana", at(200));
    REQUIRE(announced.has_value());
    CHECK(*announced == "Ana");
}

TEST_CASE("greeter: a nullopt observation restarts the stability run")
{
    FaceGreeter greeter(testConfig());

    CHECK_FALSE(greeter.observe("Ana", at(0)).has_value());
    CHECK_FALSE(greeter.observe("Ana", at(100)).has_value());
    CHECK_FALSE(greeter.observe(std::nullopt, at(200)).has_value());  // person left the frame
    CHECK_FALSE(greeter.observe("Ana", at(300)).has_value());          // count restarts at 1
    CHECK_FALSE(greeter.observe("Ana", at(400)).has_value());          // count 2
    REQUIRE(greeter.observe("Ana", at(500)).has_value());              // count 3 -> announced
}

TEST_CASE("greeter: switching names restarts the run")
{
    FaceGreeter greeter(testConfig());

    CHECK_FALSE(greeter.observe("Ana", at(0)).has_value());
    CHECK_FALSE(greeter.observe("Ana", at(100)).has_value());
    CHECK_FALSE(greeter.observe("Bob", at(200)).has_value());  // different person -> count 1
    CHECK_FALSE(greeter.observe("Bob", at(300)).has_value());  // count 2
    REQUIRE(greeter.observe("Bob", at(400)).has_value());      // count 3 -> announced
}

TEST_CASE("greeter: a person is not re-announced within the cooldown")
{
    FaceGreeter greeter(testConfig());

    CHECK_FALSE(greeter.observe("Ana", at(0)).has_value());
    CHECK_FALSE(greeter.observe("Ana", at(100)).has_value());
    const auto announced = greeter.observe("Ana", at(200));
    REQUIRE(announced.has_value());  // announced at t=200
    greeter.markGreeted(*announced, at(200));  // the caller accepted the greeting

    // Still stable, but within the 100 ms cooldown -> suppressed.
    CHECK_FALSE(greeter.observe("Ana", at(250)).has_value());
    CHECK_FALSE(greeter.observe("Ana", at(270)).has_value());

    // Past the cooldown window, a fresh observation announces again.
    REQUIRE(greeter.observe("Ana", at(400)).has_value());
}

TEST_CASE("greeter: observe retries until markGreeted is called")
{
    // A greeting can be rejected at submit time (arbiter gap/priority). observe()
    // must keep offering the candidate so the pipeline can retry on the next pulse
    // instead of the person being silenced for the whole cooldown (CHG-0071).
    FaceGreeter greeter(testConfig());

    CHECK_FALSE(greeter.observe("Ana", at(0)).has_value());
    CHECK_FALSE(greeter.observe("Ana", at(100)).has_value());
    REQUIRE(greeter.observe("Ana", at(200)).has_value());
    // Not marked: still offered.
    REQUIRE(greeter.observe("Ana", at(300)).has_value());
    REQUIRE(greeter.observe("Ana", at(400)).has_value());

    greeter.markGreeted("Ana", at(400));
    CHECK_FALSE(greeter.observe("Ana", at(450)).has_value());  // within cooldown now
}

TEST_CASE("greeter: hasActiveCooldown reflects the per-person window")
{
    FaceGreeter greeter(testConfig());  // cooldown is 100 ms

    CHECK_FALSE(greeter.hasActiveCooldown(at(0)));
    greeter.markGreeted("Ana", at(1000));
    CHECK(greeter.hasActiveCooldown(at(1050)));   // 50 ms later
    CHECK_FALSE(greeter.hasActiveCooldown(at(1100)));  // exactly at the boundary
    CHECK_FALSE(greeter.hasActiveCooldown(at(2000)));
}

TEST_CASE("greeter: empty names are treated as 'no match'")
{
    FaceGreeter greeter(testConfig());

    CHECK_FALSE(greeter.observe("Ana", at(0)).has_value());
    CHECK_FALSE(greeter.observe("", at(100)).has_value());     // treated as nullopt -> reset
    CHECK_FALSE(greeter.observe("Ana", at(200)).has_value());  // count 1
    CHECK_FALSE(greeter.observe("Ana", at(300)).has_value());  // count 2
    REQUIRE(greeter.observe("Ana", at(400)).has_value());      // count 3 -> announced
}
