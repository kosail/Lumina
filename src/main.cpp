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
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "app/describer.hpp"
#include "app/pipeline.hpp"
#include "audio/bluealsa_sink.hpp"
#include "audio/caching_tts.hpp"
#include "audio/piper_tts.hpp"
#include "audio/sink_watchdog.hpp"
#include "capture/libcamera_source.hpp"
#include "core/config_env.hpp"
#include "core/logging.hpp"
#include "sensors/proximity.hpp"
#include "status/status_writer.hpp"
#include "vision/face_recognizer.hpp"
#include "vision/ncnn_detector.hpp"

#if defined(LUMINA_HAS_FACE)
#include "vision/face.hpp"  // OpenCV implementation; only compiled when enabled
#endif

namespace {

// Set by the signal handler; polled by main(). std::atomic<bool> is lock-free on
// the platforms we target, so writing it from a signal handler is safe.
std::atomic<bool> g_running{true};

// Last stop signal received, for diagnostics only (logged when the audio-sink
// wait is interrupted). `volatile std::sig_atomic_t` is the only type the C++
// standard guarantees is safe to assign from a signal handler.
volatile std::sig_atomic_t g_stopSignal = 0;

extern "C" void handleSignal(int signal)
{
    g_stopSignal = signal;
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

    // Demo hardening (CHG-0098): a curated set of fields may be overridden from the
    // environment so the deployed unit can tune behavior without a rebuild (above
    // all, never power the device off when the audio sink is absent). Invalid
    // values are ignored; clampConfig bounds everything for the rest of the run.
    const lumina::core::Config config =
        lumina::core::clampConfig(lumina::core::applyEnvOverrides(lumina::core::defaultConfig()));

    lumina::capture::LibcameraConfig cameraConfig;
    lumina::capture::LibcameraSource camera(cameraConfig);

    lumina::vision::DetectorConfig detectorConfig;
    detectorConfig.modelDir = modelDir;
    detectorConfig.inputWidth = config.inferWidth;
    detectorConfig.inputHeight = config.inferHeight;
    detectorConfig.scoreThreshold = config.scoreThreshold;
    detectorConfig.nmsThreshold = config.nmsThreshold;
    // Reserve one of the four cores for OpenCV face inference, Piper synthesis and
    // libcamera (CHG-0071): with all four cores on YOLO those stages contend and
    // inflate latency under memory pressure. The FPS cost is small (INV-050).
    detectorConfig.numThreads = 3;
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

    // Face recognition (FR-03) is optional: it needs OpenCV (LUMINA_HAS_FACE) and
    // a fetched/enrolled store. When unavailable the pipeline runs without it, so
    // a partial build never fails to start.
    lumina::vision::IFaceRecognizer* faceRecognizer = nullptr;
    std::vector<std::string> people;  // enrolled names, reported in the status (FR-11)
#if defined(LUMINA_HAS_FACE)
    lumina::vision::FaceModelConfig faceConfig;  // defaults: models/face/{yunet,sface}.onnx
    // core::Config is the single source of truth for the runtime face tuning, so
    // the approved threshold/margin/ROI actually take effect (they were otherwise
    // ignored in favor of the FaceModelConfig defaults).
    faceConfig.matchThreshold = config.faceMatchThreshold;
    faceConfig.matchMargin = config.faceMatchMargin;
    faceConfig.roiFraction = config.faceRoiFraction;
    faceConfig.detectionSide = config.faceDetectionSide;
    lumina::vision::FaceRecognizer faceRecognizerImpl(faceConfig);
    if (faceRecognizerImpl.load()) {
        faceRecognizer = &faceRecognizerImpl;
        people = faceRecognizerImpl.store().names();
    } else {
        LUMINA_LOG_WARN("face recognition unavailable; continuing without it");
    }
#endif

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
#if defined(LUMINA_HAS_FACE)
    // Pre-warm a greeting for every enrolled person so recognition can speak
    // instantly ("<nombre> está enfrente"), not lazily mid-demo (INV-051).
    if (faceRecognizer != nullptr) {
        for (const std::string& name : faceRecognizerImpl.store().names()) {
            warmPhrases.push_back(lumina::i18n::greeting(name));
        }
    }
#endif
    const std::size_t rendered = tts.warm(warmPhrases);
    LUMINA_LOG_INFO("phrase cache: {} new phrase(s) rendered into '{}'",
                    rendered,
                    cacheConfig.directory);

    // Pre-show check (CHG-0098/CHG-0099): LUMINA_WARM_ONLY=1 loads the models,
    // renders any missing phrases, reports the cache size, and exits before the sink
    // wait and the pipeline (exit 0 = cache ready, 1 = not writable). It needs
    // neither the earbuds nor the camera, so it can be run seconds before the show.
    bool warmOnly = false;
    if (const char* warmOnlyEnv = std::getenv("LUMINA_WARM_ONLY")) {
        warmOnly = lumina::core::parseEnvBool(warmOnlyEnv).value_or(false);
    }
    if (warmOnly) {
        // cacheReady() is false when the cache directory is not usable, so the
        // check can gate a pre-show script (exit non-zero) instead of always 0.
        const bool ready = tts.cacheReady();
        LUMINA_LOG_INFO("warm-only: {} phrase(s) requested, {} newly rendered, cache '{}' {}",
                        warmPhrases.size(), rendered, cacheConfig.directory,
                        ready ? "ready" : "NOT WRITABLE");
        return ready ? 0 : 1;
    }

    lumina::audio::AlsaConfig alsaConfig; // defaults to the bluealsa PCM
    lumina::audio::AlsaSink sink(alsaConfig);

    // Install the stop handlers before the sink wait so Ctrl-C during the (up to
    // 3-minute) wait is observed instead of killing the process outright.
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // FR-06.1: never crash when the Bluetooth sink is absent. The bluealsa PCM is
    // only present while the single paired earbud is connected, so a cold boot may
    // need to wait. Retry, and on exhaustion request a host power-off; either way
    // we exit cleanly instead of failing inside pipeline.start().
    lumina::audio::SinkWaitConfig sinkWaitConfig;
    sinkWaitConfig.retryIntervalMs = config.audioSinkRetryIntervalMs;
    sinkWaitConfig.maxRetries = config.audioSinkMaxRetries;
    sinkWaitConfig.shutdownOnFailure = config.audioSinkShutdownOnFailure;
    lumina::audio::SinkWatchdog sinkWatchdog(sinkWaitConfig);
    const lumina::audio::SinkWaitResult sinkResult = sinkWatchdog.awaitReady(sink, g_running);
    if (sinkResult != lumina::audio::SinkWaitResult::Ready) {
        // Interrupted means SIGINT/SIGTERM arrived during the wait: a normal stop.
        // Exhausted means the sink never appeared; exit code 2 is listed in
        // RestartPreventExitStatus so systemd does not restart-loop (see
        // scripts/lumina.service). Log which happened so it is never silent again.
        if (sinkResult == lumina::audio::SinkWaitResult::Interrupted) {
            LUMINA_LOG_WARN("audio sink wait interrupted by signal {}; stopping",
                            static_cast<int>(g_stopSignal));
            return 0;
        }
        return 2;
    }

    // Front proximity sensor (FR-10). The factory returns a Null sensor when the
    // feature is not compiled in or is disabled, so injection is always safe; a
    // failed init also falls back to Null rather than failing startup.
    std::unique_ptr<lumina::sensors::IProximitySensor> proximity =
        lumina::sensors::makeProximitySensor(config);
    if (!proximity->init()) {
        LUMINA_LOG_WARN("proximity sensor unavailable; continuing without it");
        proximity = std::make_unique<lumina::sensors::NullProximitySensor>();
    }

    // Companion status snapshot (FR-11). The runtime stays network-free (INV-003):
    // it only writes a local key=value file that the separate lumina_agent reads.
    // The path defaults to /run (tmpfs, so no SD wear) and can be overridden with
    // LUMINA_STATUS_PATH. Failing to write is never fatal: the status thread just
    // logs once and keeps the pipeline running.
    lumina::app::PipelineConfig pipelineConfig;
    pipelineConfig.people = people;
#if defined(LUMINA_HAS_STATUS)
    const char* statusPathEnv = std::getenv("LUMINA_STATUS_PATH");
    const std::string statusPath = statusPathEnv != nullptr ? statusPathEnv : "/run/lumina/status";
    lumina::status::FileStatusWriter statusWriter(statusPath);
    pipelineConfig.statusPublisher = &statusWriter;
    LUMINA_LOG_INFO("status: publishing to '{}'", statusPath);
#endif

    lumina::app::Pipeline pipeline(&camera, &detector, &tts, &sink, config, pipelineConfig,
                                   faceRecognizer, proximity.get());
    if (!pipeline.start()) {
        LUMINA_LOG_ERROR("pipeline failed to start");
        return 1;
    }

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
