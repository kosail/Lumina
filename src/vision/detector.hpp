#pragma once

#include <vector>

#include "core/detection.hpp"
#include "core/frame.hpp"

namespace lumina::vision {

// Abstraction over object detection. The production implementation wraps NCNN and
// YOLO11n; unit tests inject a canned list of detections instead (INV-030).
class IDetector {
public:
    virtual ~IDetector() = default;

    // Run detection on one frame and return the hits in image coordinates; an empty
    // vector means "nothing found". Not `const` because implementations may reuse
    // internal scratch buffers between calls.
    [[nodiscard]] virtual std::vector<core::Detection> detect(const core::Frame& frame) = 0;
};

}  // namespace lumina::vision
