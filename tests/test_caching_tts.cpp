// ---------------------------------------------------------------------------
// Unit tests for CachingTts (on-disk phrase cache).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "audio/caching_tts.hpp"

#include "mocks/mock_audio_sink.hpp"
#include "mocks/mock_tts_engine.hpp"

using lumina::audio::CachingTts;
using lumina::audio::PhraseCacheConfig;
using lumina::tests::MockAudioSink;
using lumina::tests::MockTtsEngine;

namespace {

std::filesystem::path makeTempDir()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("lumina-cache-test-" + std::to_string(stamp));
}

} // namespace

TEST_CASE("CachingTts: warm renders phrases and later hits the cache")
{
    const std::filesystem::path dir = makeTempDir();
    std::filesystem::remove_all(dir);

    MockTtsEngine inner;
    CachingTts tts(inner, PhraseCacheConfig{dir.string(), 4096, "test"});

    CHECK(tts.warm({"hola", "adios"}) == 2);
    CHECK(inner.calls() == 2); // one synthesis per phrase during warm

    MockAudioSink sink;
    REQUIRE(sink.open());
    std::atomic<bool> stop{false};

    const auto status = tts.synthesize("hola", sink, stop);
    CHECK(status.has_value());
    CHECK(inner.calls() == 2);        // served from cache: no new synthesis
    CHECK(sink.totalSamples() == 4);  // MockTtsEngine emits one 4-sample chunk

    std::filesystem::remove_all(dir);
}

TEST_CASE("CachingTts: a miss falls through to the wrapped engine")
{
    const std::filesystem::path dir = makeTempDir();
    std::filesystem::remove_all(dir);

    MockTtsEngine inner;
    CachingTts tts(inner, PhraseCacheConfig{dir.string(), 4096, "test"});

    MockAudioSink sink;
    REQUIRE(sink.open());
    std::atomic<bool> stop{false};

    const auto status = tts.synthesize("nuevo", sink, stop);
    CHECK(status.has_value());
    CHECK(inner.calls() == 1);       // delegated
    CHECK(sink.totalSamples() == 4);

    std::filesystem::remove_all(dir);
}

TEST_CASE("CachingTts: warming twice does not re-render existing phrases")
{
    const std::filesystem::path dir = makeTempDir();
    std::filesystem::remove_all(dir);

    MockTtsEngine inner;
    CachingTts tts(inner, PhraseCacheConfig{dir.string(), 4096, "test"});

    CHECK(tts.warm({"hola"}) == 1);
    CHECK(inner.calls() == 1);
    CHECK(tts.warm({"hola"}) == 0); // already cached
    CHECK(inner.calls() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("CachingTts: a live miss is stored and then served from cache")
{
    const std::filesystem::path dir = makeTempDir();
    std::filesystem::remove_all(dir);

    MockTtsEngine inner;
    CachingTts tts(inner, PhraseCacheConfig{dir.string(), 4096, "test"});
    std::atomic<bool> stop{false};

    // First call: not cached, so it synthesizes live (and stores the samples).
    MockAudioSink first;
    REQUIRE(first.open());
    CHECK(tts.synthesize("nuevo", first, stop).has_value());
    CHECK(inner.calls() == 1);
    CHECK(first.totalSamples() == 4);

    // Second call: served from the cache, no new synthesis.
    MockAudioSink second;
    REQUIRE(second.open());
    CHECK(tts.synthesize("nuevo", second, stop).has_value());
    CHECK(inner.calls() == 1);
    CHECK(second.totalSamples() == 4);

    std::filesystem::remove_all(dir);
}
