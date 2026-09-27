// ---------------------------------------------------------------------------
// Environment-variable overrides implementation (see core/config_env.hpp).
// ---------------------------------------------------------------------------

#include "core/config_env.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <charconv>
#include <cstdlib>
#include <system_error>
#include <vector>

#include "core/logging.hpp"

namespace lumina::core {

namespace {

// ASCII-lowercase a copy, so the boolean spellings compare case-insensitively.
std::string toLower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// Strip leading/trailing ASCII whitespace (e.g. a stray CR from an edited unit).
std::string_view trim(std::string_view text)
{
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::optional<bool> parseEnvBool(std::string_view value)
{
    const std::string text = toLower(trim(value));
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<int> parseEnvInt(std::string_view value, int min, int max)
{
    const std::string_view text = trim(value);
    if (text.empty()) {
        return std::nullopt;
    }
    int parsed = 0;
    const std::from_chars_result result =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    // Reject trailing garbage ("12x") and overflow, then enforce the range.
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    if (parsed < min || parsed > max) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<float> parseEnvFloat(std::string_view value, float min, float max)
{
    const std::string_view text = trim(value);
    if (text.empty()) {
        return std::nullopt;
    }
    float parsed = 0.0F;
    const std::from_chars_result result =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    // NaN fails both comparisons, so it is rejected along with out-of-range values.
    if (!(parsed >= min && parsed <= max)) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::vector<int>> parseEnvIntList(std::string_view value, int min, int max)
{
    std::vector<int> items;
    std::size_t position = 0;
    while (position <= value.size()) {
        const std::size_t comma = value.find(',', position);
        const std::string_view item =
            value.substr(position, comma == std::string_view::npos ? std::string_view::npos
                                                                   : comma - position);
        const std::optional<int> parsed = parseEnvInt(item, min, max);
        if (!parsed.has_value()) {
            return std::nullopt;  // empty item (e.g. "1,,2" or a trailing comma) or bad value
        }
        items.push_back(*parsed);
        if (comma == std::string_view::npos) {
            break;
        }
        position = comma + 1;
    }
    if (items.empty()) {
        return std::nullopt;
    }
    return items;
}

Config applyEnvOverrides(Config config, const EnvLookup& lookup, const InvalidEnvFn& onInvalid)
{
    const auto read = [&lookup](const char* name) -> std::optional<std::string> {
        return lookup ? lookup(name) : std::nullopt;
    };
    // An unset value leaves the field untouched; a set-but-invalid value is reported
    // through `onInvalid` and also leaves the field untouched (no partial writes).
    const auto reportInvalid = [&onInvalid](const char* name, const std::string& value) {
        if (onInvalid) {
            onInvalid(name, value);
        }
    };
    const auto applyBool = [&read, &reportInvalid](const char* name, bool& target) {
        if (const std::optional<std::string> raw = read(name); raw.has_value()) {
            if (const std::optional<bool> parsed = parseEnvBool(*raw); parsed.has_value()) {
                target = *parsed;
            } else {
                reportInvalid(name, *raw);
            }
        }
    };
    const auto applyInt = [&read, &reportInvalid](const char* name, int& target, int min, int max) {
        if (const std::optional<std::string> raw = read(name); raw.has_value()) {
            if (const std::optional<int> parsed = parseEnvInt(*raw, min, max); parsed.has_value()) {
                target = *parsed;
            } else {
                reportInvalid(name, *raw);
            }
        }
    };
    const auto applyFloat = [&read, &reportInvalid](const char* name, float& target, float min,
                                                    float max) {
        if (const std::optional<std::string> raw = read(name); raw.has_value()) {
            if (const std::optional<float> parsed = parseEnvFloat(*raw, min, max);
                parsed.has_value()) {
                target = *parsed;
            } else {
                reportInvalid(name, *raw);
            }
        }
    };
    const auto applyIntList = [&read, &reportInvalid](const char* name, std::vector<int>& target,
                                                      int min, int max) {
        if (const std::optional<std::string> raw = read(name); raw.has_value()) {
            if (const std::optional<std::vector<int>> parsed = parseEnvIntList(*raw, min, max);
                parsed.has_value()) {
                target = *parsed;
            } else {
                reportInvalid(name, *raw);
            }
        }
    };
    // A rotation must be one of the four right angles (0/90/180/270).
    const auto applyRightAngle = [&read, &reportInvalid](const char* name, int& target) {
        if (const std::optional<std::string> raw = read(name); raw.has_value()) {
            const std::optional<int> parsed = parseEnvInt(*raw, 0, 270);
            if (parsed.has_value() && *parsed % 90 == 0) {
                target = *parsed;
            } else {
                reportInvalid(name, *raw);
            }
        }
    };

    applyBool("LUMINA_AUDIO_SHUTDOWN_ON_FAILURE", config.audioSinkShutdownOnFailure);
    applyInt("LUMINA_AUDIO_SINK_MAX_RETRIES", config.audioSinkMaxRetries, 1, 10000);
    applyInt("LUMINA_AUDIO_SINK_RETRY_MS", config.audioSinkRetryIntervalMs, 100, 60000);
    // faceEnabled is intentionally not overridable: face recognition must always run.
    applyBool("LUMINA_PROXIMITY_ENABLED", config.proximityEnabled);
    // Detection tuning (FR-02): object narration vs. generic obstacle warning. The
    // obstacle class list is the strongest knob (e.g. add/remove person).
    applyFloat("LUMINA_NEAR_AREA_FRACTION", config.nearAreaFraction, 0.0F, 1.0F);
    applyFloat("LUMINA_MID_AREA_FRACTION", config.midAreaFraction, 0.0F, 1.0F);
    applyFloat("LUMINA_PATH_CENTER_TOLERANCE", config.pathCenterTolerance, 0.0F, 1.0F);
    applyFloat("LUMINA_PROXIMITY_THRESHOLD_M", config.proximityThresholdM, 0.05F, 5.0F);
    applyFloat("LUMINA_PROXIMITY_RELEASE_M", config.proximityReleaseM, 0.05F, 5.0F);
    applyIntList("LUMINA_OBSTACLE_CLASSES", config.obstacleClassIds, 0, 90);
    // Camera mount correction (CHG-0101).
    applyRightAngle("LUMINA_CAMERA_ROTATION", config.cameraRotationDegrees);
    return config;
}

Config applyEnvOverrides(Config config)
{
    const EnvLookup lookup = [](const char* name) -> std::optional<std::string> {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') {
            return std::string(value);
        }
        return std::nullopt;
    };
    const InvalidEnvFn onInvalid = [](std::string_view name, std::string_view value) {
        LUMINA_LOG_WARN("runtime config: ignoring invalid {}='{}'", name, value);
    };
    return applyEnvOverrides(config, lookup, onInvalid);
}

}  // namespace lumina::core
