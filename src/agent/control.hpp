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
    ControlServer(int port, RequestHandler handler);
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Bind + listen + start the accept thread. Returns false (after logging) on
    // failure.
    [[nodiscard]] bool start();
    // Signal the accept loop to finish, then join it (and all connections).
    void stop();

private:
    void serve();
    void handleConnection(int clientFd);

    int m_port;
    RequestHandler m_handler;
    int m_listenFd = -1;
    std::atomic<bool> m_running{false};
    std::jthread m_acceptThread;
    std::vector<std::thread> m_connections;
};

}  // namespace lumina::agent
