// ---------------------------------------------------------------------------
// On-device enrollment store (FR-03/FR-04).
//
// Each enrolled person owns up to a few face embeddings captured at enrollment.
// Recognition compares a query embedding against every stored vector and keeps
// the best score. All data stays on the device (FR-03.4).
//
// This class is pure C++ (no OpenCV), so it is unit-tested on the host. The
// on-disk format is a small little-endian binary blob written atomically (temp
// file + rename) so a crash mid-save cannot corrupt an existing enrollment.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vision/face_recognizer.hpp"

namespace lumina::vision {

// One face embedding (an SFace feature vector, 128 floats in the current model).
using Embedding = std::vector<float>;

// Local store of named people and their embeddings.
class FaceStore {
public:
    // Append one embedding for `name`, creating the person if new. Once the person
    // already holds `maxPerPerson` embeddings the oldest is dropped (FIFO), so
    // re-enrollment gradually refreshes the profile without unbounded growth.
    void addEmbedding(const std::string& name, Embedding embedding,
                      std::size_t maxPerPerson = 5);

    // Best cosine match across all people, or nullopt when the store is empty or
    // the result fails the policy:
    //   - the best similarity is below `threshold`, or
    //   - there are at least two people and `best - secondBest < margin`
    //     (ambiguous match between two enrolled people).
    // The margin lets the threshold stay at the documented SFace value (0.363)
    // while still rejecting confusing near-ties. `query` must have the same length
    // as the stored vectors; a mismatch simply never matches.
    [[nodiscard]] std::optional<FaceMatch> matchBest(std::span<const float> query,
                                                     float threshold,
                                                     float margin) const;

    // Names of every enrolled person, in insertion order.
    [[nodiscard]] std::vector<std::string> names() const;

    [[nodiscard]] std::size_t personCount() const noexcept { return m_people.size(); }
    [[nodiscard]] std::size_t embeddingCount() const noexcept;
    [[nodiscard]] std::size_t dimension() const noexcept;  // 0 when empty
    [[nodiscard]] bool empty() const noexcept { return m_people.empty(); }

    // Identifier of the embedder that produced the stored vectors (e.g. the SFace
    // file name/version). Persisted with the data so a model swap cannot silently
    // compare incompatible embeddings.
    void setModelId(std::string modelId) { m_modelId = std::move(modelId); }
    [[nodiscard]] const std::string& modelId() const noexcept { return m_modelId; }

    // Persistence. `load` returns false (after logging) when the file is missing,
    // unreadable, or malformed and leaves the store unchanged in that case.
    [[nodiscard]] bool load(const std::string& path);
    // Writes via a temporary file + rename so the previous file survives failure.
    [[nodiscard]] bool save(const std::string& path) const;

    void clear() noexcept;

    // Cosine similarity of two equal-length vectors, in [-1, 1]. Returns 0 when
    // either vector is empty or the lengths differ. Kept public so tests can pin
    // the math independently of the store.
    [[nodiscard]] static float cosineSimilarity(std::span<const float> a,
                                                std::span<const float> b) noexcept;

private:
    struct Person {
        std::string name;
        std::vector<Embedding> embeddings;
    };

    std::vector<Person> m_people;
    std::string m_modelId;
};

}  // namespace lumina::vision
