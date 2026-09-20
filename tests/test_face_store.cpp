// ---------------------------------------------------------------------------
// Unit tests for FaceStore: cosine similarity, matching policy (threshold +
// margin), best-of-K, and on-disk persistence. Pure logic, no hardware.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "vision/face_store.hpp"

using lumina::vision::Embedding;
using lumina::vision::FaceStore;

namespace {

// Build a normalized embedding from explicit components (unit length), so the
// cosine similarity between two vectors is simply their dot product.
Embedding makeEmbedding(std::initializer_list<float> values)
{
    Embedding embedding(values);
    double normSq = 0.0;
    for (const float value : embedding) {
        normSq += static_cast<double>(value) * static_cast<double>(value);
    }
    const float norm = static_cast<float>(std::sqrt(normSq));
    if (norm > 0.0F) {
        for (float& value : embedding) {
            value /= norm;
        }
    }
    return embedding;
}

const float kThreshold = 0.5F;
const float kMargin = 0.05F;

}  // namespace

TEST_CASE("face_store: cosine similarity basics")
{
    const Embedding x = makeEmbedding({1.0F, 0.0F});
    const Embedding y = makeEmbedding({0.0F, 1.0F});
    const Embedding opposite = {-1.0F, 0.0F};
    const Embedding one = {1.0F};
    const Embedding none;

    CHECK(FaceStore::cosineSimilarity(x, x) == doctest::Approx(1.0F));
    CHECK(FaceStore::cosineSimilarity(x, y) == doctest::Approx(0.0F));       // orthogonal
    CHECK(FaceStore::cosineSimilarity(x, opposite) == doctest::Approx(-1.0F));

    // Length mismatch or empty input never matches.
    CHECK(FaceStore::cosineSimilarity(x, one) == doctest::Approx(0.0F));
    CHECK(FaceStore::cosineSimilarity(none, x) == doctest::Approx(0.0F));
}

TEST_CASE("face_store: an empty store never matches")
{
    FaceStore store;
    CHECK_FALSE(store.matchBest(makeEmbedding({1.0F, 0.0F}), kThreshold, kMargin).has_value());
    CHECK(store.empty());
    CHECK(store.dimension() == 0);
}

TEST_CASE("face_store: a clear winner above threshold matches")
{
    FaceStore store;
    const Embedding ana = makeEmbedding({1.0F, 0.0F});
    store.addEmbedding("Ana", ana);

    // Identical query -> cosine 1.0, single person -> no margin requirement.
    const auto match = store.matchBest(ana, kThreshold, kMargin);
    REQUIRE(match.has_value());
    CHECK(match->name == "Ana");
    CHECK(match->similarity == doctest::Approx(1.0F));
}

TEST_CASE("face_store: a weak match below threshold is rejected")
{
    FaceStore store;
    store.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F}));

    // Orthogonal query -> cosine 0.0 < 0.5.
    CHECK_FALSE(store.matchBest(makeEmbedding({0.0F, 1.0F}), kThreshold, kMargin).has_value());
}

TEST_CASE("face_store: a near-tie between two people is rejected by the margin")
{
    FaceStore store;
    store.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F}));
    store.addEmbedding("Bob", makeEmbedding({0.0F, 1.0F}));

    // Query equidistant from both -> 0.707 vs 0.707, difference 0 < margin.
    CHECK_FALSE(store.matchBest(makeEmbedding({1.0F, 1.0F}), kThreshold, kMargin).has_value());
}

TEST_CASE("face_store: best-of-K uses the person's best embedding")
{
    FaceStore store;
    // One person with two embeddings; the query matches only the second.
    store.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F}));
    store.addEmbedding("Ana", makeEmbedding({0.0F, 1.0F}));

    const auto match = store.matchBest(makeEmbedding({0.0F, 1.0F}), kThreshold, kMargin);
    REQUIRE(match.has_value());
    CHECK(match->name == "Ana");
    CHECK(match->similarity == doctest::Approx(1.0F));
}

TEST_CASE("face_store: embeddings per person are capped")
{
    FaceStore store;
    for (int i = 0; i < 10; ++i) {
        store.addEmbedding("Ana", makeEmbedding({static_cast<float>(i + 1), 0.0F}), 3);
    }
    CHECK(store.personCount() == 1);
    CHECK(store.embeddingCount() == 3);  // oldest dropped once the cap was reached
}

TEST_CASE("face_store: persistence round-trips names, embeddings, and model id")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "lumina_face_store_test.bin";
    std::filesystem::remove(path);

    FaceStore writer;
    writer.setModelId("sface-2021dec");
    writer.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F}));
    writer.addEmbedding("Bob", makeEmbedding({0.0F, 1.0F}));
    REQUIRE(writer.save(path.string()));

    FaceStore reader;
    REQUIRE(reader.load(path.string()));
    CHECK(reader.modelId() == "sface-2021dec");
    CHECK(reader.personCount() == 2);
    CHECK(reader.names() == std::vector<std::string>{"Ana", "Bob"});

    const auto match = reader.matchBest(makeEmbedding({1.0F, 0.0F}), kThreshold, kMargin);
    REQUIRE(match.has_value());
    CHECK(match->name == "Ana");

    std::filesystem::remove(path);
}

TEST_CASE("face_store: loading a missing file fails cleanly")
{
    FaceStore store;
    store.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F}));
    CHECK_FALSE(store.load("/nonexistent/lumina_face_store.bin"));
    CHECK(store.personCount() == 1);  // unchanged on failure
}

TEST_CASE("face_store: an empty store round-trips")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "lumina_face_store_empty.bin";
    std::filesystem::remove(path);

    FaceStore writer;
    writer.setModelId("sface-2021dec");
    REQUIRE(writer.save(path.string()));

    FaceStore reader;
    REQUIRE(reader.load(path.string()));
    CHECK(reader.empty());
    CHECK(reader.modelId() == "sface-2021dec");

    std::filesystem::remove(path);
}

TEST_CASE("face_store: an embedding of a different dimension is ignored")
{
    FaceStore store;
    store.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F}));              // establishes dim 2
    store.addEmbedding("Ana", makeEmbedding({1.0F, 0.0F, 0.0F, 0.0F}));  // dim 4 -> ignored
    CHECK(store.embeddingCount() == 1);
}

TEST_CASE("face_store: a file claiming an enormous dimension is rejected")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "lumina_face_store_corrupt.bin";
    std::filesystem::remove(path);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write("LUMFACE1", 8);
        const std::uint32_t version = 1;
        const std::uint32_t hugeDimension = 100000;  // > kMaxDimension
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&hugeDimension), sizeof(hugeDimension));
    }

    FaceStore store;
    CHECK_FALSE(store.load(path.string()));
    CHECK(store.empty());  // nothing was read into the store

    std::filesystem::remove(path);
}
