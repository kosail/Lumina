// ---------------------------------------------------------------------------
// FaceGreeter implementation. See face_greeter.hpp.
// ---------------------------------------------------------------------------

#include "app/face_greeter.hpp"

#include <utility>

namespace lumina::app {

FaceGreeter::FaceGreeter(FaceGreeterConfig config) : m_config(std::move(config))
{
    if (m_config.stableFrames < 1) {
        m_config.stableFrames = 1;
    }
    if (m_config.cooldown.count() < 0) {
        m_config.cooldown = std::chrono::milliseconds::zero();  // a negative cooldown is meaningless
    }
}

std::optional<std::string> FaceGreeter::observe(const std::optional<std::string>& name,
                                                core::TimePoint now)
{
    if (!name.has_value() || name->empty()) {
        // No confident match this pulse: the stability run must restart from zero.
        m_candidate.clear();
        m_count = 0;
        return std::nullopt;
    }

    if (*name != m_candidate) {
        m_candidate = *name;
        m_count = 1;
    } else {
        ++m_count;
    }

    if (m_count < m_config.stableFrames) {
        return std::nullopt;  // not seen long enough yet
    }

    const auto last = m_lastGreeted.find(*name);
    if (last != m_lastGreeted.end() && (now - last->second) < m_config.cooldown) {
        return std::nullopt;  // greeted recently; let them settle
    }

    m_lastGreeted[*name] = now;
    return m_candidate;
}

void FaceGreeter::reset() noexcept
{
    m_candidate.clear();
    m_count = 0;
}

}  // namespace lumina::app
