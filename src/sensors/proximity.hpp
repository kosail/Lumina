// ---------------------------------------------------------------------------
// Proximity sensor abstraction (front VL53L0X, FR-10 / INV-013 / INV-033).
//
// The pipeline depends only on this interface, never on the concrete I2C
// driver, so the host build and any build without the sensor behave exactly
// like the camera-only beta (NullProximitySensor). The factory in proximity.cpp
// returns the real driver only when LUMINA_ENABLE_PROXIMITY is compiled in.
//
// C++ note (for Java readers): an abstract base class with pure virtual methods
// is C++'s interface; `= 0` makes it abstract (Java's `abstract`/interface). The
// `virtual` destructor is mandatory so deleting through a base pointer runs the
// derived destructor (Java does this automatically).
// ---------------------------------------------------------------------------

#pragma once

#include <memory>
#include <optional>

#include "core/config.hpp"

namespace lumina::sensors {

// One distance sample from the sensor. `meters` is only meaningful when `valid`
// is true; an invalid sample means "no trustworthy target" (out of range, too
// close, or an I2C error), not "zero metres".
struct ProximityReading {
    float meters = 0.0F;
    bool valid = false;
};

// A single forward-facing time-of-flight distance source. `read()` is called
// from the proximity thread at the configured poll rate and must be non-blocking
// apart from the sensor's own measurement time.
class IProximitySensor {
public:
    virtual ~IProximitySensor() = default;

    // Open the bus and configure the sensor. Returns false (after logging) on
    // failure; the caller then treats proximity as unavailable.
    [[nodiscard]] virtual bool init() = 0;

    // Latest distance, or nullopt when no valid reading is available right now.
    [[nodiscard]] virtual std::optional<ProximityReading> read() = 0;
};

// Does nothing and always reports "no data". Used on the host, when the feature
// is compiled out, or when proximity is disabled in config (INV-033).
class NullProximitySensor final : public IProximitySensor {
public:
    [[nodiscard]] bool init() override { return true; }
    [[nodiscard]] std::optional<ProximityReading> read() override { return std::nullopt; }
};

// Build the configured sensor: the real VL53L0X driver when proximity support is
// compiled in and enabled, otherwise a NullProximitySensor. Never returns null.
[[nodiscard]] std::unique_ptr<IProximitySensor> makeProximitySensor(const core::Config& config);

}  // namespace lumina::sensors
