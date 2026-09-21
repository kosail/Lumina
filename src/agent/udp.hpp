// ---------------------------------------------------------------------------
// UDP telemetry transport for the companion agent (FR-11).
//
// Owns one datagram socket that both broadcasts the 1 Hz status and receives
// client subscription requests. Because the app always talks to its gateway (the
// Pi), unicast replies to a subscribing client are the reliable delivery path;
// the broadcast is best effort and can be pointed at the hotspot subnet.
//
// UDP is fire-and-forget by design: a missing client must never affect the agent
// or the runtime (INV-034).
// ---------------------------------------------------------------------------

#pragma once

#include <netinet/in.h>
#include <string>

namespace lumina::agent {

class UdpTransport {
public:
    // `port` is the shared telemetry port (default 47600). `broadcastAddress` is
    // the IPv4 destination for broadcast sends; default is the limited broadcast,
    // and the setup script may set the hotspot subnet broadcast instead.
    explicit UdpTransport(int port, std::string broadcastAddress = "255.255.255.255");
    ~UdpTransport();
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    // Create the socket, enable broadcast, and bind to 0.0.0.0:<port> so we can
    // also receive client subscriptions. Returns false (after logging) on failure.
    [[nodiscard]] bool open();

    // Wait up to `timeoutMs` for one datagram. On success fills `payload` and the
    // sender address and returns true; false on timeout or error.
    bool receive(std::string& payload, struct sockaddr_in& from, int timeoutMs);

    void sendBroadcast(const std::string& payload);
    void sendTo(const struct sockaddr_in& destination, const std::string& payload);
    void close();

private:
    int m_port;
    std::string m_broadcastAddress;
    struct sockaddr_in m_broadcast{};  // resolved in open()
    int m_fd = -1;
};

}  // namespace lumina::agent
