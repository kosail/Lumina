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
    [[nodiscard]] bool isOpen() const noexcept { return m_open; }
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
};

} // namespace lumina::tests
