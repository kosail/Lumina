// ---------------------------------------------------------------------------
// CachingTts implementation.
// ---------------------------------------------------------------------------

#include "audio/caching_tts.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/logging.hpp"

namespace lumina::audio {

namespace {

constexpr char kMagic[4] = {'L', 'P', 'C', 'M'};
constexpr std::uint32_t kVersion = 1;

// FNV-1a 64-bit: a tiny, stable hash so a phrase maps to a deterministic file
// name without keeping a text->file index in memory.
std::uint64_t fnv1a(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hashToHex(std::uint64_t value)
{
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
}

// Collects samples in memory; used only to render one phrase before writing it
// to disk (bounded to a single utterance, so it is not a RAM concern).
class CapturingSink final : public IAudioSink {
public:
    [[nodiscard]] bool open() override { return true; }
    [[nodiscard]] bool write(const float* samples, std::size_t count, int sampleRate) override
    {
        m_samples.insert(m_samples.end(), samples, samples + count);
        m_sampleRate = sampleRate;
        return true;
    }
    void drain() noexcept override {}
    void stop() noexcept override {}
    void close() noexcept override {}

    [[nodiscard]] const std::vector<float>& samples() const noexcept { return m_samples; }
    [[nodiscard]] int sampleRate() const noexcept { return m_sampleRate; }

private:
    std::vector<float> m_samples;
    int m_sampleRate = 0;
};

// Forwards writes to the real sink while capturing a copy, so a phrase heard for
// the first time is also stored for next time ("lazy" caching). It deliberately
// does NOT forward open()/close(): the pipeline owns the sink's lifecycle.
class TeeSink final : public IAudioSink {
public:
    explicit TeeSink(IAudioSink& target) : m_target(target) {}

    [[nodiscard]] bool open() override { return true; }
    [[nodiscard]] bool write(const float* samples, std::size_t count, int sampleRate) override
    {
        if (!m_target.write(samples, count, sampleRate)) {
            return false;
        }
        m_samples.insert(m_samples.end(), samples, samples + count);
        m_sampleRate = sampleRate;
        return true;
    }
    void drain() noexcept override { m_target.drain(); }
    void stop() noexcept override { m_target.stop(); }
    void close() noexcept override {}

    [[nodiscard]] const std::vector<float>& samples() const noexcept { return m_samples; }
    [[nodiscard]] int sampleRate() const noexcept { return m_sampleRate; }

private:
    IAudioSink& m_target;
    std::vector<float> m_samples;
    int m_sampleRate = 0;
};

// File layout: a fixed little-endian header, then the text, then float32 samples.
// Fields are written individually to avoid struct-padding surprises.
bool writeAll(std::ofstream& out, const std::string& text, const std::vector<float>& samples,
              int sampleRate, const std::string& path)
{
    out.write(kMagic, sizeof(kMagic));
    const std::uint32_t version = kVersion;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    const std::int32_t rate = sampleRate;
    out.write(reinterpret_cast<const char*>(&rate), sizeof(rate));
    const std::uint64_t count = samples.size();
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    const std::uint32_t textLength = static_cast<std::uint32_t>(text.size());
    out.write(reinterpret_cast<const char*>(&textLength), sizeof(textLength));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.write(reinterpret_cast<const char*>(samples.data()),
              static_cast<std::streamsize>(samples.size() * sizeof(float)));
    out.close();
    if (!out) {
        LUMINA_LOG_ERROR("phrase cache: failed writing '{}'", path);
        return false;
    }
    return true;
}

// Reads and validates the header + text. Returns the header fields on success.
struct Header {
    std::int32_t sampleRate = 0;
    std::uint64_t numSamples = 0;
};

std::optional<Header> readHeader(std::ifstream& in, const std::string& expectedText)
{
    char magic[4] = {};
    in.read(magic, sizeof(magic));
    std::uint32_t version = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    Header header;
    in.read(reinterpret_cast<char*>(&header.sampleRate), sizeof(header.sampleRate));
    in.read(reinterpret_cast<char*>(&header.numSamples), sizeof(header.numSamples));
    std::uint32_t textLength = 0;
    in.read(reinterpret_cast<char*>(&textLength), sizeof(textLength));
    if (!in || std::memcmp(magic, kMagic, sizeof(magic)) != 0 || version != kVersion) {
        return std::nullopt;
    }
    std::string text(textLength, '\0');
    in.read(text.data(), static_cast<std::streamsize>(textLength));
    if (!in || text != expectedText || header.numSamples == 0) {
        return std::nullopt;
    }
    return header;
}

} // namespace

class CachingTts::Impl {
public:
    Impl(ITtsEngine& innerEngine, PhraseCacheConfig config)
        : m_inner(innerEngine), m_config(std::move(config))
    {
    }

    [[nodiscard]] std::string pathFor(const std::string& text) const
    {
        // Include the tag (voice identity) so switching voices never replays the
        // previous voice's cached PCM.
        const std::string key = m_config.tag + '\x1f' + text;
        return m_config.directory + "/" + hashToHex(fnv1a(key)) + ".pcm";
    }

    [[nodiscard]] bool hasValid(const std::string& path, const std::string& text) const
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        return readHeader(in, text).has_value();
    }

    // Make sure the cache directory exists. Needed on the lazy path too, where
    // store() runs without a prior warm().
    [[nodiscard]] bool ensureDirectory() const
    {
        std::error_code error;
        std::filesystem::create_directories(m_config.directory, error);
        if (error) {
            LUMINA_LOG_ERROR("phrase cache: cannot create '{}': {}",
                             m_config.directory,
                             error.message());
            return false;
        }
        return true;
    }

    // Writes one phrase to disk atomically (temp file + rename).
    bool store(const std::string& path, const std::string& text,
               const std::vector<float>& samples, int sampleRate) const
    {
        if (!ensureDirectory()) {
            return false;
        }
        const std::string tmp = path + ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            LUMINA_LOG_ERROR("phrase cache: cannot write '{}'", tmp);
            return false;
        }
        if (!writeAll(out, text, samples, sampleRate, tmp)) {
            std::error_code removeError;
            std::filesystem::remove(tmp, removeError);
            return false;
        }
        std::error_code renameError;
        std::filesystem::rename(tmp, path, renameError);
        if (renameError) {
            LUMINA_LOG_ERROR("phrase cache: cannot rename '{}': {}", tmp, renameError.message());
            std::filesystem::remove(tmp, renameError);
            return false;
        }
        return true;
    }

    // Returns std::nullopt on a cache miss (caller should synthesize live);
    // otherwise the result of streaming the cached audio to `sink`.
    std::optional<core::Status> playCached(const std::string& path, const std::string& text,
                                           IAudioSink& sink, const std::atomic<bool>& stop)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt; // not cached
        }
        const std::optional<Header> header = readHeader(in, text);
        if (!header.has_value()) {
            return std::nullopt; // stale/corrupt entry: fall back to live synthesis
        }

        std::vector<float> buffer(m_config.chunkFrames);
        std::uint64_t remaining = header->numSamples;
        while (remaining > 0) {
            if (stop.load(std::memory_order_relaxed)) {
                return core::Status{}; // interrupted, treat as success
            }
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
            in.read(reinterpret_cast<char*>(buffer.data()),
                    static_cast<std::streamsize>(want * sizeof(float)));
            if (!in) {
                return core::failure("phrase cache: read failed");
            }
            if (!sink.write(buffer.data(), want, header->sampleRate)) {
                return core::failure("audio sink write failed");
            }
            remaining -= want;
        }
        return core::Status{};
    }

    ITtsEngine& m_inner;
    PhraseCacheConfig m_config;
};

CachingTts::CachingTts(ITtsEngine& innerEngine, PhraseCacheConfig config)
    : m_impl(std::make_unique<Impl>(innerEngine, std::move(config)))
{
}

CachingTts::~CachingTts() = default;

std::size_t CachingTts::warm(const std::vector<std::string>& phrases)
{
    if (!m_impl->ensureDirectory()) {
        return 0;
    }

    std::size_t rendered = 0;
    for (const std::string& phrase : phrases) {
        if (phrase.empty()) {
            continue;
        }
        const std::string path = m_impl->pathFor(phrase);
        if (m_impl->hasValid(path, phrase)) {
            continue; // already cached
        }

        CapturingSink capture;
        static const std::atomic<bool> kNeverStop{false};
        const core::Status status = m_impl->m_inner.synthesize(phrase, capture, kNeverStop);
        if (!status) {
            LUMINA_LOG_WARN("phrase cache: synthesis failed for '{}': {}", phrase, status.error());
            continue;
        }
        if (capture.samples().empty()) {
            LUMINA_LOG_WARN("phrase cache: no audio for '{}'", phrase);
            continue;
        }
        if (!m_impl->store(path, phrase, capture.samples(), capture.sampleRate())) {
            continue;
        }
        ++rendered;
        LUMINA_LOG_INFO("phrase cache: rendered '{}' ({} samples)",
                        phrase,
                        capture.samples().size());
    }
    return rendered;
}

core::Status CachingTts::synthesize(std::string_view text,
                                    IAudioSink& sink,
                                    const std::atomic<bool>& stop)
{
    const std::string key(text);
    if (auto cached = m_impl->playCached(m_impl->pathFor(key), key, sink, stop)) {
        return *cached;
    }

    // Miss: synthesize live, tee the audio to the sink while capturing it, then
    // store it so the next occurrence is a fast cache hit.
    TeeSink tee(sink);
    const core::Status status = m_impl->m_inner.synthesize(text, tee, stop);
    if (status && !stop.load(std::memory_order_relaxed) && !tee.samples().empty()) {
        (void)m_impl->store(m_impl->pathFor(key), key, tee.samples(), tee.sampleRate());
    }
    return status;
}

} // namespace lumina::audio
