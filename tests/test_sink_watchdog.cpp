// ---------------------------------------------------------------------------
// Unit tests for audio/sink_watchdog.* — the FR-06.1 retry/power-off policy.
//
// Pure logic against a mock sink and an injected power-off action, so nothing
// here touches the host (AGENTS.md §9). The retry interval is set to 0 so the
// tests run instantly.
//
// IMPORTANT (CHG-0079): the awaited flag is `running` (true = keep going). An
// earlier version of these tests asserted the opposite polarity ("stop"), which
// is exactly why they stayed green while the runtime bailed out immediately.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <atomic>

#include "audio/sink_watchdog.hpp"
#include "mocks/mock_audio_sink.hpp"

using lumina::audio::SinkWaitConfig;
using lumina::audio::SinkWaitResult;
using lumina::audio::SinkWatchdog;
using lumina::tests::MockAudioSink;

namespace {

// No real sleeping: interval 0 means "retry immediately" in tests.
SinkWaitConfig fastConfig(int maxRetries, bool shutdownOnFailure)
{
    SinkWaitConfig config;
    config.retryIntervalMs = 0;
    config.maxRetries = maxRetries;
    config.shutdownOnFailure = shutdownOnFailure;
    return config;
}

}  // namespace

TEST_CASE("SinkWatchdog returns Ready as soon as the sink opens")
{
    MockAudioSink sink;
    sink.setOpenResult(true);
    std::atomic<bool> running{true}; // runtime is running
    int powerOffCalls = 0;
    SinkWatchdog watchdog(fastConfig(5, true), [&]() {
        ++powerOffCalls;
        return true;
    });

    CHECK(watchdog.awaitReady(sink, running) == SinkWaitResult::Ready);
    CHECK(sink.openCount() == 1);
    CHECK(powerOffCalls == 0);
}

TEST_CASE("SinkWatchdog retries until a late sink opens")
{
    MockAudioSink sink;
    sink.setOpenFailuresBeforeSuccess(3); // absent for three attempts...
    std::atomic<bool> running{true};
    int powerOffCalls = 0;
    SinkWatchdog watchdog(fastConfig(10, true), [&]() {
        ++powerOffCalls;
        return true;
    });

    CHECK(watchdog.awaitReady(sink, running) == SinkWaitResult::Ready);
    CHECK(sink.openCount() == 4); // ...then opens on the fourth
    CHECK(powerOffCalls == 0);
}

TEST_CASE("SinkWatchdog powers off once the retry budget is exhausted")
{
    MockAudioSink sink;
    sink.setOpenResult(false); // never appears
    std::atomic<bool> running{true};
    int powerOffCalls = 0;
    SinkWatchdog watchdog(fastConfig(3, true), [&]() {
        ++powerOffCalls;
        return true;
    });

    CHECK(watchdog.awaitReady(sink, running) == SinkWaitResult::Exhausted);
    CHECK(sink.openCount() == 3);
    CHECK(powerOffCalls == 1);
}

TEST_CASE("SinkWatchdog skips power-off when shutdown is disabled")
{
    MockAudioSink sink;
    sink.setOpenResult(false);
    std::atomic<bool> running{true};
    int powerOffCalls = 0;
    SinkWatchdog watchdog(fastConfig(2, false), [&]() {
        ++powerOffCalls;
        return true;
    });

    CHECK(watchdog.awaitReady(sink, running) == SinkWaitResult::Exhausted);
    CHECK(powerOffCalls == 0);
}

TEST_CASE("SinkWatchdog stays Exhausted when the power-off action fails")
{
    MockAudioSink sink;
    sink.setOpenResult(false);
    std::atomic<bool> running{true};
    SinkWatchdog watchdog(fastConfig(2, true), []() { return false; });

    CHECK(watchdog.awaitReady(sink, running) == SinkWaitResult::Exhausted);
}

TEST_CASE("SinkWatchdog stops early when the runtime is no longer running")
{
    // A stop request is modelled by `running == false` (the signal handler clears
    // it). This is the regression test for CHG-0079: with the old inverted check,
    // `running == true` (the normal case) was misread as "stop".
    MockAudioSink sink;
    sink.setOpenResult(false);
    std::atomic<bool> running{false};
    int powerOffCalls = 0;
    SinkWatchdog watchdog(fastConfig(10, true), [&]() {
        ++powerOffCalls;
        return true;
    });

    CHECK(watchdog.awaitReady(sink, running) == SinkWaitResult::Interrupted);
    CHECK(sink.openCount() == 0); // never attempted
    CHECK(powerOffCalls == 0);
}

TEST_CASE("SinkWatchdog attempts the sink while running is true")
{
    // Guards the polarity directly: with running true it must NOT return
    // Interrupted, regardless of the sink result.
    MockAudioSink sink;
    sink.setOpenResult(false);
    std::atomic<bool> running{true};
    SinkWatchdog watchdog(fastConfig(1, false), []() { return true; });

    const SinkWaitResult result = watchdog.awaitReady(sink, running);
    CHECK(result == SinkWaitResult::Exhausted);
    CHECK(sink.openCount() == 1);
}
