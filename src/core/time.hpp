#pragma once

#include <chrono>

namespace lumina::core {

// Monotonic clock: it never jumps backwards when the system clock is adjusted, so
// it is safe for measuring durations such as alert latency (INV-051), though not
// for wall-clock timestamps.
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Current monotonic time.
[[nodiscard]] inline TimePoint now() noexcept { return Clock::now(); }

// Convert any chrono duration to (possibly fractional) milliseconds.
// C++ note (for Java readers): templates resolve at compile time, so this works
// for every duration type without boxing or casts at the call site.
template <typename Rep, typename Period>
[[nodiscard]] inline double toMilliseconds(std::chrono::duration<Rep, Period> duration) noexcept {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Milliseconds elapsed from `start` (e.g. a Frame::capturedAt) until now.
[[nodiscard]] inline double msSince(TimePoint start) noexcept {
    return toMilliseconds(Clock::now() - start);
}

}  // namespace lumina::core
