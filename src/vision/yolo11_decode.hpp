#pragma once

#include <span>
#include <vector>

#include "core/detection.hpp"

namespace lumina::vision {

// Geometry of the letterbox that was applied to the frame before inference.
// To map a box from model space back to camera pixels we undo the pad, then the
// scale:  x_camera = (x_model - padLeft) / scale.
struct LetterboxInfo {
    float scale = 1.0F;  // resize factor applied to the frame (<= 1 means downscaled)
    int padLeft = 0;     // left padding added to reach the square input, in model pixels
    int padTop = 0;      // top padding added to reach the square input, in model pixels
};

// Thresholds and limits for turning raw model output into detections.
struct DecodeOptions {
    int numClasses = 80;          // COCO; must equal `channels - 4`
    float scoreThreshold = 0.25F; // drop anchors below this class score
    float nmsThreshold = 0.45F;   // IoU above this suppresses a duplicate box
    int maxDetections = 300;      // safety cap; <= 0 means "no cap"
};

// Decode the output of the Ultralytics YOLO11 NCNN export into detections.
//
// The exported graph has ALREADY applied the DFL box decode and the class
// sigmoid, so `output` is simply a flat, row-major [anchors][channels] buffer:
//   channels = 4 + numClasses
//   row[0..3] = centre-x, centre-y, width, height   (in letterboxed input pixels)
//   row[4..]  = class scores in [0, 1]              (one per class)
//
// This function is pure (no NCNN, no file I/O) so it is unit-tested on the host.
// C++ note (for Java readers): `std::span` is a non-owning view over a contiguous
// array (like a Java array slice that does not copy); `[[nodiscard]]` warns if the
// caller ignores the returned detections.
[[nodiscard]] std::vector<core::Detection> decodeYolo11(std::span<const float> output, int anchors,
                                                        int channels, const LetterboxInfo& letterbox,
                                                        int frameWidth, int frameHeight,
                                                        const DecodeOptions& options);

}  // namespace lumina::vision
