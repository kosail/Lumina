// ---------------------------------------------------------------------------
// VL53L0X time-of-flight proximity driver (front sensor, FR-10 / INV-013).
//
// Talks to the sensor over the kernel i2c-dev interface and reads single-shot
// range measurements; no third-party library is used (INV-022/INV-033). The
// initialisation and ranging sequences follow ST's VL53L0X API (UM2039), as
// reproduced by the widely used open-source Pololu VL53L0X driver
// (https://github.com/pololu/vl53l0x-arduino, consulted 2026-09-20).
//
// The sensor is expected at 0x29 on /dev/i2c-1 with XSHUT released (the wiring
// verified in docs/PROXIMITY.md Phase B). XSHUT is not driven by this driver.
// ---------------------------------------------------------------------------

#pragma once

#include <memory>

#include "sensors/proximity.hpp"

namespace lumina::sensors {

class Vl53l0xProximity final : public IProximitySensor {
public:
    Vl53l0xProximity();
    ~Vl53l0xProximity() override;

    // Owns an open file descriptor: not copyable (Rule of 0 via unique_ptr).
    Vl53l0xProximity(const Vl53l0xProximity&) = delete;
    Vl53l0xProximity& operator=(const Vl53l0xProximity&) = delete;

    [[nodiscard]] bool init() override;
    [[nodiscard]] std::optional<ProximityReading> read() override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;  // RAII: closes the bus in the destructor
};

}  // namespace lumina::sensors
