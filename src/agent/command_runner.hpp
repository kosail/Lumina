// ---------------------------------------------------------------------------
// External-command runner for the companion agent (FR-11).
//
// The agent shells out to amixer / systemctl / lumina_enroll instead of
// reimplementing them. This interface keeps that logic unit-testable with a fake
// (INV-030): the volume, enrollment and runtime-control classes depend only on
// ICommandRunner, never on fork/exec directly.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace lumina::agent {

// Result of a finished command. `output` is stdout and stderr merged, in order.
struct CommandResult {
    int exitCode = -1;   // process exit status; -1 when it could not be started
    std::string output;
};

// Runs child processes. Implementations must never throw and must return promptly
// once the child exits.
class ICommandRunner {
public:
    virtual ~ICommandRunner() = default;

    // Run `argv` to completion and capture merged stdout+stderr.
    virtual CommandResult run(const std::vector<std::string>& argv) = 0;

    // Run `argv` and call `onLine` for each complete output line as it arrives
    // (used to stream enrollment progress). `cancel`, when non-null, is polled and
    // terminates the child early; the returned code is then the child's status, or
    // -1 if it had to be killed. Returns the exit code.
    virtual int runLines(const std::vector<std::string>& argv,
                         const std::function<void(const std::string&)>& onLine,
                         const std::atomic<bool>* cancel = nullptr) = 0;
};

// Production runner: fork(2)/execvp(3) with stderr redirected into stdout so a
// single pipe carries both streams in order.
class SystemCommandRunner final : public ICommandRunner {
public:
    CommandResult run(const std::vector<std::string>& argv) override;
    int runLines(const std::vector<std::string>& argv,
                 const std::function<void(const std::string&)>& onLine,
                 const std::atomic<bool>* cancel = nullptr) override;
};

}  // namespace lumina::agent
