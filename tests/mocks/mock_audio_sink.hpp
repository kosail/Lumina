// ---------------------------------------------------------------------------
// Host test double for IAudioSink (AGENTS §9). Records what was written instead
// of touching a real device, and lets tests force open()/write() failures.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <vector>

#include "audio/audio_sink.hpp"

namespace lumina::tests {

class MockAudioSink final : public audio::IAudioSink {
public:
    MockAudioSink() = default;

    [[nodiscard]] bool open() override
    {
        ++m_openCount;
        // Simulate a device that is not there yet: fail the first N attempts, then
        // succeed. Tests that never call setOpenFailuresBeforeSuccess() keep the
        // historical behavior (open() returns m_openResult immediately).
        if (m_openFailuresRemaining > 0) {
            --m_openFailuresRemaining;
            m_open = false;
            return false;
        }
        m_open = m_openResult;
        return m_open;
    }

    [[nodiscard]] bool write(const float* samples, std::size_t count, int sampleRate) override
    {
        if (!m_open) {
            return false;
        }
        m_writes.emplace_back(samples, samples + count); // copy: sink must not keep the pointer
        m_lastSampleRate = sampleRate;
        return m_writeResult;
    }

    void stop() noexcept override { ++m_stopCount; }

    void drain() noexcept override { ++m_drainCount; }

    void close() noexcept override { m_open = false; }

    // ---- test helpers -------------------------------------------------------
    void setOpenResult(bool value) { m_openResult = value; }
    void setWriteResult(bool value) { m_writeResult = value; }
    // Make the next `count` open() calls fail before honoring m_openResult, so a
    // test can exercise the sink watchdog's retry path deterministically.
    void setOpenFailuresBeforeSuccess(int count) { m_openFailuresRemaining = count; }
    [[nodiscard]] bool isOpen() const noexcept { return m_open; }
    [[nodiscard]] std::size_t openCount() const noexcept { return m_openCount; }
    [[nodiscard]] std::size_t writeCount() const noexcept { return m_writes.size(); }
    [[nodiscard]] std::size_t stopCount() const noexcept { return m_stopCount; }
    [[nodiscard]] std::size_t drainCount() const noexcept { return m_drainCount; }
    [[nodiscard]] int lastSampleRate() const noexcept { return m_lastSampleRate; }

    [[nodiscard]] std::size_t totalSamples() const noexcept
    {
        std::size_t total = 0;
        for (const std::vector<float>& chunk : m_writes) {
            total += chunk.size();
        }
        return total;
    }

private:
    std::vector<std::vector<float>> m_writes;
    int m_lastSampleRate = 0;
    std::size_t m_stopCount = 0;
    std::size_t m_drainCount = 0;
    bool m_open = false;
    bool m_openResult = true;
    bool m_writeResult = true;
    std::size_t m_openCount = 0;
    int m_openFailuresRemaining = 0;
};

} // namespace lumina::tests
