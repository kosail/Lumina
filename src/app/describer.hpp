// ---------------------------------------------------------------------------
// Detection -> Spanish sentence.
//
// Pure, hardware-free logic (AGENTS §9): it maps detector output and the i18n
// catalog to a short phrase the TTS engine speaks. Kept out of the pipeline so it
// can be unit-tested exhaustively.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/detection.hpp"
#include "i18n/es.hpp"

namespace lumina::app {

// "<article|number> <noun>" for a count of one class, e.g. "una persona",
// "un carro", "dos perros", "más de diez personas". Never emits digits, so the
// TTS engine never has to normalize numbers. Empty for ObjectClass::Unknown.
[[nodiscard]] std::string formatCount(i18n::ObjectClass objectClass, int count);

// Single-class phrases to pre-render into the TTS phrase cache (INV-051): for
// every class in config.classIds, the phrases for counts 1..maxPerClass, e.g.
// "una persona enfrente.", "dos personas enfrente.", ... Counts above
// maxPerClass are covered lazily by the cache at runtime.
[[nodiscard]] std::vector<std::string> phraseCatalog(const core::Config& config,
                                                     int maxPerClass = 3);

// Build a short Spanish description of `detections`, or "" when nothing worth
// narrating was found.
//
// Behaviour (beta slice):
//   - keeps only detections whose classId is in `config.classIds` and that the
//     i18n catalog names (unknown classes are ignored);
//   - counts occurrences per object class;
//   - mentions at most `maxItems` classes, most frequent first (ties broken by
//     class id so the result is deterministic);
//   - formats "<items> enfrente." with Spanish list joining, e.g.
//     "una persona enfrente." or "una persona, un perro y un gato enfrente."
[[nodiscard]] std::string describeDetections(const std::vector<core::Detection>& detections,
                                             const core::Config& config,
                                             std::size_t maxItems = 3);

} // namespace lumina::app
