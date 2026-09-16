// ---------------------------------------------------------------------------
// Vertical-slice pipeline: capture -> detection -> Spanish -> Piper -> sink.
//
// One thread per stage, connected by bounded queues (AGENTS §5, INV-031): the
// capture thread never blocks on inference, and stale frames/descriptions are
// dropped. Hardware is injected through interfaces, so the same class runs on the
// laptop with mocks and on the Pi with real implementations.
//
// Speech policy for the slice: a phrase must be seen for a few consecutive frames
// (hysteresis) before it is spoken; the newest phrase replaces any queued one. The
// full alert arbiter (priority/preemption/cooldown, INV-032) is Day 3, which will
// also drive m_stopSpeech for safety preemption.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

#include "audio/audio_sink.hpp"
#include "audio/tts_engine.hpp"
#include "capture/camera.hpp"
#include "core/bounded_queue.hpp"
#include "core/config.hpp"
#include "core/frame.hpp"
#include "core/time.hpp"
#include "vision/detector.hpp"

namespace lumina::app {

// Tunables for the slice.
struct PipelineConfig {
    std::size_t frameQueueCapacity = 1;   // keep only the freshest frame (INV-031)
    std::size_t speechQueueCapacity = 1;  // newest description wins (drop stale)
    int speechStableFrames = 2;           // hysteresis: ignore one-frame flicker
    std::chrono::milliseconds speechCooldown{1500}; // do not repeat the same phrase twice in a row
};

// One queued utterance: the phrase plus the capture time of the frame that
// triggered it, so speechLoop can report event->audible latency (INV-051).
struct SpeechRequest {
    std::string text;
    core::TimePoint detectedAt{};
};

// Owns the worker threads and the queues. Non-copyable.
class Pipeline {
public:
    Pipeline(capture::ICamera* camera,
             vision::IDetector* detector,
             audio::ITtsEngine* tts,
             audio::IAudioSink* sink,
             core::Config config,
             PipelineConfig pipelineConfig = {});

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    // Start the camera, open the sink, and launch the workers. Returns false
    // (after logging) if a subsystem fails; in that case nothing is left running.
    [[nodiscard]] bool start();

    // Request shutdown, unblock and join the workers, then stop the camera and
    // close the sink. Safe to call more than once.
    void stop() noexcept;

private:
    void captureLoop(std::stop_token stopToken);
    void inferenceLoop(std::stop_token stopToken);
    void speechLoop(std::stop_token stopToken);

    // Non-owning dependencies (injected; must outlive the Pipeline).
    capture::ICamera* m_camera = nullptr;
    vision::IDetector* m_detector = nullptr;
    audio::ITtsEngine* m_tts = nullptr;
    audio::IAudioSink* m_sink = nullptr;

    core::Config m_config;
    PipelineConfig m_pipelineConfig;

    core::BoundedQueue<core::Frame> m_frames;    // capture -> inference
    core::BoundedQueue<SpeechRequest> m_speech;  // inference -> speech

    std::atomic<bool> m_running{false};    // guards start()/stop() idempotency
    std::atomic<bool> m_stopSpeech{false}; // true only to abort on shutdown/preemption

    std::jthread m_captureThread;
    std::jthread m_inferenceThread;
    std::jthread m_speechThread;

    // Hysteresis state (inference thread only).
    std::string m_candidateText;
    int m_candidateCount = 0;

    // De-duplication state (inference thread only).
    std::string m_lastText;
    core::TimePoint m_lastSpokenAt{};
};

} // namespace lumina::app
