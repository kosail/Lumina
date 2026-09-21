// ---------------------------------------------------------------------------
// Sink availability watchdog implementation (see sink_watchdog.hpp).
// ---------------------------------------------------------------------------

#include "audio/sink_watchdog.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "core/logging.hpp"
#include "core/power.hpp"

namespace lumina::audio {

SinkWatchdog::SinkWatchdog(SinkWaitConfig config, PowerOffFn powerOff)
    : m_config(config), m_powerOff(std::move(powerOff))
{
}

SinkWaitResult SinkWatchdog::awaitReady(IAudioSink& sink, const std::atomic<bool>& running)
{
    // Observability: without this line a wait that ends before the first open() is
    // indistinguishable from the runtime never reaching the watchdog at all.
    LUMINA_LOG_INFO("audio sink wait: up to {} attempt(s), {} ms apart",
                    m_config.maxRetries,
                    m_config.retryIntervalMs);

    for (int attempt = 1; attempt <= m_config.maxRetries; ++attempt) {
        // `running` is true while the runtime should keep going; it becomes false
        // only when a signal handler asks us to stop (CHG-0079 inverted this).
        if (!running.load(std::memory_order_relaxed)) {
            return SinkWaitResult::Interrupted;
        }

        if (sink.open()) {
            LUMINA_LOG_INFO("audio sink ready (attempt {}/{})", attempt, m_config.maxRetries);
            return SinkWaitResult::Ready;
        }
        LUMINA_LOG_WARN("audio sink unavailable (attempt {}/{}); retrying in {} ms",
                        attempt,
                        m_config.maxRetries,
                        m_config.retryIntervalMs);

        // Sleep in <=50 ms slices (a sleep that cannot be interrupted by a signal)
        // so the stop flag is observed quickly. A non-positive interval is a test
        // configuration meaning "no delay".
        long long remainingMs = m_config.retryIntervalMs;
        while (remainingMs > 0) {
            if (!running.load(std::memory_order_relaxed)) {
                return SinkWaitResult::Interrupted;
            }
            const long long step = std::min<long long>(50, remainingMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
            remainingMs -= step;
        }
    }

    const long long totalSeconds =
        static_cast<long long>(m_config.maxRetries) * m_config.retryIntervalMs / 1000;
    LUMINA_LOG_ERROR("audio sink unavailable after {} attempts ({} s)",
                     m_config.maxRetries,
                     totalSeconds);

    // The runtime cannot function without audio output, so on the target we ask
    // the OS to power the device down. If that is not permitted we simply return
    // and let main() stop the runtime.
    if (!m_config.shutdownOnFailure) {
        LUMINA_LOG_WARN("shutdown-on-sink-failure is disabled; stopping the runtime instead");
        return SinkWaitResult::Exhausted;
    }
    // An empty injected action means "use the real host power-off".
    const PowerOffFn powerOff = m_powerOff ? m_powerOff : PowerOffFn(core::requestPowerOff);
    if (powerOff()) {
        LUMINA_LOG_INFO("power-off requested after audio sink timeout");
    } else {
        LUMINA_LOG_ERROR("could not power off; stopping the Lúmina runtime instead");
    }
    return SinkWaitResult::Exhausted;
}

}  // namespace lumina::audio
