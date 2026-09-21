// ---------------------------------------------------------------------------
// Minimal JSON helpers for the companion agent (FR-11).
//
// The control protocol is a flat set of known keys, so we deliberately avoid a
// full JSON library (INV-022): this file provides string escaping for building the
// telemetry datagram and small typed readers for the incoming requests. Everything
// is pure and unit-tested on the host.
// ---------------------------------------------------------------------------

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumina::agent {

// Escape a string for embedding inside JSON double quotes. Handles the mandatory
// escapes (quote, backslash, control characters) and leaves UTF-8 bytes untouched,
// so Spanish names such as "David Solís" survive unchanged.
[[nodiscard]] std::string escapeJson(std::string_view text);

// Readers for a flat JSON object. Each returns nullopt when the key is absent or
// the value is not the expected type. `json` is the whole request line.
[[nodiscard]] std::optional<std::string> jsonGetString(std::string_view json, std::string_view key);
[[nodiscard]] std::optional<long long> jsonGetInt(std::string_view json, std::string_view key);
[[nodiscard]] std::optional<bool> jsonGetBool(std::string_view json, std::string_view key);
// Reads an array of strings, e.g. "images":["<b64>","<b64>"]. Returns an empty
// vector when the key is absent or not an array.
[[nodiscard]] std::vector<std::string> jsonGetStringArray(std::string_view json,
                                                          std::string_view key);

}  // namespace lumina::agent
