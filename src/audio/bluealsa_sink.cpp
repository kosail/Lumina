// ---------------------------------------------------------------------------
// AlsaSink implementation. The only translation unit that includes
// <alsa/asoundlib.h>.
// ---------------------------------------------------------------------------

#include "audio/bluealsa_sink.hpp"

#include <alsa/asoundlib.h> // libasound; the bluealsa PCM is provided by its plugin

#include <utility>

#include "core/logging.hpp"

namespace lumina::audio {

// Out-of-class definition of the private nested type declared as `class Impl;`
// in the header (pimpl idiom). It owns the ALSA PCM handle and the effective config.
class AlsaSink::Impl {
public:
    explicit Impl(AlsaConfig config) : m_config(std::move(config)) {}

    AlsaConfig m_config;
    snd_pcm_t* m_pcm = nullptr; // non-null while open()
};

AlsaSink::AlsaSink(AlsaConfig config) : m_impl(std::make_unique<Impl>(std::move(config)))
{
}

AlsaSink::~AlsaSink()
{
    close(); // RAII: never leak the device handle
}

bool AlsaSink::open()
{
    if (m_impl->m_pcm != nullptr) {
        return true; // already open
    }
    AlsaConfig& cfg = m_impl->m_config;

    int rc = snd_pcm_open(&m_impl->m_pcm, cfg.pcmName.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        LUMINA_LOG_ERROR("ALSA: snd_pcm_open('{}') failed: {}", cfg.pcmName, snd_strerror(rc));
        m_impl->m_pcm = nullptr;
        return false;
    }

    // Configure hardware parameters: interleaved float32 at cfg.sampleRate.
    // snd_pcm_hw_params_alloca() allocates the params object on the stack.
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_hw_params_alloca(&hw);

    rc = snd_pcm_hw_params_any(m_impl->m_pcm, hw);
    if (rc >= 0) {
        rc = snd_pcm_hw_params_set_access(m_impl->m_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    }
    if (rc >= 0) {
        rc = snd_pcm_hw_params_set_format(m_impl->m_pcm, hw, SND_PCM_FORMAT_FLOAT_LE);
    }
    if (rc >= 0) {
        rc = snd_pcm_hw_params_set_channels(m_impl->m_pcm, hw, static_cast<unsigned>(cfg.channels));
    }

    unsigned int rate = static_cast<unsigned>(cfg.sampleRate);
    if (rc >= 0) {
        rc = snd_pcm_hw_params_set_rate_near(m_impl->m_pcm, hw, &rate, nullptr);
    }
    if (rc >= 0) {
        // Ask for a modest buffer so the first word is not delayed (INV-051).
        snd_pcm_uframes_t buffer =
            static_cast<snd_pcm_uframes_t>(static_cast<unsigned long>(rate) * cfg.latencyMs / 1000U);
        rc = snd_pcm_hw_params_set_buffer_size_near(m_impl->m_pcm, hw, &buffer);
    }
    if (rc >= 0) {
        rc = snd_pcm_hw_params(m_impl->m_pcm, hw);
    }
    if (rc >= 0) {
        rc = snd_pcm_prepare(m_impl->m_pcm);
    }

    if (rc < 0) {
        LUMINA_LOG_ERROR("ALSA: configuring '{}' failed: {}", cfg.pcmName, snd_strerror(rc));
        snd_pcm_close(m_impl->m_pcm);
        m_impl->m_pcm = nullptr;
        return false;
    }

    LUMINA_LOG_INFO("ALSA sink ready: pcm='{}' {} Hz {} ch float32",
                    cfg.pcmName,
                    rate,
                    cfg.channels);
    return true;
}

bool AlsaSink::ensurePrepared() noexcept
{
    if (m_impl->m_pcm == nullptr) {
        return false;
    }
    const snd_pcm_state_t state = snd_pcm_state(m_impl->m_pcm);
    if (state == SND_PCM_STATE_RUNNING || state == SND_PCM_STATE_PREPARED) {
        return true;
    }
    // SETUP (after drain), XRUN, SUSPENDED, ... all need a prepare before writei,
    // otherwise writei fails with EBADFD (not recoverable by snd_pcm_recover).
    const int rc = snd_pcm_prepare(m_impl->m_pcm);
    if (rc < 0) {
        LUMINA_LOG_ERROR("ALSA: prepare failed: {}", snd_strerror(rc));
        return false;
    }
    return true;
}

bool AlsaSink::write(const float* samples, std::size_t count, int sampleRate)
{
    if (m_impl->m_pcm == nullptr) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    // The voice should always match the configured rate. If it does not, rebuild
    // the PCM at the new rate so playback is not pitched.
    if (sampleRate != m_impl->m_config.sampleRate) {
        LUMINA_LOG_WARN("ALSA: sample rate {} != configured {}; reopening",
                        sampleRate,
                        m_impl->m_config.sampleRate);
        m_impl->m_config.sampleRate = sampleRate;
        close();
        if (!open()) {
            return false;
        }
    }

    if (!ensurePrepared()) {
        return false;
    }

    // Mono: one frame == one sample. Loop because ALSA may accept fewer frames
    // than offered, and recover from underruns (EPIPE).
    std::size_t offset = 0;
    while (offset < count) {
        const snd_pcm_sframes_t written = snd_pcm_writei(
            m_impl->m_pcm, samples + offset, static_cast<snd_pcm_uframes_t>(count - offset));
        if (written < 0) {
            const int err = static_cast<int>(written);
            const int rc = snd_pcm_recover(m_impl->m_pcm, err, 1);
            if (rc < 0) {
                // Last resort for EBADFD and similar: re-prepare and retry once.
                if (snd_pcm_prepare(m_impl->m_pcm) < 0) {
                    LUMINA_LOG_ERROR("ALSA: write failed: {}", snd_strerror(err));
                    return false;
                }
            }
            continue; // recovered; retry the remainder
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

void AlsaSink::drain() noexcept
{
    // Wait for the queued samples to reach the device. Without this, the next
    // utterance can discard the tail of the current sentence.
    if (m_impl->m_pcm != nullptr) {
        const int rc = snd_pcm_drain(m_impl->m_pcm);
        if (rc < 0) {
            LUMINA_LOG_ERROR("ALSA: drain failed: {}", snd_strerror(rc));
        }
        // snd_pcm_drain() leaves the stream in SETUP; prepare it so the next
        // utterance's write() does not fail with EBADFD.
        (void)ensurePrepared();
    }
}

void AlsaSink::stop() noexcept
{
    // Drop buffered frames so an alert can replace ongoing speech immediately.
    if (m_impl->m_pcm != nullptr) {
        snd_pcm_drop(m_impl->m_pcm);
        snd_pcm_prepare(m_impl->m_pcm);
    }
}

void AlsaSink::close() noexcept
{
    if (m_impl->m_pcm != nullptr) {
        snd_pcm_close(m_impl->m_pcm);
        m_impl->m_pcm = nullptr;
    }
}

} // namespace lumina::audio
