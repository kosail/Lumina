// ---------------------------------------------------------------------------
// Vertical-slice pipeline: capture -> detection -> Spanish -> Piper -> sink.
//
// One thread per stage, connected by bounded queues (AGENTS §5, INV-031): the
// capture thread never blocks on inference, and stale frames are dropped. Hardware
// is injected through interfaces, so the same class runs on the laptop with mocks
// and on the Pi with real implementations.
//
// Speech is owned by the AlertArbiter (FR-07, INV-032): the inference thread
// submits a candidate Alert per frame (an obstacle warning or a description) and
// the single speech worker speaks the highest-priority one, preempting a
// lower-priority utterance when a safety/warning alert arrives.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <cstddef>
#include <thread>

#include "alerts/arbiter.hpp"
#include "audio/audio_sink.hpp"
#include "audio/tts_engine.hpp"
#include "capture/camera.hpp"
#include "core/bounded_queue.hpp"
#include "core/config.hpp"
#include "core/frame.hpp"
#include "vision/detector.hpp"

namespace lumina::app {

// Tunables for the pipeline. The speech policy (priority, stability, cooldown,
// preemption) lives in the arbiter config.
struct PipelineConfig {
    std::size_t frameQueueCapacity = 1; // keep only the freshest frame (INV-031)
    alerts::ArbiterConfig arbiter{};    // priority/cooldown/preemption (INV-032)
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

    core::BoundedQueue<core::Frame> m_frames; // capture -> inference
    alerts::AlertArbiter m_arbiter;           // inference -> speech (priority + preemption)

    std::atomic<bool> m_running{false}; // guards start()/stop() idempotency

    std::jthread m_captureThread;
    std::jthread m_inferenceThread;
    std::jthread m_speechThread;
};

} // namespace lumina::app
