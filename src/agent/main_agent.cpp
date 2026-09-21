// ---------------------------------------------------------------------------
// lumina_agent — companion telemetry/control agent (FR-11).
//
// A separate process from the runtime. It reads /run/lumina/status plus OS stats,
// broadcasts telemetry over UDP, and serves the token-gated TCP control channel
// (volume, people, runtime start/stop, enrollment). It is the only part of the
// system that touches the network; the runtime stays network-free (INV-003).
//
// Usage (normally via lumina-agent.service):
//   LUMINA_HOME=/home/lumina LUMINA_AGENT_TOKEN=... ./lumina_agent
// ---------------------------------------------------------------------------

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include "agent/agent.hpp"
#include "agent/command_runner.hpp"
#include "core/logging.hpp"

namespace {

// Set by the signal handler; polled by main so we can shut down cleanly.
std::atomic<bool> g_running{true};

extern "C" void handleSignal(int signal)
{
    (void)signal;
    g_running.store(false);
}

std::string envOr(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::string(value) : fallback;
}

int envInt(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return std::atoi(value);
}

// The shared control token: LUMINA_AGENT_TOKEN wins, else $LUMINA_HOME/agent.token.
std::string readToken(const std::string& home)
{
    if (const char* token = std::getenv("LUMINA_AGENT_TOKEN"); token != nullptr && token[0] != '\0') {
        return token;
    }
    std::ifstream input(home + "/agent.token");
    if (!input) {
        return {};
    }
    std::string token;
    std::getline(input, token);
    return token;
}

void applyLogLevelFromEnv()
{
    const std::string level = envOr("LUMINA_LOG_LEVEL", "info");
    if (level == "trace") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Trace);
    } else if (level == "debug") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Debug);
    } else if (level == "warn") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Warn);
    } else if (level == "error") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Error);
    } else {
        lumina::core::setLogLevel(lumina::core::LogLevel::Info);
    }
}

}  // namespace

int main()
{
    applyLogLevelFromEnv();

    // Deployed layout: LUMINA_HOME is the directory that holds `lumina`,
    // `lumina_enroll`, `models/` and `third_party/` (the service's WorkingDirectory).
    const std::string home = envOr("LUMINA_HOME", envOr("HOME", "/home/lumina"));

    lumina::agent::AgentConfig config;
    config.telemetryPort = envInt("LUMINA_AGENT_UDP_PORT", config.telemetryPort);
    config.controlPort = envInt("LUMINA_AGENT_TCP_PORT", config.controlPort);
    config.statusPath = envOr("LUMINA_STATUS_PATH", config.statusPath);
    config.token = readToken(home);
    config.enroll.binary = home + "/lumina_enroll";
    config.enroll.modelDir = home + "/models/face";
    config.enroll.store = home + "/models/face/embeddings.bin";
    config.enroll.piperLib = home + "/third_party/libpiper/lib";

    if (config.token.empty()) {
        LUMINA_LOG_WARN("agent: no control token (set LUMINA_AGENT_TOKEN or create {}); "
                        "control commands will be rejected",
                        home + "/agent.token");
    }
    LUMINA_LOG_INFO("agent: home '{}', telemetry UDP {}, control TCP {}", home,
                    config.telemetryPort, config.controlPort);

    lumina::agent::SystemCommandRunner runner;
    lumina::agent::Agent agent(config, runner);
    if (!agent.start()) {
        LUMINA_LOG_ERROR("agent: failed to start");
        return 1;
    }

    // Install handlers after start so a fast failure still returns non-zero.
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // The control server owns its own accept thread; the main thread just waits for
    // a stop signal. This keeps the signal handler trivial (only an atomic).
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    LUMINA_LOG_INFO("agent: stopping");
    agent.stop();
    return 0;
}
