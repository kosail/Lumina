// ---------------------------------------------------------------------------
// Alert arbiter: the single owner of the speech queue.
//
// Replaces the ad-hoc "hysteresis + last-text cooldown" that lived in the
// inference loop. Producers (the inference thread) submit candidate Alerts every
// frame; the arbiter decides whether to queue one, and the single speech worker
// pops them. Policy enforced here:
//   - priority ordering (Safety/Warning before Description),
//   - stability: a description must be seen for N consecutive frames, while a
//     safety/warning alert fires immediately (latency, INV-051),
//   - de-duplication: a per-key cooldown stops the same phrase repeating while a
//     scene is unchanged (FR-02.2, FR-07.3),
//   - a global minimum gap between utterances (avoids machine-gun speech),
//   - preemption: a strictly higher-priority alert interrupts in-progress speech
//     via an atomic flag the TTS polls (INV-032, FR-02.3),
//   - a bounded queue that never grows without limit (FR-07.2) and never discards
//     a pending Safety alert to make room for a Description.
//
// Threading: submit() is called by the inference thread and, for pre-stabilized
// Safety alerts, by the proximity thread; waitPop()/finishSpeaking() by the
// single speech thread. All mutable state is guarded by `m_mutex`, so multiple
// producers are safe; the interrupt signal is a separate std::atomic<bool>
// because the TTS engine reads it from inside synthesis without taking the lock.
// `now` is passed in by the caller (no clock call inside) so behaviour is fully
// deterministic in tests.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "alerts/alert.hpp"
#include "core/time.hpp"

namespace lumina::alerts {

// Tunables. Defaults are the values approved for Day 3.
struct ArbiterConfig {
    std::size_t queueCapacity = 4;                    // bounded (FR-07.2)
    int descriptionStableFrames = 2;                  // safety/warning always use 1
    std::chrono::milliseconds dedupCooldown{4000};    // same scene key
    std::chrono::milliseconds globalMinGap{600};      // between utterances
    bool safetyBypassesGap = true;                    // Warnings/Safety ignore the gap
    bool interruptOnHigherPriority = true;            // strictly higher preempts
};

class AlertArbiter {
public:
    explicit AlertArbiter(ArbiterConfig config = {});

    AlertArbiter(const AlertArbiter&) = delete;
    AlertArbiter& operator=(const AlertArbiter&) = delete;

    // Producer side. Returns true when `alert` was accepted into the queue (it
    // may still be preempted later). Returns false when the arbiter is closed, the
    // alert has not yet been stable long enough, it is suppressed by the cooldown
    // or gap, or the queue is full of equally/more urgent work.
    // `alert` is taken by value so the caller can move a freshly built string in.
    [[nodiscard]] bool submit(Alert alert, core::TimePoint now);

    // Producer side. Call when a frame produced nothing to say, so the stability
    // counter only counts *consecutive* sightings of the same scene instead of
    // spanning gaps where the object disappeared.
    void clearCandidate();

    // Consumer side. Blocks until an alert is available, then hands out the
    // highest-priority, oldest alert and marks the arbiter as "speaking". Returns
    // false when the arbiter was closed (any queued items are abandoned).
    [[nodiscard]] bool waitPop(Alert& out);

    // Consumer side. Call after the utterance finished (or was interrupted) to
    // release the active key, start its cooldown, and record the global gap.
    void finishSpeaking(core::TimePoint now);

    // The TTS engine polls this; it becomes true when a strictly higher-priority
    // alert arrives while an utterance is being spoken. waitPop() clears it when
    // the next utterance starts, so it never leaks across utterances.
    [[nodiscard]] const std::atomic<bool>& interruptFlag() const noexcept { return m_interrupt; }

    // Wake every waiter and stop accepting new alerts. Also raises the interrupt
    // flag so an in-progress utterance aborts promptly.
    void close();

    // ---- introspection (tests / diagnostics) --------------------------------
    [[nodiscard]] std::size_t queuedCount() const;
    [[nodiscard]] bool speaking() const;
    [[nodiscard]] Priority speakingPriority() const;

private:
    // Consecutive frames a priority needs before it may be queued.
    [[nodiscard]] int stableFramesFor(Priority priority) const noexcept;

    // True when the inter-utterance gap should suppress a new alert of `priority`.
    // Caller must hold m_mutex.
    [[nodiscard]] bool gapBlocksAt(Priority priority, core::TimePoint now) const noexcept;

    // Index of the lowest-priority queue entry (oldest wins ties), or npos when empty.
    // Caller must hold m_mutex.
    [[nodiscard]] std::size_t lowestPriorityIndexLocked() const noexcept;

    ArbiterConfig m_config;
    std::size_t m_capacity = 1; // normalised queueCapacity (>= 1)

    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::deque<Alert> m_queue;              // guarded
    bool m_closed = false;                  // guarded
    bool m_speaking = false;                // guarded
    Priority m_speakingPriority = Priority::Info; // guarded
    std::string m_activeKey;                // guarded
    std::unordered_set<std::string> m_pendingKeys; // queued or speaking; guarded
    std::string m_candidateKey;             // consecutive-frame stability; guarded
    int m_candidateCount = 0;               // guarded
    std::unordered_map<std::string, core::TimePoint> m_lastCompleted; // guarded
    bool m_hasCompleted = false;            // guarded
    core::TimePoint m_lastCompletedAt{};    // guarded

    std::atomic<bool> m_interrupt{false};
};

} // namespace lumina::alerts
