// ---------------------------------------------------------------------------
// Frame geometry helpers implementation (see core/frame_transform.hpp).
// ---------------------------------------------------------------------------

#include "core/frame_transform.hpp"

#include <cstddef>
#include <string>

namespace lumina::core {

namespace {

constexpr int kBytesPerPixel = 3;  // RGB888

// True for the four right angles we support.
[[nodiscard]] bool isRightAngle(int degrees) noexcept
{
    return degrees == 90 || degrees == 180 || degrees == 270;
}

}  // namespace

Frame rotateFrame(const Frame& frame, int degrees)
{
    // Nothing to do for 0, an unsupported angle, a non-RGB frame, or an empty one:
    // return a plain copy so the caller always gets a valid frame back.
    if (degrees == 0 || !isRightAngle(degrees) || frame.format != PixelFormat::Rgb888 ||
        frame.empty() || frame.width <= 0 || frame.height <= 0) {
        return frame;
    }

    const int inWidth = frame.width;
    const int inHeight = frame.height;
    // 90/270 swap the axes; 180 keeps them.
    const bool swap = (degrees == 90 || degrees == 270);
    const int outWidth = swap ? inHeight : inWidth;
    const int outHeight = swap ? inWidth : inHeight;
    const int outStride = outWidth * kBytesPerPixel;

    Frame out;
    out.width = outWidth;
    out.height = outHeight;
    out.stride = outStride;
    out.format = frame.format;
    out.capturedAt = frame.capturedAt;
    out.data.assign(static_cast<std::size_t>(outStride) * static_cast<std::size_t>(outHeight), 0);

    const std::uint8_t* inData = frame.data.data();
    std::uint8_t* outData = out.data.data();

    for (int y = 0; y < inHeight; ++y) {
        const std::uint8_t* inRow = inData + static_cast<std::size_t>(y) * frame.stride;
        for (int x = 0; x < inWidth; ++x) {
            const std::uint8_t* src = inRow + static_cast<std::size_t>(x) * kBytesPerPixel;

            int outX = 0;
            int outY = 0;
            if (degrees == 90) {
                // Clockwise: the top row becomes the right column.
                outX = inHeight - 1 - y;
                outY = x;
            } else if (degrees == 180) {
                outX = inWidth - 1 - x;
                outY = inHeight - 1 - y;
            } else {  // 270: counter-clockwise, the top row becomes the left column.
                outX = y;
                outY = inWidth - 1 - x;
            }

            std::uint8_t* dst =
                outData + static_cast<std::size_t>(outY) * outStride +
                static_cast<std::size_t>(outX) * kBytesPerPixel;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }

    return out;
}

std::vector<std::uint8_t> encodePpm(const Frame& frame)
{
    if (frame.format != PixelFormat::Rgb888 || frame.empty() || frame.width <= 0 ||
        frame.height <= 0) {
        return {};
    }

    // PPM (P6) header: magic, width, height, max value, then raw RGB bytes.
    const std::string header = "P6\n" + std::to_string(frame.width) + " " +
                               std::to_string(frame.height) + "\n255\n";

    std::vector<std::uint8_t> bytes(header.begin(), header.end());
    bytes.reserve(bytes.size() +
                  static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) *
                      kBytesPerPixel);

    // Copy row by row so a padded `stride` is stripped and the PPM stays tightly packed.
    const int rowBytes = frame.width * kBytesPerPixel;
    const std::uint8_t* inData = frame.data.data();
    for (int y = 0; y < frame.height; ++y) {
        const std::uint8_t* inRow = inData + static_cast<std::size_t>(y) * frame.stride;
        bytes.insert(bytes.end(), inRow, inRow + rowBytes);
    }

    return bytes;
}

}  // namespace lumina::core
