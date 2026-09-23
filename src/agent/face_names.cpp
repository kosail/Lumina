// ---------------------------------------------------------------------------
// Enrolled-person names reader implementation. See agent/face_names.hpp.
// ---------------------------------------------------------------------------

#include "agent/face_names.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace lumina::agent {

namespace {

// Mirror the constants in src/vision/face_store.cpp. A mismatch here only makes the
// reader reject a file the writer produced; the round-trip unit test catches it.
constexpr char kMagic[8] = {'L', 'U', 'M', 'F', 'A', 'C', 'E', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxDimension = 4096;          // floats per embedding
constexpr std::uint32_t kMaxPersons = 1000;
constexpr std::uint32_t kMaxEmbeddingsPerPerson = 100;
constexpr std::uint32_t kMaxStringBytes = 1024;        // name and modelId
constexpr std::uintmax_t kMaxStoreBytes = 64ULL * 1024ULL * 1024ULL;

// A bounds-checked cursor over the whole file held in memory. Every read reports
// failure instead of throwing, so a truncated or hostile file cannot crash the
// agent (RAII: the buffer is owned by the caller's vector).
class ByteReader {
public:
    explicit ByteReader(const std::vector<char>& data) : m_data(data) {}

    bool readRaw(void* out, std::size_t count)
    {
        if (count > remaining()) {
            return false;
        }
        std::memcpy(out, m_data.data() + m_offset, count);
        m_offset += count;
        return true;
    }

    bool readU32(std::uint32_t& value) { return readRaw(&value, sizeof(value)); }

    bool readString(std::string& text)
    {
        std::uint32_t length = 0;
        if (!readU32(length) || length > kMaxStringBytes || length > remaining()) {
            return false;
        }
        text.assign(m_data.data() + m_offset, length);
        m_offset += length;
        return true;
    }

    bool skip(std::uint64_t count)
    {
        if (count > remaining()) {
            return false;
        }
        m_offset += static_cast<std::size_t>(count);
        return true;
    }

private:
    [[nodiscard]] std::size_t remaining() const { return m_data.size() - m_offset; }

    const std::vector<char>& m_data;
    std::size_t m_offset = 0;
};

// Load the whole store into memory, or return false when it is missing or larger
// than the cap. The store is a few KB of floats in practice.
bool loadStoreBytes(const std::string& path, std::vector<char>& out)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > kMaxStoreBytes) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        input.read(out.data(), static_cast<std::streamsize>(size));  // streamoff -> streamsize
    }
    return static_cast<bool>(input);
}

}  // namespace

std::vector<std::string> readEnrolledNames(const std::string& storePath)
{
    std::vector<char> data;
    if (!loadStoreBytes(storePath, data)) {
        return {};
    }

    ByteReader reader(data);
    char magic[sizeof(kMagic)];
    if (!reader.readRaw(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        return {};
    }

    std::uint32_t version = 0;
    std::uint32_t dimension = 0;
    std::uint32_t personCount = 0;
    std::string modelId;
    if (!reader.readU32(version) || version != kVersion) {
        return {};
    }
    if (!reader.readU32(dimension) || dimension > kMaxDimension) {
        return {};
    }
    if (!reader.readString(modelId)) {
        return {};
    }
    if (!reader.readU32(personCount) || personCount > kMaxPersons) {
        return {};
    }

    std::vector<std::string> names;
    names.reserve(personCount);
    for (std::uint32_t i = 0; i < personCount; ++i) {
        std::string name;
        std::uint32_t embeddingCount = 0;
        if (!reader.readString(name)) {
            return {};
        }
        if (!reader.readU32(embeddingCount) || embeddingCount > kMaxEmbeddingsPerPerson) {
            return {};
        }
        // Skip the embeddings: dimension fits in u32 and the caps bound the product,
        // so the widened 64-bit math cannot overflow.
        const std::uint64_t bytes = static_cast<std::uint64_t>(embeddingCount) * dimension *
                                    static_cast<std::uint64_t>(sizeof(float));
        if (!reader.skip(bytes)) {
            return {};
        }
        names.push_back(std::move(name));
    }
    return names;
}

}  // namespace lumina::agent
