#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "vision/face_recognizer.hpp"

namespace lumina::tests {

// Deterministic IFaceRecognizer: returns a configured FaceMatch (or nullopt) and
// ignores the frame contents, so the greeting pipeline can be tested without a
// model or camera.
class MockFaceRecognizer final : public vision::IFaceRecognizer {
public:
    MockFaceRecognizer() = default;
    explicit MockFaceRecognizer(std::optional<vision::FaceMatch> match) : m_match(std::move(match)) {}

    [[nodiscard]] std::optional<vision::FaceMatch> identify(
        const core::Frame& /*frame*/, const core::Detection& /*personDetection*/) override {
        ++m_calls;
        return m_match;
    }

    // --- Test helpers -------------------------------------------------------
    void setMatch(std::optional<vision::FaceMatch> match) { m_match = std::move(match); }
    [[nodiscard]] std::size_t calls() const noexcept { return m_calls; }

private:
    std::optional<vision::FaceMatch> m_match;
    std::size_t m_calls = 0;
};

}  // namespace lumina::tests
