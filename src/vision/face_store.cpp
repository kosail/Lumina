// ---------------------------------------------------------------------------
// FaceStore implementation. See face_store.hpp.
// ---------------------------------------------------------------------------

#include "vision/face_store.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "core/logging.hpp"

namespace lumina::vision {

namespace {

// File magic and version. The 8-byte magic makes a truncated/foreign file easy to
// reject; bump the version whenever the layout changes.
constexpr char kMagic[8] = {'L', 'U', 'M', 'F', 'A', 'C', 'E', '1'};
constexpr std::uint32_t kVersion = 1;

// Sanity caps applied when reading a file. A corrupt or hostile file must be
// rejected, not allowed to trigger a multi-gigabyte allocation (OOM) on the Pi.
// Real data is tiny: SFace embeddings are 128 floats, with a handful of people.
constexpr std::uint32_t kMaxDimension = 4096;          // floats per embedding
constexpr std::uint32_t kMaxPersons = 1000;
constexpr std::uint32_t kMaxEmbeddingsPerPerson = 100;
constexpr std::uintmax_t kMaxTotalBytes = 64ULL * 1024ULL * 1024ULL;  // whole store

// The Pi (aarch64) and the host (x86_64) are both little-endian, so we can write
// the raw bytes of fixed-width integers. A big-endian port would need byte
// swapping here; the format is otherwise explicit.
void writeU32(std::ostream& out, std::uint32_t value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

// Reads a u32. Returns false on a short/failed read so callers can bail cleanly.
bool readU32(std::istream& in, std::uint32_t& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(in);
}

void writeString(std::ostream& out, const std::string& text)
{
    writeU32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool readString(std::istream& in, std::string& text)
{
    std::uint32_t length = 0;
    if (!readU32(in, length)) {
        return false;
    }
    // Guard against a corrupt length causing a huge allocation.
    constexpr std::uint32_t kMaxNameBytes = 1024;
    if (length > kMaxNameBytes) {
        return false;
    }
    text.resize(length);
    in.read(text.data(), static_cast<std::streamsize>(length));
    return static_cast<bool>(in);
}

}  // namespace

void FaceStore::addEmbedding(const std::string& name, Embedding embedding,
                             std::size_t maxPerPerson)
{
    if (embedding.empty()) {
        return;  // a zero-length vector can never match; ignore it
    }
    // All embeddings in one store must share a single dimension: the file format
    // stores that dimension once, so a mismatched vector would corrupt the file.
    const std::size_t establishedDim = dimension();
    if (establishedDim != 0 && embedding.size() != establishedDim) {
        LUMINA_LOG_WARN("FaceStore: ignoring a {}-value embedding (store uses {})",
                        embedding.size(), establishedDim);
        return;
    }
    if (maxPerPerson == 0) {
        maxPerPerson = 1;  // always keep at least one
    }

    auto person = std::find_if(m_people.begin(), m_people.end(),
                               [&name](const Person& entry) { return entry.name == name; });
    if (person == m_people.end()) {
        m_people.push_back(Person{name, {}});
        person = std::prev(m_people.end());
    }

    person->embeddings.push_back(std::move(embedding));
    // Drop the oldest vectors once the cap is exceeded (FIFO refresh).
    while (person->embeddings.size() > maxPerPerson) {
        person->embeddings.erase(person->embeddings.begin());
    }
}

std::optional<FaceMatch> FaceStore::matchBest(std::span<const float> query, float threshold,
                                              float margin) const
{
    if (query.empty() || m_people.empty()) {
        return std::nullopt;
    }

    // Score each person by their best-matching stored embedding.
    struct Scored {
        const Person* person = nullptr;
        float score = -1.0F;
    };
    std::vector<Scored> scored;
    scored.reserve(m_people.size());
    for (const Person& person : m_people) {
        float best = -1.0F;
        for (const Embedding& embedding : person.embeddings) {
            best = std::max(best, cosineSimilarity(query, embedding));
        }
        if (best >= 0.0F) {  // skip people whose vectors had the wrong dimension
            scored.push_back(Scored{&person, best});
        }
    }
    if (scored.empty()) {
        return std::nullopt;
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& lhs, const Scored& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;  // higher similarity first
        }
        return lhs.person->name < rhs.person->name;  // stable tie-break for tests
    });

    const Scored& top = scored.front();
    if (top.score < threshold) {
        return std::nullopt;  // too weak to trust
    }
    // With two or more people, require a clear winner so a near-tie is not spoken.
    if (scored.size() >= 2 && (top.score - scored[1].score) < margin) {
        return std::nullopt;
    }

    FaceMatch match;
    match.name = top.person->name;
    match.similarity = top.score;
    return match;
}

std::vector<std::string> FaceStore::names() const
{
    std::vector<std::string> result;
    result.reserve(m_people.size());
    for (const Person& person : m_people) {
        result.push_back(person.name);
    }
    return result;
}

std::size_t FaceStore::embeddingCount() const noexcept
{
    std::size_t total = 0;
    for (const Person& person : m_people) {
        total += person.embeddings.size();
    }
    return total;
}

std::size_t FaceStore::dimension() const noexcept
{
    for (const Person& person : m_people) {
        if (!person.embeddings.empty()) {
            return person.embeddings.front().size();
        }
    }
    return 0;
}

bool FaceStore::load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LUMINA_LOG_WARN("FaceStore: cannot open '{}'", path);
        return false;
    }

    char magic[8] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::string(magic, sizeof(magic)) != std::string(kMagic, sizeof(kMagic))) {
        LUMINA_LOG_WARN("FaceStore: '{}' is not a Lúmina face store", path);
        return false;
    }

    std::uint32_t version = 0;
    std::uint32_t dimension = 0;
    if (!readU32(in, version) || version != kVersion) {
        LUMINA_LOG_WARN("FaceStore: '{}' has unsupported version", path);
        return false;
    }
    if (!readU32(in, dimension) || dimension > kMaxDimension) {
        LUMINA_LOG_WARN("FaceStore: '{}' has an invalid dimension", path);
        return false;
    }

    std::string modelId;
    if (!readString(in, modelId)) {
        LUMINA_LOG_WARN("FaceStore: '{}' has a corrupt model id", path);
        return false;
    }

    std::uint32_t personCount = 0;
    if (!readU32(in, personCount) || personCount > kMaxPersons) {
        LUMINA_LOG_WARN("FaceStore: '{}' has an invalid person count", path);
        return false;
    }
    // An empty store legitimately has dimension 0 (nobody enrolled yet); a store
    // that claims people must also declare a real embedding dimension.
    if (personCount > 0 && dimension == 0) {
        LUMINA_LOG_WARN("FaceStore: '{}' has people but no embedding dimension", path);
        return false;
    }

    // Build into locals, then swap in on success so a bad file leaves us untouched.
    std::vector<Person> people;
    people.reserve(personCount);
    std::uintmax_t totalBytes = 0;
    for (std::uint32_t i = 0; i < personCount; ++i) {
        Person person;
        if (!readString(in, person.name)) {
            LUMINA_LOG_WARN("FaceStore: '{}' has a corrupt name", path);
            return false;
        }
        std::uint32_t embeddingCount = 0;
        if (!readU32(in, embeddingCount) || embeddingCount > kMaxEmbeddingsPerPerson) {
            LUMINA_LOG_WARN("FaceStore: '{}' has an invalid embedding count", path);
            return false;
        }
        // Bound the cumulative size too, so many small records cannot exhaust RAM.
        totalBytes += static_cast<std::uintmax_t>(embeddingCount) * dimension * sizeof(float);
        if (totalBytes > kMaxTotalBytes) {
            LUMINA_LOG_WARN("FaceStore: '{}' exceeds the size limit", path);
            return false;
        }
        person.embeddings.reserve(embeddingCount);
        for (std::uint32_t e = 0; e < embeddingCount; ++e) {
            Embedding embedding(dimension);
            in.read(reinterpret_cast<char*>(embedding.data()),
                    static_cast<std::streamsize>(dimension * sizeof(float)));
            if (!in) {
                LUMINA_LOG_WARN("FaceStore: '{}' is truncated", path);
                return false;
            }
            person.embeddings.push_back(std::move(embedding));
        }
        people.push_back(std::move(person));
    }

    m_people = std::move(people);
    m_modelId = std::move(modelId);
    LUMINA_LOG_INFO("FaceStore: loaded {} person(s), {} embedding(s) from '{}'", m_people.size(),
                    embeddingCount(), path);
    return true;
}

bool FaceStore::save(const std::string& path) const
{
    const std::string tmpPath = path + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            LUMINA_LOG_ERROR("FaceStore: cannot write '{}'", tmpPath);
            return false;
        }

        out.write(kMagic, sizeof(kMagic));
        writeU32(out, kVersion);
        writeU32(out, static_cast<std::uint32_t>(dimension()));
        writeString(out, m_modelId);
        writeU32(out, static_cast<std::uint32_t>(m_people.size()));
        for (const Person& person : m_people) {
            writeString(out, person.name);
            writeU32(out, static_cast<std::uint32_t>(person.embeddings.size()));
            for (const Embedding& embedding : person.embeddings) {
                out.write(reinterpret_cast<const char*>(embedding.data()),
                          static_cast<std::streamsize>(embedding.size() * sizeof(float)));
            }
        }
        out.flush();
        if (!out) {
            LUMINA_LOG_ERROR("FaceStore: write failed for '{}'", tmpPath);
            std::remove(tmpPath.c_str());
            return false;
        }
    }

    // Atomic replace: a reader either sees the old file or the new one, never a
    // half-written mixture.
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        LUMINA_LOG_ERROR("FaceStore: cannot replace '{}'", path);
        std::remove(tmpPath.c_str());
        return false;
    }
    LUMINA_LOG_INFO("FaceStore: saved {} person(s) to '{}'", m_people.size(), path);
    return true;
}

void FaceStore::clear() noexcept
{
    m_people.clear();
    m_modelId.clear();
}

float FaceStore::cosineSimilarity(std::span<const float> a, std::span<const float> b) noexcept
{
    if (a.empty() || a.size() != b.size()) {
        return 0.0F;
    }
    // Accumulate in double: float accumulation over 128 terms loses precision and
    // could flip the threshold comparison.
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        normA += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        normB += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    if (normA <= 0.0 || normB <= 0.0) {
        return 0.0F;
    }
    return static_cast<float>(dot / (std::sqrt(normA) * std::sqrt(normB)));
}

}  // namespace lumina::vision
