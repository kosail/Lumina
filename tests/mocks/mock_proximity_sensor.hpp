// ---------------------------------------------------------------------------
// Mock proximity sensor for host tests (AGENTS §9). Returns a scripted queue of
// readings so the pipeline's proximity thread can be driven deterministically.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <deque>
#include <optional>

#include "sensors/proximity.hpp"

namespace lumina::tests {

class MockProximitySensor final : public sensors::IProximitySensor {
public:
    [[nodiscard]] bool init() override
    {
        ++initCalls;
        return initResult;
    }

    [[nodiscard]] std::optional<sensors::ProximityReading> read() override
    {
        ++readCalls;
        if (m_readings.empty()) {
            return std::nullopt;
        }
        const sensors::ProximityReading reading = m_readings.front();
        m_readings.pop_front();
        return reading;
    }

    // Queue one reading (valid or not). `meters` <= 0 means "invalid sample".
    void push(float meters)
    {
        sensors::ProximityReading reading;
        reading.meters = meters;
        reading.valid = meters > 0.0F;
        m_readings.push_back(reading);
    }

    bool initResult = true;
    int initCalls = 0;
    int readCalls = 0;

private:
    std::deque<sensors::ProximityReading> m_readings;
};

}  // namespace lumina::tests
