#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "capture/camera.hpp"

namespace lumina::tests {

// Deterministic ICamera for host unit tests: it serves preloaded frames in order
// and never touches hardware. It lives under tests/ so production code cannot
// accidentally depend on it (INV-030).
class MockCamera final : public capture::ICamera {
public:
    MockCamera() = default;
    explicit MockCamera(std::vector<core::Frame> frames) : m_frames(std::move(frames)) {}

    // Always succeeds and marks the mock as running.
    [[nodiscard]] bool start() override {
        m_started = true;
        return true;
    }

    // Pops the next preloaded frame. Returns false once the list is exhausted or if
    // the camera was not started, mirroring "no new frame" from a real camera.
    [[nodiscard]] bool getLatest(core::Frame& out) override {
        if (!m_started || m_next >= m_frames.size()) {
            return false;
        }
        out = std::move(m_frames[m_next]);  // move the pixels out; no copy
        ++m_next;
        return true;
    }

    void stop() override { m_started = false; }

    // --- Test helpers -------------------------------------------------------
    void pushFrame(core::Frame frame) { m_frames.push_back(std::move(frame)); }
    [[nodiscard]] bool isStarted() const noexcept { return m_started; }
    [[nodiscard]] std::size_t remaining() const noexcept { return m_frames.size() - m_next; }

private:
    std::vector<core::Frame> m_frames;  // frames to serve, in order
    std::size_t m_next = 0;             // index of the next frame to serve
    bool m_started = false;
};

}  // namespace lumina::tests
