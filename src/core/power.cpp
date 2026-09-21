// ---------------------------------------------------------------------------
// Host power control implementation (see core/power.hpp for the contract).
// ---------------------------------------------------------------------------

#include "core/power.hpp"

#include <cstdlib>   // std::system
#include <unistd.h>  // geteuid()

#include "core/logging.hpp"

namespace lumina::core {

bool requestPowerOff()
{
    // When we are root (the autostart service runs as root by default) no
    // privilege escalation is needed. Otherwise `sudo -n` runs non-interactively:
    // if the sudoers rule is missing it fails immediately instead of hanging on a
    // password prompt, and the caller just stops the runtime.
    const bool isRoot = (::geteuid() == 0);
    const char* command =
        isRoot ? "systemctl poweroff --no-wall" : "sudo -n systemctl poweroff --no-wall";

    LUMINA_LOG_INFO("requesting power-off: {}", command);
    const int rc = std::system(command);
    if (rc == 0) {
        return true;
    }
    LUMINA_LOG_ERROR("power-off command failed (rc={})", rc);
    return false;
}

}  // namespace lumina::core
