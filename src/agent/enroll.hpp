// ---------------------------------------------------------------------------
// Enrollment orchestration for the companion agent (FR-11).
//
// Runs the existing `lumina_enroll` tool. The camera route must free the camera
// first (libcamera is single-client), so it stops `lumina.service` and ALWAYS
// restarts it afterwards, even on failure or cancellation. The image route never
// touches the runtime. Parsing is pure and unit-tested.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/command_runner.hpp"

namespace lumina::agent {

// One parsed line of `lumina_enroll` output, or a phase change the orchestrator
// emits itself.
struct EnrollEvent {
    enum class Kind {
        Phase,     // message = "stopping_runtime" | "starting_runtime"
        Captured,  // captured/total embeddings collected
        NoFace,    // a frame was checked but no face was found
        Enrolled,  // final success line; captured = embeddings written
        Error,     // a tool error line (stderr); message = raw text
    };
    Kind kind = Kind::NoFace;
    int captured = 0;
    int total = 0;
    std::string message;
};

// Parse one line of enrollment output. Returns nullopt for lines with no meaning.
[[nodiscard]] std::optional<EnrollEvent> parseEnrollLine(std::string_view line);

// Decode standard base64 (with '=' padding, whitespace tolerated) into bytes.
// Returns nullopt on malformed input. Used for the phone-camera enrollment route,
// where the app sends captured JPEGs as base64 strings.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> decodeBase64(std::string_view text);

// Controls `lumina.service`. Abstracted so the orchestrator is testable and so
// the privileged `systemctl` calls stay in one place.
class IRuntimeControl {
public:
    virtual ~IRuntimeControl() = default;
    virtual bool stop() = 0;
    virtual bool start() = 0;
    [[nodiscard]] virtual bool isRunning() = 0;
};

// Production control: `sudo -n systemctl stop|start lumina` (the agent runs as user
// `lumina` with a narrow sudoers rule) and `systemctl is-active lumina`.
class SystemRuntimeControl final : public IRuntimeControl {
public:
    explicit SystemRuntimeControl(ICommandRunner& runner);
    bool stop() override;
    bool start() override;
    [[nodiscard]] bool isRunning() override;

private:
    ICommandRunner& m_runner;
};

using EnrollCallback = std::function<void(const EnrollEvent&)>;

// Runs the enrollment tool. Paths are explicit so the agent does not depend on its
// working directory.
class EnrollOrchestrator {
public:
    struct Paths {
        std::string binary;    // e.g. /home/lumina/lumina_enroll
        std::string modelDir;  // e.g. /home/lumina/models/face
        std::string store;     // e.g. /home/lumina/models/face/embeddings.bin
        std::string piperLib;  // LD_LIBRARY_PATH for libonnxruntime; may be empty
    };

    EnrollOrchestrator(ICommandRunner& runner, IRuntimeControl& runtime, Paths paths);

    // Stop runtime -> `--camera --frames N` -> ALWAYS restart runtime. Streams
    // events; honors `cancel`. Returns the tool's exit code.
    int enrollFromCamera(const std::string& name, int frames, const EnrollCallback& onEvent,
                         const std::atomic<bool>* cancel);

    // Enroll from image files already written to disk; the runtime is untouched.
    int enrollFromImages(const std::string& name, const std::vector<std::string>& imagePaths,
                         const EnrollCallback& onEvent);

private:
    // [env LD_LIBRARY_PATH=...,] <binary> — shared prefix for both routes.
    [[nodiscard]] std::vector<std::string> baseArgs() const;

    ICommandRunner& m_runner;
    IRuntimeControl& m_runtime;
    Paths m_paths;
};

}  // namespace lumina::agent
