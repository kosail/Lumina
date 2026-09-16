#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "vision/detector.hpp"

namespace lumina::tests {

// Deterministic IDetector: returns whatever detections the test configured and
// ignores the frame contents. Lets alert/label logic be exercised without a model.
class MockDetector final : public vision::IDetector {
public:
    MockDetector() = default;
    explicit MockDetector(std::vector<core::Detection> detections)
        : m_detections(std::move(detections)) {}

    // Returns a copy of the configured detections so the caller may freely mutate
    // its result without affecting the mock.
    [[nodiscard]] std::vector<core::Detection> detect(const core::Frame& /*frame*/) override {
        return m_detections;
    }

    // --- Test helpers -------------------------------------------------------
    void setDetections(std::vector<core::Detection> detections) {
        m_detections = std::move(detections);
    }
    [[nodiscard]] std::size_t detectionCount() const noexcept { return m_detections.size(); }

private:
    std::vector<core::Detection> m_detections;
};

}  // namespace lumina::tests
