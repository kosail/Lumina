// ---------------------------------------------------------------------------
// Face recognition interface (FR-03).
//
// Lúmina recognizes a small set of enrolled people and greets them by name. The
// OpenCV implementation (YuNet detector + SFace embedder) lives behind this pure,
// OpenCV-free interface so the pipeline is testable on the host with a mock and
// never links OpenCV in the laptop build (INV-030, AGENTS §5).
// ---------------------------------------------------------------------------

#pragma once

#include <optional>
#include <string>

#include "core/detection.hpp"
#include "core/frame.hpp"

namespace lumina::vision {

// A confident identity match: the enrolled person's name plus the cosine
// similarity that produced it. The score is for logging/diagnostics only and is
// never spoken.
//
// C++ note (for Java readers): this is a plain value struct (like a Java record
// without accessors). Copying it copies the string.
struct FaceMatch {
    std::string name;
    float similarity = 0.0F;
};

// Abstraction over "who is this person?" for one frame.
class IFaceRecognizer {
public:
    virtual ~IFaceRecognizer() = default;

    // Identify the enrolled person visible in `frame`.
    //
    // `personDetection` is the object detector's `person` box for the same frame;
    // an implementation may use it to choose among several faces, but must not
    // require it (a face can be found even when the person box is absent).
    //
    // Returns nullopt when no face is found, the store is empty, or the best match
    // does not clear the configured threshold/margin. Not `const` because
    // implementations may reuse internal scratch buffers between calls.
    [[nodiscard]] virtual std::optional<FaceMatch> identify(
        const core::Frame& frame, const core::Detection& personDetection) = 0;
};

}  // namespace lumina::vision
