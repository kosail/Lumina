#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace lumina::core {

// Project-wide error type: a human-readable message.
// C++ note (for Java readers): C++ has no checked exceptions; recoverable errors
// are returned as values instead. `std::expected<T, E>` (C++23) holds either a
// value T or an error E, and callers must check before using the value.
using Error = std::string;

// A fallible operation that produces a value on success.
template <typename T>
using Result = std::expected<T, Error>;

// A fallible operation that produces nothing on success.
using Status = std::expected<void, Error>;

// Convenience helper to build the error half of a Result/Status.
[[nodiscard]] inline std::unexpected<Error> failure(std::string_view message) {
    return std::unexpected<Error>(std::string(message));
}

}  // namespace lumina::core
