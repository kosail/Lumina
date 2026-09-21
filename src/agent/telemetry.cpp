// ---------------------------------------------------------------------------
// Telemetry parsing and serialization implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/telemetry.hpp"

#include <cctype>
#include <charconv>
#include <format>
#include <string>
#include <system_error>
#include <vector>

#include "agent/json.hpp"

namespace lumina::agent {

namespace {

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// Parse a whole string_view as a double; nullopt on failure.
std::optional<double> toDouble(std::string_view text)
{
    const std::string value(trim(text));
    if (value.empty()) {
        return std::nullopt;
    }
    double out = 0.0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, out);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return out;
}

std::optional<long long> toInt(std::string_view text)
{
    const std::string value(trim(text));
    if (value.empty()) {
        return std::nullopt;
    }
    long long out = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const std::from_chars_result result = std::from_chars(begin, end, out);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return out;
}

}  // namespace

std::optional<RuntimeStatus> parseStatusBlock(std::string_view text)
{
    RuntimeStatus status;
    bool sawRunning = false;

    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t end = text.find('\n', position);
        const std::string_view line =
            text.substr(position, end == std::string_view::npos ? std::string_view::npos
                                                                : end - position);
        position = end == std::string_view::npos ? text.size() : end + 1;

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = line.substr(equals + 1);

        if (key == "running") {
            if (const auto parsed = toInt(value)) {
                status.running = *parsed != 0;
                sawRunning = true;
            }
        } else if (key == "uptime_s") {
            if (const auto parsed = toInt(value)) {
                status.uptimeSeconds = static_cast<int>(*parsed);
            }
        } else if (key == "fps") {
            if (const auto parsed = toDouble(value)) {
                status.fps = *parsed;
            }
        } else if (key == "rss_mb") {
            if (const auto parsed = toDouble(value)) {
                status.rssMb = *parsed;
            }
        } else if (key == "mean_luma") {
            if (const auto parsed = toDouble(value)) {
                status.meanLuma = static_cast<float>(*parsed);
            }
        } else if (key == "face_count") {
            if (const auto parsed = toInt(value)) {
                // Never let a negative value wrap into a huge size_t.
                status.faceCount = *parsed > 0 ? static_cast<std::size_t>(*parsed) : 0;
            }
        } else if (key == "sink_ready") {
            if (const auto parsed = toInt(value)) {
                status.sinkReady = *parsed != 0;
            }
        } else if (key == "person") {
            status.people.emplace_back(value);  // repeated key: one name per line
        }
    }

    if (!sawRunning) {
        return std::nullopt;
    }
    return status;
}

double parseThermalC(std::string_view milliCelsiusText)
{
    const auto value = toInt(milliCelsiusText);
    if (!value.has_value()) {
        return -1.0;
    }
    return static_cast<double>(*value) / 1000.0;
}

long parseMemAvailableKb(std::string_view meminfoText)
{
    std::size_t position = 0;
    while (position < meminfoText.size()) {
        const std::size_t end = meminfoText.find('\n', position);
        const std::string_view line =
            meminfoText.substr(position, end == std::string_view::npos ? std::string_view::npos
                                                                       : end - position);
        position = end == std::string_view::npos ? meminfoText.size() : end + 1;

        if (line.starts_with("MemAvailable:")) {
            // "MemAvailable:   65536 kB" -> the number is the second token.
            std::string_view rest = trim(line.substr(13));
            const std::size_t space = rest.find_first_of(" \tk");
            const std::string_view number = rest.substr(0, space);
            if (const auto parsed = toInt(number)) {
                return static_cast<long>(*parsed);
            }
            return -1;
        }
    }
    return -1;
}

double parseLoad1(std::string_view loadavgText)
{
    const std::string_view first = loadavgText.substr(0, loadavgText.find(' '));
    const auto value = toDouble(first);
    return value.has_value() ? *value : -1.0;
}

std::string deriveDayNight(float luma)
{
    if (luma < 0.0F) {
        return "unknown";
    }
    return luma >= 0.35F ? "day" : "night";
}

std::string buildTelemetryJson(const TelemetryState& state)
{
    // Build the people array with proper escaping.
    std::string peopleJson = "[";
    for (std::size_t i = 0; i < state.people.size(); ++i) {
        if (i > 0) {
            peopleJson += ",";
        }
        peopleJson += "\"" + escapeJson(state.people[i]) + "\"";
    }
    peopleJson += "]";

    // Volume as an integer, or null when unknown.
    const std::string volumeJson = state.volume < 0 ? "null" : std::format("{}", state.volume);
    const std::string tempJson = state.tempC < 0.0 ? "null" : std::format("{:.1f}", state.tempC);
    const std::string loadJson = state.load1 < 0.0 ? "null" : std::format("{:.2f}", state.load1);
    const std::string memJson =
        state.memAvailableKb < 0 ? "null" : std::format("{}", state.memAvailableKb);

    std::string enroll = std::format("\"active\":{}", state.enrollActive ? "true" : "false");
    if (state.enrollActive) {
        enroll += std::format(",\"phase\":\"{}\",\"captured\":{},\"total\":{}",
                              escapeJson(state.enrollPhase), state.enrollCaptured, state.enrollTotal);
    }

    return std::format(
        "{{\"t\":\"status\",\"proto\":1,\"ts\":{},"
        "\"runtime\":{{\"reachable\":{},\"running\":{},\"uptimeS\":{},\"sink\":\"{}\",\"faceCount\":{}}},"
        "\"core\":{{\"fps\":{:.1f},\"rssMb\":{:.1f},\"memAvailableKb\":{},\"tempC\":{},\"load1\":{}}},"
        "\"sensors\":{{\"volume\":{},\"muted\":{},\"luma\":{:.3f},\"dayNight\":\"{}\"}},"
        "\"people\":{},"
        "\"enroll\":{{{}}}}}",
        state.ts, state.runtimeReachable ? "true" : "false", state.running ? "true" : "false",
        state.uptimeS, escapeJson(state.sink), state.faceCount, state.fps, state.rssMb, memJson,
        tempJson, loadJson, volumeJson, state.muted ? "true" : "false", state.luma,
        deriveDayNight(state.luma), peopleJson, enroll);
}

}  // namespace lumina::agent
