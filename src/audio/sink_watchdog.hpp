// ---------------------------------------------------------------------------
// Bluetooth audio sink availability watchdog (FR-06, INV-014).
//
// The `bluealsa` ALSA PCM only exists while the single paired earbud is
// connected, so on a cold boot the sink may be missing for a while. Rather than
// fail hard (the previous behavior: pipeline start returned false and main exited
// non-zero), the runtime waits and retries, and only if the sink never appears
// does it shut the device down.
//
// The retry loop is pure logic with injected side effects, so it runs on the host
// against a mock sink and a fake power-off (INV-030, AGENTS §9).
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <functional>

#include "audio/audio_sink.hpp"

namespace lumina::audio {

// Tunables for waiting on the audio sink. The defaults give 60 x 3 s = 3 minutes.
struct SinkWaitConfig {
    int retryIntervalMs = 3000;    // pause between open() attempts
    int maxRetries = 60;           // open() attempts before giving up (FR-06.1)
    bool shutdownOnFailure = true; // request a host power-off when exhausted
};

// How awaitReady() finished.
enum class SinkWaitResult {
    Ready,        // the sink opened (possibly after retries)
    Exhausted,    // the sink never opened within the retry budget
    Interrupted,  // a stop request (SIGINT/SIGTERM) arrived while waiting
};

// Waits for the audio sink to become available without ever crashing the process.
//
// Threading: awaitReady() runs on the main thread before the pipeline starts. Its
// `running` argument is the process-wide flag that holds `true` while the runtime
// should keep going and that the signal handler clears (`false`) on SIGINT/SIGTERM
// (note the polarity: true means KEEP RUNNING, not "stop"). When it reads `false`
// the wait returns Interrupted so Ctrl-C is honoured promptly.
class SinkWatchdog {
public:
    // Power-off action. The default (an empty std::function) means "use the real
    // core::requestPowerOff"; unit tests inject a lambda so they never touch the
    // host. C++ note (for Java readers): std::function is like a functional
    // interface you can pass a lambda to; an empty one is the "unset" state.
    using PowerOffFn = std::function<bool()>;

    explicit SinkWatchdog(SinkWaitConfig config, PowerOffFn powerOff = {});

    // Try `sink.open()` until it succeeds, the budget runs out, or `running`
    // becomes false. Logs every failed attempt. On Exhausted, and only when
    // shutdownOnFailure is set, requests a power-off through the injected action.

    // C++ note (for Java readers): `[[nodiscard]]` makes the compiler complain if
    // the caller ignores the returned outcome, which is the whole point here.
    [[nodiscard]] SinkWaitResult awaitReady(IAudioSink& sink, const std::atomic<bool>& running);

private:
    SinkWaitConfig m_config; // retry budget
    PowerOffFn m_powerOff;   // empty => use core::requestPowerOff
};

}  // namespace lumina::audio
