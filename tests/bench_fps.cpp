// ---------------------------------------------------------------------------
// lumina_bench_fps — on-device YOLO11n throughput/memory/thermal benchmark.
//
// Runs the detector on a synthetic frame (no camera needed) so the number is
// pure inference cost, which is what INV-050 (>= 5 FPS) is about. Also reports
// peak resident memory and the SoC temperature.
//
// Usage:
//   lumina_bench_fps <modelDir> <inputWidth> <inputHeight> [iterations] [--threads N]
// Examples:
//   lumina_bench_fps models/yolo11n_ncnn_320x256 320 256 200
//   lumina_bench_fps models/yolo11n_ncnn_256 256 256 200 --threads 1
//
// `--threads N` sets the NCNN worker count (default 4, the Pi Zero 2 W core
// count). Sweeping 1..4 shows how well the workload scales and whether we are
// compute- or synchronization-bound.
//
// Exit code: 0 on success, 1 if the model fails to load, 2 on bad arguments.
// ---------------------------------------------------------------------------

#include <sys/resource.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/frame.hpp"
#include "core/logging.hpp"
#include "core/time.hpp"
#include "vision/ncnn_detector.hpp"

namespace {

// Build a synthetic RGB888 frame (a simple gradient so the input is not degenerate).
lumina::core::Frame makeSyntheticFrame(int width, int height) {
    lumina::core::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = lumina::core::PixelFormat::Rgb888;
    frame.data.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * frame.stride + x * 3;
            frame.data[index + 0] = static_cast<std::uint8_t>(x & 0xFF);
            frame.data[index + 1] = static_cast<std::uint8_t>(y & 0xFF);
            frame.data[index + 2] = static_cast<std::uint8_t>((x + y) & 0xFF);
        }
    }
    return frame;
}

// Peak resident set size in MiB (Linux reports ru_maxrss in kibibytes).
double residentMegabytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
}

// SoC temperature in Celsius, or a negative value when unavailable.
double socTemperatureCelsius() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    long milliDegrees = 0;
    if (file >> milliDegrees) {
        return static_cast<double>(milliDegrees) / 1000.0;
    }
    return -1.0;
}

void printUsage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s <modelDir> <inputWidth> <inputHeight> [iterations] [--threads N]\n"
                 "  modelDir     directory with model.ncnn.param + model.ncnn.bin\n"
                 "  inputWidth   network input width in px (must match the export)\n"
                 "  inputHeight  network input height in px (must match the export)\n"
                 "  iterations   timed runs (default 200)\n"
                 "  --threads N  NCNN worker threads (default 4)\n",
                 program);
}

}  // namespace

int main(int argc, char** argv) {
    std::string modelDir;
    int inputWidth = 0;
    int inputHeight = 0;
    int iterations = 200;
    int threads = 4;
    int positionals = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "--threads") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "bench: --threads needs a value\n");
                return 2;
            }
            threads = std::atoi(argv[++i]);
        } else if (arg.rfind("--threads=", 0) == 0) {
            threads = std::atoi(arg.c_str() + std::string("--threads=").size());
        } else {
            switch (positionals) {
                case 0:
                    modelDir = arg;
                    break;
                case 1:
                    inputWidth = std::atoi(arg.c_str());
                    break;
                case 2:
                    inputHeight = std::atoi(arg.c_str());
                    break;
                case 3:
                    iterations = std::atoi(arg.c_str());
                    break;
                default:
                    std::fprintf(stderr, "bench: unexpected argument '%s'\n", arg.c_str());
                    return 2;
            }
            ++positionals;
        }
    }

    if (positionals < 3 || inputWidth <= 0 || inputHeight <= 0 || iterations <= 0 || threads <= 0) {
        printUsage(argv[0]);
        return 2;
    }

    lumina::core::setLogLevel(lumina::core::LogLevel::Info);

    lumina::vision::DetectorConfig config;
    config.modelDir = modelDir;
    config.inputWidth = inputWidth;
    config.inputHeight = inputHeight;
    config.numThreads = threads;

    lumina::vision::NcnnDetector detector(config);
    if (!detector.load()) {
        std::fprintf(stderr, "bench: failed to load model from '%s'\n", modelDir.c_str());
        return 1;
    }

    // A realistic capture size; the detector will letterbox it to the input size.
    const lumina::core::Frame frame = makeSyntheticFrame(640, 480);

    // Warm-up: let NCNN allocate its buffers and the CPU reach a steady clock.
    for (int i = 0; i < 5; ++i) {
        (void)detector.detect(frame);
    }

    double totalMs = 0.0;
    double minMs = 1e9;
    double maxMs = 0.0;
    std::size_t lastCount = 0;

    const lumina::core::TimePoint start = lumina::core::now();
    for (int i = 0; i < iterations; ++i) {
        const lumina::core::TimePoint frameStart = lumina::core::now();
        const std::vector<lumina::core::Detection> detections = detector.detect(frame);
        const double ms = lumina::core::msSince(frameStart);
        totalMs += ms;
        minMs = ms < minMs ? ms : minMs;
        maxMs = ms > maxMs ? ms : maxMs;
        lastCount = detections.size();
    }
    const double elapsedMs = lumina::core::msSince(start);

    const double averageMs = totalMs / static_cast<double>(iterations);
    const double fps = elapsedMs > 0.0 ? (static_cast<double>(iterations) * 1000.0) / elapsedMs : 0.0;
    const double temperature = socTemperatureCelsius();

    std::printf("model        : %s (%dx%d)\n", modelDir.c_str(), inputWidth, inputHeight);
    std::printf("threads      : %d\n", threads);
    std::printf("iterations   : %d\n", iterations);
    std::printf("latency      : min %.2f ms  avg %.2f ms  max %.2f ms\n", minMs, averageMs, maxMs);
    std::printf("throughput   : %.2f FPS\n", fps);
    std::printf("peak RSS     : %.1f MB\n", residentMegabytes());
    if (temperature >= 0.0) {
        std::printf("soc temp     : %.1f C\n", temperature);
    } else {
        std::printf("soc temp     : n/a\n");
    }
    std::printf("detections   : %zu (synthetic input)\n", lastCount);

    return 0;
}
