// ---------------------------------------------------------------------------
// Speech events and their safety priority.
//
// Every thing Lúmina says is modelled as an Alert so a single arbiter can own
// the speech queue, order it by safety priority, and preempt descriptions with
// obstacle warnings (INV-032, FR-02, FR-07). Pure data, no hardware.
// ---------------------------------------------------------------------------

#pragma once

#include <string>

#include "core/time.hpp"

namespace lumina::alerts {

// Safety priority. Higher value = more urgent. The values are explicit so the
// arbiter can compare them numerically and tests can assert the order.
//   Safety   - imminent collision (e.g. proximity sensor, Phase C)
//   Warning  - nearby obstacle in the user's path (bbox heuristic, FR-02)
//   Description - scene narration ("una persona enfrente.")
//   Info     - non-urgent notices
enum class Priority : int {
    Info = 0,
    Description = 1,
    Warning = 2,
    Safety = 3,
};

// Where an alert came from. Kept for logging/routing; the arbiter only uses
// `priority` for ordering.
enum class Source {
    Description, // scene narration
    Obstacle,    // bbox distance heuristic
    Proximity,   // VL53L0X sensor (Phase C; reserved)
    Face,        // face recognition (Day 4; reserved)
};

// One queued utterance.
//
// C++ note (for Java readers): this is a plain value struct (like a Java record
// without accessors). Copying it copies the strings; the pipeline builds one per
// relevant frame and moves it into the arbiter's queue.
struct Alert {
    Priority priority = Priority::Description;
    Source source = Source::Description;
    std::string text;      // the exact Spanish phrase to synthesize
    // Scene signature for de-duplication/cooldown. Two frames that describe the
    // same situation must produce the same key; a changed scene must produce a
    // different one. An empty key disables de-duplication for this alert.
    std::string dedupKey;
    // Capture time of the frame that produced the alert, so the speech worker can
    // report event->audible latency (INV-051).
    core::TimePoint detectedAt{};
    // True when this alert was already stabilized by its producer and must not be
    // held back by the arbiter's scene-description stability counter. Face
    // greetings (FR-03) are one-shot events with their own stability + cooldown
    // upstream, so they set this instead of relying on consecutive-frame counting.
    bool preStabilized = false;
};

} // namespace lumina::alerts
