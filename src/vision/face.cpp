// ---------------------------------------------------------------------------
// OpenCV face path implementation. See face.hpp.
//
// Every OpenCV call is wrapped so no cv::Exception can escape: load() runs on the
// main thread and embed*() runs on the face worker thread, and an exception
// crossing a thread boundary would call std::terminate (AGENTS §6). Failures are
// logged and reported as false / std::nullopt.
// ---------------------------------------------------------------------------

#include "vision/face.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/face.hpp>

#include "core/frame.hpp"
#include "core/logging.hpp"
#include "core/time.hpp"

namespace lumina::vision {

// ---------------------------------------------------------------------------
// FaceEmbedder::Impl — owns the two OpenCV networks.
// ---------------------------------------------------------------------------
class FaceEmbedder::Impl {
public:
    explicit Impl(FaceModelConfig config) : m_config(std::move(config)) {}

    [[nodiscard]] bool load()
    {
        // YuNet/SFace are CPU networks; the object detector's NCNN already uses all
        // four cores, so restrict OpenCV's internal thread pool to one thread to
        // avoid oversubscription (INV-050). This is a process-wide setting; the
        // only other OpenCV consumer is the enrollment tool, which runs alone.
        cv::setNumThreads(1);

        try {
            m_detector = cv::FaceDetectorYN::create(m_config.yunetPath,
                                                    "",
                                                    cv::Size(320, 320),
                                                    m_config.scoreThreshold,
                                                    m_config.nmsThreshold,
                                                    5000);
            if (m_detector.empty()) {
                LUMINA_LOG_ERROR("face: failed to create YuNet from '{}'", m_config.yunetPath);
                return false;
            }
            m_sface = cv::FaceRecognizerSF::create(m_config.sfacePath, "");
            if (m_sface.empty()) {
                LUMINA_LOG_ERROR("face: failed to create SFace from '{}'", m_config.sfacePath);
                return false;
            }
        } catch (const cv::Exception& error) {
            LUMINA_LOG_ERROR("face: OpenCV failed to load the models: {}", error.what());
            return false;
        } catch (const std::exception& error) {
            LUMINA_LOG_ERROR("face: failed to load the models: {}", error.what());
            return false;
        }

        // Warm both networks once, while the system is still idle. The very first
        // real inference otherwise allocates all the DNN layers at once (measured
        // ~1.7-3.3 s on the Pi, CHG-0071) while the camera and Piper are already
        // running, which spikes memory and can tip the ~447 MB board into swap.
        // Running a blank frame here moves that one-off allocation to startup.
        try {
            cv::Mat blank(240, 320, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::Mat faces;
            m_detector->setInputSize(blank.size());  // YuNet layer alloc
            m_detector->detect(blank, faces);
            m_lastInputSize = blank.size();  // the live 640x480 frame downscales to this
            cv::Mat aligned(112, 112, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::Mat feature;
            m_sface->feature(aligned, feature);  // SFace's heavy layer alloc
        } catch (const cv::Exception& error) {
            // Non-fatal: the first real inference just pays the allocation instead.
            LUMINA_LOG_WARN("face: warm-up failed ({}); continuing", error.what());
        }

        LUMINA_LOG_INFO("face: YuNet + SFace loaded");
        return true;
    }

    [[nodiscard]] std::optional<Embedding> embedLargestFace(const core::Frame& frame)
    {
        // Mirror NcnnDetector's guards: a malformed frame would otherwise build an
        // invalid cv::Mat and throw/UB inside OpenCV.
        if (frame.format != core::PixelFormat::Rgb888 || frame.data.empty() ||
            frame.width <= 0 || frame.height <= 0 || frame.stride != frame.width * 3) {
            LUMINA_LOG_WARN("face: expected a non-empty, tightly packed RGB888 frame");
            return std::nullopt;
        }

        try {
            // Non-owning view over the frame's pixel buffer, then cvtColor into an
            // owned BGR image. OpenCV's native order is BGR and both models were
            // trained on BGR (the OpenCV Zoo demo reads BGR via cv::imread).
            // C++ note (for Java readers): `const_cast` drops const-ness only because
            // OpenCV's Mat constructor needs a `void*`; cvtColor never writes to it.
            const cv::Mat rgb(frame.height,
                              frame.width,
                              CV_8UC3,
                              const_cast<std::uint8_t*>(frame.data.data()),
                              frame.stride);
            cv::Mat bgr;
            cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

            // Downscale before YuNet. At the full 640x480 frame YuNet's DNN
            // workspace churns tens of MB per inference, which forces SD swap on
            // the 415 MB Pi and starves the camera (INV-052, CHG-0070). YuNet's
            // native size is 320 and a person covering >= faceMinBoxFraction still
            // yields a face well inside YuNet's reliable range.
            cv::Mat detection = bgr;
            const int longestSide = std::max(bgr.cols, bgr.rows);
            if (m_config.detectionSide > 0 && longestSide > m_config.detectionSide) {
                const double scale = static_cast<double>(m_config.detectionSide) /
                                     static_cast<double>(longestSide);
                cv::resize(bgr, detection, cv::Size(), scale, scale, cv::INTER_AREA);
            }
            return embedBgr(detection);
        } catch (const cv::Exception& error) {
            LUMINA_LOG_WARN("face: frame processing failed: {}", error.what());
            return std::nullopt;
        } catch (const std::exception& error) {
            LUMINA_LOG_WARN("face: frame processing failed: {}", error.what());
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<Embedding> embedBgrImage(const std::uint8_t* pixels, int width,
                                                         int height, int stride)
    {
        if (pixels == nullptr || width <= 0 || height <= 0 || stride < width * 3) {
            LUMINA_LOG_WARN("face: invalid BGR buffer ({}x{}, stride {})", width, height, stride);
            return std::nullopt;
        }
        try {
            // Non-owning view over the decoded pixels; embedBgr never writes to it.
            const cv::Mat bgr(height,
                              width,
                              CV_8UC3,
                              const_cast<std::uint8_t*>(pixels),
                              static_cast<std::size_t>(stride));
            return embedBgr(bgr);
        } catch (const cv::Exception& error) {
            LUMINA_LOG_WARN("face: BGR image processing failed: {}", error.what());
            return std::nullopt;
        } catch (const std::exception& error) {
            LUMINA_LOG_WARN("face: BGR image processing failed: {}", error.what());
            return std::nullopt;
        }
    }

private:
    // Shared detection + embedding on an already-BGR image. Never throws (the
    // public callers wrap it, but keeping the body free of early returns helps).
    [[nodiscard]] std::optional<Embedding> embedBgr(const cv::Mat& bgr)
    {
        if (bgr.empty() || bgr.type() != CV_8UC3) {
            return std::nullopt;
        }
        const core::TimePoint started = core::now();

        const cv::Mat roi = centralRoi(bgr);
        if (roi.empty() || roi.cols < 1 || roi.rows < 1) {
            return std::nullopt;
        }

        // YuNet is fully convolutional, so it runs at any input size; re-init the
        // network only when the ROI size actually changes (re-initing per frame
        // would be wasted work).
        if (m_lastInputSize != roi.size()) {
            m_detector->setInputSize(roi.size());
            m_lastInputSize = roi.size();
        }

        cv::Mat faces;  // N x 15: x, y, w, h, five landmarks (x,y), score
        const int faceCount = m_detector->detect(roi, faces);
        if (faceCount <= 0 || faces.rows == 0) {
            LUMINA_LOG_DEBUG("face: identify {}x{} took {:.0f} ms (no face)", roi.cols, roi.rows,
                             core::msSince(started));
            return std::nullopt;
        }

        // Embed the largest face (max area) so the primary subject is recognized,
        // not a smaller face in the background.
        int bestRow = -1;
        float bestArea = 0.0F;
        for (int i = 0; i < faces.rows; ++i) {
            const float* face = faces.ptr<float>(i);
            const float area = face[2] * face[3];  // width * height
            if (area > bestArea) {
                bestArea = area;
                bestRow = i;
            }
        }
        if (bestRow < 0) {
            return std::nullopt;
        }

        // alignCrop warps the face to the model's canonical pose using the five
        // landmarks YuNet produced; feature() returns a 1 x N float row.
        cv::Mat aligned;
        m_sface->alignCrop(roi, faces.row(bestRow), aligned);
        cv::Mat feature;
        m_sface->feature(aligned, feature);
        if (feature.empty() || feature.cols <= 0 || feature.type() != CV_32F) {
            return std::nullopt;
        }

        Embedding embedding;
        const float* values = feature.ptr<float>(0);
        embedding.assign(values, values + feature.cols);
        LUMINA_LOG_DEBUG("face: identify {}x{} took {:.0f} ms", roi.cols, roi.rows,
                         core::msSince(started));
        return embedding;
    }

    // Central crop per INV-011: the lens distorts at the edges, so recognition
    // prefers the middle of the frame. roiFraction 1.0 means "use the full frame".
    [[nodiscard]] cv::Mat centralRoi(const cv::Mat& image) const
    {
        if (m_config.roiFraction >= 1.0F) {
            return image;
        }
        // Guard against a tiny fraction producing a zero-size rectangle.
        const int cropW = std::max(1, static_cast<int>(image.cols * m_config.roiFraction));
        const int cropH = std::max(1, static_cast<int>(image.rows * m_config.roiFraction));
        if (cropW > image.cols || cropH > image.rows) {
            return image;  // defensive: never build an out-of-bounds ROI
        }
        const int x = (image.cols - cropW) / 2;
        const int y = (image.rows - cropH) / 2;
        return image(cv::Rect(x, y, cropW, cropH));  // non-owning window into `image`
    }

    FaceModelConfig m_config;
    cv::Ptr<cv::FaceDetectorYN> m_detector;
    cv::Ptr<cv::FaceRecognizerSF> m_sface;
    cv::Size m_lastInputSize{-1, -1};
};

// ---------------------------------------------------------------------------
// FaceEmbedder (public wrapper over Impl).
// ---------------------------------------------------------------------------
FaceEmbedder::FaceEmbedder(FaceModelConfig config)
    : m_impl(std::make_unique<Impl>(std::move(config)))
{
}

FaceEmbedder::~FaceEmbedder() = default;

bool FaceEmbedder::load()
{
    return m_impl->load();
}

std::optional<Embedding> FaceEmbedder::embedLargestFace(const core::Frame& frame)
{
    return m_impl->embedLargestFace(frame);
}

std::optional<Embedding> FaceEmbedder::embedBgrImage(const std::uint8_t* pixels, int width,
                                                     int height, int stride)
{
    return m_impl->embedBgrImage(pixels, width, height, stride);
}

// ---------------------------------------------------------------------------
// FaceRecognizer.
// ---------------------------------------------------------------------------
FaceRecognizer::FaceRecognizer(FaceModelConfig config)
    : m_config(std::move(config))
    , m_embedder(m_config)  // the embedder takes its own copy of the config
{
}

FaceRecognizer::~FaceRecognizer() = default;

bool FaceRecognizer::load()
{
    if (!m_embedder.load()) {
        return false;
    }

    if (m_config.storePath.empty()) {
        m_store.setModelId(kFaceModelId);  // in-memory only (tests/tools)
        return true;
    }

    bool loaded = false;
    try {
        if (std::filesystem::exists(m_config.storePath)) {
            loaded = m_store.load(m_config.storePath);
        } else {
            LUMINA_LOG_INFO("face: no enrollment store yet at '{}'", m_config.storePath);
        }
    } catch (const std::filesystem::filesystem_error& error) {
        LUMINA_LOG_WARN("face: cannot access '{}': {}", m_config.storePath, error.what());
    }

    if (loaded && !m_store.modelId().empty() && m_store.modelId() != kFaceModelId) {
        // A store written by a different embedder cannot be compared safely.
        LUMINA_LOG_ERROR("face: store '{}' was written by '{}', expected '{}'; ignoring it",
                         m_config.storePath, m_store.modelId(), kFaceModelId);
        m_store.clear();
    }
    if (m_store.modelId().empty()) {
        // Fresh, unreadable, or just-cleared store: adopt the current embedder tag
        // so a later model swap is detectable.
        m_store.setModelId(kFaceModelId);
    }
    return true;
}

std::optional<FaceMatch> FaceRecognizer::identify(const core::Frame& frame,
                                                  const core::Detection& /*personDetection*/)
{
    if (m_store.empty()) {
        return std::nullopt;  // nobody enrolled yet
    }
    const std::optional<Embedding> feature = m_embedder.embedLargestFace(frame);
    if (!feature.has_value()) {
        return std::nullopt;  // no face, or no confident face, in this frame
    }
    return m_store.matchBest(*feature, m_config.matchThreshold, m_config.matchMargin);
}

}  // namespace lumina::vision
