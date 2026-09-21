// ---------------------------------------------------------------------------
// Companion agent (FR-11).
//
// Ties together the pieces: reads the runtime status file and OS stats, broadcasts
// telemetry over UDP, and serves the token-gated TCP control channel. It is a
// separate process from the runtime, so it can shell out to systemctl/amixer and
// never shares threads or memory with the detection path (INV-030/INV-031).
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "agent/amixer.hpp"
#include "agent/command_runner.hpp"
#include "agent/control.hpp"
#include "agent/enroll.hpp"
#include "agent/telemetry.hpp"
#include "agent/udp.hpp"

namespace lumina::agent {

// Everything the agent needs to run. Defaults match the deployed layout.
struct AgentConfig {
    int telemetryPort = 47600;
    int controlPort = 47601;
    std::string token;                               // shared secret; required for control
    std::string statusPath = "/run/lumina/status";   // written by the runtime
    std::string thermalPath = "/sys/class/thermal/thermal_zone0/temp";
    std::string meminfoPath = "/proc/meminfo";
    std::string loadavgPath = "/proc/loadavg";
    std::string enrollTempDir = "/run/lumina/enroll";  // scratch for image enrollment
    int enrollFrames = 10;                             // always 10 frames
    EnrollOrchestrator::Paths enroll;                  // binary/model/store/piper paths
    int statusStaleSeconds = 5;                        // runtime considered down after this
};

// The single long-lived agent object. Non-copyable; owns its sockets and threads.
class Agent {
public:
    Agent(AgentConfig config, ICommandRunner& runner);
    ~Agent();
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // Open the UDP/TCP sockets and start the telemetry thread. Returns false
    // (after logging) when a socket cannot be opened.
    [[nodiscard]] bool start();
    // Stop the telemetry thread and close the sockets. Safe to call twice.
    void stop();

private:
    // Telemetry thread body: once per second, collect and broadcast a snapshot.
    void telemetryLoop(std::stop_token stopToken);
    [[nodiscard]] TelemetryState collectState();
    // Handle one control request line. Returns false to close the connection.
    bool handleRequest(const std::string& line, const ReplyFn& reply);

    // Command handlers. Return the reply lines through `reply`.
    void handleVolumeGet(const ReplyFn& reply);
    void handleVolumeSet(long long value, const ReplyFn& reply);
    void handleVolumeMute(bool value, const ReplyFn& reply);
    void handlePeopleList(const ReplyFn& reply);
    void handleRuntimeState(const ReplyFn& reply);
    void handleRuntimeStart(const ReplyFn& reply);
    void handleRuntimeStop(const ReplyFn& reply);
    void handleEnrollCamera(const std::string& name, long long frames, const ReplyFn& reply);
    void handleEnrollImages(const std::string& name, const std::string& imagesJson,
                            const ReplyFn& reply);

    // Read the runtime status file (fresh or stale). Returns nullopt when absent.
    [[nodiscard]] std::optional<RuntimeStatus> readRuntimeStatus();

    AgentConfig m_config;
    ICommandRunner& m_runner;
    SystemRuntimeControl m_runtime;
    AmixerVolume m_volume;
    EnrollOrchestrator m_enroller;
    UdpBroadcaster m_udp;
    std::unique_ptr<ControlServer> m_control;
    std::jthread m_telemetryThread;

    // Serialize volume access: the telemetry thread reads it while a control
    // connection may set it. Also serializes enrollment (one at a time).
    std::mutex m_volumeMutex;
    std::mutex m_enrollMutex;
    std::mutex m_stateMutex;  // guards the enrollment state fields below

    // Enrollment progress, surfaced in telemetry and used to build replies.
    bool m_enrollActive = false;
    std::string m_enrollPhase;
    int m_enrollCaptured = 0;
    int m_enrollTotal = 0;
    std::atomic<bool> m_enrollCancel{false};
};

}  // namespace lumina::agent
