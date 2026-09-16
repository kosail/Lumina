// ---------------------------------------------------------------------------
// Implementation of the project logger declared in core/logging.hpp.
//
// The logger writes one line per message to stderr and is safe to call from any
// thread. We deliberately avoid <iostream> here: fprintf is lighter and does not
// pull in the iostream globals, which matters on the 512 MB target (INV-052).
// ---------------------------------------------------------------------------

#include "core/logging.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace lumina::core {
namespace {

// Process-wide minimum level. Atomic so worker threads can read it lock-free.
std::atomic g_minLevel{LogLevel::Info};

// Serialises whole lines so concurrent threads cannot interleave their output.
std::mutex g_writeMutex;

// Human-readable name for a level. No `default` so the compiler warns if a new
// enumerator is added and this mapping is not updated.
const char* levelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Off:
            return "OFF";
    }
    return "?";
}

}  // namespace

void setLogLevel(LogLevel level) noexcept {
    g_minLevel.store(level, std::memory_order_relaxed);
}

LogLevel logLevel() noexcept { return g_minLevel.load(std::memory_order_relaxed); }

void logMessage(LogLevel level, std::string_view file, int line, std::string_view message) {
    // std::localtime returns a pointer to shared static storage, so it is only
    // called while holding g_writeMutex (which also keeps output lines atomic).
    std::lock_guard lock(g_writeMutex);

    const auto nowSeconds = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local = *std::localtime(&nowSeconds);  // ISO C++ (not POSIX localtime_r)

    char timestamp[16];
    std::snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d", local.tm_hour, local.tm_min,
                  local.tm_sec);

    std::fprintf(stderr, "[%s] %-5s %.*s:%d %.*s\n", timestamp, levelName(level),
                 static_cast<int>(file.size()), file.data(), line, static_cast<int>(message.size()),
                 message.data());
}

}  // namespace lumina::core
