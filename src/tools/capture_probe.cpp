// ---------------------------------------------------------------------------
// lumina_capture_test — on-device libcamera (OV5647) capture probe.
//
// Purpose (Day 0-1 milestone): prove the sysroot's libcamera can open the camera
// and deliver frames on the real Raspberry Pi Zero 2 W, and report the frame
// format/size actually chosen by the pipeline. This is a headless tool: it prints
// text only, never opens a window (INV-021).
//
// It also applies the same camera-mount rotation as the runtime (CHG-0101), so
// `--dump` writes an already-upright frame that can be `scp`d to confirm the
// correction angle (90 vs 270) without guessing from narration. The first frames
// after start() are startup frames captured before auto-exposure converges, so
// `--dump` waits `--warmup` seconds (default 2) before saving the newest frame.
//
// Usage:
//   lumina_capture_test [width] [height] [frames] [--dump <file.ppm>] [--warmup <s>]
// Defaults: 640x480, 30 frames, 5 second timeout, 2 s warm-up when dumping.
//
// Exit code: 0 if at least one frame was captured, 2 if none, 1 on start failure.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "capture/libcamera_source.hpp"
#include "core/config.hpp"
#include "core/config_env.hpp"
#include "core/frame.hpp"
#include "core/frame_transform.hpp"
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

// Mean/min/max of the RGB bytes, so a black (or dark) frame is obvious in the text
// output rather than only in the image.
void printByteStats(const lumina::core::Frame& frame) {
    std::uint8_t minimum = 255;
    std::uint8_t maximum = 0;
    unsigned long long sum = 0;
    unsigned long long count = 0;
    for (int y = 0; y < frame.height; ++y) {
        const std::uint8_t* row =
            frame.data.data() + static_cast<std::size_t>(y) * frame.stride;
        for (int i = 0; i < frame.width * 3; ++i) {
            const std::uint8_t value = row[i];
            if (value < minimum) {
                minimum = value;
            }
            if (value > maximum) {
                maximum = value;
            }
            sum += value;
            ++count;
        }
    }
    const double mean = count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    std::printf("  frame bytes: mean=%.1f min=%u max=%u (a near-zero mean means black)\n", mean,
                static_cast<unsigned>(minimum), static_cast<unsigned>(maximum));
}

// Write one frame as a binary PPM. Returns false (after printing a reason) on any
// I/O error. The frame is already rotated by the capture source.
bool dumpPpm(const lumina::core::Frame& frame, const std::string& path) {
    printByteStats(frame);

    const std::vector<std::uint8_t> bytes = lumina::core::encodePpm(frame);
    if (bytes.empty()) {
        std::fprintf(stderr, "capture probe: cannot dump a non-RGB888/empty frame\n");
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "capture probe: cannot open '%s' for writing\n", path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::fprintf(stderr, "capture probe: failed writing '%s'\n", path.c_str());
        return false;
    }
    return true;
}

void printUsage() {
    std::printf(
        "usage: lumina_capture_test [width] [height] [frames] [--dump <file.ppm>] "
        "[--warmup <s>]\n"
        "  Defaults: 640x480, 30 frames, 2 s warm-up. The frame is rotated per "
        "LUMINA_CAMERA_ROTATION (default 90).\n");
}

}  // namespace

int main(int argc, char** argv) {
    int width = 640;
    int height = 480;
    int targetFrames = 30;
    std::string dumpPath;
    double warmupSeconds = 2.0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "capture probe: --dump needs a file path\n");
                return 1;
            }
            dumpPath = argv[++i];
        } else if (arg == "--warmup") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "capture probe: --warmup needs a number of seconds\n");
                return 1;
            }
            warmupSeconds = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() > 0) {
        width = std::atoi(positional[0].c_str());
    }
    if (positional.size() > 1) {
        height = std::atoi(positional[1].c_str());
    }
    if (positional.size() > 2) {
        targetFrames = std::atoi(positional[2].c_str());
    }
    if (width <= 0) {
        width = 640;
    }
    if (height <= 0) {
        height = 480;
    }
    if (targetFrames <= 0) {
        targetFrames = 30;
    }
    if (warmupSeconds < 0.0) {
        warmupSeconds = 0.0;
    }

    lumina::core::setLogLevel(lumina::core::LogLevel::Info);

    lumina::capture::LibcameraConfig config;
    config.width = width;
    config.height = height;
    // Same mount correction as the runtime (CHG-0101); defaults to 90, overridable.
    config.rotationDegrees =
        lumina::core::applyEnvOverrides(lumina::core::defaultConfig()).cameraRotationDegrees;

    lumina::capture::LibcameraSource source(config);
    if (!source.start()) {
        std::fprintf(stderr, "capture probe: failed to start the camera\n");
        return 1;
    }

    const lumina::core::TimePoint startTime = lumina::core::now();
    const auto startedAt = std::chrono::steady_clock::now();
    const bool dumping = !dumpPath.empty();
    // Give the run enough time to reach the warm-up before the hard deadline.
    const double runSeconds = dumping ? (warmupSeconds + 3.0) : 5.0;
    const auto runMillis = static_cast<long long>(runSeconds * 1000.0);
    const auto deadline = startedAt + std::chrono::milliseconds(runMillis);

    int received = 0;
    lumina::core::Frame lastFrame;
    bool haveFrame = false;
    while (std::chrono::steady_clock::now() < deadline) {
        lumina::core::Frame frame;
        if (!source.getLatest(frame)) {
            // Nothing new yet; yield briefly instead of busy-waiting.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        ++received;
        std::printf("frame %d: %dx%d stride=%d bytes=%zu format=%s\n", received, frame.width,
                    frame.height, frame.stride, frame.sizeBytes(), formatName(frame.format));
        lastFrame = std::move(frame);
        haveFrame = true;

        if (dumping) {
            // Keep the newest frame until the auto-exposure warm-up has elapsed.
            const double elapsed = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - startedAt)
                                       .count();
            if (elapsed >= warmupSeconds) {
                break;
            }
        } else if (received >= targetFrames) {
            break;
        }
    }

    if (dumping && haveFrame) {
        std::printf("dumping newest frame after %.1f s warm-up (rotation %d)\n", warmupSeconds,
                    config.rotationDegrees);
        if (dumpPpm(lastFrame, dumpPath)) {
            std::printf("dumped %dx%d -> %s\n", lastFrame.width, lastFrame.height,
                        dumpPath.c_str());
        }
    }

    const double elapsedMs = lumina::core::msSince(startTime);
    source.stop();

    const double elapsedSeconds = elapsedMs / 1000.0;
    const double fps = elapsedSeconds > 0.0 ? static_cast<double>(received) / elapsedSeconds : 0.0;
    std::printf("captured %d frame(s) in %.1f ms (%.2f FPS)\n", received, elapsedMs, fps);

    return received > 0 ? 0 : 2;
}
