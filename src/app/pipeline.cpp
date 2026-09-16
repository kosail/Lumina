// ---------------------------------------------------------------------------
// Pipeline implementation (worker threads).
// ---------------------------------------------------------------------------

#include "app/pipeline.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

#include <unistd.h> // sysconf(_SC_PAGESIZE) for the RSS line

#include "app/describer.hpp"
#include "core/logging.hpp"

namespace lumina::app {

namespace {

// Resident set size in MiB from /proc/self/statm (field 2 = resident pages).
// Returns 0 when unavailable; used only for the periodic diagnostic line.
double readRssMb()
{
    std::FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr) {
        return 0.0;
    }
    long residentPages = 0;
    // "%*d" skips field 1 (total size); we only need field 2 (resident pages).
    const int matched = std::fscanf(statm, "%*d %ld", &residentPages);
    std::fclose(statm);
    if (matched != 1) {
        return 0.0;
    }
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        return 0.0;
    }
    return static_cast<double>(residentPages) * static_cast<double>(pageSize) / (1024.0 * 1024.0);
}

} // namespace

Pipeline::Pipeline(capture::ICamera* camera,
                   vision::IDetector* detector,
                   audio::ITtsEngine* tts,
                   audio::IAudioSink* sink,
                   core::Config config,
                   PipelineConfig pipelineConfig)
    : m_camera(camera)
    , m_detector(detector)
    , m_tts(tts)
    , m_sink(sink)
    , m_config(std::move(config))
    , m_pipelineConfig(pipelineConfig)
    , m_frames(pipelineConfig.frameQueueCapacity)
    , m_speech(pipelineConfig.speechQueueCapacity)
{
}

bool Pipeline::start()
{
    if (m_running.exchange(true)) {
        return true; // already running
    }
    if (m_camera == nullptr || m_detector == nullptr || m_tts == nullptr || m_sink == nullptr) {
        LUMINA_LOG_ERROR("Pipeline: a dependency is null");
        m_running.store(false);
        return false;
    }
    if (!m_camera->start()) {
        LUMINA_LOG_ERROR("Pipeline: camera failed to start");
        m_running.store(false);
        return false;
    }
    if (!m_sink->open()) {
        LUMINA_LOG_ERROR("Pipeline: audio sink failed to open");
        m_camera->stop();
        m_running.store(false);
        return false;
    }

    // std::jthread runs the lambda and passes its stop_token; it joins in its
    // destructor (no detached threads, AGENTS §5).
    m_captureThread = std::jthread([this](std::stop_token token) { captureLoop(token); });
    m_inferenceThread = std::jthread([this](std::stop_token token) { inferenceLoop(token); });
    m_speechThread = std::jthread([this](std::stop_token token) { speechLoop(token); });
    return true;
}

void Pipeline::stop() noexcept
{
    if (!m_running.exchange(false)) {
        return; // never started, or already stopped
    }

    // Abort any in-progress utterance, then unblock the loops by closing the queues.
    m_stopSpeech.store(true, std::memory_order_relaxed);
    m_captureThread.request_stop();
    m_inferenceThread.request_stop();
    m_speechThread.request_stop();
    m_frames.close();
    m_speech.close();

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    if (m_inferenceThread.joinable()) {
        m_inferenceThread.join();
    }
    if (m_speechThread.joinable()) {
        m_speechThread.join();
    }

    if (m_sink != nullptr) {
        m_sink->stop(); // drop any audio left buffered
    }
    if (m_camera != nullptr) {
        m_camera->stop();
    }
    if (m_sink != nullptr) {
        m_sink->close();
    }
}

void Pipeline::captureLoop(std::stop_token stopToken)
{
    core::Frame frame;
    while (!stopToken.stop_requested()) {
        if (m_camera->getLatest(frame)) {
            m_frames.push(std::move(frame)); // never blocks; drops the oldest frame
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void Pipeline::inferenceLoop(std::stop_token stopToken)
{
    core::Frame frame;
    std::size_t framesProcessed = 0;
    core::TimePoint windowStart = core::now();

    while (!stopToken.stop_requested()) {
        if (!m_frames.waitPop(frame)) {
            break; // queue closed during shutdown
        }
        ++framesProcessed;

        const std::vector<core::Detection> detections = m_detector->detect(frame);
        const std::string text = describeDetections(detections, m_config);

        if (text.empty()) {
            // Nothing to narrate: reset hysteresis so it restarts cleanly.
            m_candidateText.clear();
            m_candidateCount = 0;
        } else {
            // Hysteresis: require the same phrase for `speechStableFrames` frames
            // so a single flickering detection does not start speech.
            if (text == m_candidateText) {
                ++m_candidateCount;
            } else {
                m_candidateText = text;
                m_candidateCount = 1;
            }

            if (m_candidateCount >= m_pipelineConfig.speechStableFrames) {
                const core::TimePoint now = core::now();
                const bool repeatTooSoon =
                    text == m_lastText &&
                    (now - m_lastSpokenAt) < m_pipelineConfig.speechCooldown;
                if (!repeatTooSoon) {
                    m_lastText = text;
                    m_lastSpokenAt = now;
                    // Carry the capture time so speechLoop can measure event->audible.
                    m_speech.push(SpeechRequest{text, frame.capturedAt});
                    LUMINA_LOG_DEBUG("detection {:.1f} ms after capture -> '{}'",
                                     core::msSince(frame.capturedAt),
                                     text);
                }
            }
        }

        // Periodic throughput/memory line for the Day-2 gate (INV-051/INV-052).
        const core::TimePoint now = core::now();
        const auto window = now - windowStart;
        if (window >= std::chrono::seconds(5)) {
            const double seconds = core::toMilliseconds(window) / 1000.0;
            const double fps = seconds > 0.0 ? static_cast<double>(framesProcessed) / seconds : 0.0;
            LUMINA_LOG_INFO("inference {:.1f} FPS, RSS {:.0f} MB", fps, readRssMb());
            framesProcessed = 0;
            windowStart = now;
        }
    }
}

void Pipeline::speechLoop(std::stop_token stopToken)
{
    SpeechRequest request;
    while (!stopToken.stop_requested()) {
        if (!m_speech.waitPop(request)) {
            break; // queue closed during shutdown
        }
        m_stopSpeech.store(false, std::memory_order_relaxed);
        LUMINA_LOG_INFO("speech: '{}' (event->speech-start {:.0f} ms)",
                        request.text,
                        core::msSince(request.detectedAt));

        const core::Status status = m_tts->synthesize(request.text, *m_sink, m_stopSpeech);
        if (!status) {
            LUMINA_LOG_WARN("speech failed: {}", status.error());
            continue;
        }

        // Let the sentence play out before taking the next one. Skip on shutdown,
        // where m_stopSpeech is set and the utterance was aborted.
        if (!m_stopSpeech.load(std::memory_order_relaxed)) {
            m_sink->drain();
            LUMINA_LOG_INFO("spoken: event->end {:.0f} ms", core::msSince(request.detectedAt));
        }
    }
}

} // namespace lumina::app
