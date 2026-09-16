#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace lumina::core {

// Pixel layouts the pipeline may encounter. Only formats we actually decode are
// listed; `Unknown` exists so a default-constructed Frame is explicitly invalid.
enum class PixelFormat {
    Unknown,
    Rgb888,
    Bgr888,
    Rgba8888,
    Nv12,
    Yuv420,
};

// One captured image plus the metadata the vision stages need.
//
// C++ note (for Java readers): this is a plain `struct`, a value type with public
// members. Unlike a Java class it has no implicit heap identity; copying a Frame
// copies the pixel buffer (std::vector owns the bytes). Hot paths `std::move`
// frames between stages to transfer ownership instead of copying.
struct Frame {
    std::vector<std::uint8_t> data;  // contiguous pixel bytes, owned by this frame
    int width = 0;
    int height = 0;
    int stride = 0;  // bytes per row; may exceed width * bytesPerPixel (alignment)
    PixelFormat format = PixelFormat::Unknown;
    std::chrono::steady_clock::time_point capturedAt{};  // for latency (INV-051)

    // True when the frame carries no pixels; lets callers detect "no data yet".
    [[nodiscard]] bool empty() const noexcept { return data.empty(); }

    // Size of the pixel buffer in bytes.
    [[nodiscard]] std::size_t sizeBytes() const noexcept { return data.size(); }
};

}  // namespace lumina::core
