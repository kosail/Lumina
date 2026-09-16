// ---------------------------------------------------------------------------
// NCNN YOLO11n detector — implementation.
//
// The installed NCNN package puts its headers in <prefix>/include/ncnn/, so the
// public headers are included as <net.h>/<mat.h> (see third_party/ncnn/lib/cmake).
//
// Verified export facts (2026-09-16): the model was produced by
// `yolo export format=ncnn`, exposes input blob "in0" and output blob "out0",
// and its graph already applies the DFL box decode and the class sigmoid, then
// concatenates to `4 + numClasses` channels per anchor (84 for COCO). The anchor
// count is baked in per export (1680 at 320x256, 2100 at 320x320, 3549 at 416x416).
// ---------------------------------------------------------------------------

#include "vision/ncnn_detector.hpp"

#include <net.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/detection.hpp"
#include "core/frame.hpp"
#include "core/logging.hpp"
#include "vision/yolo11_decode.hpp"

namespace lumina::vision {
namespace {

// YOLO11 detects on three feature maps with strides 8, 16 and 32. The NCNN export
// bakes the summed anchor count for the training input size (2100 at 320x320, 3549
// at 416x416, 1680 at 320x256), so this lets us catch a model/input mismatch.
int expectedAnchorCount(int width, int height) noexcept {
    const int strides[3] = {8, 16, 32};
    int total = 0;
    for (int stride : strides) {
        total += (width / stride) * (height / stride);
    }
    return total;
}

}  // namespace

// Hidden implementation. Holds the NCNN network and the resolved blob names.
class NcnnDetector::Impl {
public:
    explicit Impl(DetectorConfig config) : m_config(std::move(config)) {}

    [[nodiscard]] bool load();
    [[nodiscard]] std::vector<core::Detection> detect(const core::Frame& frame);

private:
    DetectorConfig m_config;
    ncnn::Net m_net;
    std::string m_inputName;
    std::string m_outputName;
    bool m_loaded = false;
    std::atomic<bool> m_loggedShape{false};
};

bool NcnnDetector::Impl::load() {
    // The VideoCore IV has no usable GPU compute; CPU inference only.
    m_net.opt.use_vulkan_compute = false;
    m_net.opt.num_threads = m_config.numThreads;
    // Force fp32 blob storage. ncnn defaults to fp16 storage, and Mat::row()/
    // operator[] always reinterpret as float, so an fp16 output blob would be
    // silently misread. The A53 has no fp16 arithmetic anyway (INV-012), so this
    // costs nothing but guarantees the output type our decoder expects (INV-001).
    m_net.opt.use_fp16_storage = true;
    m_net.opt.use_fp16_packed = true;
    m_net.opt.use_fp16_arithmetic = true;

    const std::string paramPath = m_config.modelDir + "/model.ncnn.param";
    const std::string binPath = m_config.modelDir + "/model.ncnn.bin";

    if (m_net.load_param(paramPath.c_str()) != 0) {
        LUMINA_LOG_ERROR("failed to load ncnn param '{}'", paramPath);
        return false;
    }
    if (m_net.load_model(binPath.c_str()) != 0) {
        LUMINA_LOG_ERROR("failed to load ncnn weights '{}'", binPath);
        return false;
    }

    // Read the blob names instead of hardcoding them (they are "in0"/"out0" for
    // this export, but querying is robust across re-exports).
    if (m_net.input_names().empty() || m_net.output_names().empty()) {
        LUMINA_LOG_ERROR("ncnn model has no named input/output");
        return false;
    }
    m_inputName = m_net.input_names().front();
    m_outputName = m_net.output_names().front();
    m_loaded = true;

    LUMINA_LOG_INFO("ncnn detector ready: '{}' (in='{}' out='{}', {}x{}, {} threads)",
                    m_config.modelDir, m_inputName, m_outputName, m_config.inputWidth,
                    m_config.inputHeight, m_config.numThreads);
    return true;
}

std::vector<core::Detection> NcnnDetector::Impl::detect(const core::Frame& frame) {
    std::vector<core::Detection> results;
    if (!m_loaded) {
        return results;
    }
    if (frame.empty() || frame.width <= 0 || frame.height <= 0) {
        return results;
    }
    if (frame.format != core::PixelFormat::Rgb888) {
        LUMINA_LOG_WARN("ncnn detector expects RGB888 frames (got format {})",
                        static_cast<int>(frame.format));
        return results;
    }
    // from_pixels_resize assumes tightly packed pixels; the camera gives
    // stride == width * 3 (verified: 640x480 RGB888, stride 1920).
    if (frame.stride != frame.width * 3) {
        LUMINA_LOG_WARN("ncnn detector expects tightly packed RGB (stride {} != {})", frame.stride,
                        frame.width * 3);
        return results;
    }

    // --- Letterbox the frame into the network input ---------------------------
    // Ultralytics trains and exports with an aspect-preserving letterbox padded
    // with grey 114. We must reproduce it exactly or boxes are offset.
    const float scale =
        std::min(static_cast<float>(m_config.inputWidth) / static_cast<float>(frame.width),
                 static_cast<float>(m_config.inputHeight) / static_cast<float>(frame.height));
    const int newWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(frame.width) * scale)));
    const int newHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(frame.height) * scale)));
    const int padWidth = m_config.inputWidth - newWidth;
    const int padHeight = m_config.inputHeight - newHeight;

    LetterboxInfo letterbox;
    letterbox.scale = scale;
    letterbox.padLeft = padWidth / 2;
    letterbox.padTop = padHeight / 2;

    ncnn::Mat resized = ncnn::Mat::from_pixels_resize(frame.data.data(), ncnn::Mat::PIXEL_RGB,
                                                      frame.width, frame.height, newWidth, newHeight);
    ncnn::Mat input;
    ncnn::copy_make_border(resized, input, letterbox.padTop, padHeight - letterbox.padTop,
                           letterbox.padLeft, padWidth - letterbox.padLeft, ncnn::BORDER_CONSTANT, 114.0F);

    // The export already applies the class sigmoid; we only normalise pixels to
    // [0, 1] (mean 0, scale 1/255), matching the Ultralytics preprocessing.
    const float normalized[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    input.substract_mean_normalize(nullptr, normalized);

    // --- Run the network ------------------------------------------------------
    ncnn::Extractor extractor = m_net.create_extractor();
    extractor.input(m_inputName.c_str(), input);
    ncnn::Mat output;
    if (extractor.extract(m_outputName.c_str(), output) != 0) {
        LUMINA_LOG_ERROR("ncnn inference failed");
        return results;
    }

    // Defensive: Mat::row()/operator[] only make sense for fp32, unpacked blobs.
    // load() disables fp16 storage; verify anyway so we fail loudly instead of
    // silently misreading memory.
    if (output.elembits() != 32 || output.elempack != 1) {
        LUMINA_LOG_ERROR("unexpected ncnn output type: elembits={} elempack={} (expected 32/1)",
                         output.elembits(), output.elempack);
        return results;
    }
    if (output.dims < 1 || output.dims > 3) {
        LUMINA_LOG_ERROR("unexpected ncnn output dims: {}", output.dims);
        return results;
    }

    // --- Flatten the output to a row-major [anchors][channels] buffer ---------
    // NCNN stores a Mat as [c][h][w] with `w` innermost, but the axis carrying the
    // channels depends on how pnnx lowered the tensor. We find the axis equal to
    // `channels` and transpose into the layout the decoder expects. Anchor order
    // does not matter because the boxes are absolute (already scaled by stride).
    const int channels = 4 + m_config.numClasses;
    const int w = output.w;
    const int h = output.dims >= 2 ? output.h : 1;
    const int c = output.dims >= 3 ? output.c : 1;

    if (!m_loggedShape.load()) {
        LUMINA_LOG_INFO("ncnn output: dims={} w={} h={} c={} (channels={})", output.dims, w, h, c,
                        channels);
        m_loggedShape.store(true);
    }

    int anchors = 0;
    std::vector<float> flat;
    if (w == channels) {
        // Channel axis is w: each row already is one anchor's channel vector.
        anchors = h * c;
        flat.resize(static_cast<std::size_t>(anchors) * channels);
        for (int z = 0; z < c; ++z) {
            const ncnn::Mat plane = (output.dims >= 3) ? output.channel(z) : output;
            for (int y = 0; y < h; ++y) {
                const float* src = plane.row(y);
                float* dst = flat.data() + static_cast<std::size_t>(z * h + y) * channels;
                for (int k = 0; k < channels; ++k) {
                    dst[k] = src[k];
                }
            }
        }
    } else if (h == channels) {
        // Channel axis is h: gather each channel column into an anchor row.
        anchors = w * c;
        flat.resize(static_cast<std::size_t>(anchors) * channels);
        for (int z = 0; z < c; ++z) {
            const ncnn::Mat plane = (output.dims >= 3) ? output.channel(z) : output;
            for (int x = 0; x < w; ++x) {
                float* dst = flat.data() + static_cast<std::size_t>(z * w + x) * channels;
                for (int k = 0; k < channels; ++k) {
                    dst[k] = plane.row(k)[x];
                }
            }
        }
    } else if (c == channels) {
        // Channel axis is c (dims==3): scatter each channel into its column.
        anchors = w * h;
        flat.assign(static_cast<std::size_t>(anchors) * channels, 0.0F);
        for (int k = 0; k < channels; ++k) {
            const ncnn::Mat plane = output.channel(k);
            for (int y = 0; y < h; ++y) {
                const float* row = plane.row(y);
                for (int x = 0; x < w; ++x) {
                    flat[(static_cast<std::size_t>(y) * w + x) * channels + k] = row[x];
                }
            }
        }
    } else {
        LUMINA_LOG_ERROR(
            "unexpected ncnn output shape: w={} h={} c={}, no axis matches {} channels", w, h, c,
            channels);
        return results;
    }

    // The anchor count is baked into the model per export size; a mismatch means
    // the wrong model/input pair was used and decoding would be meaningless.
    const int expectedAnchors = expectedAnchorCount(m_config.inputWidth, m_config.inputHeight);
    if (expectedAnchors > 0 && anchors != expectedAnchors) {
        LUMINA_LOG_ERROR("model/input mismatch: {} anchors != expected {} for {}x{}", anchors,
                         expectedAnchors, m_config.inputWidth, m_config.inputHeight);
        return results;
    }

    DecodeOptions options;
    options.numClasses = m_config.numClasses;
    options.scoreThreshold = m_config.scoreThreshold;
    options.nmsThreshold = m_config.nmsThreshold;

    return decodeYolo11(flat, anchors, channels, letterbox, frame.width, frame.height, options);
}

NcnnDetector::NcnnDetector(DetectorConfig config)
    : m_impl(std::make_unique<Impl>(std::move(config))) {}

NcnnDetector::~NcnnDetector() = default;

bool NcnnDetector::load() { return m_impl->load(); }

std::vector<core::Detection> NcnnDetector::detect(const core::Frame& frame) {
    return m_impl->detect(frame);
}

}  // namespace lumina::vision
