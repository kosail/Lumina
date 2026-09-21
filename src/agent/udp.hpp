// ---------------------------------------------------------------------------
// UDP telemetry broadcaster for the companion agent (FR-11).
//
// One datagram per second to the broadcast address; clients simply listen. UDP is
// fire-and-forget by design: a missing client must never affect the agent or the
// runtime (INV-034).
// ---------------------------------------------------------------------------

#pragma once

#include <string>

namespace lumina::agent {

class UdpBroadcaster {
public:
    explicit UdpBroadcaster(int port);
    ~UdpBroadcaster();
    UdpBroadcaster(const UdpBroadcaster&) = delete;
    UdpBroadcaster& operator=(const UdpBroadcaster&) = delete;

    // Create + configure the socket. Returns false (after logging) on failure.
    [[nodiscard]] bool open();
    void send(const std::string& payload);
    void close();

private:
    int m_port;
    int m_fd = -1;
};

}  // namespace lumina::agent
