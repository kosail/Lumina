// ---------------------------------------------------------------------------
// On-disk phrase cache (ITtsEngine decorator).
//
// Piper synthesis on the A53 takes seconds, which blows the < 600 ms alert budget
// (INV-051). Our narration vocabulary is small and fixed, so we pre-render those
// phrases once and replay them instantly. PCM is stored on disk (default under
// /tmp) rather than kept in RAM, because memory is tight (INV-052): the cache is
// just an index-free set of files, one per phrase, validated by a small header.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "audio/tts_engine.hpp"

namespace lumina::audio {

// Configuration for the on-disk phrase cache.
struct PhraseCacheConfig {
    // Directory for cached PCM. Kept on disk, not in RAM (INV-052). main() sets a
    // persistent default ($HOME/.cache/lumina/phrase-cache); this relative value
    // is only a fallback. Override with LUMINA_PHRASE_CACHE_DIR.
    std::string directory = "phrase-cache";
    // Samples streamed per file read. Kept small (~47 ms at 22050 Hz) so a
    // preempting alert can cut a cached utterance quickly (INV-032/INV-051).
    std::size_t chunkFrames = 1024;
    // Identifies the voice/engine the cache was rendered with (e.g. the voice
    // path). Mixed into the file key so a cache produced by a different voice is
    // never replayed after the voice changes.
    std::string tag;
};

// ITtsEngine decorator that plays pre-rendered phrases from disk with near-zero
// latency. Text that is not cached falls through to the wrapped engine, so the
// behaviour is always correct (just slower).
//
// Threading: used by the single speech worker. warm() must be called once at
// startup, before the pipeline starts, so there is no concurrent access.
class CachingTts final : public ITtsEngine {
public:
    explicit CachingTts(ITtsEngine& innerEngine, PhraseCacheConfig config = {});
    ~CachingTts() override;

    CachingTts(const CachingTts&) = delete;
    CachingTts& operator=(const CachingTts&) = delete;

    // Pre-render `phrases` into the cache directory. Phrases already present with
    // a valid header and matching text are skipped, so restarts are cheap.
    // Returns the number of phrases newly rendered. Never throws.
    std::size_t warm(const std::vector<std::string>& phrases);

    [[nodiscard]] core::Status synthesize(std::string_view text,
                                          IAudioSink& sink,
                                          const std::atomic<bool>& stop) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace lumina::audio
