// ---------------------------------------------------------------------------
// AmixerVolume implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/amixer.hpp"

#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

#include "core/logging.hpp"

namespace lumina::agent {

std::vector<std::string> parseAmixerControls(std::string_view output)
{
    std::vector<std::string> controls;
    std::size_t position = 0;
    while (position < output.size()) {
        const std::size_t end = output.find('\n', position);
        const std::string_view line =
            output.substr(position, end == std::string_view::npos ? std::string_view::npos
                                                                  : end - position);
        position = end == std::string_view::npos ? output.size() : end + 1;

        // Lines look like: "Simple mixer control 'TWS A2DP',0".
        const std::size_t quote = line.find('\'');
        if (quote == std::string_view::npos) {
            continue;
        }
        const std::size_t close = line.find('\'', quote + 1);
        if (close == std::string_view::npos) {
            continue;
        }
        controls.emplace_back(line.substr(quote + 1, close - quote - 1));
    }
    return controls;
}

std::optional<int> parseAmixerVolume(std::string_view sgetOutput)
{
    // Every channel prints "[NN%]"; the highest is the effective volume.
    int best = -1;
    std::size_t position = 0;
    while ((position = sgetOutput.find('[', position)) != std::string_view::npos) {
        const std::size_t close = sgetOutput.find(']', position);
        if (close == std::string_view::npos) {
            break;
        }
        const std::string_view inside = sgetOutput.substr(position + 1, close - position - 1);
        if (!inside.empty() && inside.back() == '%') {
            const std::string_view digits = inside.substr(0, inside.size() - 1);
            int value = 0;
            const std::from_chars_result result =
                std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (result.ec == std::errc{} && result.ptr == digits.data() + digits.size()) {
                best = value > best ? value : best;
            }
        }
        position = close + 1;
    }
    return best >= 0 ? std::optional<int>(best) : std::nullopt;
}

bool parseAmixerMuted(std::string_view sgetOutput)
{
    return sgetOutput.find("[off]") != std::string_view::npos;
}

AmixerVolume::AmixerVolume(ICommandRunner& runner, std::string device)
    : m_runner(runner), m_device(std::move(device))
{
}

bool AmixerVolume::discoverControl()
{
    const CommandResult result = m_runner.run({"amixer", "-D", m_device, "scontrols"});
    const std::vector<std::string> controls = parseAmixerControls(result.output);
    if (controls.empty()) {
        return false;
    }
    m_control = controls.front();  // single A2DP sink (INV-014)
    LUMINA_LOG_INFO("agent: volume control '{}' on '{}'", m_control, m_device);
    return true;
}

AmixerVolume::State AmixerVolume::read()
{
    State state;
    if (m_control.empty() && !discoverControl()) {
        return state;  // no earbuds / no mixer: leave unknown
    }
    const CommandResult result = m_runner.run({"amixer", "-D", m_device, "sget", m_control});
    if (const std::optional<int> percent = parseAmixerVolume(result.output)) {
        state.percent = *percent;
    }
    state.muted = parseAmixerMuted(result.output);
    return state;
}

bool AmixerVolume::setPercent(int percent)
{
    if (percent < 0 || percent > 100) {
        return false;
    }
    if (m_control.empty() && !discoverControl()) {
        return false;
    }
    const CommandResult result =
        m_runner.run({"amixer", "-D", m_device, "sset", m_control, std::to_string(percent) + "%"});
    return result.exitCode == 0;
}

bool AmixerVolume::setMuted(bool muted)
{
    if (m_control.empty() && !discoverControl()) {
        return false;
    }
    const CommandResult result = m_runner.run(
        {"amixer", "-D", m_device, "sset", m_control, muted ? "mute" : "unmute"});
    return result.exitCode == 0;
}

}  // namespace lumina::agent
