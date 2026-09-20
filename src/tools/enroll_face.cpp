// ---------------------------------------------------------------------------
// lumina_enroll — enroll one named person into the on-device face store (FR-04).
//
// Two sources:
//   - Photos:  --image <file> (repeatable) and/or --images-dir <folder>
//   - Camera:  --camera   (live capture; only if built with LUMINA_ENABLE_LIBCAMERA)
//
// Each source produces face embeddings (YuNet detection + SFace feature) that are
// appended to the persisted store under `--name`. Run once per person; enrollment
// data never leaves the device (FR-03.4). Headless: text only (INV-021).
//
// Usage:
//   lumina_enroll --name <nombre> [--image FILE ...] [--images-dir DIR]
//                 [--camera [--frames N]] [--embeddings K]
//                 [--model-dir DIR] [--store PATH]
// Defaults: model-dir models/face, store <model-dir>/embeddings.bin, K 10.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/logging.hpp"
#include "core/time.hpp"
#include "vision/face.hpp"
#include "vision/face_store.hpp"
#include "vision/image_list.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#if defined(LUMINA_HAS_LIBCAMERA)
#include "capture/libcamera_source.hpp"
#include "core/frame.hpp"
#endif

namespace {

// Longest side (px) an enrollment photo is downscaled to before detection. It
// bounds YuNet's memory on the 512 MB Pi and keeps typical faces inside YuNet's
// reliable ~10-300 px range (INV-052). The runtime binary does not link OpenCV's
// image codecs; only this tool does, so the heavy GDAL dependency stays out of
// the runtime (see CMakeLists.txt).
constexpr int kMaxDetectionSide = 1280;

// Decode one photo (cv::imread gives BGR, applying EXIF rotation), downscale it
// if very large, and return its face embedding (nullopt when unreadable or no
// face). The decoded buffer is fed to the embedder, which never stores it.
std::optional<lumina::vision::Embedding> embedPhoto(lumina::vision::FaceEmbedder& embedder,
                                                    const std::string& path)
{
    const cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        LUMINA_LOG_WARN("enroll: cannot read image '{}'", path);
        return std::nullopt;
    }
    const int longestSide = std::max(image.cols, image.rows);
    if (longestSide > kMaxDetectionSide) {
        const double scale = static_cast<double>(kMaxDetectionSide) /
                             static_cast<double>(longestSide);
        cv::Mat resized;
        cv::resize(image, resized, cv::Size(), scale, scale, cv::INTER_AREA);
        return embedder.embedBgrImage(resized.data, resized.cols, resized.rows,
                                      static_cast<int>(resized.step));
    }
    return embedder.embedBgrImage(image.data, image.cols, image.rows,
                                  static_cast<int>(image.step));
}

// Parsed command-line options. Kept as a plain struct so main() stays readable.
struct Options {
    std::string name;
    std::vector<std::string> images;  // explicit --image values, in order
    std::string imagesDir;            // optional --images-dir
    bool camera = false;
    int frames = 8;                   // camera capture target
    int embeddings = 10;              // embeddings kept per person (cap)
    std::string modelDir = "models/face";
    std::string store;                // empty => <modelDir>/embeddings.bin
    bool help = false;
};

void printUsage()
{
    std::fprintf(stderr,
                 "usage: lumina_enroll --name <nombre> [--image FILE ...] [--images-dir DIR]\n"
                 "                      [--camera [--frames N]] [--embeddings K]\n"
                 "                      [--model-dir DIR] [--store PATH]\n");
}

// Parse argv into `options`. Returns false and fills `error` on a bad flag or a
// missing value. Unknown flags are rejected so a typo cannot silently do nothing.
bool parseArgs(int argc, char** argv, Options& options, std::string& error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        // Helper: consume the value that follows a flag.
        const auto takeValue = [&](const char* flag, std::string& out) -> bool {
            if (i + 1 >= argc) {
                error = std::string("missing value for ") + flag;
                return false;
            }
            out = argv[++i];
            return true;
        };

        if (argument == "--name") {
            if (!takeValue("--name", options.name)) {
                return false;
            }
        } else if (argument == "--image") {
            std::string value;
            if (!takeValue("--image", value)) {
                return false;
            }
            options.images.push_back(std::move(value));
        } else if (argument == "--images-dir") {
            if (!takeValue("--images-dir", options.imagesDir)) {
                return false;
            }
        } else if (argument == "--camera") {
            options.camera = true;
        } else if (argument == "--frames") {
            std::string value;
            if (!takeValue("--frames", value)) {
                return false;
            }
            options.frames = std::atoi(value.c_str());
        } else if (argument == "--embeddings") {
            std::string value;
            if (!takeValue("--embeddings", value)) {
                return false;
            }
            options.embeddings = std::atoi(value.c_str());
        } else if (argument == "--model-dir") {
            if (!takeValue("--model-dir", options.modelDir)) {
                return false;
            }
        } else if (argument == "--store") {
            if (!takeValue("--store", options.store)) {
                return false;
            }
        } else if (argument == "-h" || argument == "--help") {
            options.help = true;
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    lumina::core::setLogLevel(lumina::core::LogLevel::Info);

    Options options;
    std::string error;
    if (!parseArgs(argc, argv, options, error)) {
        std::fprintf(stderr, "enroll: %s\n", error.c_str());
        printUsage();
        return 2;
    }
    if (options.help) {
        printUsage();
        return 0;
    }
    if (options.name.empty()) {
        std::fprintf(stderr, "enroll: --name is required\n");
        printUsage();
        return 2;
    }
    if (options.frames <= 0) {
        options.frames = 8;
    }
    if (options.embeddings <= 0) {
        options.embeddings = 10;
    }
    if (options.store.empty()) {
        options.store = options.modelDir + "/embeddings.bin";
    }

    const bool hasPhotos = !options.images.empty() || !options.imagesDir.empty();
    if (!hasPhotos && !options.camera) {
        std::fprintf(stderr, "enroll: provide --image/--images-dir or --camera\n");
        printUsage();
        return 2;
    }
#if !defined(LUMINA_HAS_LIBCAMERA)
    if (options.camera) {
        std::fprintf(stderr, "enroll: this build has no camera support "
                             "(rebuild with LUMINA_ENABLE_LIBCAMERA=ON)\n");
        return 2;
    }
#endif

    // Configure the OpenCV face path from the (possibly overridden) model dir.
    lumina::vision::FaceModelConfig faceConfig;
    faceConfig.yunetPath = options.modelDir + "/yunet.onnx";
    faceConfig.sfacePath = options.modelDir + "/sface.onnx";
    faceConfig.storePath = options.store;
    faceConfig.maxEmbeddingsPerPerson = options.embeddings;

    lumina::vision::FaceEmbedder embedder(faceConfig);
    if (!embedder.load()) {
        std::fprintf(stderr, "enroll: failed to load the face models from '%s'\n",
                     options.modelDir.c_str());
        return 1;
    }

    // Load any existing enrollment so we append, not overwrite (FR-04.3). A file
    // that exists but cannot be read is treated as an error: continuing would
    // silently discard everyone already enrolled.
    lumina::vision::FaceStore store;
    store.setModelId(lumina::vision::kFaceModelId);
    std::error_code existsError;
    const bool storeExists = std::filesystem::exists(options.store, existsError);
    if (existsError) {
        std::fprintf(stderr, "enroll: cannot access '%s': %s\n", options.store.c_str(),
                     existsError.message().c_str());
        return 1;
    }
    if (storeExists) {
        if (!store.load(options.store)) {
            std::fprintf(stderr, "enroll: existing store '%s' could not be read; "
                                 "move it aside to start over\n",
                         options.store.c_str());
            return 1;
        }
        if (!store.modelId().empty() && store.modelId() != lumina::vision::kFaceModelId) {
            std::fprintf(stderr, "enroll: store '%s' was written by a different model ('%s'); "
                                 "move it aside to start over\n",
                         options.store.c_str(), store.modelId().c_str());
            return 1;
        }
        store.setModelId(lumina::vision::kFaceModelId);  // adopt the tag if it was empty
        LUMINA_LOG_INFO("enroll: appending to the existing store at '{}'", options.store);
    } else {
        LUMINA_LOG_INFO("enroll: no existing store; creating a new one at '{}'", options.store);
    }

    const std::size_t cap = static_cast<std::size_t>(options.embeddings);
    int collected = 0;
    std::printf("Enrolling '%s'.\n", options.name.c_str());

    // --- Photo enrollment ----------------------------------------------------
    std::vector<std::string> photoPaths = options.images;
    if (!options.imagesDir.empty()) {
        const std::vector<std::string> fromDir = lumina::vision::listImageFiles(options.imagesDir);
        if (fromDir.empty()) {
            std::fprintf(stderr, "enroll: no image files found in '%s'\n",
                         options.imagesDir.c_str());
        }
        photoPaths.insert(photoPaths.end(), fromDir.begin(), fromDir.end());
    }
    for (const std::string& path : photoPaths) {
        if (static_cast<std::size_t>(collected) >= cap) {
            std::printf("  reached the %zu-embedding cap; ignoring remaining photos\n", cap);
            break;
        }
        std::optional<lumina::vision::Embedding> feature = embedPhoto(embedder, path);
        if (!feature.has_value()) {
            std::printf("  skipped '%s' (unreadable or no face)\n", path.c_str());
            continue;
        }
        store.addEmbedding(options.name, std::move(*feature), cap);
        ++collected;
        std::printf("  captured embedding %d from '%s'\n", collected, path.c_str());
    }

    // --- Camera enrollment (optional) ---------------------------------------
#if defined(LUMINA_HAS_LIBCAMERA)
    if (options.camera) {
        lumina::capture::LibcameraConfig cameraConfig;  // default 640x480
        lumina::capture::LibcameraSource camera(cameraConfig);
        if (!camera.start()) {
            std::fprintf(stderr, "enroll: failed to start the camera\n");
            return 1;
        }
        std::printf("Enrolling '%s' from the camera: look straight at it.\n", options.name.c_str());

        const int cameraTarget = std::min(options.frames, options.embeddings);
        const auto deadline = lumina::core::now() + std::chrono::seconds(60);
        while (collected < cameraTarget && lumina::core::now() < deadline) {
            lumina::core::Frame frame;
            if (!camera.getLatest(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));  // no new frame yet
                continue;
            }
            std::optional<lumina::vision::Embedding> feature = embedder.embedLargestFace(frame);
            if (!feature.has_value()) {
                std::printf("  (no face detected — keep looking at the camera)\n");
                continue;
            }
            store.addEmbedding(options.name, std::move(*feature), cap);
            ++collected;
            std::printf("  captured embedding %d/%d\n", collected, cameraTarget);
            // Pause so consecutive captures catch slightly different head angles.
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        camera.stop();
    }
#endif

    if (collected == 0) {
        std::fprintf(stderr, "enroll: no face was captured; no data written\n");
        return 2;
    }

    if (!store.save(options.store)) {
        std::fprintf(stderr, "enroll: failed to save the store to '%s'\n", options.store.c_str());
        return 1;
    }

    std::printf("Enrolled '%s' with %d embedding(s) into '%s'.\n", options.name.c_str(), collected,
                options.store.c_str());
    return 0;
}
