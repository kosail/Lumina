// ---------------------------------------------------------------------------
// Greeting policy for recognized people (FR-03).
//
// Pure, hardware-free state machine (AGENTS §9): given one recognition result per
// face-worker pulse, decides *when* to greet a person by name. Two guards:
//   - stability: the same name must be recognized for N consecutive observations
//     so a single noisy frame cannot announce someone (FR-03.2), and
//   - per-person cooldown: the same person is not greeted again for a while, so a
//     person lingering in view is announced once, not every pulse.
// Kept out of Pipeline so the policy is unit-tested without threads.
// ---------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/time.hpp"

namespace lumina::app {

// Tunables. Defaults match the Day-4 decisions (3 stable frames, 30 s cooldown).
struct FaceGreeterConfig {
    int stableFrames = 3;
    std::chrono::milliseconds cooldown{30000};
};

class FaceGreeter {
public:
    explicit FaceGreeter(FaceGreeterConfig config = {});

    // Feed one recognition observation. `name` is the recognized enrolled person,
    // or nullopt when this pulse found no confident match. Returns the name to
    // greet exactly once: when it has been seen for `stableFrames` consecutive
    // observations and is past its cooldown. A nullopt observation breaks the
    // stability run (the person left the frame).
    [[nodiscard]] std::optional<std::string> observe(const std::optional<std::string>& name,
                                                     core::TimePoint now);

    // Forget the current stability run without touching the cooldowns. Used when
    // the face path is disabled or reset.
    void reset() noexcept;

private:
    FaceGreeterConfig m_config;
    std::string m_candidate;  // name currently being counted
    int m_count = 0;          // consecutive observations of m_candidate
    // Last time each person was greeted, for the per-person cooldown.
    std::unordered_map<std::string, core::TimePoint> m_lastGreeted;
};

}  // namespace lumina::app
