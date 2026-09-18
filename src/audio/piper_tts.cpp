// ---------------------------------------------------------------------------
// PiperTts implementation. The only translation unit that includes <piper.h>.
// ---------------------------------------------------------------------------

#include "audio/piper_tts.hpp"

#include <piper.h> // libpiper C API (third_party/libpiper/include)

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "core/logging.hpp"
#include "core/time.hpp"

namespace lumina::audio {

namespace {

// Playback write granularity. libpiper can return a whole utterance as one chunk
// and exposes no cancellation, so the only place a live synthesis can be aborted
// is during its playback write; we poll `stop` between these small sub-blocks
// (~46 ms of audio at 22050 Hz) instead of blocking on one multi-second write.
constexpr std::size_t kWriteFrames = 1024;

} // namespace

// Out-of-class definition of the private nested type declared as `class Impl;`
// in the header (pimpl idiom). It owns the piper_synthesizer* handle and config.
class PiperTts::Impl {
public:
    explicit Impl(PiperConfig config) : m_config(std::move(config)) {}

    PiperConfig m_config;
    piper_synthesizer* m_synth = nullptr; // non-null once load() succeeds
};

PiperTts::PiperTts(PiperConfig config)
    : m_impl(std::make_unique<Impl>(std::move(config)))
{
}

PiperTts::~PiperTts()
{
    // RAII: release the native engine when the wrapper dies, even on early exit.
    if (m_impl != nullptr && m_impl->m_synth != nullptr) {
        piper_free(m_impl->m_synth);
        m_impl->m_synth = nullptr;
    }
}

bool PiperTts::load()
{
    if (m_impl->m_synth != nullptr) {
        return true; // already loaded
    }
    if (m_impl->m_config.modelPath.empty()) {
        LUMINA_LOG_ERROR("Piper: modelPath is empty");
        return false;
    }

    // piper_create() treats NULL configPath as "<modelPath>.json"; NULL
    // espeakDataDir is only valid for raw-phoneme models, which ours are not.
    const char* configPath =
        m_impl->m_config.configPath.empty() ? nullptr : m_impl->m_config.configPath.c_str();
    const char* espeakData =
        m_impl->m_config.espeakDataDir.empty() ? nullptr : m_impl->m_config.espeakDataDir.c_str();

    m_impl->m_synth = piper_create(m_impl->m_config.modelPath.c_str(), configPath, espeakData);
    if (m_impl->m_synth == nullptr) {
        LUMINA_LOG_ERROR("Piper: piper_create failed for '{}'", m_impl->m_config.modelPath);
        return false;
    }

    LUMINA_LOG_INFO("Piper ready: model='{}' (libpiper {})",
                    m_impl->m_config.modelPath,
                    piper_version());
    return true;
}

core::Status PiperTts::synthesize(std::string_view text,
                                  IAudioSink& sink,
                                  const std::atomic<bool>& stop)
{
    if (m_impl->m_synth == nullptr) {
        return core::failure("Piper engine not loaded");
    }
    if (text.empty()) {
        return {}; // nothing to say
    }

    // piper_synthesize_start wants a NUL-terminated C string; make a stable copy
    // because string_view is not guaranteed to be terminated.
    const std::string textCopy(text);
    if (piper_synthesize_start(m_impl->m_synth, textCopy.c_str(), nullptr) != PIPER_OK) {
        return core::failure("piper_synthesize_start failed");
    }

    // Stream chunk by chunk. Checking `stop` between chunks is what lets a safety
    // alert cut in mid-utterance (INV-032).
    //
    // IMPORTANT (libpiper gotcha): piper_synthesize_next() returns the final chunk
    // TOGETHER WITH PIPER_DONE (piper.cpp: `return chunk->is_last ? PIPER_DONE :
    // PIPER_OK`). So we must process `chunk` BEFORE acting on the status; checking
    // DONE first would discard the last chunk — which is the whole utterance for a
    // short one-clause phrase like "una persona enfrente." (the library's own
    // README example has the same pitfall).
    const core::TimePoint startedAt = core::now();
    std::size_t totalSamples = 0;
    bool firstAudioLogged = false;
    piper_audio_chunk chunk{};
    for (;;) {
        if (stop.load(std::memory_order_relaxed)) {
            LUMINA_LOG_DEBUG("Piper: speech interrupted after {} samples", totalSamples);
            return {};
        }
        const int rc = piper_synthesize_next(m_impl->m_synth, &chunk);
        if (rc != PIPER_OK && rc != PIPER_DONE) {
            return core::failure("piper_synthesize_next failed");
        }

        LUMINA_LOG_DEBUG("Piper chunk: {} samples, {} phoneme ids, last={}",
                         chunk.num_samples,
                         chunk.num_phoneme_ids,
                         chunk.is_last);

        if (chunk.num_samples > 0) {
            // Write the chunk in small sub-blocks, polling `stop` between them.
            std::size_t offset = 0;
            while (offset < chunk.num_samples) {
                if (stop.load(std::memory_order_relaxed)) {
                    LUMINA_LOG_DEBUG("Piper: speech interrupted after {} samples", totalSamples);
                    return {};
                }
                const std::size_t remaining = chunk.num_samples - offset;
                const std::size_t writeCount = std::min(kWriteFrames, remaining);
                if (!sink.write(chunk.samples + offset, writeCount, chunk.sample_rate)) {
                    return core::failure("audio sink write failed");
                }
                if (!firstAudioLogged) {
                    // Time until the first samples are handed to the device — this is
                    // the synthesis contribution to INV-051 (event->audible).
                    LUMINA_LOG_INFO("Piper: first audio after {:.0f} ms", core::msSince(startedAt));
                    firstAudioLogged = true;
                }
                offset += writeCount;
            }
            totalSamples += chunk.num_samples;
        }

        if (rc == PIPER_DONE) {
            break; // utterance finished (the chunk above was already written)
        }
    }
    LUMINA_LOG_INFO("Piper: wrote {} samples in {:.0f} ms", totalSamples, core::msSince(startedAt));
    return {};
}

} // namespace lumina::audio
