// ---------------------------------------------------------------------------
// Telemetry parsing and serialization for the companion agent (FR-11).
//
// Pure functions only: the runtime-status block, the OS stat files and the JSON
// datagram are all string-in/string-out, so they are unit-tested on the host. The
// agent's I/O loop just reads the files and calls these.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumina::agent {

// Runtime health parsed from /run/lumina/status. `reachable` is set by the caller
// from the file's age, not by parseStatusBlock.
struct RuntimeStatus {
    bool running = false;
    int uptimeSeconds = 0;
    double fps = 0.0;
    double rssMb = 0.0;
    float meanLuma = -1.0F;
    std::size_t faceCount = 0;
    bool sinkReady = false;
    std::vector<std::string> people;
};

// Parse the key=value block written by the runtime (docs/COMPANION.md). Unknown
// lines are ignored. Returns nullopt when the block is empty or lacks the
// "running" key, so a truncated read is not mistaken for a valid snapshot.
[[nodiscard]] std::optional<RuntimeStatus> parseStatusBlock(std::string_view text);

// Parse OS stat contents. Each returns a negative value for malformed input so the
// caller can show "unknown" instead of a bogus number.
[[nodiscard]] double parseThermalC(std::string_view milliCelsiusText); // "46160\n" -> 46.16
[[nodiscard]] long parseMemAvailableKb(std::string_view meminfoText);  // looks up MemAvailable
[[nodiscard]] double parseLoad1(std::string_view loadavgText);         // first field

// Everything the UDP datagram needs. Mirrors docs/APP_PROTOCOL.md field-for-field.
struct TelemetryState {
    long long ts = 0;              // agent epoch seconds (best effort; see RTC note)
    bool runtimeReachable = false;
    bool running = false;
    int uptimeS = 0;
    std::string sink = "absent";   // "ready" | "waiting" | "absent"
    std::size_t faceCount = 0;
    double fps = 0.0;
    double rssMb = 0.0;
    long memAvailableKb = -1;
    double tempC = -1.0;
    double load1 = -1.0;
    int volume = -1;               // 0..100, -1 = unknown
    bool muted = false;
    float luma = -1.0F;            // -1 = unknown
    std::vector<std::string> people;
    bool enrollActive = false;
    std::string enrollPhase;
    int enrollCaptured = 0;
    int enrollTotal = 0;
};

// Derive "day"/"night"/"unknown" from the mean luminance. The beta camera has no
// physical day/night switch, so this is a light-level proxy (docs/COMPANION.md).
[[nodiscard]] std::string deriveDayNight(float luma);

// Serialize the 1 Hz status datagram: one JSON object, no trailing newline.
[[nodiscard]] std::string buildTelemetryJson(const TelemetryState& state);

}  // namespace lumina::agent
