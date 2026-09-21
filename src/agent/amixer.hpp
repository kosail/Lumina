// ---------------------------------------------------------------------------
// Volume control via the BlueALSA ALSA mixer (FR-11).
//
// The runtime does not implement volume; the agent drives `amixer -D bluealsa`,
// which the on-device probe confirmed works (`amixer -D bluealsa sset 'TWS A2DP'
// 100%`). The control name is derived from the Bluetooth device name, so it is
// discovered at runtime and never hardcoded. Parsers are pure and unit-tested.
// ---------------------------------------------------------------------------

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/command_runner.hpp"

namespace lumina::agent {

// The ALSA mixer control names listed by `amixer -D <device> scontrols`, in order.
[[nodiscard]] std::vector<std::string> parseAmixerControls(std::string_view output);

// Highest playback percentage found in `amixer sget` output, or nullopt.
[[nodiscard]] std::optional<int> parseAmixerVolume(std::string_view sgetOutput);

// True when the sget output shows a channel switched off ("[off]").
[[nodiscard]] bool parseAmixerMuted(std::string_view sgetOutput);

// Reads and sets the A2DP playback volume through ICommandRunner.
class AmixerVolume {
public:
    struct State {
        int percent = -1;   // -1 = unknown
        bool muted = false;
    };

    // `device` is the ALSA device ("bluealsa"); `runner` must outlive this object.
    explicit AmixerVolume(ICommandRunner& runner, std::string device = "bluealsa");

    // Find the mixer control by listing scontrols and choosing the first one
    // (there is a single A2DP sink, INV-014). Returns false when none is found.
    bool discoverControl();

    [[nodiscard]] State read();
    bool setPercent(int percent);
    bool setMuted(bool muted);

    [[nodiscard]] const std::string& control() const noexcept { return m_control; }

private:
    ICommandRunner& m_runner;
    std::string m_device;
    std::string m_control;  // resolved on first use
};

}  // namespace lumina::agent
