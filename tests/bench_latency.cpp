// ---------------------------------------------------------------------------
// On-device latency benchmark for the cached-alert path (NFR-02, INV-051).
//
// Measures event -> first audio written to the sink for a cached alert phrase,
// exercising the real CachingTts (Piper cache hit) and, when available, the real
// AlsaSink (bluealsa). It does NOT include inference (the detector is not used)
// nor the Bluetooth radio hop, which adds roughly 150-300 ms to audibility.
//
// Run on the Pi (needs LUMINA_ENABLE_AUDIO=ON and LUMINA_BUILD_BENCH=ON), in the
// working directory that has third_party/ and models/:
//   LD_LIBRARY_PATH=$PWD/third_party/libpiper/lib ./build/aarch64/tests/lumina_bench_latency
//   args: <voicePath> <espeak-ng-data-dir> [iterations]
//
// If the ALSA device cannot be opened (no earbuds), it falls back to a discard
// sink so the synthesis latency is still reported.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

#include "alerts/alert.hpp"
#include "alerts/arbiter.hpp"
#include "app/describer.hpp"
#include "audio/audio_sink.hpp"
#include "audio/bluealsa_sink.hpp"
#include "audio/caching_tts.hpp"
#include "audio/piper_tts.hpp"
#include "core/time.hpp"
#include "i18n/es.hpp"

namespace {

// Sink that records the time of the first write after arm(), then sets `stop` so
// the (cached) utterance aborts after the first chunk. This isolates the latency
// we care about instead of waiting for the whole sentence to play.
class TimingSink final : public lumina::audio::IAudioSink {
public:
    TimingSink(lumina::audio::IAudioSink& inner, std::atomic<bool>& stop)
        : m_inner(inner), m_stop(stop)
    {
    }

    [[nodiscard]] bool open() override { return m_inner.open(); }

    [[nodiscard]] bool write(const float* samples, std::size_t count, int sampleRate) override
    {
        if (!m_firstWrite) {
            m_firstWrite = true;
            m_firstMs = lumina::core::msSince(m_start);
            m_stop.store(true, std::memory_order_relaxed); // stop after this chunk
        }
        return m_inner.write(samples, count, sampleRate);
    }

    void stop() noexcept override { m_inner.stop(); }
    void drain() noexcept override { m_inner.drain(); }
    void close() noexcept override { m_inner.close(); }

    // Reset for a new measurement, anchored at the event time `start`.
    void arm(lumina::core::TimePoint start) noexcept
    {
        m_start = start;
        m_firstWrite = false;
        m_firstMs = -1.0;
    }
    [[nodiscard]] double firstMs() const noexcept { return m_firstMs; }

private:
    lumina::audio::IAudioSink& m_inner;
    std::atomic<bool>& m_stop;
    lumina::core::TimePoint m_start{};
    bool m_firstWrite = false;
    double m_firstMs = -1.0;
};

// Minimal sink used when no ALSA device is available.
class DiscardSink final : public lumina::audio::IAudioSink {
public:
    [[nodiscard]] bool open() override { return true; }
    [[nodiscard]] bool write(const float* /*samples*/, std::size_t /*count*/,
                             int /*sampleRate*/) override
    {
        return true;
    }
    void stop() noexcept override {}
    void drain() noexcept override {}
    void close() noexcept override {}
};

[[nodiscard]] double percentile(std::vector<double> sorted, double fraction) noexcept
{
    if (sorted.empty()) {
        return -1.0;
    }
    std::sort(sorted.begin(), sorted.end());
    const std::size_t index =
        static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

} // namespace

int main(int argc, char** argv)
{
    const std::string voicePath =
        argc > 1 ? argv[1] : "models/voices/es_MX-claude-high.onnx";
    const std::string espeakDataDir =
        argc > 2 ? argv[2] : "third_party/libpiper/share/espeak-ng-data";
    const int iterations = argc > 3 ? std::max(1, std::atoi(argv[3])) : 10;

    lumina::audio::PiperConfig piperConfig;
    piperConfig.modelPath = voicePath;
    piperConfig.espeakDataDir = espeakDataDir;
    lumina::audio::PiperTts piper(piperConfig);
    if (!piper.load()) {
        std::fprintf(stderr, "bench_latency: failed to load Piper voice '%s'\n", voicePath.c_str());
        return 1;
    }

    lumina::audio::PhraseCacheConfig cacheConfig;
    cacheConfig.tag = voicePath;
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        cacheConfig.directory = std::string(home) + "/.cache/lumina/phrase-cache";
    }
    if (const char* dir = std::getenv("LUMINA_PHRASE_CACHE_DIR")) {
        cacheConfig.directory = dir;
    }
    lumina::audio::CachingTts tts(piper, cacheConfig);
    const std::size_t rendered = tts.warm(lumina::app::alertPhraseCatalog());
    std::printf("phrase cache: %zu new phrase(s) rendered into '%s'\n",
                rendered,
                cacheConfig.directory.c_str());

    lumina::audio::AlsaSink alsa(lumina::audio::AlsaConfig{});
    DiscardSink discard;
    lumina::audio::IAudioSink* inner = &discard;
    if (alsa.open()) {
        inner = &alsa;
        std::printf("sink: bluealsa\n");
    } else {
        if (!discard.open()) {
            std::fprintf(stderr, "bench_latency: discard sink failed to open\n");
            return 1;
        }
        std::printf("sink: discard (bluealsa unavailable; Bluetooth hop not measured)\n");
    }

    std::atomic<bool> stop{false};
    TimingSink sink(*inner, stop);
    lumina::alerts::AlertArbiter arbiter;

    const std::string phrase = lumina::i18n::proximityAlertPhrase(true);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(iterations));

    for (int i = 0; i < iterations; ++i) {
        stop.store(false, std::memory_order_relaxed);
        const lumina::core::TimePoint t0 = lumina::core::now();
        sink.arm(t0);

        lumina::alerts::Alert alert;
        alert.priority = lumina::alerts::Priority::Warning; // bypasses the global gap
        alert.source = lumina::alerts::Source::Obstacle;
        alert.text = phrase;
        alert.dedupKey = "bench-latency-" + std::to_string(i); // avoid dedup/cooldown
        alert.detectedAt = t0;

        if (!arbiter.submit(alert, t0)) {
            std::fprintf(stderr, "bench_latency: submit rejected at iteration %d\n", i);
            continue;
        }
        lumina::alerts::Alert popped;
        if (!arbiter.waitPop(popped)) {
            std::fprintf(stderr, "bench_latency: waitPop failed at iteration %d\n", i);
            continue;
        }

        const lumina::core::Status status = tts.synthesize(popped.text, sink, stop);
        if (!status) {
            std::fprintf(stderr, "bench_latency: synthesize failed: %s\n", status.error().c_str());
        }
        const double ms = sink.firstMs();
        if (ms >= 0.0) {
            samples.push_back(ms);
            std::printf("  #%2d  %7.2f ms  (event -> first audio)\n", i, ms);
        }
        arbiter.finishSpeaking(lumina::core::now());
        inner->stop(); // drop the rest so the next iteration is not blocked
    }

    if (samples.empty()) {
        std::fprintf(stderr, "bench_latency: no measurements collected\n");
        return 1;
    }

    const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    const double average = sum / static_cast<double>(samples.size());
    std::printf("\niterations: %zu\n", samples.size());
    std::printf("min: %.2f ms\n", percentile(samples, 0.0));
    std::printf("avg: %.2f ms\n", average);
    std::printf("p95: %.2f ms\n", percentile(samples, 0.95));
    std::printf("max: %.2f ms\n", percentile(samples, 1.0));
    std::printf("(add ~150-300 ms Bluetooth for audible latency; INV-051 target < 600 ms)\n");
    return 0;
}
