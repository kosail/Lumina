// ---------------------------------------------------------------------------
// lumina_capture_test — on-device libcamera (OV5647) capture probe.
//
// Purpose (Day 0-1 milestone): prove the sysroot's libcamera can open the camera
// and deliver frames on the real Raspberry Pi Zero 2 W, and report the frame
// format/size actually chosen by the pipeline. This is a headless tool: it prints
// text only, never opens a window (INV-021).
//
// Usage:
//   lumina_capture_test [width] [height] [frames]
// Defaults: 640x480, 30 frames, 5 second timeout.
//
// Exit code: 0 if at least one frame was captured, 2 if none, 1 on start failure.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <thread>

#include "capture/libcamera_source.hpp"
#include "core/frame.hpp"
#include "core/logging.hpp"
#include "core/time.hpp"

namespace {

// Text name for our pixel-format enum, for the probe's report.
const char* formatName(lumina::core::PixelFormat format) {
    switch (format) {
        case lumina::core::PixelFormat::Rgb888:
            return "RGB888";
        case lumina::core::PixelFormat::Bgr888:
            return "BGR888";
        case lumina::core::PixelFormat::Rgba8888:
            return "RGBA8888";
        case lumina::core::PixelFormat::Nv12:
            return "NV12";
        case lumina::core::PixelFormat::Yuv420:
            return "YUV420";
        case lumina::core::PixelFormat::Unknown:
            return "Unknown";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    int width = argc > 1 ? std::atoi(argv[1]) : 640;
    int height = argc > 2 ? std::atoi(argv[2]) : 480;
    int targetFrames = argc > 3 ? std::atoi(argv[3]) : 30;
    if (width <= 0) {
        width = 640;
    }
    if (height <= 0) {
        height = 480;
    }
    if (targetFrames <= 0) {
        targetFrames = 30;
    }

    lumina::core::setLogLevel(lumina::core::LogLevel::Info);

    lumina::capture::LibcameraConfig config;
    config.width = width;
    config.height = height;

    lumina::capture::LibcameraSource source(config);
    if (!source.start()) {
        std::fprintf(stderr, "capture probe: failed to start the camera\n");
        return 1;
    }

    const lumina::core::TimePoint startTime = lumina::core::now();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    int received = 0;
    while (received < targetFrames && std::chrono::steady_clock::now() < deadline) {
        lumina::core::Frame frame;
        if (!source.getLatest(frame)) {
            // Nothing new yet; yield briefly instead of busy-waiting.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        ++received;
        std::printf("frame %d: %dx%d stride=%d bytes=%zu format=%s\n", received, frame.width,
                    frame.height, frame.stride, frame.sizeBytes(), formatName(frame.format));
    }

    const double elapsedMs = lumina::core::msSince(startTime);
    source.stop();

    const double elapsedSeconds = elapsedMs / 1000.0;
    const double fps = elapsedSeconds > 0.0 ? static_cast<double>(received) / elapsedSeconds : 0.0;
    std::printf("captured %d frame(s) in %.1f ms (%.2f FPS)\n", received, elapsedMs, fps);

    return received > 0 ? 0 : 2;
}
