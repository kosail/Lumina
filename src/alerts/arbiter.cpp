// ---------------------------------------------------------------------------
// Alert arbiter implementation. See arbiter.hpp for the policy and threading
// contract.
// ---------------------------------------------------------------------------

#include "alerts/arbiter.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace lumina::alerts {

namespace {

// Sentinel for "no queue entry" returned by lowestPriorityIndexLocked().
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

// Scoped enums compare fine directly, but casting once keeps the intent obvious
// and avoids surprising overload resolution in generic code.
[[nodiscard]] constexpr int priorityValue(Priority priority) noexcept
{
    return static_cast<int>(priority);
}

} // namespace

AlertArbiter::AlertArbiter(ArbiterConfig config)
    : m_config(config)
    , m_capacity(config.queueCapacity == 0 ? 1 : config.queueCapacity)
{
    // Normalise the tunables so a zero/negative value can never disable a guard
    // or create a nonsensical duration.
    if (m_config.descriptionStableFrames < 1) {
        m_config.descriptionStableFrames = 1;
    }
    if (m_config.dedupCooldown.count() < 0) {
        m_config.dedupCooldown = std::chrono::milliseconds::zero();
    }
    if (m_config.globalMinGap.count() < 0) {
        m_config.globalMinGap = std::chrono::milliseconds::zero();
    }
}

int AlertArbiter::stableFramesFor(Priority priority) const noexcept
{
    // Safety and warnings are latency-critical: fire on the first frame. Only
    // descriptions wait for stability (INV-051 vs. flicker).
    if (priorityValue(priority) >= priorityValue(Priority::Warning)) {
        return 1;
    }
    return m_config.descriptionStableFrames;
}

bool AlertArbiter::gapBlocksAt(Priority priority, core::TimePoint now) const noexcept
{
    if (m_config.safetyBypassesGap && priorityValue(priority) >= priorityValue(Priority::Warning)) {
        return false;
    }
    if (!m_hasCompleted) {
        return false; // nothing spoken yet, no gap to respect
    }
    return (now - m_lastCompletedAt) < m_config.globalMinGap;
}

std::size_t AlertArbiter::lowestPriorityIndexLocked() const noexcept
{
    std::size_t best = kNoIndex;
    for (std::size_t i = 0; i < m_queue.size(); ++i) {
        // Strict `<` keeps the oldest entry as the victim on a tie.
        if (best == kNoIndex || priorityValue(m_queue[i].priority) < priorityValue(m_queue[best].priority)) {
            best = i;
        }
    }
    return best;
}

bool AlertArbiter::submit(Alert alert, core::TimePoint now)
{
    std::lock_guard lock(m_mutex);
    if (m_closed) {
        return false;
    }

    // Stability: count consecutive frames with the same scene key. A changed key
    // restarts the count, so a one-frame flicker never becomes an utterance.
    if (alert.dedupKey == m_candidateKey) {
        ++m_candidateCount;
    } else {
        m_candidateKey = alert.dedupKey;
        m_candidateCount = 1;
    }
    if (m_candidateCount < stableFramesFor(alert.priority)) {
        return false;
    }

    const bool hasKey = !alert.dedupKey.empty();

    // De-duplication: never queue a key that is already queued/speaking, and
    // respect the per-key cooldown after it was last spoken.
    if (hasKey) {
        if (m_pendingKeys.find(alert.dedupKey) != m_pendingKeys.end()) {
            return false;
        }
        const auto last = m_lastCompleted.find(alert.dedupKey);
        if (last != m_lastCompleted.end() && (now - last->second) < m_config.dedupCooldown) {
            return false;
        }
    }

    if (gapBlocksAt(alert.priority, now)) {
        return false;
    }

    // Bounded enqueue: if full, evict the lowest-priority (oldest) entry, but only
    // when it is strictly less urgent than the newcomer — never drop a pending
    // Safety alert for a Description (FR-07.2).
    while (m_queue.size() >= m_capacity) {
        const std::size_t victim = lowestPriorityIndexLocked();
        if (victim == kNoIndex ||
            priorityValue(m_queue[victim].priority) >= priorityValue(alert.priority)) {
            return false;
        }
        m_pendingKeys.erase(m_queue[victim].dedupKey);
        m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(victim));
    }

    // The alert will definitely be queued, so it is safe to preempt now. Setting
    // the interrupt is atomic; the TTS engine polls it without taking the lock.
    if (m_config.interruptOnHigherPriority && m_speaking &&
        priorityValue(alert.priority) > priorityValue(m_speakingPriority)) {
        m_interrupt.store(true, std::memory_order_relaxed);
    }

    m_queue.push_back(std::move(alert));
    if (hasKey) {
        // Read the key back from the moved-to element: `alert` is moved-from here.
        m_pendingKeys.insert(m_queue.back().dedupKey);
    }
    m_cond.notify_one();
    return true;
}

void AlertArbiter::clearCandidate()
{
    std::lock_guard lock(m_mutex);
    m_candidateKey.clear();
    m_candidateCount = 0;
}

bool AlertArbiter::waitPop(Alert& out)
{
    std::unique_lock lock(m_mutex);
    m_cond.wait(lock, [this] { return m_closed || !m_queue.empty(); });
    if (m_closed) {
        return false; // shutdown: abandon anything still queued
    }

    // Highest priority first; among equals the oldest (front-most) one.
    std::size_t best = 0;
    for (std::size_t i = 1; i < m_queue.size(); ++i) {
        if (priorityValue(m_queue[i].priority) > priorityValue(m_queue[best].priority)) {
            best = i;
        }
    }

    out = std::move(m_queue[best]);
    m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(best));

    m_speaking = true;
    m_speakingPriority = out.priority;
    m_activeKey = out.dedupKey;
    // Clearing here (under the lock, atomically with becoming "speaking") confines
    // any preemption signal to the utterance that should be interrupted.
    m_interrupt.store(false, std::memory_order_relaxed);
    return true;
}

void AlertArbiter::finishSpeaking(core::TimePoint now)
{
    std::lock_guard lock(m_mutex);
    if (!m_speaking && m_activeKey.empty()) {
        return; // nothing was speaking (defensive)
    }
    m_speaking = false;
    if (!m_activeKey.empty()) {
        m_pendingKeys.erase(m_activeKey);
        m_lastCompleted[m_activeKey] = now;
    }
    m_activeKey.clear();
    m_hasCompleted = true;
    m_lastCompletedAt = now;
}

void AlertArbiter::close()
{
    {
        std::lock_guard lock(m_mutex);
        m_closed = true;
    }
    // Abort any utterance in progress, then wake a blocked consumer.
    m_interrupt.store(true, std::memory_order_relaxed);
    m_cond.notify_all();
}

std::size_t AlertArbiter::queuedCount() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}

bool AlertArbiter::speaking() const
{
    std::lock_guard lock(m_mutex);
    return m_speaking;
}

Priority AlertArbiter::speakingPriority() const
{
    std::lock_guard lock(m_mutex);
    return m_speakingPriority;
}

} // namespace lumina::alerts
