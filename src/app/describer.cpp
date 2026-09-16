// ---------------------------------------------------------------------------
// describeDetections implementation.
// ---------------------------------------------------------------------------

#include "app/describer.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "i18n/es.hpp"

namespace lumina::app {

namespace {

// Spanish list joining: "a", "a y b", "a, b y c" (commas with "y" before the
// last item).
std::string joinItems(const std::vector<std::string>& items)
{
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += (i + 1 == items.size()) ? " y " : ", ";
        }
        out += items[i];
    }
    return out;
}

} // namespace

std::string formatCount(i18n::ObjectClass objectClass, int count)
{
    if (count <= 0) {
        return {};
    }
    if (count == 1) {
        // Spanish uses the article, not the numeral one: "una persona", "un carro".
        const char* article = i18n::indefiniteArticle(objectClass);
        const char* noun = i18n::spanishLabel(objectClass);
        if (article[0] == '\0' || noun[0] == '\0') {
            return {};
        }
        return std::string(article) + " " + noun;
    }

    const char* noun = i18n::spanishPlural(objectClass);
    if (noun[0] == '\0') {
        return {};
    }
    if (const char* word = i18n::numberWord(count); word != nullptr) {
        return std::string(word) + " " + noun;
    }
    // Above ten: still no digits, so the TTS engine never normalizes numbers.
    return "más de diez " + std::string(noun);
}

std::vector<std::string> phraseCatalog(const core::Config& config, int maxPerClass)
{
    std::vector<std::string> phrases;
    if (maxPerClass < 1) {
        return phrases;
    }
    for (const int cocoId : config.classIds) {
        const i18n::ObjectClass objectClass = i18n::fromCocoId(cocoId);
        if (objectClass == i18n::ObjectClass::Unknown) {
            continue;
        }
        for (int count = 1; count <= maxPerClass; ++count) {
            const std::string item = formatCount(objectClass, count);
            if (!item.empty()) {
                phrases.push_back(item + " enfrente.");
            }
        }
    }
    return phrases;
}

std::string describeDetections(const std::vector<core::Detection>& detections,
                               const core::Config& config,
                               std::size_t maxItems)
{
    // Count how many of each narratable object class we saw.
    std::unordered_map<i18n::ObjectClass, int> counts;
    for (const core::Detection& detection : detections) {
        // Keep only the configured COCO classes; everything else is out of scope.
        const bool configured = std::find(config.classIds.begin(),
                                          config.classIds.end(),
                                          detection.classId) != config.classIds.end();
        if (!configured) {
            continue;
        }
        const i18n::ObjectClass objectClass = i18n::fromCocoId(detection.classId);
        if (objectClass == i18n::ObjectClass::Unknown) {
            continue; // model knows it, we do not narrate it
        }
        ++counts[objectClass];
    }

    if (counts.empty() || maxItems == 0) {
        return {};
    }

    // Most frequent first; ties by class id for a stable sentence.
    std::vector<std::pair<i18n::ObjectClass, int>> ordered(counts.begin(), counts.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second > rhs.second;
        }
        return static_cast<int>(lhs.first) < static_cast<int>(rhs.first);
    });

    std::vector<std::string> items;
    const std::size_t limit = std::min(maxItems, ordered.size());
    for (std::size_t i = 0; i < limit; ++i) {
        const auto& [objectClass, count] = ordered[i];
        std::string item = formatCount(objectClass, count);
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
    }
    if (items.empty()) {
        return {};
    }
    // Lúmina narrates the environment to the user, so it is a noun phrase with a
    // direction, not a sentence about the device ("una persona enfrente").
    return joinItems(items) + " enfrente.";
}

} // namespace lumina::app
