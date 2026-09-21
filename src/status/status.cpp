// ---------------------------------------------------------------------------
// Status snapshot formatting and frame-luminance measurement (FR-11).
//
// Everything here is pure (no file, no socket, no hardware) so it is unit-tested
// on the host without a camera (INV-030, AGENTS §9).
// ---------------------------------------------------------------------------

#include "status/status.hpp"

#include <algorithm>
#include <cstdint>
#include <format>

namespace lumina::status {

namespace {

// Cap on how many pixels meanLuma() inspects, so a large frame costs the same as a
// 320x256 one. A few thousand samples give a stable mean for a status readout.
constexpr std::size_t kMaxLumaSamples = 4096;

// Byte offset of pixel (x, y) for a tightly sampled format. The caller compares it
// against the buffer size (plus the pixel width) so a malformed frame is skipped
// instead of reading past the end of the buffer.
[[nodiscard]] std::size_t packedOffset(const core::Frame& frame, int x, int y, std::size_t bytesPerPixel)
{
    const std::size_t rowBytes = static_cast<std::size_t>(frame.stride);
    const std::size_t offset =
        static_cast<std::size_t>(y) * rowBytes + static_cast<std::size_t>(x) * bytesPerPixel;
    return offset;
}

}  // namespace

std::string toKeyValue(const StatusSnapshot& snapshot)
{
    // std::format keeps the field order stable and mirrors the layout documented in
    // docs/COMPANION.md. Booleans are written as 0/1 for trivial parsing.
    std::string text =
        std::format("running={}\nuptime_s={}\nfps={:.1f}\nrss_mb={:.1f}\n"
                    "mean_luma={:.3f}\nface_count={}\nsink_ready={}\n",
                    snapshot.running ? 1 : 0, snapshot.uptimeSeconds, snapshot.fps,
                    snapshot.rssMb, snapshot.meanLuma, snapshot.faceCount,
                    snapshot.sinkReady ? 1 : 0);
    // One "person=" line per enrolled name, so a name may contain any character
    // except a newline. The agent collects these into the JSON `people` array.
    for (const std::string& person : snapshot.people) {
        text += "person=";
        text += person;
        text += '\n';
    }
    return text;
}

float meanLuma(const core::Frame& frame)
{
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return -1.0F;  // no pixels / malformed: caller must treat as "unknown"
    }
    if (frame.format == core::PixelFormat::Unknown) {
        return -1.0F;
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    if (pixelCount == 0) {
        return -1.0F;
    }

    // Sample every `step`-th pixel; step >= 1 always, so a tiny frame is exact.
    const std::size_t step = std::max<std::size_t>(1, pixelCount / kMaxLumaSamples);

    // Bytes per pixel for the packed formats; 0 selects the planar (Y-plane) path.
    std::size_t bytesPerPixel = 0;
    switch (frame.format) {
    case core::PixelFormat::Rgb888:
    case core::PixelFormat::Bgr888:
        bytesPerPixel = 3;
        break;
    case core::PixelFormat::Rgba8888:
        bytesPerPixel = 4;
        break;
    case core::PixelFormat::Nv12:
    case core::PixelFormat::Yuv420:
        bytesPerPixel = 0;  // luminance is the first plane (width*height bytes)
        break;
    case core::PixelFormat::Unknown:
        return -1.0F;
    }

    double sum = 0.0;
    std::size_t samples = 0;
    for (std::size_t index = 0; index < pixelCount; index += step) {
        const int x = static_cast<int>(index % static_cast<std::size_t>(frame.width));
        const int y = static_cast<int>(index / static_cast<std::size_t>(frame.width));
        float luma = -1.0F;

        if (bytesPerPixel == 0) {
            // Planar Y: one byte per pixel at y*stride + x.
            const std::size_t offset = packedOffset(frame, x, y, 1);
            if (offset < frame.data.size()) {
                luma = static_cast<float>(frame.data[offset]) / 255.0F;
            }
        } else {
            const std::size_t offset = packedOffset(frame, x, y, bytesPerPixel);
            if (offset + bytesPerPixel <= frame.data.size()) {
                const std::uint8_t* pixel = frame.data.data() + offset;
                // Rgb888 stores R,G,B; Bgr888 stores B,G,R. Green is at index 1
                // for both, so only R and B need the format-dependent swap.
                const std::uint8_t r = frame.format == core::PixelFormat::Bgr888 ? pixel[2] : pixel[0];
                const std::uint8_t g = pixel[1];
                const std::uint8_t b = frame.format == core::PixelFormat::Bgr888 ? pixel[0] : pixel[2];
                const float rf = static_cast<float>(r);
                const float gf = static_cast<float>(g);
                const float bf = static_cast<float>(b);
                luma = (0.299F * rf + 0.587F * gf + 0.114F * bf) / 255.0F;
            }
        }

        if (luma >= 0.0F) {
            sum += static_cast<double>(luma);
            ++samples;
        }
    }

    if (samples == 0) {
        return -1.0F;
    }
    return static_cast<float>(sum / static_cast<double>(samples));
}

}  // namespace lumina::status
