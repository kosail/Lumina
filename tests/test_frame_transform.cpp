// ---------------------------------------------------------------------------
// Unit tests for core/frame_transform.hpp — clockwise frame rotation and PPM
// encoding (CHG-0101). Pure logic: no hardware, no clock.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/frame.hpp"
#include "core/frame_transform.hpp"

using lumina::core::encodePpm;
using lumina::core::Frame;
using lumina::core::PixelFormat;
using lumina::core::rotateFrame;

namespace {

// A w x h RGB888 frame with a unique, recognizable pattern per pixel: pixel p has
// R=p, G=100+p, B=200+p. `stride` may exceed w*3 to exercise row padding.
Frame makeFrame(int w, int h, int stride) {
    Frame frame;
    frame.width = w;
    frame.height = h;
    frame.stride = stride;
    frame.format = PixelFormat::Rgb888;
    frame.data.assign(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int p = y * w + x;
            std::uint8_t* px = frame.data.data() + static_cast<std::size_t>(y) * stride +
                               static_cast<std::size_t>(x) * 3;
            px[0] = static_cast<std::uint8_t>(p);
            px[1] = static_cast<std::uint8_t>(100 + p);
            px[2] = static_cast<std::uint8_t>(200 + p);
        }
    }
    return frame;
}

int redAt(const Frame& frame, int x, int y) {
    return frame.data[static_cast<std::size_t>(y) * frame.stride +
                      static_cast<std::size_t>(x) * 3];
}

}  // namespace

TEST_CASE("rotateFrame: 0 degrees is an unchanged copy") {
    const Frame in = makeFrame(2, 3, 6);
    const Frame out = rotateFrame(in, 0);
    CHECK(out.width == 2);
    CHECK(out.height == 3);
    CHECK(out.data == in.data);
}

TEST_CASE("rotateFrame: 90 degrees rotates clockwise and swaps dimensions") {
    // 2x3 -> 3x2. Clockwise: the left column (bottom-to-top) becomes the top row.
    const Frame out = rotateFrame(makeFrame(2, 3, 6), 90);
    CHECK(out.width == 3);
    CHECK(out.height == 2);
    CHECK(out.stride == 9);
    CHECK(redAt(out, 0, 0) == 4);  // bottom-left of the input
    CHECK(redAt(out, 1, 0) == 2);
    CHECK(redAt(out, 2, 0) == 0);  // top-left of the input
    CHECK(redAt(out, 0, 1) == 5);
    CHECK(redAt(out, 1, 1) == 3);
    CHECK(redAt(out, 2, 1) == 1);
}

TEST_CASE("rotateFrame: 180 degrees reverses both axes") {
    const Frame out = rotateFrame(makeFrame(2, 3, 6), 180);
    CHECK(out.width == 2);
    CHECK(out.height == 3);
    CHECK(redAt(out, 0, 0) == 5);  // bottom-right of the input
    CHECK(redAt(out, 1, 0) == 4);
    CHECK(redAt(out, 0, 2) == 1);
    CHECK(redAt(out, 1, 2) == 0);  // top-left of the input
}

TEST_CASE("rotateFrame: 270 degrees rotates counter-clockwise") {
    const Frame out = rotateFrame(makeFrame(2, 3, 6), 270);
    CHECK(out.width == 3);
    CHECK(out.height == 2);
    CHECK(redAt(out, 0, 0) == 1);
    CHECK(redAt(out, 2, 0) == 5);
    CHECK(redAt(out, 0, 1) == 0);
    CHECK(redAt(out, 2, 1) == 4);
}

TEST_CASE("rotateFrame: an unsupported angle or non-RGB frame is unchanged") {
    const Frame in = makeFrame(2, 3, 6);
    CHECK(rotateFrame(in, 45).data == in.data);
    CHECK(rotateFrame(in, 45).width == 2);

    Frame gray = in;
    gray.format = PixelFormat::Yuv420;
    const Frame out = rotateFrame(gray, 90);
    CHECK(out.width == gray.width);
    CHECK(out.data == gray.data);
}

TEST_CASE("rotateFrame: honours a padded stride") {
    // 2x3 with 4 padding bytes per row: stride 10 instead of 6.
    const Frame out = rotateFrame(makeFrame(2, 3, 10), 90);
    CHECK(out.width == 3);
    CHECK(out.height == 2);
    CHECK(out.stride == 9);
    // Same mapping as the tightly packed case; padding must not leak in.
    CHECK(redAt(out, 0, 0) == 4);
    CHECK(redAt(out, 2, 0) == 0);
    CHECK(redAt(out, 2, 1) == 1);
}

TEST_CASE("encodePpm: writes a P6 header and tightly packed pixels") {
    const Frame frame = makeFrame(2, 3, 6);
    const std::vector<std::uint8_t> ppm = encodePpm(frame);

    const std::string header = "P6\n2 3\n255\n";
    REQUIRE(ppm.size() == header.size() + 2u * 3u * 3u);
    CHECK(std::string(reinterpret_cast<const char*>(ppm.data()), header.size()) == header);

    // First pixel (p=0) then the last (p=5) must be the raw RGB triples.
    const std::size_t body = header.size();
    CHECK(ppm[body + 0] == 0);
    CHECK(ppm[body + 1] == 100);
    CHECK(ppm[body + 2] == 200);
    CHECK(ppm[body + 15] == 5);
    CHECK(ppm[body + 16] == 105);
    CHECK(ppm[body + 17] == 205);
}

TEST_CASE("encodePpm: returns empty for a non-RGB or empty frame") {
    Frame gray = makeFrame(2, 3, 6);
    gray.format = PixelFormat::Yuv420;
    CHECK(encodePpm(gray).empty());
    CHECK(encodePpm(Frame{}).empty());
}
