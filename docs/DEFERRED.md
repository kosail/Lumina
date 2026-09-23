# docs/DEFERRED.md — Deferred work registry

> **Append-only list of features and decisions intentionally postponed.**
> An item recorded here is **not** to be implemented until the user explicitly confirms resuming
> it, in writing. Deferring is a decision, so every addition is also logged in `CHANGELOG.md`
> (INV-070). Each item keeps enough context (what, why, the reviewed design, the blockers, the
> prerequisites) that a future agent does **not** have to re-derive the analysis from scratch.
>
> Status legend:
> - **DEFERRED** — intentionally postponed; resume only on explicit user confirmation.
> - **BLOCKED** — waiting on an external dependency or a user action.
> - **DONE** — resumed and completed; leave the entry for history (do not delete).

---

## D-001 — Physical volume buttons (GPIO17 / GPIO27) — **DEFERRED (indefinite)**

- **Deferred:** 2026-09-22, by user decision (schedule constraints). Resume **only** on explicit
  manual confirmation; see `CHG-0095`.
- **Origin:** teammate prototype (originally `volume_control/src/main.c`), reviewed but **not
  merged**. Archived verbatim at [`reference/volume_control_main.c`](reference/volume_control_main.c).
- **What it is:** two physical push-buttons that raise/lower the earbud volume by 5% per press,
  with press-and-hold repeat. The value for a blind user is hands-free volume control — no need to
  pull out the phone or use the companion app.
- **Why it is worth having (if resumed):** genuine accessibility win; complements the app's
  volume control.

### Review findings (do not re-derive)

The prototype does not work on this device and does not fit the architecture. Three hard blockers:

1. **Wrong mixer (functional).** The prototype calls
   `amixer -D pulse sset Master 5%+`. This project routes audio to **BlueALSA**, and the agent
   already does it correctly: device **`bluealsa`** (`src/agent/amixer.hpp:39`) with the control
   name **discovered dynamically** (`src/agent/amixer.cpp:77-84`, e.g. `"TWS A2DP"`), never
   hard-coded `Master` (INV-014). Pi OS Lite has no PulseAudio in the audio path, so the buttons
   would print `SUBIR`/`BAJAR` but change nothing (failures are swallowed by `waitpid(pid, NULL, 0)`).
2. **libgpiod v1 API (build).** The prototype uses the v1 API (`gpiod_chip_get_line`,
   `gpiod_line_request`, `gpiod_line_bulk`, `gpiod_line_event_*`). Upstream `libgpiod v2.0` removed
   the v1 data model ("breaks compatibility with the v1.6.x series"), and the target OS
   (Debian 13 "trixie", INV-025) ships v2.x — `docs/PROXIMITY.md:111` already notes the v1/v2
   syntax difference. It will not compile; it must be rewritten against the v2 API.
3. **GPIO pin conflict (hardware).** The prototype uses **GPIO17** and **GPIO27**, which
   `docs/PROXIMITY.md` reserves for the front/rear proximity `XSHUT` (`:76`, `:206`; INV-075).
   **User confirmation (2026-09-22):** in this build `XSHUT` is **not wired** and the rear sensor
   is not built, so GPIO17/GPIO27 are currently free — but this must be **re-checked before
   resuming** (wiring the front `XSHUT` would re-claim GPIO17).

Secondary: not integrated (no `CMakeLists.txt`, no systemd unit, no tests; C with file-scope
globals vs the repo's C++23 + interface/DI rules); would be a **second process owning the same
mixer** as `lumina_agent`, bypassing `m_volumeMutex` (`src/agent/agent.hpp:142`); GPIO permissions
unaddressed; ignores the debounce return; no handling for "no earbuds connected".

The blocking mechanics themselves were reasonable: blocking `event_wait_bulk(NULL, …)` (0% CPU),
falling-edge-only on pull-up buttons, kernel debounce, `SIGINT`/`SIGTERM` with EINTR handling, and
`fork` + `execlp` without a shell.

### Approved design, if resumed (Option A)

Fold the buttons into `lumina_agent` (same process as the mixer), reusing `AmixerVolume` +
`ICommandRunner` + `m_volumeMutex`:

- `src/agent/button_input.{hpp,cpp}` — host-buildable: `ButtonAction`/`ButtonEvent`,
  `ButtonRepeatTiming`, pure `clampVolumePercent`, `volumeStepsDue`, `nextButtonPoll`, and the
  `IButtonInput` interface (unit-tested pure logic, mirroring `runtime_state.hpp`).
- `src/agent/gpio_button_input.{hpp,cpp}` — libgpiod **v2** implementation, compiled only under a
  new `LUMINA_ENABLE_BUTTONS` CMake option (`#ifdef LUMINA_HAS_BUTTONS`); requests both offsets in
  one line-request with pull-up bias, both edges, 10 ms debounce; polls the request fd.
- `Agent` gains an injected non-owning `IButtonInput*` and a `buttonLoop(std::stop_token)` thread:
  one step on press, repeats while held, stop on release; unknown mixer is logged and ignored;
  GPIO open failure is non-fatal.
- Behavior: **5% steps, press-and-hold repeat (≈400 ms initial, ≈150 ms interval), no mute.**
- Run as the **same unprivileged user as `lumina_agent`** (`lumina`).

### Prerequisites to resume

- `sudo apt install libgpiod-dev` on the Pi, then re-run `scripts/1-sync_sysroot.sh` (the cross
  sysroot needs `gpiod.h`, `libgpiod.so`, `libgpiod.pc`; the runtime needs `libgpiod.so.2`, already
  present via the `gpiod` package). Verify `pkg-config --modversion libgpiod` ≥ 2.0 (INV-001).
- GPIO access for the agent user: verify `ls -l /dev/gpiochip0`; if `root:gpio 0660`, add
  `SupplementaryGroups=gpio` to `scripts/lumina-agent.service`, otherwise a udev rule / group
  membership.
- Confirm the pins are still free (see blocker 3).
- New dependency **libgpiod** requires INV-022 approval — **already given by the user (2026-09-22)**.
- Add `"LUMINA_ENABLE_BUTTONS": "ON"` to the `aarch64` preset only (host build stays libgpiod-free).

### Cross-references

- `CHANGELOG.md` — `CHG-0095` (this deferral).
- `RAW_PLAN.md` §5, nice-to-have item 8.
- `docs/PROXIMITY.md` — pin reservations (INV-075).
- `docs/COMPANION.md` — the agent this would extend.
