// ---------------------------------------------------------------------------
// Host power control.
//
// The runtime must be able to shut the device down when it cannot become
// functional — specifically when the Bluetooth audio sink never appears (FR-06.1).
// This is the only place that shells out to the OS; it is kept tiny and
// non-throwing so a caller can simply exit the process when power-off is not
// permitted (e.g. a manual run without the prepared sudoers rule).
// ---------------------------------------------------------------------------

#pragma once

namespace lumina::core {

// Ask the OS to power the whole machine off. Returns true when the power-off
// command was issued successfully, false when it failed or is unavailable (the
// reason is logged). Never throws and never blocks indefinitely.
//
// Implementation: `systemctl poweroff --no-wall`, run directly when already root
// (the systemd service case) or through `sudo -n` otherwise. `scripts/
// 9-setup_autostart.sh` installs a narrow sudoers rule so the unprivileged
// developer user can also power off; without it, `sudo -n` fails fast and the
// caller falls back to stopping just the runtime.
[[nodiscard]] bool requestPowerOff();

}  // namespace lumina::core
