// ---------------------------------------------------------------------------
// Enrollment orchestration implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/enroll.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "core/logging.hpp"

namespace lumina::agent {

namespace {

// Parse the integer at the start of `text` (after any leading spaces).
std::optional<int> leadingInt(std::string_view text)
{
    const std::size_t start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    int value = 0;
    std::size_t index = start;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        value = value * 10 + (text[index] - '0');
        ++index;
    }
    if (index == start) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

std::optional<EnrollEvent> parseEnrollLine(std::string_view line)
{
    EnrollEvent event;

    // "  captured embedding 3/10" (camera) or "  captured embedding 1 from 'x'"
    // (photo). Both start with the same marker.
    if (const std::size_t marker = line.find("captured embedding "); marker != std::string_view::npos) {
        const std::string_view rest = line.substr(marker + 19);
        if (const std::optional<int> captured = leadingInt(rest)) {
            event.kind = EnrollEvent::Kind::Captured;
            event.captured = *captured;
            // Camera form has "/total"; the photo form has " from".
            if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
                if (const std::optional<int> total = leadingInt(rest.substr(slash + 1))) {
                    event.total = *total;
                }
            }
            return event;
        }
    }

    // "(no face detected — keep looking at the camera)"
    if (line.find("no face detected") != std::string_view::npos) {
        event.kind = EnrollEvent::Kind::NoFace;
        event.message = std::string(line);
        return event;
    }

    // "Enrolled 'Ana' with 10 embedding(s) into '...'."
    if (line.find("Enrolled '") != std::string_view::npos) {
        event.kind = EnrollEvent::Kind::Enrolled;
        const std::size_t with = line.find("with ");
        if (with != std::string_view::npos) {
            if (const std::optional<int> count = leadingInt(line.substr(with + 5))) {
                event.captured = *count;
            }
        }
        return event;
    }

    // "enroll: no face was captured; no data written" and friends (stderr).
    if (line.find("enroll:") != std::string_view::npos) {
        event.kind = EnrollEvent::Kind::Error;
        event.message = std::string(line);
        return event;
    }

    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> decodeBase64(std::string_view text)
{
    // Map one base64 character to its 6-bit value (-1 for padding/whitespace).
    auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;  // also catches '=' and whitespace, handled by the caller
    };

    std::vector<std::uint8_t> out;
    unsigned int accumulator = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            continue;  // padding / whitespace
        }
        const int value = valueOf(c);
        if (value < 0) {
            return std::nullopt;
        }
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}


int clampFrameCount(long long requested, int fallback, int max)
{
    if (max < 1) {
        max = 1;
    }
    if (requested <= 0) {
        const int safeFallback = fallback < 1 ? 1 : fallback;
        return safeFallback > max ? max : safeFallback;
    }
    return requested > max ? max : static_cast<int>(requested);
}

SystemRuntimeControl::SystemRuntimeControl(ICommandRunner& runner) : m_runner(runner) {}

bool SystemRuntimeControl::stop()
{
    // `sudo -n` fails fast instead of prompting; the sudoers rule grants exactly
    // these two systemctl commands (scripts/10-setup_agent.sh).
    const CommandResult result = m_runner.run({"sudo", "-n", "systemctl", "stop", "lumina"});
    if (result.exitCode != 0) {
        LUMINA_LOG_WARN("agent: could not stop lumina.service: {}", result.output);
        return false;
    }
    return true;
}

bool SystemRuntimeControl::start()
{
    const CommandResult result = m_runner.run({"sudo", "-n", "systemctl", "start", "lumina"});
    if (result.exitCode != 0) {
        LUMINA_LOG_ERROR("agent: could not start lumina.service: {}", result.output);
        return false;
    }
    return true;
}

bool SystemRuntimeControl::isRunning()
{
    const CommandResult result = m_runner.run({"systemctl", "is-active", "lumina"});
    return result.exitCode == 0 && result.output.find("active") != std::string::npos;
}

EnrollOrchestrator::EnrollOrchestrator(ICommandRunner& runner, IRuntimeControl& runtime, Paths paths)
    : m_runner(runner), m_runtime(runtime), m_paths(std::move(paths))
{
}

std::vector<std::string> EnrollOrchestrator::baseArgs() const
{
    std::vector<std::string> args;
    if (!m_paths.piperLib.empty()) {
        // `env` sets LD_LIBRARY_PATH only for the child, so the agent's own
        // environment is never mutated. The enrollment tool links lumina_core,
        // which needs libonnxruntime when the audio path is enabled.
        args.push_back("env");
        args.push_back("LD_LIBRARY_PATH=" + m_paths.piperLib);
    }
    args.push_back(m_paths.binary);
    return args;
}

int EnrollOrchestrator::enrollFromCamera(const std::string& name, int frames,
                                         const EnrollCallback& onEvent,
                                         const std::atomic<bool>* cancel)
{
    EnrollEvent stopping;
    stopping.kind = EnrollEvent::Kind::Phase;
    stopping.message = "stopping_runtime";
    onEvent(stopping);
    m_runtime.stop();  // frees the camera; a failure surfaces as an enroll error below

    std::vector<std::string> args = baseArgs();
    args.insert(args.end(), {"--name", name, "--camera", "--frames", std::to_string(frames),
                             "--embeddings", std::to_string(frames), "--model-dir", m_paths.modelDir,
                             "--store", m_paths.store});

    const int exitCode = m_runner.runLines(
        args,
        [&onEvent](const std::string& line) {
            if (const std::optional<EnrollEvent> event = parseEnrollLine(line)) {
                onEvent(*event);
            }
        },
        cancel);

    // ALWAYS bring the runtime back, whatever happened above.
    EnrollEvent starting;
    starting.kind = EnrollEvent::Kind::Phase;
    starting.message = "starting_runtime";
    onEvent(starting);
    m_runtime.start();
    return exitCode;
}

int EnrollOrchestrator::enrollFromImages(const std::string& name,
                                         const std::vector<std::string>& imagePaths,
                                         const EnrollCallback& onEvent,
                                         const std::atomic<bool>* cancel)
{
    std::vector<std::string> args = baseArgs();
    args.insert(args.end(), {"--name", name});
    for (const std::string& path : imagePaths) {
        args.insert(args.end(), {"--image", path});
    }
    args.insert(args.end(), {"--model-dir", m_paths.modelDir, "--store", m_paths.store});

    return m_runner.runLines(
        args,
        [&onEvent](const std::string& line) {
            if (const std::optional<EnrollEvent> event = parseEnrollLine(line)) {
                onEvent(*event);
            }
        },
        cancel);
}

}  // namespace lumina::agent
