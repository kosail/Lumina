// ---------------------------------------------------------------------------
// VL53L0X driver implementation. See vl53l0x_proximity.hpp.
//
// The initialisation and single-shot ranging sequences are a C++ port of ST's
// VL53L0X API (UM2039) as reproduced by the Pololu VL53L0X driver
// (https://github.com/pololu/vl53l0x-arduino, consulted 2026-09-20). Only the
// subset needed for single-shot ranging is included; continuous mode, GPIO
// interrupts, threshold windows and XSHUT control are not used.
// ---------------------------------------------------------------------------

#include "sensors/vl53l0x_proximity.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <optional>
#include <sys/ioctl.h>
#include <unistd.h>

#include "core/logging.hpp"

namespace lumina::sensors {

namespace {

// --- VL53L0X register map (ST API / UM2039) --------------------------------
constexpr std::uint8_t kRegSysRangeStart = 0x00;
constexpr std::uint8_t kRegSystemSequenceConfig = 0x01;
constexpr std::uint8_t kRegSystemInterruptConfigGpio = 0x0A;
constexpr std::uint8_t kRegSystemInterruptClear = 0x0B;
constexpr std::uint8_t kRegResultInterruptStatus = 0x13;
constexpr std::uint8_t kRegResultRangeStatus = 0x14;
constexpr std::uint8_t kRegFinalRangeMinCountRate = 0x44;
constexpr std::uint8_t kRegMsrcConfigControl = 0x60;
constexpr std::uint8_t kRegGpioHvMuxActiveHigh = 0x84;
constexpr std::uint8_t kRegVhvConfigPadSclSdaExtsupHv = 0x89;
constexpr std::uint8_t kRegGlobalConfigSpadEnablesRef0 = 0xB0;
constexpr std::uint8_t kRegGlobalConfigRefEnStartSelect = 0xB6;
constexpr std::uint8_t kRegModelId = 0xC0;
constexpr std::uint8_t kRegDynamicSpadNumRequestedRefSpad = 0x4E;
constexpr std::uint8_t kRegDynamicSpadRefEnStartOffset = 0x4F;
constexpr std::uint8_t kRegMsrcConfigTimeoutMacrop = 0x46;
constexpr std::uint8_t kRegPreRangeConfigVcselPeriod = 0x50;
constexpr std::uint8_t kRegPreRangeConfigTimeoutMacropHi = 0x51;
constexpr std::uint8_t kRegFinalRangeConfigVcselPeriod = 0x70;
constexpr std::uint8_t kRegFinalRangeConfigTimeoutMacropHi = 0x71;

constexpr const char* kBusPath = "/dev/i2c-1";
constexpr std::uint8_t kSensorAddress = 0x29;  // INV-075: front sensor at 0x29
constexpr std::uint8_t kExpectedModelId = 0xEE;

// A single-shot measurement normally answers in ~33 ms; 100 ms is a generous
// guard against a hung device before we give up for this poll.
constexpr std::int64_t kRangeTimeoutMs = 100;
// Documented usable range; readings outside it mean "no trustworthy target".
constexpr int kMinRangeMm = 30;
constexpr int kMaxRangeMm = 2000;

constexpr std::uint8_t kModeSingleShot = 0x01;  // SYSRANGE_MODE_SINGLESHOT
constexpr std::uint8_t kModeStartStop = 0x01;   // SYSRANGE_MODE_START_STOP

struct SequenceStepEnables {
    bool tcc = false;
    bool dss = false;
    bool msrc = false;
    bool preRange = false;
    bool finalRange = false;
};

struct SequenceStepTimeouts {
    std::uint8_t preRangeVcselPeriodPclks = 0;
    std::uint16_t msrcDssTccMclks = 0;
    std::uint32_t msrcDssTccUs = 0;
    std::uint16_t preRangeMclks = 0;
    std::uint32_t preRangeUs = 0;
    std::uint8_t finalRangeVcselPeriodPclks = 0;
    std::uint16_t finalRangeMclks = 0;
    std::uint32_t finalRangeUs = 0;
};

// ST "DefaultTuningSettings" (vl53l0x_tuning.h): register/value pairs applied
// verbatim during init.
struct RegValue {
    std::uint8_t reg;
    std::uint8_t value;
};

constexpr RegValue kTuningSettings[] = {
    {0xFF, 0x01}, {0x00, 0x00},
    {0xFF, 0x00}, {0x09, 0x00}, {0x10, 0x00}, {0x11, 0x00},
    {0x24, 0x01}, {0x25, 0xFF}, {0x75, 0x00},
    {0xFF, 0x01}, {0x4E, 0x2C}, {0x48, 0x00}, {0x30, 0x20},
    {0xFF, 0x00}, {0x30, 0x09}, {0x54, 0x00}, {0x31, 0x04}, {0x32, 0x03},
    {0x40, 0x83}, {0x46, 0x25}, {0x60, 0x00}, {0x27, 0x00}, {0x50, 0x06},
    {0x51, 0x00}, {0x52, 0x96}, {0x56, 0x08}, {0x57, 0x30}, {0x61, 0x00},
    {0x62, 0x00}, {0x64, 0x00}, {0x65, 0x00}, {0x66, 0xA0},
    {0xFF, 0x01}, {0x22, 0x32}, {0x47, 0x14}, {0x49, 0xFF}, {0x4A, 0x00},
    {0xFF, 0x00}, {0x7A, 0x0A}, {0x7B, 0x00}, {0x78, 0x21},
    {0xFF, 0x01}, {0x23, 0x34}, {0x42, 0x00}, {0x44, 0xFF}, {0x45, 0x26},
    {0x46, 0x05}, {0x40, 0x40}, {0x0E, 0x06}, {0x20, 0x1A}, {0x43, 0x40},
    {0xFF, 0x00}, {0x34, 0x03}, {0x35, 0x44},
    {0xFF, 0x01}, {0x31, 0x04}, {0x4B, 0x09}, {0x4C, 0x05}, {0x4D, 0x04},
    {0xFF, 0x00}, {0x44, 0x00}, {0x45, 0x20}, {0x47, 0x08}, {0x48, 0x28},
    {0x67, 0x00}, {0x70, 0x04}, {0x71, 0x01}, {0x72, 0xFE}, {0x76, 0x00},
    {0x77, 0x00},
    {0xFF, 0x01}, {0x0D, 0x01},
    {0xFF, 0x00}, {0x80, 0x01}, {0x01, 0xF8},
    {0xFF, 0x01}, {0x8E, 0x01}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00},
};

std::int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Decode a VCSEL pulse-period register value into PCLKs.
constexpr std::uint8_t decodeVcselPeriod(std::uint8_t regValue) noexcept
{
    return static_cast<std::uint8_t>((regValue + 1) << 1);
}

// Macro period in nanoseconds from the VCSEL period in PCLKs.
constexpr std::uint32_t calcMacroPeriod(std::uint8_t vcselPeriodPclks) noexcept
{
    return ((2304UL * vcselPeriodPclks * 1655UL) + 500UL) / 1000UL;
}

// Decode a sequence-step timeout register value into macro periods (MCLKs).
std::uint16_t decodeTimeout(std::uint16_t regValue) noexcept
{
    const std::uint32_t lsByte = regValue & 0x00FFU;
    std::uint32_t msByte = (regValue & 0xFF00U) >> 8;
    if (msByte > 31U) {
        msByte = 31U;  // defensive: the format never uses an exponent this large
    }
    return static_cast<std::uint16_t>((lsByte << msByte) + 1U);
}

// Encode a sequence-step timeout in MCLKs into its register value.
std::uint16_t encodeTimeout(std::uint32_t timeoutMclks) noexcept
{
    if (timeoutMclks == 0U) {
        return 0U;
    }
    std::uint32_t lsByte = timeoutMclks - 1U;
    std::uint16_t msByte = 0;
    while ((lsByte & 0xFFFFFF00U) > 0U) {
        lsByte >>= 1U;
        ++msByte;
    }
    return static_cast<std::uint16_t>((msByte << 8) | (lsByte & 0xFFU));
}

std::uint32_t mclksToMicroseconds(std::uint16_t timeoutMclks,
                                  std::uint8_t vcselPeriodPclks) noexcept
{
    const std::uint32_t macroPeriodNs = calcMacroPeriod(vcselPeriodPclks);
    return ((static_cast<std::uint32_t>(timeoutMclks) * macroPeriodNs) + 500U) / 1000U;
}

std::uint32_t microsecondsToMclks(std::uint32_t timeoutUs,
                                  std::uint8_t vcselPeriodPclks) noexcept
{
    const std::uint32_t macroPeriodNs = calcMacroPeriod(vcselPeriodPclks);
    return (((timeoutUs * 1000U) + (macroPeriodNs / 2U)) / macroPeriodNs);
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl — owns the open I2C bus and all driver state. Used only by the
// proximity thread, so no internal locking is needed.
// ---------------------------------------------------------------------------
class Vl53l0xProximity::Impl {
public:
    Impl() = default;
    ~Impl()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    [[nodiscard]] bool init();
    [[nodiscard]] std::optional<ProximityReading> read();

private:
    // I2C primitives. Writes are a single message; reads use a two-message
    // transfer (repeated start) as the sensor expects.
    [[nodiscard]] bool writeBytes(std::uint8_t reg, const std::uint8_t* src, std::size_t count);
    [[nodiscard]] bool readBytes(std::uint8_t reg, std::uint8_t* dst, std::size_t count);

    bool writeReg(std::uint8_t reg, std::uint8_t value);
    bool writeReg16(std::uint8_t reg, std::uint16_t value);
    bool writeMulti(std::uint8_t reg, const std::uint8_t* src, std::size_t count);
    std::uint8_t readReg(std::uint8_t reg);
    std::uint16_t readReg16(std::uint8_t reg);
    bool readMulti(std::uint8_t reg, std::uint8_t* dst, std::size_t count);

    bool setSignalRateLimit(float limitMcps);
    bool getSpadInfo(std::uint8_t& count, bool& typeIsAperture);
    void getSequenceStepEnables(SequenceStepEnables& enables);
    void getSequenceStepTimeouts(const SequenceStepEnables& enables,
                                 SequenceStepTimeouts& timeouts);
    std::uint32_t getMeasurementTimingBudget();
    bool setMeasurementTimingBudget(std::uint32_t budgetUs);
    std::uint8_t getVcselPulsePeriod(bool finalRange);
    bool performSingleRefCalibration(std::uint8_t vhvInitByte);
    bool readRangeSingleMillimeters(std::uint16_t& rangeMm);

    int m_fd = -1;
    std::uint8_t m_stopVariable = 0;
    std::uint32_t m_budgetUs = 33000;
    // Set by any failed I2C transfer during the current init()/read() call, so a
    // dead or unstable bus fails cleanly instead of returning bogus zero values.
    bool m_ioError = false;
};

bool Vl53l0xProximity::Impl::writeBytes(std::uint8_t reg, const std::uint8_t* src,
                                        std::size_t count)
{
    if (m_fd < 0 || count > 8) {
        return false;
    }
    std::uint8_t buffer[1 + 8];
    buffer[0] = reg;
    for (std::size_t i = 0; i < count; ++i) {
        buffer[1 + i] = src[i];
    }
    struct i2c_msg message {};
    message.addr = kSensorAddress;
    message.flags = 0;
    message.len = static_cast<std::uint16_t>(1 + count);
    message.buf = buffer;

    struct i2c_rdwr_ioctl_data data {};
    data.msgs = &message;
    data.nmsgs = 1;
    // I2C_RDWR returns the number of messages transferred, or a negative errno.
    return ::ioctl(m_fd, I2C_RDWR, &data) >= 0;
}

bool Vl53l0xProximity::Impl::readBytes(std::uint8_t reg, std::uint8_t* dst, std::size_t count)
{
    if (m_fd < 0) {
        return false;
    }
    struct i2c_msg messages[2] {};
    messages[0].addr = kSensorAddress;
    messages[0].flags = 0;
    messages[0].len = 1;
    messages[0].buf = &reg;
    messages[1].addr = kSensorAddress;
    messages[1].flags = I2C_M_RD;
    messages[1].len = static_cast<std::uint16_t>(count);
    messages[1].buf = dst;

    struct i2c_rdwr_ioctl_data data {};
    data.msgs = messages;
    data.nmsgs = 2;
    // I2C_RDWR returns the number of messages transferred, or a negative errno.
    return ::ioctl(m_fd, I2C_RDWR, &data) >= 0;
}

bool Vl53l0xProximity::Impl::writeReg(std::uint8_t reg, std::uint8_t value)
{
    const bool ok = writeBytes(reg, &value, 1);
    if (!ok) {
        m_ioError = true;
    }
    return ok;
}

bool Vl53l0xProximity::Impl::writeReg16(std::uint8_t reg, std::uint16_t value)
{
    const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(value >> 8),
                                   static_cast<std::uint8_t>(value & 0xFFU)};
    const bool ok = writeBytes(reg, bytes, 2);
    if (!ok) {
        m_ioError = true;
    }
    return ok;
}

bool Vl53l0xProximity::Impl::writeMulti(std::uint8_t reg, const std::uint8_t* src,
                                        std::size_t count)
{
    const bool ok = writeBytes(reg, src, count);
    if (!ok) {
        m_ioError = true;
    }
    return ok;
}

std::uint8_t Vl53l0xProximity::Impl::readReg(std::uint8_t reg)
{
    std::uint8_t value = 0;
    if (!readBytes(reg, &value, 1)) {
        m_ioError = true;
    }
    return value;
}

std::uint16_t Vl53l0xProximity::Impl::readReg16(std::uint8_t reg)
{
    std::uint8_t bytes[2] = {0, 0};
    if (!readBytes(reg, bytes, 2)) {
        m_ioError = true;
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

bool Vl53l0xProximity::Impl::readMulti(std::uint8_t reg, std::uint8_t* dst, std::size_t count)
{
    const bool ok = readBytes(reg, dst, count);
    if (!ok) {
        m_ioError = true;
    }
    return ok;
}

bool Vl53l0xProximity::Impl::setSignalRateLimit(float limitMcps)
{
    if (limitMcps < 0.0F || limitMcps > 511.99F) {
        return false;
    }
    // Q9.7 fixed point (9 integer bits, 7 fractional bits).
    writeReg16(kRegFinalRangeMinCountRate,
               static_cast<std::uint16_t>(limitMcps * static_cast<float>(1U << 7)));
    return true;
}

bool Vl53l0xProximity::Impl::getSpadInfo(std::uint8_t& count, bool& typeIsAperture)
{
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, static_cast<std::uint8_t>(readReg(0x83) | 0x04U));
    writeReg(0xFF, 0x07);
    writeReg(0x81, 0x01);
    writeReg(0x80, 0x01);
    writeReg(0x94, 0x6B);
    writeReg(0x83, 0x00);

    const std::int64_t deadline = nowMs() + kRangeTimeoutMs;
    while (readReg(0x83) == 0x00) {
        if (nowMs() > deadline) {
            return false;
        }
    }
    writeReg(0x83, 0x01);
    const std::uint8_t tmp = readReg(0x92);
    count = static_cast<std::uint8_t>(tmp & 0x7FU);
    typeIsAperture = ((tmp >> 7) & 0x01U) != 0;

    writeReg(0x81, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, static_cast<std::uint8_t>(readReg(0x83) & ~0x04U));
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);
    return true;
}

void Vl53l0xProximity::Impl::getSequenceStepEnables(SequenceStepEnables& enables)
{
    const std::uint8_t sequenceConfig = readReg(kRegSystemSequenceConfig);
    enables.tcc = ((sequenceConfig >> 4) & 0x1U) != 0;
    enables.dss = ((sequenceConfig >> 3) & 0x1U) != 0;
    enables.msrc = ((sequenceConfig >> 2) & 0x1U) != 0;
    enables.preRange = ((sequenceConfig >> 6) & 0x1U) != 0;
    enables.finalRange = ((sequenceConfig >> 7) & 0x1U) != 0;
}

void Vl53l0xProximity::Impl::getSequenceStepTimeouts(const SequenceStepEnables& enables,
                                                     SequenceStepTimeouts& timeouts)
{
    timeouts.preRangeVcselPeriodPclks = getVcselPulsePeriod(false);
    timeouts.msrcDssTccMclks = static_cast<std::uint16_t>(readReg(kRegMsrcConfigTimeoutMacrop) + 1U);
    timeouts.msrcDssTccUs = mclksToMicroseconds(timeouts.msrcDssTccMclks,
                                                timeouts.preRangeVcselPeriodPclks);
    timeouts.preRangeMclks = decodeTimeout(readReg16(kRegPreRangeConfigTimeoutMacropHi));
    timeouts.preRangeUs = mclksToMicroseconds(timeouts.preRangeMclks,
                                              timeouts.preRangeVcselPeriodPclks);

    timeouts.finalRangeVcselPeriodPclks = getVcselPulsePeriod(true);
    timeouts.finalRangeMclks = decodeTimeout(readReg16(kRegFinalRangeConfigTimeoutMacropHi));
    if (enables.preRange) {
        timeouts.finalRangeMclks =
            static_cast<std::uint16_t>(timeouts.finalRangeMclks - timeouts.preRangeMclks);
    }
    timeouts.finalRangeUs = mclksToMicroseconds(timeouts.finalRangeMclks,
                                                timeouts.finalRangeVcselPeriodPclks);
}

std::uint8_t Vl53l0xProximity::Impl::getVcselPulsePeriod(bool finalRange)
{
    const std::uint8_t reg =
        finalRange ? kRegFinalRangeConfigVcselPeriod : kRegPreRangeConfigVcselPeriod;
    return decodeVcselPeriod(readReg(reg));
}

std::uint32_t Vl53l0xProximity::Impl::getMeasurementTimingBudget()
{
    constexpr std::uint16_t startOverhead = 1910;
    constexpr std::uint16_t endOverhead = 960;
    constexpr std::uint16_t msrcOverhead = 660;
    constexpr std::uint16_t tccOverhead = 590;
    constexpr std::uint16_t dssOverhead = 690;
    constexpr std::uint16_t preRangeOverhead = 660;
    constexpr std::uint16_t finalRangeOverhead = 550;

    SequenceStepEnables enables;
    SequenceStepTimeouts timeouts;
    getSequenceStepEnables(enables);
    getSequenceStepTimeouts(enables, timeouts);

    std::uint32_t budgetUs = startOverhead + endOverhead;
    if (enables.tcc) {
        budgetUs += timeouts.msrcDssTccUs + tccOverhead;
    }
    if (enables.dss) {
        budgetUs += 2U * (timeouts.msrcDssTccUs + dssOverhead);
    } else if (enables.msrc) {
        budgetUs += timeouts.msrcDssTccUs + msrcOverhead;
    }
    if (enables.preRange) {
        budgetUs += timeouts.preRangeUs + preRangeOverhead;
    }
    if (enables.finalRange) {
        budgetUs += timeouts.finalRangeUs + finalRangeOverhead;
    }
    m_budgetUs = budgetUs;
    return budgetUs;
}

bool Vl53l0xProximity::Impl::setMeasurementTimingBudget(std::uint32_t budgetUs)
{
    constexpr std::uint16_t startOverhead = 1910;
    constexpr std::uint16_t endOverhead = 960;
    constexpr std::uint16_t msrcOverhead = 660;
    constexpr std::uint16_t tccOverhead = 590;
    constexpr std::uint16_t dssOverhead = 690;
    constexpr std::uint16_t preRangeOverhead = 660;
    constexpr std::uint16_t finalRangeOverhead = 550;

    SequenceStepEnables enables;
    SequenceStepTimeouts timeouts;
    getSequenceStepEnables(enables);
    getSequenceStepTimeouts(enables, timeouts);

    std::uint32_t usedBudgetUs = startOverhead + endOverhead;
    if (enables.tcc) {
        usedBudgetUs += timeouts.msrcDssTccUs + tccOverhead;
    }
    if (enables.dss) {
        usedBudgetUs += 2U * (timeouts.msrcDssTccUs + dssOverhead);
    } else if (enables.msrc) {
        usedBudgetUs += timeouts.msrcDssTccUs + msrcOverhead;
    }
    if (enables.preRange) {
        usedBudgetUs += timeouts.preRangeUs + preRangeOverhead;
    }
    if (enables.finalRange) {
        usedBudgetUs += finalRangeOverhead;
        if (usedBudgetUs > budgetUs) {
            LUMINA_LOG_WARN("proximity: requested timing budget {} us is too small", budgetUs);
            return false;
        }
        const std::uint32_t finalRangeTimeoutUs = budgetUs - usedBudgetUs;
        std::uint32_t finalRangeTimeoutMclks =
            microsecondsToMclks(finalRangeTimeoutUs, timeouts.finalRangeVcselPeriodPclks);
        if (enables.preRange) {
            finalRangeTimeoutMclks += timeouts.preRangeMclks;
        }
        writeReg16(kRegFinalRangeConfigTimeoutMacropHi, encodeTimeout(finalRangeTimeoutMclks));
        m_budgetUs = budgetUs;
    }
    return true;
}

bool Vl53l0xProximity::Impl::performSingleRefCalibration(std::uint8_t vhvInitByte)
{
    writeReg(kRegSysRangeStart, static_cast<std::uint8_t>(kModeStartStop | vhvInitByte));

    const std::int64_t deadline = nowMs() + kRangeTimeoutMs;
    while ((readReg(kRegResultInterruptStatus) & 0x07U) == 0) {
        if (nowMs() > deadline) {
            return false;
        }
    }
    writeReg(kRegSystemInterruptClear, 0x01);
    writeReg(kRegSysRangeStart, 0x00);
    return true;
}

bool Vl53l0xProximity::Impl::readRangeSingleMillimeters(std::uint16_t& rangeMm)
{
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    writeReg(0x91, m_stopVariable);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);
    writeReg(kRegSysRangeStart, kModeSingleShot);

    std::int64_t deadline = nowMs() + kRangeTimeoutMs;
    while ((readReg(kRegSysRangeStart) & 0x01U) != 0) {  // wait for the start bit to clear
        if (nowMs() > deadline) {
            return false;
        }
    }
    deadline = nowMs() + kRangeTimeoutMs;
    while ((readReg(kRegResultInterruptStatus) & 0x07U) == 0) {  // wait for data ready
        if (nowMs() > deadline) {
            return false;
        }
    }
    rangeMm = readReg16(static_cast<std::uint8_t>(kRegResultRangeStatus + 10));
    writeReg(kRegSystemInterruptClear, 0x01);
    return true;
}

bool Vl53l0xProximity::Impl::init()
{
    m_ioError = false;
    if (m_fd < 0) {
        m_fd = ::open(kBusPath, O_RDWR);
        if (m_fd < 0) {
            LUMINA_LOG_ERROR("proximity: cannot open '{}': {}", kBusPath, std::strerror(errno));
            return false;
        }
    }
    if (readReg(kRegModelId) != kExpectedModelId) {
        LUMINA_LOG_ERROR("proximity: no VL53L0X at 0x{:02x} on '{}' (model id 0x{:02x})",
                         kSensorAddress, kBusPath, readReg(kRegModelId));
        return false;
    }

    // --- DataInit -----------------------------------------------------------
    writeReg(kRegVhvConfigPadSclSdaExtsupHv,
             static_cast<std::uint8_t>(readReg(kRegVhvConfigPadSclSdaExtsupHv) | 0x01U));  // 2V8
    writeReg(0x88, 0x00);  // I2C standard mode
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    m_stopVariable = readReg(0x91);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);

    // Disable SIGNAL_RATE_MSRC (bit 1) and SIGNAL_RATE_PRE_RANGE (bit 4) checks.
    writeReg(kRegMsrcConfigControl,
             static_cast<std::uint8_t>(readReg(kRegMsrcConfigControl) | 0x12U));
    setSignalRateLimit(0.25F);  // 0.25 MCPS
    writeReg(kRegSystemSequenceConfig, 0xFF);

    // --- StaticInit ---------------------------------------------------------
    std::uint8_t spadCount = 0;
    bool spadIsAperture = false;
    if (!getSpadInfo(spadCount, spadIsAperture)) {
        LUMINA_LOG_ERROR("proximity: reading SPAD info failed");
        return false;
    }

    std::uint8_t refSpadMap[6] = {0, 0, 0, 0, 0, 0};
    if (!readMulti(kRegGlobalConfigSpadEnablesRef0, refSpadMap, 6)) {
        LUMINA_LOG_ERROR("proximity: reading the reference SPAD map failed");
        return false;
    }
    writeReg(0xFF, 0x01);
    writeReg(kRegDynamicSpadRefEnStartOffset, 0x00);
    writeReg(kRegDynamicSpadNumRequestedRefSpad, 0x2C);
    writeReg(0xFF, 0x00);
    writeReg(kRegGlobalConfigRefEnStartSelect, 0xB4);

    const std::uint8_t firstSpadToEnable = spadIsAperture ? 12 : 0;
    std::uint8_t spadsEnabled = 0;
    for (std::uint8_t i = 0; i < 48; ++i) {
        if (i < firstSpadToEnable || spadsEnabled == spadCount) {
            refSpadMap[i / 8] &= static_cast<std::uint8_t>(~(1U << (i % 8)));
        } else if (((refSpadMap[i / 8] >> (i % 8)) & 0x1U) != 0) {
            ++spadsEnabled;
        }
    }
    writeMulti(kRegGlobalConfigSpadEnablesRef0, refSpadMap, 6);

    for (const RegValue& entry : kTuningSettings) {
        writeReg(entry.reg, entry.value);
    }

    // GPIO interrupt on "new sample ready"; active low.
    writeReg(kRegSystemInterruptConfigGpio, 0x04);
    writeReg(kRegGpioHvMuxActiveHigh,
             static_cast<std::uint8_t>(readReg(kRegGpioHvMuxActiveHigh) & ~0x10U));
    writeReg(kRegSystemInterruptClear, 0x01);

    m_budgetUs = getMeasurementTimingBudget();
    writeReg(kRegSystemSequenceConfig, 0xE8);  // disable MSRC and TCC steps
    if (!setMeasurementTimingBudget(m_budgetUs)) {
        return false;
    }

    // --- Reference calibration (VHV + phase) --------------------------------
    writeReg(kRegSystemSequenceConfig, 0x01);
    if (!performSingleRefCalibration(0x40)) {
        LUMINA_LOG_ERROR("proximity: VHV calibration failed");
        return false;
    }
    writeReg(kRegSystemSequenceConfig, 0x02);
    if (!performSingleRefCalibration(0x00)) {
        LUMINA_LOG_ERROR("proximity: phase calibration failed");
        return false;
    }
    writeReg(kRegSystemSequenceConfig, 0xE8);

    // Any failed transfer during the sequence means the bus/sensor is not usable.
    if (m_ioError) {
        LUMINA_LOG_ERROR("proximity: I2C communication error during init on '{}'", kBusPath);
        return false;
    }

    LUMINA_LOG_INFO("proximity: VL53L0X ready on '{}' (timing budget {} us)", kBusPath,
                    m_budgetUs);
    return true;
}

std::optional<ProximityReading> Vl53l0xProximity::Impl::read()
{
    m_ioError = false;
    std::uint16_t rangeMm = 0;
    if (!readRangeSingleMillimeters(rangeMm)) {
        return std::nullopt;
    }
    if (m_ioError) {
        // A failed transfer means the reading (likely 0) is not trustworthy.
        LUMINA_LOG_DEBUG("proximity: I2C read failed; skipping this sample");
        return std::nullopt;
    }
    if (rangeMm < kMinRangeMm || rangeMm > kMaxRangeMm) {
        return std::nullopt;  // out of the documented range: no trustworthy target
    }
    return ProximityReading{static_cast<float>(rangeMm) / 1000.0F, true};
}

Vl53l0xProximity::Vl53l0xProximity() : m_impl(std::make_unique<Impl>()) {}

Vl53l0xProximity::~Vl53l0xProximity() = default;

bool Vl53l0xProximity::init()
{
    return m_impl->init();
}

std::optional<ProximityReading> Vl53l0xProximity::read()
{
    return m_impl->read();
}

}  // namespace lumina::sensors
