// ---------------------------------------------------------------------------
// Runtime status snapshot (FR-11).
//
// The runtime is deliberately network-free (INV-003): instead of talking to the
// companion app, it publishes a tiny local status snapshot at ~1 Hz that the
// separate `lumina_agent` process reads and forwards as UDP telemetry. This header
// defines the value type, the publisher interface (so the Pipeline is testable with
// a fake), and the pure formatting/measurement helpers.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/frame.hpp"

namespace lumina::status {

// One point-in-time view of the runtime's health, assembled by the Pipeline.
//
// C++ note (for Java readers): this is a plain value `struct` (like a Java record
// without accessors). Copying it copies the numbers and the names; there is no
// hidden ownership. Every field has a safe default so a default-constructed
// snapshot is valid.
struct StatusSnapshot {
    bool running = false;       // pipeline started and not yet stopped
    int uptimeSeconds = 0;      // seconds since the pipeline started
    double fps = 0.0;           // inference throughput over the last window
    double rssMb = 0.0;         // resident set size in MiB (diagnostic only)
    float meanLuma = -1.0F;     // mean frame luminance in [0,1]; negative = unknown
    std::size_t faceCount = 0;  // number of enrolled people
    bool sinkReady = false;     // audio sink open and usable
    // Names of the enrolled people, in insertion order (FR-11). Emitted one per
    // line in the status file so the agent can list them without parsing the
    // binary enrollment store.
    std::vector<std::string> people;
};

// Destination for status snapshots. The Pipeline depends only on this interface so
// it stays testable with a fake and free of file/socket ownership (INV-030).
// Implementations must be cheap and must never block: publish() runs on the status
// thread at ~1 Hz, so a slow sink can only delay the next snapshot, never the
// capture/inference/speech path.
class IStatusPublisher {
public:
    virtual ~IStatusPublisher() = default;
    virtual void publish(const StatusSnapshot& snapshot) = 0;
};

// No-op publisher used when the status hook is disabled or not injected.
class NullStatusPublisher final : public IStatusPublisher {
public:
    void publish(const StatusSnapshot& /*snapshot*/) override {}
};

// Format a snapshot as the internal `key=value` block the agent reads (one field
// per line, trailing newline). Deliberately not JSON: this is a private interface
// between two of our own C++ processes, and a flat key/value block needs no parser
// on either side. The public JSON contract for the app is docs/APP_PROTOCOL.md.
[[nodiscard]] std::string toKeyValue(const StatusSnapshot& snapshot);

// Mean luminance of a frame in [0,1] using the Rec.601 weights
// (0.299R + 0.587G + 0.114B); for NV12/YUV420 the Y plane already is luminance.
// Pixels are sampled on a grid so the cost is bounded regardless of resolution.
// Returns -1 for an empty/Unknown frame so callers can tell "no data" apart from
// "a black image" (luma 0).
[[nodiscard]] float meanLuma(const core::Frame& frame);

}  // namespace lumina::status
