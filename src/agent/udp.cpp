// ---------------------------------------------------------------------------
// UDP telemetry broadcaster implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/udp.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/logging.hpp"

namespace lumina::agent {

UdpBroadcaster::UdpBroadcaster(int port) : m_port(port) {}

UdpBroadcaster::~UdpBroadcaster()
{
    close();
}

bool UdpBroadcaster::open()
{
    m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0) {
        LUMINA_LOG_ERROR("agent: telemetry socket() failed: {}", std::strerror(errno));
        return false;
    }
    int broadcast = 1;
    if (::setsockopt(m_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) != 0) {
        LUMINA_LOG_ERROR("agent: SO_BROADCAST failed: {}", std::strerror(errno));
        close();
        return false;
    }
    return true;
}

void UdpBroadcaster::send(const std::string& payload)
{
    if (m_fd < 0) {
        return;
    }
    struct sockaddr_in destination;
    std::memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<std::uint16_t>(m_port));
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);  // 255.255.255.255

    ::sendto(m_fd, payload.data(), payload.size(), 0,
             reinterpret_cast<struct sockaddr*>(&destination), sizeof(destination));
    // A failed send (no client, transient network) is intentionally ignored.
}

void UdpBroadcaster::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

}  // namespace lumina::agent
