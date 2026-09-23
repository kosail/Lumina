// ---------------------------------------------------------------------------
// Environment-variable overrides for the runtime configuration (demo hardening).
//
// A small, curated set of core::Config fields can be overridden from the process
// environment so the deployed systemd unit can tune behavior without a rebuild —
// above all, never power the device off when the Bluetooth sink is missing
// (FR-06.1 would otherwise do exactly that on stage). Parsing is pure and
// unit-tested; the only impure part (reading std::getenv) is isolated in the
// convenience overload.
//
// C++ note (for Java readers): `std::optional<T>` is like `Optional<T>`;
// `std::function` is a functional interface you can pass a lambda to.
// ---------------------------------------------------------------------------

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/config.hpp"

namespace lumina::core {

// Parse a boolean environment value, case-insensitively:
//   1 / true / yes / on   -> true
//   0 / false / no / off  -> false
// anything else or empty -> nullopt (the caller keeps the existing value).
[[nodiscard]] std::optional<bool> parseEnvBool(std::string_view value);

// Parse a decimal integer. Surrounding whitespace is ignored; a non-numeric value
// or one outside [min, max] yields nullopt (the caller keeps the existing value).
[[nodiscard]] std::optional<int> parseEnvInt(std::string_view value, int min, int max);

// Reads one variable by name. Returning nullopt means "unset or invalid", so the
// caller keeps the field's current value. Injecting this makes the override logic
// testable without touching the real environment.
using EnvLookup = std::function<std::optional<std::string>(const char* name)>;

// Called with (name, rawValue) when a recognized variable is set but cannot be
// parsed/range-checked, so a typo is never silent. Defaulted to empty; the real
// environment overload logs a warning. Keeping it a callback keeps this header
// pure (no logging dependency) and the behavior unit-testable.
using InvalidEnvFn = std::function<void(std::string_view name, std::string_view value)>;

// Apply the curated overrides to `config` and return it. Only these variables are
// recognized (all optional):
//   LUMINA_AUDIO_SHUTDOWN_ON_FAILURE  bool -> audioSinkShutdownOnFailure
//   LUMINA_AUDIO_SINK_MAX_RETRIES     int  -> audioSinkMaxRetries        [1, 10000]
//   LUMINA_AUDIO_SINK_RETRY_MS        int  -> audioSinkRetryIntervalMs   [100, 60000]
//   LUMINA_PROXIMITY_ENABLED          bool -> proximityEnabled
// `faceEnabled` is deliberately NOT overridable: face recognition is core to the
// demo and must always run. Values are range-checked here; call clampConfig for the
// non-environment fields.
[[nodiscard]] Config applyEnvOverrides(Config config, const EnvLookup& lookup,
                                       const InvalidEnvFn& onInvalid = {});

// Convenience overload that reads the real process environment.
[[nodiscard]] Config applyEnvOverrides(Config config);

}  // namespace lumina::core
