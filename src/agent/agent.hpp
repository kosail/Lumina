// ---------------------------------------------------------------------------
// Companion agent (FR-11).
//
// Ties together the pieces: reads the runtime status file and OS stats, broadcasts
// telemetry over UDP (and unicasts to subscribed clients), and serves the
// token-gated TCP control channel. It is a separate process from the runtime, so it
// can shell out to systemctl/amixer and never shares threads or memory with the
// detection path (INV-030/INV-031).
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "agent/amixer.hpp"
#include "agent/command_runner.hpp"
#include "agent/control.hpp"
#include "agent/enroll.hpp"
#include "agent/runtime_state.hpp"
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
    // Control listens on this local IPv4 address. The setup script points it at the
    // hotspot gateway (the address the app uses); "0.0.0.0" listens everywhere.
    std::string bindAddress = "0.0.0.0";
    // Telemetry broadcast target. Default limited broadcast; the setup script may
    // set the hotspot subnet broadcast (e.g. 10.42.0.255), which is delivered more
    // reliably on a Wi-Fi AP. Unicast subscribers are the robust path regardless.
    std::string broadcastAddress = "255.255.255.255";
    int subscriberTtlSeconds = 10;                   // unicast subscriber lifetime
    std::size_t maxSubscribers = 8;                   // cap on unicast fan-out
    std::string enrollTempDir;                       // disk scratch for image enroll
    int enrollFrames = 10;                           // default AND maximum frames
    std::size_t maxEnrollImages = 12;                // image-route cap: count
    std::size_t maxEnrollImageBytes = 8u * 1024u * 1024u;  // image-route cap: total bytes
    bool enrollImagesStopRuntime = false;            // stop the runtime to free RAM
    EnrollOrchestrator::Paths enroll;                // binary/model/store/piper paths
    int statusStaleSeconds = 5;                      // runtime considered down after this
    // Control-bind retry (CHG-0094): at boot the hotspot gateway address may not exist yet, so the
    // TCP bind can fail with EADDRNOTAVAIL. Retry for ~60 s before giving up.
    int controlBindMaxAttempts = 60;
    int controlBindRetryIntervalMs = 1000;
};

// The single long-lived agent object. Non-copyable; owns its sockets and threads.
class Agent {
public:
    Agent(AgentConfig config, ICommandRunner& runner);
    ~Agent();
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // Open the UDP/TCP sockets and start the telemetry + subscription threads.
    // Returns false (after logging) when a socket cannot be opened.
    [[nodiscard]] bool start();
    // Stop the threads and close the sockets. Safe to call twice.
    void stop();

private:
    // One client that asked for telemetry (remembered from its source address).
    struct Subscriber {
        struct sockaddr_in address{};
        std::chrono::steady_clock::time_point lastSeen{};
    };

    // Telemetry thread: once per second, broadcast the snapshot and unicast it to
    // every unexpired subscriber.
    void telemetryLoop(std::stop_token stopToken);
    // Subscription thread: receive client datagrams and remember their source.
    void udpLoop(std::stop_token stopToken);
    [[nodiscard]] TelemetryState collectState();
    // Build the snapshot, broadcast it, and unicast it to live subscribers.
    void publishTelemetry();

    // Handle one control request line. Returns false to close the connection.
    bool handleRequest(const std::string& line, const ReplyFn& reply);

    // Command handlers. Return the reply lines through `reply`.
    void handleVolumeGet(const ReplyFn& reply, bool ok = true);
    void handleVolumeSet(long long value, const ReplyFn& reply);
    void handleVolumeMute(bool value, const ReplyFn& reply);
    void handlePeopleList(const ReplyFn& reply);
    void handleRuntimeState(const ReplyFn& reply);
    void handleRuntimeStart(const ReplyFn& reply);
    void handleRuntimeStop(const ReplyFn& reply);
    void handleEnrollCamera(const std::string& name, long long frames, const ReplyFn& reply);
    void handleEnrollImages(const std::string& name, const std::string& imagesJson,
                            const ReplyFn& reply);

    // Read the runtime status file. Returns nullopt when absent/unparseable.
    [[nodiscard]] std::optional<RuntimeStatus> readRuntimeStatus();

    // Enrolled names, read from the persisted face store (not the live status file)
    // so the list survives the runtime being stopped. Cached by the store's
    // mtime/size so the 1 Hz telemetry loop only re-reads when the file changes.
    // Thread-safe (telemetry + control both call it).
    [[nodiscard]] std::vector<std::string> enrolledNames();

    // Whether the runtime is "initializing": `systemctl is-active` (cached, probed
    // at a low rate) but not yet reporting a fresh running status. `ready` is the
    // caller's readiness check (fresh status file + running). The decision lives in
    // the pure runtime_state helper; this only supplies times and probe results.
    [[nodiscard]] bool runtimeInitializing(bool ready);

    // Record a known liveness (from a control request or an enrollment phase) and
    // reset the probe clock so the next telemetry tick does not immediately re-probe.
    void setRuntimeLiveness(RuntimeLiveness liveness);

    AgentConfig m_config;
    ICommandRunner& m_runner;
    SystemRuntimeControl m_runtime;
    AmixerVolume m_volume;
    EnrollOrchestrator m_enroller;
    UdpTransport m_udp;
    std::unique_ptr<ControlServer> m_control;
    std::jthread m_telemetryThread;
    std::jthread m_udpThread;

    // Serialize volume access: the telemetry thread reads it while a control
    // connection may set it. Also serializes enrollment (one at a time).
    std::mutex m_volumeMutex;
    std::mutex m_enrollMutex;
    std::mutex m_stateMutex;  // guards the enrollment state fields below
    std::mutex m_subscribersMutex;
    std::vector<Subscriber> m_subscribers;

    // Enrollment progress, surfaced in telemetry and used to build replies.
    bool m_enrollActive = false;
    std::string m_enrollPhase;
    int m_enrollCaptured = 0;
    int m_enrollTotal = 0;
    std::atomic<bool> m_enrollCancel{false};

    // Cached enrolled names (guarded by m_namesMutex). mtime/size/inode form the
    // store's stat identity; a change triggers a re-read. The inode is included
    // because enrollment rewrites via temp + rename, which changes the inode even
    // when mtime (coarse resolution) and size coincide (F2).
    std::mutex m_namesMutex;
    std::vector<std::string> m_namesCache;
    std::int64_t m_namesMtime = -1;
    std::uintmax_t m_namesSize = 0;
    std::uintmax_t m_namesInode = 0;
    bool m_namesValid = false;

    // Cached `systemctl is-active` liveness for the initializing flag. Guarded by
    // m_runtimeStateMutex; m_runtimeStateGeneration lets a probe performed outside
    // the lock detect that another thread changed the state meanwhile (F5).
    std::mutex m_runtimeStateMutex;
    RuntimeLiveness m_runtimeLiveness = RuntimeLiveness::Unknown;
    std::chrono::steady_clock::time_point m_lastActiveProbe{};
    std::uint64_t m_runtimeStateGeneration = 0;
};

}  // namespace lumina::agent
