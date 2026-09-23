// ---------------------------------------------------------------------------
// Environment-variable overrides implementation (see core/config_env.hpp).
// ---------------------------------------------------------------------------

#include "core/config_env.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <system_error>

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

    applyBool("LUMINA_AUDIO_SHUTDOWN_ON_FAILURE", config.audioSinkShutdownOnFailure);
    applyInt("LUMINA_AUDIO_SINK_MAX_RETRIES", config.audioSinkMaxRetries, 1, 10000);
    applyInt("LUMINA_AUDIO_SINK_RETRY_MS", config.audioSinkRetryIntervalMs, 100, 60000);
    // faceEnabled is intentionally not overridable: face recognition must always run.
    applyBool("LUMINA_PROXIMITY_ENABLED", config.proximityEnabled);
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
