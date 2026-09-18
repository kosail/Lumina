// ---------------------------------------------------------------------------
// Unit tests for the alert arbiter: stability, priority, cooldown, gap,
// preemption, bounded queue and clean shutdown. All timing is synthetic (we pass
// `now` in) so the tests are fully deterministic.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "alerts/alert.hpp"
#include "alerts/arbiter.hpp"
#include "core/time.hpp"

using lumina::alerts::Alert;
using lumina::alerts::AlertArbiter;
using lumina::alerts::ArbiterConfig;
using lumina::alerts::Priority;
using lumina::core::TimePoint;

namespace {

// Monotonic synthetic clock: t(1000) is 1 second after the epoch.
TimePoint t(int milliseconds)
{
    return TimePoint{} + std::chrono::milliseconds(milliseconds);
}

Alert makeAlert(Priority priority, std::string key, std::string text = "frase")
{
    Alert alert;
    alert.priority = priority;
    alert.dedupKey = std::move(key);
    alert.text = std::move(text);
    return alert;
}

Alert description(std::string key)
{
    return makeAlert(Priority::Description, std::move(key));
}

Alert warning(std::string key)
{
    return makeAlert(Priority::Warning, std::move(key));
}

// Pop and discard one alert; fails the test if the queue was unexpectedly empty.
void popOne(AlertArbiter& arbiter)
{
    Alert out;
    REQUIRE(arbiter.waitPop(out));
}

} // namespace

TEST_CASE("arbiter: a description needs two stable frames")
{
    AlertArbiter arbiter; // descriptionStableFrames = 2 by default
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100)));
    CHECK(arbiter.queuedCount() == 1);
}

TEST_CASE("arbiter: a warning fires on the first frame")
{
    AlertArbiter arbiter;
    CHECK(arbiter.submit(warning("w1"), t(0)));
    CHECK(arbiter.queuedCount() == 1);
}

TEST_CASE("arbiter: changing the scene restarts the stability count")
{
    AlertArbiter arbiter;
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));   // count 1
    CHECK_FALSE(arbiter.submit(description("d2"), t(100))); // count resets to 1
    CHECK_FALSE(arbiter.submit(description("d1"), t(200))); // resets again
    CHECK(arbiter.queuedCount() == 0);
}

TEST_CASE("arbiter: the same key is not queued twice while pending")
{
    AlertArbiter arbiter;
    CHECK(arbiter.submit(warning("w1"), t(0)));
    CHECK_FALSE(arbiter.submit(warning("w1"), t(100)));
    CHECK(arbiter.queuedCount() == 1);
}

TEST_CASE("arbiter: clearCandidate breaks the stability run")
{
    AlertArbiter arbiter;
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));   // count 1
    arbiter.clearCandidate();                                // scene vanished
    CHECK_FALSE(arbiter.submit(description("d1"), t(200)));  // count restarts at 1
    CHECK(arbiter.submit(description("d1"), t(300)));        // count 2 => queued
}

TEST_CASE("arbiter: pops the highest priority first")
{
    AlertArbiter arbiter;
    // Queue a description, then a warning; the warning must come out first.
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100)));
    CHECK(arbiter.submit(warning("w1"), t(200)));

    Alert out;
    REQUIRE(arbiter.waitPop(out));
    CHECK(out.priority == Priority::Warning);
    CHECK(out.dedupKey == "w1");
}

TEST_CASE("arbiter: per-key cooldown suppresses a repeat, then allows it")
{
    AlertArbiter arbiter;
    CHECK(arbiter.submit(warning("w1"), t(0)));
    popOne(arbiter); // hand the warning to the speech worker (speaking = w1)
    arbiter.finishSpeaking(t(50));

    CHECK_FALSE(arbiter.submit(warning("w1"), t(1000)));  // within 4 s
    CHECK_FALSE(arbiter.submit(warning("w1"), t(4040)));  // still < 4050
    CHECK(arbiter.submit(warning("w1"), t(4100)));        // > 50 + 4000
}

TEST_CASE("arbiter: global gap delays a different description")
{
    ArbiterConfig config;
    config.globalMinGap = std::chrono::milliseconds(600);
    AlertArbiter arbiter(config);

    // Speak and finish d1 at t=0.
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100)));
    popOne(arbiter);
    arbiter.finishSpeaking(t(0));

    // d2 is a new key (no cooldown) but the gap still applies.
    CHECK_FALSE(arbiter.submit(description("d2"), t(100))); // stable 1
    CHECK_FALSE(arbiter.submit(description("d2"), t(200))); // stable 2, but gap < 600
    CHECK(arbiter.submit(description("d2"), t(700)));       // gap elapsed
}

TEST_CASE("arbiter: a warning bypasses the global gap")
{
    ArbiterConfig config;
    config.globalMinGap = std::chrono::milliseconds(600);
    AlertArbiter arbiter(config);

    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100)));
    popOne(arbiter);
    arbiter.finishSpeaking(t(0));

    // Only 100 ms after the previous utterance, but warnings skip the gap.
    CHECK(arbiter.submit(warning("w1"), t(100)));
}

TEST_CASE("arbiter: a higher-priority warning preempts a description")
{
    AlertArbiter arbiter;
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100)));

    Alert out;
    REQUIRE(arbiter.waitPop(out));
    CHECK(arbiter.speaking());
    CHECK(arbiter.speakingPriority() == Priority::Description);
    CHECK_FALSE(arbiter.interruptFlag().load());

    // A warning arrives while the description is being spoken.
    CHECK(arbiter.submit(warning("w1"), t(200)));
    CHECK(arbiter.interruptFlag().load());

    // The TTS would observe the flag and stop; the worker then records the end.
    arbiter.finishSpeaking(t(250));
    REQUIRE(arbiter.waitPop(out));
    CHECK(out.priority == Priority::Warning);
    CHECK_FALSE(arbiter.interruptFlag().load()); // cleared for the new utterance
}

TEST_CASE("arbiter: an equal-priority alert does not preempt")
{
    AlertArbiter arbiter;
    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100)));
    popOne(arbiter); // speaking d1 (Description)

    CHECK_FALSE(arbiter.submit(description("d2"), t(200))); // stable 1
    CHECK(arbiter.submit(description("d2"), t(300)));       // stable 2, queued behind
    CHECK_FALSE(arbiter.interruptFlag().load());
}

TEST_CASE("arbiter: a lower-priority alert does not preempt")
{
    AlertArbiter arbiter;
    CHECK(arbiter.submit(warning("w1"), t(0)));
    popOne(arbiter); // speaking w1 (Warning)

    CHECK_FALSE(arbiter.submit(description("d1"), t(100))); // stable 1
    CHECK(arbiter.submit(description("d1"), t(200)));       // stable 2, queued behind
    CHECK_FALSE(arbiter.interruptFlag().load());
}

TEST_CASE("arbiter: a description cannot evict a pending warning")
{
    ArbiterConfig config;
    config.queueCapacity = 1;
    AlertArbiter arbiter(config);

    CHECK(arbiter.submit(warning("w1"), t(0)));              // queue full (warning)
    CHECK_FALSE(arbiter.submit(description("d1"), t(100)));  // stable 1
    CHECK_FALSE(arbiter.submit(description("d1"), t(200)));  // would evict warning -> refused
    CHECK(arbiter.queuedCount() == 1);
    Alert out;
    REQUIRE(arbiter.waitPop(out));
    CHECK(out.dedupKey == "w1"); // the warning survived
}

TEST_CASE("arbiter: a higher-priority alert evicts a pending description")
{
    ArbiterConfig config;
    config.queueCapacity = 1;
    AlertArbiter arbiter(config);

    CHECK_FALSE(arbiter.submit(description("d1"), t(0)));
    CHECK(arbiter.submit(description("d1"), t(100))); // queue full (description)
    CHECK(arbiter.submit(warning("w1"), t(200)));     // evicts the description
    CHECK(arbiter.queuedCount() == 1);

    Alert out;
    REQUIRE(arbiter.waitPop(out));
    CHECK(out.dedupKey == "w1");
}

TEST_CASE("arbiter: close() unblocks the consumer and stops submissions")
{
    AlertArbiter arbiter;
    CHECK(arbiter.submit(warning("w1"), t(0)));

    std::atomic<bool> consumerSawShutdown{false};
    std::thread consumer([&arbiter, &consumerSawShutdown] {
        Alert out;
        // waitPop() returns false once close() is observed (whether or not a queued
        // alert was popped first); the loop must then exit.
        while (arbiter.waitPop(out)) {
            arbiter.finishSpeaking(TimePoint{});
        }
        consumerSawShutdown.store(true);
    });

    arbiter.close();
    consumer.join();

    CHECK(consumerSawShutdown.load());
    CHECK_FALSE(arbiter.submit(warning("w2"), t(1000)));
}
