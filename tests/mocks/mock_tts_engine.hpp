// ---------------------------------------------------------------------------
// Host test double for ITtsEngine (AGENTS §9). Emits a fixed number of small
// chunks so tests can drive the sink without libpiper.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "audio/tts_engine.hpp"

namespace lumina::tests {

class MockTtsEngine final : public audio::ITtsEngine {
public:
    MockTtsEngine() = default;

    [[nodiscard]] core::Status synthesize(std::string_view text,
                                          audio::IAudioSink& sink,
                                          const std::atomic<bool>& stop) override
    {
        m_lastText = std::string(text);
        m_calls += 1;
        if (m_fail) {
            return core::failure("mock tts failure");
        }
        static constexpr float kChunk[4] = {0.0F, 0.1F, 0.0F, -0.1F};
        for (int i = 0; i < m_chunks; ++i) {
            if (stop.load(std::memory_order_relaxed)) {
                break;
            }
            if (!sink.write(kChunk, 4, kSampleRate)) {
                return core::failure("mock sink write failed");
            }
        }
        return {};
    }

    // ---- test helpers -------------------------------------------------------
    void setChunks(int chunks) { m_chunks = chunks; }
    void setFail(bool fail) { m_fail = fail; }
    [[nodiscard]] int calls() const noexcept { return m_calls; }
    [[nodiscard]] const std::string& lastText() const noexcept { return m_lastText; }

private:
    static constexpr int kSampleRate = 22050;
    int m_chunks = 1;
    int m_calls = 0;
    bool m_fail = false;
    std::string m_lastText;
};

} // namespace lumina::tests
