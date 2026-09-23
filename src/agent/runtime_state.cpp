// ---------------------------------------------------------------------------
// Runtime readiness logic implementation. See agent/runtime_state.hpp.
// ---------------------------------------------------------------------------

#include "agent/runtime_state.hpp"

namespace lumina::agent {

bool runtimeProbeDue(RuntimeLiveness liveness, std::chrono::seconds sinceLastProbe,
                     const RuntimeProbeIntervals& intervals)
{
    switch (liveness) {
    case RuntimeLiveness::Unknown:
        return true;  // no reading yet
    case RuntimeLiveness::Active:
        return sinceLastProbe >= intervals.active;
    case RuntimeLiveness::Inactive:
        return sinceLastProbe >= intervals.inactive;
    }
    return true;
}

RuntimeReadiness evaluateRuntimeReadiness(const RuntimeReadinessInput& input)
{
    if (input.ready) {
        // A fresh running status means the start finished.
        return {RuntimeLiveness::Active, false};
    }

    RuntimeLiveness liveness = input.liveness;
    if (input.probeDue) {
        liveness = input.activeProbe ? RuntimeLiveness::Active : RuntimeLiveness::Inactive;
    }
    return {liveness, liveness == RuntimeLiveness::Active};
}

}  // namespace lumina::agent
