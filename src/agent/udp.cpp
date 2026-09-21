// ---------------------------------------------------------------------------
// UdpTransport implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/udp.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

#include "core/logging.hpp"

namespace lumina::agent {

UdpTransport::UdpTransport(int port, std::string broadcastAddress)
    : m_port(port), m_broadcastAddress(std::move(broadcastAddress))
{
}

UdpTransport::~UdpTransport()
{
    close();
}

bool UdpTransport::open()
{
    m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0) {
        LUMINA_LOG_ERROR("agent: telemetry socket() failed: {}", std::strerror(errno));
        return false;
    }
    // Close-on-exec: spawned commands must not inherit the telemetry socket.
    ::fcntl(m_fd, F_SETFD, FD_CLOEXEC);

    int broadcast = 1;
    if (::setsockopt(m_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) != 0) {
        LUMINA_LOG_ERROR("agent: SO_BROADCAST failed: {}", std::strerror(errno));
        close();
        return false;
    }

    m_broadcast = {};
    m_broadcast.sin_family = AF_INET;
    m_broadcast.sin_port = htons(static_cast<std::uint16_t>(m_port));
    if (::inet_pton(AF_INET, m_broadcastAddress.c_str(), &m_broadcast.sin_addr) != 1) {
        LUMINA_LOG_WARN("agent: invalid broadcast address '{}'; falling back to 255.255.255.255",
                        m_broadcastAddress);
        m_broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }

    // Bind so clients can send their subscribe datagrams to this port.
    struct sockaddr_in local;
    std::memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(static_cast<std::uint16_t>(m_port));
    if (::bind(m_fd, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) != 0) {
        LUMINA_LOG_ERROR("agent: telemetry bind(:{}) failed: {}", m_port, std::strerror(errno));
        close();
        return false;
    }
    return true;
}

bool UdpTransport::receive(std::string& payload, struct sockaddr_in& from, int timeoutMs)
{
    if (m_fd < 0) {
        return false;
    }
    struct pollfd descriptor;
    descriptor.fd = m_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    const int ready = ::poll(&descriptor, 1, timeoutMs);
    if (ready <= 0) {
        return false;  // timeout or error (EINTR is treated as "no data")
    }

    char buffer[2048];
    struct sockaddr_in sender;
    socklen_t senderLength = sizeof(sender);
    const ssize_t count = ::recvfrom(m_fd, buffer, sizeof(buffer), 0,
                                     reinterpret_cast<struct sockaddr*>(&sender), &senderLength);
    if (count <= 0) {
        return false;
    }
    payload.assign(buffer, static_cast<std::size_t>(count));
    from = sender;
    return true;
}

void UdpTransport::sendBroadcast(const std::string& payload)
{
    if (m_fd < 0) {
        return;
    }
    ::sendto(m_fd, payload.data(), payload.size(), 0,
             reinterpret_cast<const struct sockaddr*>(&m_broadcast), sizeof(m_broadcast));
    // A failed send (no client, transient network) is intentionally ignored.
}

void UdpTransport::sendTo(const struct sockaddr_in& destination, const std::string& payload)
{
    if (m_fd < 0) {
        return;
    }
    ::sendto(m_fd, payload.data(), payload.size(), 0,
             reinterpret_cast<const struct sockaddr*>(&destination), sizeof(destination));
}

void UdpTransport::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

}  // namespace lumina::agent
