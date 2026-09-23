// ---------------------------------------------------------------------------
// Control-bind retry policy for the companion agent (FR-11).
//
// At boot the hotspot gateway address may not exist yet, so binding the TCP control
// socket to it fails with EADDRNOTAVAIL ("Cannot assign requested address"). The
// agent retries for a bounded window instead of exiting and waiting for a systemd
// restart, so the app can connect as soon as the AP is up (CHG-0094).
//
// The decision is a pure function so it is unit-tested (AGENTS.md §9); the loop and
// the sleeping live in Agent::start().
// ---------------------------------------------------------------------------

#pragma once

namespace lumina::agent {

// True while another control-bind attempt is allowed. `attempt` is 1-based: after
// `attempt` attempts have been made, retry only if attempt < maxAttempts. A
// maxAttempts of 1 (or less) means "try once, no retries".
[[nodiscard]] inline bool shouldRetryControlBind(int attempt, int maxAttempts) noexcept
{
    return attempt < maxAttempts;
}

}  // namespace lumina::agent
