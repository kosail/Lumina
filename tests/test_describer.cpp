// ---------------------------------------------------------------------------
// Unit tests for describeDetections (pure Spanish sentence builder).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <algorithm>

#include "app/describer.hpp"

#include "core/config.hpp"
#include "core/detection.hpp"

using lumina::app::alertPhraseCatalog;
using lumina::app::describeDetections;
using lumina::app::phraseCatalog;
using lumina::app::twoClassPhraseCatalog;
using lumina::core::Config;
using lumina::core::defaultConfig;
using lumina::core::Detection;

namespace {

Detection makeDetection(int classId, float score = 0.9F)
{
    Detection detection;
    detection.classId = classId;
    detection.score = score;
    detection.box = {0.0F, 0.0F, 10.0F, 10.0F};
    return detection;
}

} // namespace

TEST_CASE("describeDetections: nothing -> empty")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({}, config).empty());
}

TEST_CASE("describeDetections: one person uses the article, not a number")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(0)}, config) == "una persona enfrente.");
}

TEST_CASE("describeDetections: two people uses a spelled number")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(0), makeDetection(0)}, config) == "dos personas enfrente.");
}

TEST_CASE("describeDetections: masculine singular article (un carro)")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(2)}, config) == "un carro enfrente.");
}

TEST_CASE("describeDetections: feminine singular article (una bicicleta)")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(1)}, config) == "una bicicleta enfrente.");
}

TEST_CASE("describeDetections: deferred classes (motorcycle, truck) are not narrated")
{
    // Motorcycle (3) and truck (7) were deferred as a nice-to-have (INV-040), so the
    // default class set must ignore them even though i18n still has dormant labels.
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(3)}, config).empty());
    CHECK(describeDetections({makeDetection(7)}, config).empty());
}

TEST_CASE("describeDetections: accented-vowel noun plural")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(57), makeDetection(57)}, config) == "dos sofás enfrente.");
}

TEST_CASE("describeDetections: accent-dropping plural (autobus -> autobuses)")
{
    const Config config = defaultConfig();
    CHECK(describeDetections({makeDetection(5), makeDetection(5)}, config) == "dos autobuses enfrente.");
}

TEST_CASE("describeDetections: more than ten avoids digits")
{
    const Config config = defaultConfig();
    std::vector<Detection> people(11, makeDetection(0));
    CHECK(describeDetections(people, config) == "más de diez personas enfrente.");
}

TEST_CASE("describeDetections: ignores classes outside config.classIds")
{
    Config config = defaultConfig();
    config.classIds = {0}; // person only
    CHECK(describeDetections({makeDetection(16)}, config).empty()); // dog ignored
}

TEST_CASE("describeDetections: two items are joined with 'y'")
{
    const Config config = defaultConfig();
    const auto text =
        describeDetections({makeDetection(16), makeDetection(0), makeDetection(0)}, config);
    CHECK(text == "dos personas y un perro enfrente.");
}

TEST_CASE("describeDetections: three items use commas and 'y'")
{
    const Config config = defaultConfig();
    const auto text = describeDetections(
        {makeDetection(0), makeDetection(16), makeDetection(15)}, config);
    CHECK(text == "una persona, un gato y un perro enfrente.");
}

TEST_CASE("describeDetections: respects maxItems")
{
    const Config config = defaultConfig();
    const auto text = describeDetections(
        {makeDetection(0), makeDetection(16), makeDetection(15)}, config, 2);
    CHECK(text == "una persona y un gato enfrente.");
}

TEST_CASE("phraseCatalog: one phrase per class per count")
{
    const Config config = defaultConfig();
    const auto phrases = phraseCatalog(config, 3);
    CHECK(phrases.size() == config.classIds.size() * 3);
    CHECK(phrases.front() == "una persona enfrente.");
    CHECK(phrases[1] == "dos personas enfrente.");
    CHECK(phrases[2] == "tres personas enfrente.");
}

TEST_CASE("alertPhraseCatalog: near and mid proximity phrases")
{
    const auto phrases = alertPhraseCatalog();
    REQUIRE(phrases.size() == 2);
    CHECK(phrases[0] == "cuidado, obstáculo cerca.");
    CHECK(phrases[1] == "obstáculo cerca.");
}

TEST_CASE("twoClassPhraseCatalog: pairs of narrated classes, counts 1..2")
{
    const Config config = defaultConfig();
    const auto phrases = twoClassPhraseCatalog(config, 2);

    const std::size_t classCount = config.classIds.size();
    const std::size_t pairs = classCount * (classCount - 1) / 2;
    CHECK(phrases.size() == pairs * 4); // 4 count combinations per pair

    const auto has = [&phrases](const std::string& phrase) {
        return std::find(phrases.begin(), phrases.end(), phrase) != phrases.end();
    };
    // person (class 0) precedes bicycle (class 1); the higher count goes first,
    // matching describeDetections.
    CHECK(has("una persona y una bicicleta enfrente."));
    CHECK(has("dos personas y una bicicleta enfrente."));
    CHECK(has("dos bicicletas y una persona enfrente."));
    CHECK(has("dos personas y dos bicicletas enfrente."));
}
