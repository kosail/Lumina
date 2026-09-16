// ---------------------------------------------------------------------------
// Host unit tests for the pure YOLO11 decoder (vision/yolo11_decode.hpp).
//
// These construct synthetic model output (no camera, no NCNN) to verify the box
// math, thresholding, class-aware NMS, and the letterbox reverse mapping.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include "vision/yolo11_decode.hpp"

using lumina::core::Detection;
using lumina::vision::DecodeOptions;
using lumina::vision::decodeYolo11;
using lumina::vision::LetterboxInfo;

namespace {

constexpr int kChannels = 84;  // 4 box values + 80 COCO classes

// Allocate an all-zero output buffer for `anchors` rows.
std::vector<float> makeOutput(int anchors) {
    return std::vector<float>(static_cast<std::size_t>(anchors) * kChannels, 0.0F);
}

// Write one anchor as centre-x, centre-y, width, height + a single class score.
void setAnchor(std::vector<float>& output, int anchor, float cx, float cy, float w, float h,
               int classId, float score) {
    float* row = output.data() + static_cast<std::size_t>(anchor) * kChannels;
    row[0] = cx;
    row[1] = cy;
    row[2] = w;
    row[3] = h;
    row[4 + classId] = score;
}

}  // namespace

TEST_CASE("decodeYolo11 decodes one confident anchor and drops a weak one") {
    std::vector<float> output = makeOutput(2);
    setAnchor(output, 0, 160.0F, 160.0F, 100.0F, 50.0F, 0, 0.9F);   // person
    setAnchor(output, 1, 50.0F, 50.0F, 10.0F, 10.0F, 16, 0.10F);    // dog, below threshold

    LetterboxInfo letterbox;  // scale 1, no padding
    DecodeOptions options;

    const std::vector<Detection> detections =
        decodeYolo11(output, 2, kChannels, letterbox, 320, 320, options);

    REQUIRE(detections.size() == 1);
    CHECK(detections[0].classId == 0);
    CHECK(detections[0].score == doctest::Approx(0.9F));
    CHECK(detections[0].box.x == doctest::Approx(110.0F));
    CHECK(detections[0].box.y == doctest::Approx(135.0F));
    CHECK(detections[0].box.width == doctest::Approx(100.0F));
    CHECK(detections[0].box.height == doctest::Approx(50.0F));
}

TEST_CASE("decodeYolo11 applies class-aware non-maximum suppression") {
    std::vector<float> output = makeOutput(3);
    setAnchor(output, 0, 100.0F, 100.0F, 100.0F, 100.0F, 0, 0.90F);  // person
    setAnchor(output, 1, 105.0F, 105.0F, 100.0F, 100.0F, 0, 0.80F);  // person, heavy overlap -> suppressed
    setAnchor(output, 2, 105.0F, 105.0F, 100.0F, 100.0F, 16, 0.70F); // dog, same box, other class -> kept

    LetterboxInfo letterbox;
    DecodeOptions options;

    const std::vector<Detection> detections =
        decodeYolo11(output, 3, kChannels, letterbox, 320, 320, options);

    REQUIRE(detections.size() == 2);
    CHECK(detections[0].classId == 0);  // highest score first
    CHECK(detections[0].score == doctest::Approx(0.90F));
    CHECK(detections[1].classId == 16);
    CHECK(detections[1].score == doctest::Approx(0.70F));
}

TEST_CASE("decodeYolo11 reverses a letterbox transform") {
    std::vector<float> output = makeOutput(1);
    // A 100x100 box centred at (160,120) in the 320-space letterboxed image.
    setAnchor(output, 0, 160.0F, 120.0F, 100.0F, 100.0F, 0, 0.95F);

    LetterboxInfo letterbox;
    letterbox.scale = 0.5F;   // the frame was downscaled by half
    letterbox.padLeft = 40;   // then padded 40 px on the left
    letterbox.padTop = 0;

    DecodeOptions options;
    const std::vector<Detection> detections =
        decodeYolo11(output, 1, kChannels, letterbox, 640, 480, options);

    REQUIRE(detections.size() == 1);
    // x0 = (160 - 50 - 40)/0.5 = 140 ; y0 = (120 - 50)/0.5 = 140
    CHECK(detections[0].box.x == doctest::Approx(140.0F));
    CHECK(detections[0].box.y == doctest::Approx(140.0F));
    CHECK(detections[0].box.width == doctest::Approx(200.0F));
    CHECK(detections[0].box.height == doctest::Approx(200.0F));
}

TEST_CASE("decodeYolo11 rejects a buffer whose size does not match the shape") {
    std::vector<float> tooSmall(10, 0.0F);
    LetterboxInfo letterbox;
    DecodeOptions options;
    CHECK(decodeYolo11(tooSmall, 2, kChannels, letterbox, 320, 320, options).empty());
}
