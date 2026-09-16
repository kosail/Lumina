#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/detection.hpp"
#include "core/frame.hpp"
#include "vision/detector.hpp"

namespace lumina::vision {

// Everything the detector needs to load and run one YOLO11n NCNN model.
// `inputWidth`/`inputHeight` must match the size the model was exported at: the
// exported graph has the anchor count baked in, so a mismatch produces garbage.
// The approved target is 320x256 (a 4:3 frame letterboxes to 320x240 with minimal
// padding, ~20% less compute than 320x320 at the same effective resolution).
struct DetectorConfig {
    std::string modelDir;              // directory holding model.ncnn.param + model.ncnn.bin
    int inputWidth = 320;              // network input width in pixels
    int inputHeight = 256;             // network input height in pixels
    int numClasses = 80;               // COCO
    int numThreads = 4;                // CPU threads (the Pi has 4 cores)
    float scoreThreshold = 0.25F;      // passed through to the decoder
    float nmsThreshold = 0.45F;        // passed through to the decoder
};

// IDetector backed by NCNN (YOLO11n). See RAW_PLAN.md §1.
//
// C++ note (for Java readers): the NCNN types are hidden behind a
// `std::unique_ptr<Impl>` ("pimpl"). This keeps `<net.h>` out of our headers so
// the rest of the project (and its host tests) never has to include or link NCNN.
// `load()` is separate from the constructor because constructors cannot cleanly
// report failure; the caller checks the boolean.
class NcnnDetector final : public IDetector {
public:
    explicit NcnnDetector(DetectorConfig config);

    // Declared here, defined in the .cpp: needed because `Impl` is incomplete.
    ~NcnnDetector() override;

    // Owns an NCNN network: not copyable.
    NcnnDetector(const NcnnDetector&) = delete;
    NcnnDetector& operator=(const NcnnDetector&) = delete;

    // Load the .param/.bin pair. Returns false (after logging) on failure.
    [[nodiscard]] bool load();

    // Run detection on one RGB888 frame. Empty vector means "nothing found".
    [[nodiscard]] std::vector<core::Detection> detect(const core::Frame& frame) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace lumina::vision
