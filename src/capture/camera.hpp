#pragma once

#include "core/frame.hpp"

namespace lumina::capture {

// Abstraction over any frame source: libcamera today, a test double in unit tests,
// a file player later. Hardware is only ever reached through this interface so the
// pipeline stays testable without a camera (INV-030, AGENTS.md §5).
class ICamera {
public:
    // C++ note (for Java readers): `= default` makes the compiler generate this
    // destructor, and it must be `virtual` so deleting through an ICamera* runs the
    // derived destructor (Java does this automatically).
    virtual ~ICamera() = default;

    // Open the device and begin producing frames. Returns false on failure after
    // logging the reason; does not throw across the API boundary.
    [[nodiscard]] virtual bool start() = 0;

    // Non-blocking. On success fills `out` with the most recent frame and returns
    // true; returns false when no new frame is ready. Stale frames may be dropped
    // so capture never blocks inference (INV-031).
    [[nodiscard]] virtual bool getLatest(core::Frame& out) = 0;

    // Stop producing frames and release the device. Safe to call more than once.
    virtual void stop() = 0;
};

}  // namespace lumina::capture
