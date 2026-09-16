// ---------------------------------------------------------------------------
// ALSA / bluealsa audio sink.
//
// Writes mono float PCM to an ALSA PCM. In the beta the PCM name is "bluealsa",
// which the bluealsa plugin exposes at runtime so audio reaches the single paired
// Bluetooth A2DP earbuds (INV-014, INV-022, RAW_PLAN §9).
// ---------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>

#include "audio/audio_sink.hpp"

namespace lumina::audio {

// ALSA sink configuration.
struct AlsaConfig {
    std::string pcmName = "bluealsa"; // ALSA PCM provided by the bluealsa plugin
    int sampleRate = 22050;           // Piper Spanish voices are 22050 Hz, mono, float
    int channels = 1;
    unsigned int latencyMs = 200;     // target device buffer; keeps first-word delay low
};

// Concrete IAudioSink over libasound. Not copyable. pimpl hides <alsa/asoundlib.h>.
class AlsaSink final : public IAudioSink {
public:
    explicit AlsaSink(AlsaConfig config);
    ~AlsaSink() override;

    AlsaSink(const AlsaSink&) = delete;
    AlsaSink& operator=(const AlsaSink&) = delete;

    [[nodiscard]] bool open() override;
    [[nodiscard]] bool write(const float* samples, std::size_t count, int sampleRate) override;
    void drain() noexcept override;
    void stop() noexcept override;
    void close() noexcept override;

private:
    // Make sure the PCM is ready to accept writes. After a drain (or an xrun) the
    // stream sits in SETUP, where writei fails with EBADFD, so we prepare it again.
    [[nodiscard]] bool ensurePrepared() noexcept;

    class Impl;
    std::unique_ptr<Impl> m_impl; // RAII: closes the PCM handle on destruction
};

} // namespace lumina::audio
