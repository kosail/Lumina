// ---------------------------------------------------------------------------
// Runtime readiness logic for the companion agent (FR-11).
//
// The agent reports an additive `initializing` flag: `lumina.service` is active
// (`systemctl is-active`) but the runtime has not yet reported a fresh running
// status (models + the BlueALSA sink wait can take ~18-60 s). The decision is a
// small state machine over "liveness" (what we last saw from systemctl) and
// "readiness" (a fresh status file that says running).
//
// Keeping this pure — no sockets, no clock, no processes — makes it fully
// unit-testable; the Agent supplies the probe results and times it. See
// docs/API_CONTRACT.md §4.1/§4.4 and docs/COMPANION.md §4.
// ---------------------------------------------------------------------------

#pragma once

#include <chrono>

namespace lumina::agent {

// What the agent last learned from `systemctl is-active lumina`.
enum class RuntimeLiveness {
    Unknown,   // not probed yet (or state invalidated)
    Active,    // systemctl says the unit is active (may still be initializing)
    Inactive,  // systemctl says the unit is not active
};

// How often `systemctl is-active` may be spawned, per liveness state.
//
// While starting we probe often so readiness/failure is noticed quickly. Once the
// unit is known down we still probe occasionally: an external start (systemd
// `Restart=on-failure`, a manual `systemctl start`, or the runtime restart that
// follows a camera enrollment) must not be missed forever (F1). The inactive
// interval bounds idle systemctl spawns to ~2/min on the Pi Zero.
struct RuntimeProbeIntervals {
    std::chrono::seconds active{5};
    std::chrono::seconds inactive{30};
};

// True when a probe is due for this liveness after `sinceLastProbe`. Unknown
// always probes (we have no reading yet).
[[nodiscard]] bool runtimeProbeDue(RuntimeLiveness liveness, std::chrono::seconds sinceLastProbe,
                                   const RuntimeProbeIntervals& intervals = {});

// One readiness evaluation. Pure data: `probeDue` and `activeProbe` are supplied
// by the caller (`activeProbe` only matters when `probeDue` is true).
struct RuntimeReadinessInput {
    RuntimeLiveness liveness = RuntimeLiveness::Unknown;
    bool ready = false;       // fresh status file AND running (the start finished)
    bool probeDue = false;    // the caller decided a probe is due
    bool activeProbe = false; // result of `systemctl is-active` when probeDue
};

struct RuntimeReadiness {
    RuntimeLiveness liveness = RuntimeLiveness::Unknown;  // state to store
    bool initializing = false;                            // value to report
};

// Apply one evaluation. A ready runtime is Active and not initializing; otherwise
// a due probe updates liveness from `activeProbe`, and the runtime is initializing
// exactly while liveness is Active.
[[nodiscard]] RuntimeReadiness evaluateRuntimeReadiness(const RuntimeReadinessInput& input);

}  // namespace lumina::agent
