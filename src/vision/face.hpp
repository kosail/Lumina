// ---------------------------------------------------------------------------
// OpenCV face path: YuNet face detection + SFace embedding (FR-03).
//
// Two pieces:
//   - FaceEmbedder: turns a frame (runtime) or an image file (enrollment) into
//     one embedding for the largest face.
//   - FaceRecognizer: the production IFaceRecognizer = FaceEmbedder + FaceStore.
//
// All OpenCV types are hidden behind pimpl so the rest of the project (and its
// host tests) never has to include or link OpenCV — the same pattern as
// NcnnDetector. This file is compiled only when LUMINA_ENABLE_FACE is ON.
//
// Channel order: libcamera delivers RGB888; OpenCV (and these two models, which
// the OpenCV Zoo demo feeds BGR images) expect BGR, so the frame entry point
// converts with cvtColor. The enrollment tool decodes photo files and passes BGR
// buffers in. Both go through the same detection/alignment helper.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "vision/face_recognizer.hpp"
#include "vision/face_store.hpp"

namespace lumina::vision {

// Identity of the embedder, persisted with the enrollment store so a model swap
// cannot silently compare incompatible embeddings. Shared by the recognizer and
// the enrollment tool so the tag can never drift between them.
inline constexpr const char* kFaceModelId = "sface-2021dec";

// Everything the OpenCV face path needs. Paths default to the deployed layout.
struct FaceModelConfig {
    std::string yunetPath = "models/face/yunet.onnx";      // YuNet (face_detection_yunet_2023mar)
    std::string sfacePath = "models/face/sface.onnx";      // SFace (face_recognition_sface_2021dec)
    std::string storePath = "models/face/embeddings.bin";  // persisted enrollments
    float roiFraction = 1.0F;      // central-crop fraction 0.1..1.0 (INV-011)
    float scoreThreshold = 0.7F;   // YuNet face confidence cutoff
    float nmsThreshold = 0.3F;     // YuNet non-max-suppression IoU
    float matchThreshold = 0.363F; // SFace cosine cutoff (OpenCV Zoo reference)
    float matchMargin = 0.05F;     // best must beat 2nd-best by this much
    int detectionSide = 320;       // longest side fed to YuNet (downscale, INV-052)
    int maxEmbeddingsPerPerson = 10;  // enrollment cap (photos or camera frames)
};

// Wraps YuNet + SFace and exposes "largest face -> embedding". Owns the OpenCV
// networks; not copyable.
class FaceEmbedder {
public:
    explicit FaceEmbedder(FaceModelConfig config);

    // Declared here, defined in the .cpp: needed because `Impl` is incomplete.
    ~FaceEmbedder();
    FaceEmbedder(const FaceEmbedder&) = delete;
    FaceEmbedder& operator=(const FaceEmbedder&) = delete;

    // Load both models. Returns false (after logging) on failure; never throws.
    [[nodiscard]] bool load();

    // Detect the largest face in one runtime frame and return its SFace feature,
    // or nullopt when there is no face. Requires a tightly packed RGB888 frame.
    [[nodiscard]] std::optional<Embedding> embedLargestFace(const core::Frame& frame);

    // Detect the largest face in an already-decoded BGR image and return its SFace
    // feature, or nullopt when no face is found. `pixels` must point at `height`
    // rows of `stride` bytes of CV_8UC3 data. The enrollment tool decodes files
    // with cv::imread and passes the buffer here, which keeps OpenCV's image
    // codecs (and their heavy GDAL dependency) out of the runtime binary. Never
    // throws.
    [[nodiscard]] std::optional<Embedding> embedBgrImage(const std::uint8_t* pixels, int width,
                                                         int height, int stride);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// Production face recognizer: YuNet + SFace + a persisted FaceStore.
class FaceRecognizer final : public IFaceRecognizer {
public:
    explicit FaceRecognizer(FaceModelConfig config);

    ~FaceRecognizer() override;
    FaceRecognizer(const FaceRecognizer&) = delete;
    FaceRecognizer& operator=(const FaceRecognizer&) = delete;

    // Load models and the enrollment store (an absent store is a valid, empty
    // state). Returns false (after logging) only when a model fails to load.
    [[nodiscard]] bool load();

    // See IFaceRecognizer. `personDetection` is advisory: the embedder uses the
    // largest detected face; it does not rely on the object detector's box.
    [[nodiscard]] std::optional<FaceMatch> identify(
        const core::Frame& frame, const core::Detection& personDetection) override;

    // The enrollment store, exposed so main()/the enrollment tool can pre-warm
    // greetings and add people without reaching into the recognizer.
    [[nodiscard]] const FaceStore& store() const noexcept { return m_store; }
    [[nodiscard]] FaceStore& store() noexcept { return m_store; }

private:
    FaceModelConfig m_config;
    FaceEmbedder m_embedder;
    FaceStore m_store;
};

}  // namespace lumina::vision
