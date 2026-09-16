// ---------------------------------------------------------------------------
// Pure decoder for the Ultralytics YOLO11 NCNN output (see yolo11_decode.hpp).
//
// Verified against the artifacts produced by `yolo export format=ncnn` (2026-09-16):
// the .param ends with `... -> Sigmoid -> Concat ... out0`, i.e. the box decode
// (DFL) and class sigmoid are already inside the graph, and the anchors are baked
// in (2100 at 320 px, 3549 at 416 px). No softmax/DFL is done here.
// ---------------------------------------------------------------------------

#include "vision/yolo11_decode.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace lumina::vision {
namespace {

// Area of the overlap of two axis-aligned boxes (0 when they do not intersect).
[[nodiscard]] float intersectionArea(const core::BoundingBox& a, const core::BoundingBox& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    const float w = x1 - x0;
    const float h = y1 - y0;
    if (w <= 0.0F || h <= 0.0F) {
        return 0.0F;
    }
    return w * h;
}

}  // namespace

std::vector<core::Detection> decodeYolo11(std::span<const float> output, int anchors, int channels,
                                          const LetterboxInfo& letterbox, int frameWidth,
                                          int frameHeight, const DecodeOptions& options) {
    std::vector<core::Detection> detections;

    // Defensive checks: a malformed buffer must not read out of bounds.
    if (anchors <= 0 || channels <= 4 || frameWidth <= 0 || frameHeight <= 0) {
        return detections;
    }
    if (letterbox.scale <= 0.0F) {
        return detections;
    }
    if (output.size() != static_cast<std::size_t>(anchors) * static_cast<std::size_t>(channels)) {
        return detections;
    }
    // `channels` must be 4 box values + the declared number of classes; a mismatch
    // means the caller passed inconsistent values, so refuse rather than guess.
    if (options.numClasses <= 0 || channels != 4 + options.numClasses) {
        return detections;
    }

    // A candidate is a detection that passed the score threshold but not yet NMS.
    struct Candidate {
        core::BoundingBox box;
        float score = 0.0F;
        int classId = -1;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<std::size_t>(anchors));

    const float maxX = static_cast<float>(frameWidth - 1);
    const float maxY = static_cast<float>(frameHeight - 1);

    for (int anchor = 0; anchor < anchors; ++anchor) {
        const float* row = output.data() + static_cast<std::size_t>(anchor) * channels;

        // Pick the highest-scoring class for this anchor.
        int bestClass = -1;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (int c = 4; c < channels; ++c) {
            if (row[c] > bestScore) {
                bestScore = row[c];
                bestClass = c - 4;
            }
        }
        if (bestClass < 0 || bestScore < options.scoreThreshold) {
            continue;
        }

        // Undo the letterbox: model space (cx, cy, w, h) -> camera pixels (x0,y0,x1,y1).
        const float cx = row[0];
        const float cy = row[1];
        const float halfW = row[2] * 0.5F;
        const float halfH = row[3] * 0.5F;

        float x0 = (cx - halfW - letterbox.padLeft) / letterbox.scale;
        float y0 = (cy - halfH - letterbox.padTop) / letterbox.scale;
        float x1 = (cx + halfW - letterbox.padLeft) / letterbox.scale;
        float y1 = (cy + halfH - letterbox.padTop) / letterbox.scale;

        // Keep boxes inside the frame.
        x0 = std::clamp(x0, 0.0F, maxX);
        y0 = std::clamp(y0, 0.0F, maxY);
        x1 = std::clamp(x1, 0.0F, maxX);
        y1 = std::clamp(y1, 0.0F, maxY);
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }

        candidates.push_back(Candidate{{x0, y0, x1 - x0, y1 - y0}, bestScore, bestClass});
    }

    // Highest score first, so NMS keeps the most confident box of each cluster.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs) { return lhs.score > rhs.score; });

    // Class-aware non-maximum suppression: only suppress duplicates of the SAME
    // class (a chair in front of a person should not hide the person).
    for (const Candidate& candidate : candidates) {
        bool keep = true;
        for (const core::Detection& kept : detections) {
            if (kept.classId != candidate.classId) {
                continue;
            }
            const float inter = intersectionArea(candidate.box, kept.box);
            const float uni = candidate.box.area() + kept.box.area() - inter;
            if (uni > 0.0F && (inter / uni) > options.nmsThreshold) {
                keep = false;
                break;
            }
        }
        if (!keep) {
            continue;
        }
        detections.push_back(core::Detection{candidate.box, candidate.score, candidate.classId});
        if (options.maxDetections > 0 &&
            static_cast<int>(detections.size()) >= options.maxDetections) {
            break;
        }
    }

    return detections;
}

}  // namespace lumina::vision
