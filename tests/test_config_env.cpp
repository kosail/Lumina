// ---------------------------------------------------------------------------
// Unit tests for core/config_env.hpp — environment overrides of runtime settings
// (demo hardening, CHG-0098). Pure parsing logic: no hardware, no real env, no
// clock (AGENTS.md §9).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/config_env.hpp"

using lumina::core::applyEnvOverrides;
using lumina::core::defaultConfig;
using lumina::core::EnvLookup;
using lumina::core::isValid;
using lumina::core::parseEnvBool;
using lumina::core::parseEnvFloat;
using lumina::core::parseEnvInt;
using lumina::core::parseEnvIntList;

namespace {

// A fake environment: only the names in the list are "set". Injecting this keeps
// the tests independent of the real process environment.
EnvLookup fakeEnv(std::initializer_list<std::pair<const char*, const char*>> values)
{
    auto map = std::make_shared<std::map<std::string, std::string>>();
    for (const auto& [name, value] : values) {
        (*map)[name] = value;
    }
    return [map](const char* name) -> std::optional<std::string> {
        const auto it = map->find(name);
        if (it == map->end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

}  // namespace

TEST_CASE("parseEnvBool accepts the documented spellings, case-insensitively")
{
    CHECK(parseEnvBool("1") == std::optional<bool>(true));
    CHECK(parseEnvBool("true") == std::optional<bool>(true));
    CHECK(parseEnvBool("YES") == std::optional<bool>(true));
    CHECK(parseEnvBool("On") == std::optional<bool>(true));
    CHECK(parseEnvBool(" true ") == std::optional<bool>(true));  // stray whitespace

    CHECK(parseEnvBool("0") == std::optional<bool>(false));
    CHECK(parseEnvBool("false") == std::optional<bool>(false));
    CHECK(parseEnvBool("No") == std::optional<bool>(false));
    CHECK(parseEnvBool("OFF") == std::optional<bool>(false));

    CHECK_FALSE(parseEnvBool("").has_value());
    CHECK_FALSE(parseEnvBool("maybe").has_value());
    CHECK_FALSE(parseEnvBool("2").has_value());
}

TEST_CASE("parseEnvInt accepts decimals in range and rejects the rest")
{
    CHECK(parseEnvInt("42", 1, 100) == std::optional<int>(42));
    CHECK(parseEnvInt(" 7 ", 1, 100) == std::optional<int>(7));
    CHECK(parseEnvInt("100", 1, 100) == std::optional<int>(100));

    CHECK_FALSE(parseEnvInt("", 1, 100).has_value());
    CHECK_FALSE(parseEnvInt("abc", 1, 100).has_value());
    CHECK_FALSE(parseEnvInt("12x", 1, 100).has_value());  // trailing garbage
    CHECK_FALSE(parseEnvInt("0", 1, 100).has_value());    // below range
    CHECK_FALSE(parseEnvInt("101", 1, 100).has_value());  // above range
}

TEST_CASE("applyEnvOverrides with an empty environment leaves the defaults")
{
    const auto base = defaultConfig();
    const auto result = applyEnvOverrides(base, fakeEnv({}));
    CHECK(result.audioSinkShutdownOnFailure == base.audioSinkShutdownOnFailure);
    CHECK(result.audioSinkMaxRetries == base.audioSinkMaxRetries);
    CHECK(result.audioSinkRetryIntervalMs == base.audioSinkRetryIntervalMs);
    CHECK(result.faceEnabled == base.faceEnabled);
    CHECK(result.proximityEnabled == base.proximityEnabled);
}

TEST_CASE("applyEnvOverrides applies the curated demo values")
{
    const auto result = applyEnvOverrides(defaultConfig(),
                                          fakeEnv({{"LUMINA_AUDIO_SHUTDOWN_ON_FAILURE", "0"},
                                                   {"LUMINA_AUDIO_SINK_MAX_RETRIES", "10000"},
                                                   {"LUMINA_AUDIO_SINK_RETRY_MS", "500"},
                                                   {"LUMINA_PROXIMITY_ENABLED", "off"}}));
    CHECK_FALSE(result.audioSinkShutdownOnFailure);
    CHECK(result.audioSinkMaxRetries == 10000);
    CHECK(result.audioSinkRetryIntervalMs == 500);
    CHECK_FALSE(result.proximityEnabled);
    CHECK(isValid(result));  // overrides stay within the clamp ranges
    // faceEnabled is intentionally not overridable (face must always run).
    CHECK(result.faceEnabled);
}

TEST_CASE("applyEnvOverrides ignores invalid values and keeps the default")
{
    const auto base = defaultConfig();
    const auto result = applyEnvOverrides(base,
                                          fakeEnv({{"LUMINA_AUDIO_SHUTDOWN_ON_FAILURE", "maybe"},
                                                   {"LUMINA_AUDIO_SINK_MAX_RETRIES", "999999"},
                                                   {"LUMINA_AUDIO_SINK_RETRY_MS", "-5"},
                                                   {"LUMINA_PROXIMITY_ENABLED", "sure"}}));
    CHECK(result.audioSinkShutdownOnFailure == base.audioSinkShutdownOnFailure);
    CHECK(result.audioSinkMaxRetries == base.audioSinkMaxRetries);
    CHECK(result.audioSinkRetryIntervalMs == base.audioSinkRetryIntervalMs);
    CHECK(result.proximityEnabled == base.proximityEnabled);
    CHECK(isValid(result));
}

TEST_CASE("applyEnvOverrides reports invalid values through the callback")
{
    std::vector<std::pair<std::string, std::string>> invalid;
    const auto onInvalid = [&invalid](std::string_view name, std::string_view value) {
        invalid.emplace_back(std::string(name), std::string(value));
    };

    const auto result = applyEnvOverrides(defaultConfig(),
                                          fakeEnv({{"LUMINA_AUDIO_SHUTDOWN_ON_FAILURE", "maybe"},
                                                   {"LUMINA_PROXIMITY_ENABLED", "true"}}),
                                          onInvalid);

    // Exactly one bad value is reported (name + raw value); the valid one is not.
    REQUIRE(invalid.size() == 1);
    CHECK(invalid[0].first == "LUMINA_AUDIO_SHUTDOWN_ON_FAILURE");
    CHECK(invalid[0].second == "maybe");
    CHECK(result.proximityEnabled);  // valid override still applied
    CHECK(result.audioSinkShutdownOnFailure);  // invalid one kept the default
}

TEST_CASE("an injected lookup that is empty is treated as unset")
{
    const auto base = defaultConfig();
    const auto result = applyEnvOverrides(base, EnvLookup{});
    CHECK(result.audioSinkMaxRetries == base.audioSinkMaxRetries);
    CHECK(isValid(result));
}

TEST_CASE("parseEnvFloat accepts decimals in range and rejects the rest")
{
    CHECK(parseEnvFloat("0.25", 0.0F, 1.0F) == std::optional<float>(0.25F));
    CHECK(parseEnvFloat(" 0.5 ", 0.0F, 1.0F) == std::optional<float>(0.5F));
    CHECK(parseEnvFloat("1", 0.0F, 1.0F) == std::optional<float>(1.0F));

    CHECK_FALSE(parseEnvFloat("", 0.0F, 1.0F).has_value());
    CHECK_FALSE(parseEnvFloat("abc", 0.0F, 1.0F).has_value());
    CHECK_FALSE(parseEnvFloat("1.5", 0.0F, 1.0F).has_value());  // above range
    CHECK_FALSE(parseEnvFloat("0.5x", 0.0F, 1.0F).has_value()); // trailing garbage
}

TEST_CASE("parseEnvIntList accepts a CSV of ints and rejects malformed input")
{
    const auto ok = parseEnvIntList("1,2,5,56,57,60", 0, 90);
    REQUIRE(ok.has_value());
    CHECK(*ok == std::vector<int>{1, 2, 5, 56, 57, 60});

    const auto spaced = parseEnvIntList(" 1 , 2 ", 0, 90);
    REQUIRE(spaced.has_value());
    CHECK(*spaced == std::vector<int>{1, 2});

    CHECK_FALSE(parseEnvIntList("", 0, 90).has_value());
    CHECK_FALSE(parseEnvIntList("1,,2", 0, 90).has_value());  // empty item
    CHECK_FALSE(parseEnvIntList("1,", 0, 90).has_value());    // trailing comma
    CHECK_FALSE(parseEnvIntList("1,x", 0, 90).has_value());   // bad item
    CHECK_FALSE(parseEnvIntList("1,91", 0, 90).has_value());  // out of range
}

TEST_CASE("applyEnvOverrides applies the detection tuning values")
{
    const auto result = applyEnvOverrides(defaultConfig(),
                                          fakeEnv({{"LUMINA_NEAR_AREA_FRACTION", "0.3"},
                                                   {"LUMINA_MID_AREA_FRACTION", "0.1"},
                                                   {"LUMINA_PATH_CENTER_TOLERANCE", "0.2"},
                                                   {"LUMINA_PROXIMITY_THRESHOLD_M", "1.0"},
                                                   {"LUMINA_PROXIMITY_RELEASE_M", "1.5"},
                                                   {"LUMINA_OBSTACLE_CLASSES", "1,2,5"}}));
    CHECK(result.nearAreaFraction == doctest::Approx(0.3F));
    CHECK(result.midAreaFraction == doctest::Approx(0.1F));
    CHECK(result.pathCenterTolerance == doctest::Approx(0.2F));
    CHECK(result.proximityThresholdM == doctest::Approx(1.0F));
    CHECK(result.proximityReleaseM == doctest::Approx(1.5F));
    CHECK(result.obstacleClassIds == std::vector<int>{1, 2, 5});
    CHECK(isValid(result));
}

TEST_CASE("applyEnvOverrides reports invalid detection values and keeps the defaults")
{
    const auto base = defaultConfig();
    std::vector<std::pair<std::string, std::string>> invalid;
    const auto onInvalid = [&invalid](std::string_view name, std::string_view value) {
        invalid.emplace_back(std::string(name), std::string(value));
    };

    const auto result =
        applyEnvOverrides(base,
                          fakeEnv({{"LUMINA_PATH_CENTER_TOLERANCE", "wide"},
                                   {"LUMINA_OBSTACLE_CLASSES", "1,,2"}}),
                          onInvalid);

    REQUIRE(invalid.size() == 2);
    CHECK(result.pathCenterTolerance == doctest::Approx(base.pathCenterTolerance));
    CHECK(result.obstacleClassIds == base.obstacleClassIds);
    CHECK(isValid(result));
}

TEST_CASE("applyEnvOverrides applies and validates the camera rotation")
{
    const auto applied =
        applyEnvOverrides(defaultConfig(), fakeEnv({{"LUMINA_CAMERA_ROTATION", "270"}}));
    CHECK(applied.cameraRotationDegrees == 270);

    // A non-right angle is reported and the default (90) is kept.
    std::vector<std::pair<std::string, std::string>> invalid;
    const auto onInvalid = [&invalid](std::string_view name, std::string_view value) {
        invalid.emplace_back(std::string(name), std::string(value));
    };
    const auto kept = applyEnvOverrides(
        defaultConfig(), fakeEnv({{"LUMINA_CAMERA_ROTATION", "45"}}), onInvalid);
    CHECK(kept.cameraRotationDegrees == 90);
    REQUIRE(invalid.size() == 1);
    CHECK(invalid[0].first == "LUMINA_CAMERA_ROTATION");
    CHECK(invalid[0].second == "45");
}
