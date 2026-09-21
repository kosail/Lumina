// ---------------------------------------------------------------------------
// TCP control server implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/control.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

#include "core/logging.hpp"

namespace lumina::agent {

namespace {

// Largest request line we accept over the control channel. A client that sends a
// huge line without a newline is dropped rather than growing the buffer without
// bound. It must be large enough for image enrollment: the contract allows up to
// 8 MiB of decoded images in one JSON line, which is ~10.7 MiB of base64 plus
// overhead. 16 MiB leaves margin for that without being effectively unbounded.
constexpr std::size_t kMaxLineBytes = 16 * 1024 * 1024;

}  // namespace

ControlServer::ControlServer(int port, std::string bindAddress, RequestHandler handler)
    : m_port(port), m_bindAddress(std::move(bindAddress)), m_handler(std::move(handler))
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start()
{
    m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        LUMINA_LOG_ERROR("agent: control socket() failed: {}", std::strerror(errno));
        return false;
    }
    // Close-on-exec: spawned commands must not inherit the listening socket.
    ::fcntl(m_listenFd, F_SETFD, FD_CLOEXEC);
    int reuse = 1;
    ::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(m_port));
    if (::inet_pton(AF_INET, m_bindAddress.c_str(), &address.sin_addr) != 1) {
        LUMINA_LOG_ERROR("agent: invalid bind address '{}'", m_bindAddress);
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (::bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0) {
        LUMINA_LOG_ERROR("agent: control bind(:{}) failed: {}", m_port, std::strerror(errno));
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    if (::listen(m_listenFd, 4) != 0) {
        LUMINA_LOG_ERROR("agent: control listen() failed: {}", std::strerror(errno));
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    m_running.store(true, std::memory_order_relaxed);
    m_acceptThread = std::jthread([this] { serve(); });
    LUMINA_LOG_INFO("agent: control listening on TCP :{}", m_port);
    return true;
}

void ControlServer::serve()
{
    while (m_running.load(std::memory_order_relaxed)) {
        // Poll with a timeout so stop() is observed even when no client connects.
        struct pollfd descriptor;
        descriptor.fd = m_listenFd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        const int ready = ::poll(&descriptor, 1, 200);
        if (ready <= 0) {
            continue;
        }
        const int clientFd = ::accept(m_listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!m_running.load(std::memory_order_relaxed)) {
                break;
            }
            LUMINA_LOG_WARN("agent: control accept() failed: {}", std::strerror(errno));
            continue;
        }
        // Close-on-exec: spawned commands must not inherit the client socket.
        ::fcntl(clientFd, F_SETFD, FD_CLOEXEC);
        m_connections.emplace_back([this, clientFd] { handleConnection(clientFd); });
    }

    // The accept loop has ended: every connection thread sees m_running == false
    // within its poll timeout, so joining here is bounded and guarantees no thread
    // outlives the server.
    for (std::thread& connection : m_connections) {
        if (connection.joinable()) {
            connection.join();
        }
    }
    m_connections.clear();
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
}

void ControlServer::handleConnection(int clientFd)
{
    std::string pending;
    char buffer[2048];

    while (m_running.load(std::memory_order_relaxed)) {
        struct pollfd descriptor;
        descriptor.fd = clientFd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        const int ready = ::poll(&descriptor, 1, 200);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;  // timeout: re-check m_running
        }

        const ssize_t count = ::recv(clientFd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;  // peer closed or error
        }
        pending.append(buffer, static_cast<std::size_t>(count));
        if (pending.size() > kMaxLineBytes) {
            LUMINA_LOG_WARN("agent: control client sent an oversized line; closing");
            break;
        }

        bool keepOpen = true;
        std::size_t newline = 0;
        while (keepOpen && (newline = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (line.empty()) {
                continue;
            }
            const ReplyFn reply = [clientFd](const std::string& jsonLine) {
                const std::string out = jsonLine + "\n";
                // MSG_NOSIGNAL: a dead peer returns EPIPE instead of raising SIGPIPE.
                ::send(clientFd, out.data(), out.size(), MSG_NOSIGNAL);
            };
            keepOpen = m_handler(line, reply);
        }
        if (!keepOpen) {
            break;
        }
    }
    ::close(clientFd);
}

void ControlServer::stop()
{
    // Signal only; the accept thread notices within its poll timeout, then joins
    // the connections and closes the listening socket itself. This keeps all socket
    // and thread cleanup on one thread (no close/unblock race).
    if (!m_running.exchange(false)) {
        return;
    }
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
}

}  // namespace lumina::agent
