// ---------------------------------------------------------------------------
// Proximity sensor factory. See proximity.hpp.
//
// Kept in its own translation unit so the pipeline (and the host tests) never
// pull in the I2C driver unless the feature is compiled in (INV-022/INV-033).
// ---------------------------------------------------------------------------

#include "sensors/proximity.hpp"

#if defined(LUMINA_HAS_PROXIMITY)
#include "sensors/vl53l0x_proximity.hpp"
#endif

namespace lumina::sensors {

std::unique_ptr<IProximitySensor> makeProximitySensor(const core::Config& config)
{
#if defined(LUMINA_HAS_PROXIMITY)
    if (config.proximityEnabled) {
        return std::make_unique<Vl53l0xProximity>();
    }
#else
    // Unused in the Null build; cast keeps -Wunused-parameter quiet.
    (void)config;
#endif
    return std::make_unique<NullProximitySensor>();
}

}  // namespace lumina::sensors
