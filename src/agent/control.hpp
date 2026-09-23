// ---------------------------------------------------------------------------
// TCP control server for the companion agent (FR-11).
//
// A thin, line-delimited JSON socket layer. All protocol logic lives in the Agent
// handler, so this class only accepts connections, reads lines, and writes replies.
// One thread per connection lets a long-running enrollment stream while another
// connection issues a cancel or a status query.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace lumina::agent {

// Sends one reply line (JSON text without the trailing newline).
using ReplyFn = std::function<void(const std::string& jsonLine)>;

// Handles one request line. Returns true to keep the connection open, false to
// close it (e.g. after an authentication failure).
using RequestHandler = std::function<bool(const std::string& requestLine, const ReplyFn& reply)>;

// Line-delimited JSON control server. Owns its accept thread; stop() joins it and
// every connection thread, so destruction is always clean.
class ControlServer {
public:
    // `bindAddress` is the local IPv4 address to listen on ("0.0.0.0" = all). The
    // setup script points it at the hotspot gateway so the app (which always talks
    // to its gateway) reaches it without exposing the port on other networks.
    ControlServer(int port, std::string bindAddress, RequestHandler handler);
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Bind + listen + start the accept thread. Returns false on failure. `logFailure` is false
    // when a caller is retrying the bind on purpose (e.g. waiting for the hotspot address at boot),
    // so the expected "Cannot assign requested address" is not logged on every attempt (CHG-0094).
    [[nodiscard]] bool start(bool logFailure = true);
    // Signal the accept loop to finish, then join it (and all connections).
    void stop();

private:
    void serve();
    void handleConnection(int clientFd);

    int m_port;
    std::string m_bindAddress;
    RequestHandler m_handler;
    int m_listenFd = -1;
    std::atomic<bool> m_running{false};
    std::jthread m_acceptThread;
    std::vector<std::thread> m_connections;
};

}  // namespace lumina::agent
