// ---------------------------------------------------------------------------
// Lúmina beta runtime — program entry point.
//
// On the target (all of libcamera + NCNN + audio enabled) this constructs the
// concrete implementations, injects them into the vertical-slice Pipeline, and
// runs until interrupted. On the host (or any partial configuration) it prints a
// notice and exits, so the project always builds (AGENTS §5).
//
// C++ note (for Java readers): `main` is a free function, not a method. Returning
// 0 signals success to the OS. Names are fully qualified (`lumina::core::...`)
// rather than `using namespace`, matching the rest of the project.
// ---------------------------------------------------------------------------

#include <cstdio>

#include "core/config.hpp"

#if defined(LUMINA_HAS_LIBCAMERA) && defined(LUMINA_HAS_NCNN) && defined(LUMINA_HAS_AUDIO)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "app/describer.hpp"
#include "app/pipeline.hpp"
#include "audio/bluealsa_sink.hpp"
#include "audio/caching_tts.hpp"
#include "audio/piper_tts.hpp"
#include "capture/libcamera_source.hpp"
#include "core/logging.hpp"
#include "vision/ncnn_detector.hpp"

namespace {

// Set by the signal handler; polled by main(). std::atomic<bool> is lock-free on
// the platforms we target, so writing it from a signal handler is safe.
std::atomic<bool> g_running{true};

extern "C" void handleSignal(int /*signal*/)
{
    g_running.store(false, std::memory_order_relaxed);
}

// Raise/lower verbosity from the environment: LUMINA_LOG_LEVEL=debug (or trace,
// info, warn, error, off). Useful to see the per-detection DEBUG lines and the
// default is INFO when unset.
void applyLogLevelFromEnv()
{
    const char* value = std::getenv("LUMINA_LOG_LEVEL");
    if (value == nullptr) {
        return;
    }
    const std::string_view level(value);
    if (level == "trace") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Trace);
    } else if (level == "debug") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Debug);
    } else if (level == "info") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Info);
    } else if (level == "warn") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Warn);
    } else if (level == "error") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Error);
    } else if (level == "off") {
        lumina::core::setLogLevel(lumina::core::LogLevel::Off);
    }
}

// Persistent default for the TTS phrase cache: $HOME/.cache/lumina/phrase-cache
// (falls back to ./phrase-cache if HOME is unset). We deliberately avoid /tmp:
// it is a tmpfs on the target OS, so it would consume RAM (INV-052).
std::string defaultPhraseCacheDir()
{
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.cache/lumina/phrase-cache";
    }
    return "phrase-cache";
}

} // namespace

int main(int argc, char** argv)
{
    applyLogLevelFromEnv();

    // Paths default to the layout used on the Pi; override by argument if needed.
    const std::string modelDir = argc > 1 ? argv[1] : "models/yolo11n_ncnn_320x256";
    const std::string voicePath =
        argc > 2 ? argv[2] : "models/voices/es_MX-claude-high.onnx";
    const std::string espeakDataDir =
        argc > 3 ? argv[3] : "third_party/libpiper/share/espeak-ng-data";

    const lumina::core::Config config = lumina::core::defaultConfig();

    lumina::capture::LibcameraConfig cameraConfig;
    lumina::capture::LibcameraSource camera(cameraConfig);

    lumina::vision::DetectorConfig detectorConfig;
    detectorConfig.modelDir = modelDir;
    detectorConfig.inputWidth = config.inferWidth;
    detectorConfig.inputHeight = config.inferHeight;
    detectorConfig.scoreThreshold = config.scoreThreshold;
    detectorConfig.nmsThreshold = config.nmsThreshold;
    lumina::vision::NcnnDetector detector(detectorConfig);
    if (!detector.load()) {
        LUMINA_LOG_ERROR("failed to load detector from '{}'", modelDir);
        return 1;
    }

    lumina::audio::PiperConfig piperConfig;
    piperConfig.modelPath = voicePath;
    piperConfig.espeakDataDir = espeakDataDir;
    lumina::audio::PiperTts piper(piperConfig);
    if (!piper.load()) {
        LUMINA_LOG_ERROR("failed to load Piper voice '{}'", voicePath);
        return 1;
    }

    // Wrap Piper in the on-disk phrase cache so the fixed narration phrases play
    // with near-zero latency (INV-051). Warm before the pipeline starts.
    lumina::audio::PhraseCacheConfig cacheConfig;
    cacheConfig.tag = voicePath; // tie cached audio to this voice
    cacheConfig.directory = defaultPhraseCacheDir();
    if (const char* cacheDir = std::getenv("LUMINA_PHRASE_CACHE_DIR")) {
        cacheConfig.directory = cacheDir;
    }
    lumina::audio::CachingTts tts(piper, cacheConfig);
    // Warm descriptions AND alert phrases together so both play instantly
    // (INV-051); a safety alert must never be a slow lazy cache miss. Single-class,
    // two-class, and alert phrases are warmed; anything else is a lazy miss. The
    // first run after changing the voice/catalog can take several minutes — this is
    // one-time and persisted in the cache directory.
    std::vector<std::string> warmPhrases = lumina::app::phraseCatalog(config);
    const std::vector<std::string> pairPhrases = lumina::app::twoClassPhraseCatalog(config);
    warmPhrases.insert(warmPhrases.end(), pairPhrases.begin(), pairPhrases.end());
    const std::vector<std::string> alertPhrases = lumina::app::alertPhraseCatalog();
    warmPhrases.insert(warmPhrases.end(), alertPhrases.begin(), alertPhrases.end());
    const std::size_t rendered = tts.warm(warmPhrases);
    LUMINA_LOG_INFO("phrase cache: {} new phrase(s) rendered into '{}'",
                    rendered,
                    cacheConfig.directory);

    lumina::audio::AlsaConfig alsaConfig; // defaults to the bluealsa PCM
    lumina::audio::AlsaSink sink(alsaConfig);

    lumina::app::Pipeline pipeline(&camera, &detector, &tts, &sink, config);
    if (!pipeline.start()) {
        LUMINA_LOG_ERROR("pipeline failed to start");
        return 1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    LUMINA_LOG_INFO("Lúmina running; press Ctrl-C to stop");

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    pipeline.stop();
    LUMINA_LOG_INFO("Lúmina stopped");
    return 0;
}

#else

int main()
{
    std::fprintf(stderr,
                 "Lumina beta runtime: built without the full vertical slice.\n"
                 "Enable LUMINA_ENABLE_LIBCAMERA, LUMINA_ENABLE_NCNN and LUMINA_ENABLE_AUDIO "
                 "(see RAW_PLAN.md and AGENTS.md).\n");
    return 1;
}

#endif
