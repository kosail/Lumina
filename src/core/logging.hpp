#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace lumina::core {

// Severity levels, ordered from most to least verbose; `Off` silences everything.
enum class LogLevel { Trace = 0, Debug, Info, Warn, Error, Off };

// Set/get the process-wide minimum level. Messages below it are dropped before
// formatting, so disabled logs are nearly free. This one value is intentionally
// process-wide: a logging sink is infrastructure, not pipeline state (INV-030).
void setLogLevel(LogLevel level) noexcept;
[[nodiscard]] LogLevel logLevel() noexcept;

// Low-level sink: writes one fully-formed line to stderr. Implemented in
// logging.cpp; prefer the macros below, which check the format at compile time.
void logMessage(LogLevel level, std::string_view file, int line, std::string_view message);

// Compile-time-checked formatting helper. `std::format_string<Args...>` requires
// the format to be a string literal, catching argument mistakes at build time
// rather than crashing at run time.
// C++ note (for Java readers): variadic templates (`Args...`) are C++'s
// compile-time alternative to Java varargs, and `std::forward` preserves move
// semantics when an argument is an rvalue.
template <typename... Args>
void logFormat(LogLevel level, std::string_view file, int line, std::format_string<Args...> fmt,
               Args&&... args) {
    if (static_cast<int>(level) < static_cast<int>(logLevel())) {
        return;  // below threshold: skip the formatting cost entirely
    }
    const std::string message = std::format(fmt, std::forward<Args>(args)...);
    logMessage(level, file, line, message);
}

}  // namespace lumina::core

// Logging macros capture file/line for free and keep call sites short.
#define LUMINA_LOG_TRACE(...) \
    ::lumina::core::logFormat(::lumina::core::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define LUMINA_LOG_DEBUG(...) \
    ::lumina::core::logFormat(::lumina::core::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define LUMINA_LOG_INFO(...) \
    ::lumina::core::logFormat(::lumina::core::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LUMINA_LOG_WARN(...) \
    ::lumina::core::logFormat(::lumina::core::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define LUMINA_LOG_ERROR(...) \
    ::lumina::core::logFormat(::lumina::core::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
