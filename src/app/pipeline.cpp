// ---------------------------------------------------------------------------
// Pipeline implementation (worker threads).
// ---------------------------------------------------------------------------

#include "app/pipeline.hpp"

#include <chrono>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

#include <unistd.h> // sysconf(_SC_PAGESIZE) for the RSS line

#include "app/scene.hpp"
#include "core/logging.hpp"
#include "core/time.hpp"
#include "i18n/es.hpp"

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
                   PipelineConfig pipelineConfig,
                   vision::IFaceRecognizer* faceRecognizer,
                   sensors::IProximitySensor* proximity)
    : m_camera(camera)
    , m_detector(detector)
    , m_tts(tts)
    , m_sink(sink)
    , m_faceRecognizer(faceRecognizer)
    , m_proximity(proximity)
    , m_config(std::move(config))
    , m_faceGreeter(FaceGreeterConfig{
          m_config.faceStableFrames,
          std::chrono::milliseconds(m_config.faceGreetingCooldownMs)})
    , m_frames(pipelineConfig.frameQueueCapacity)
    , m_arbiter(pipelineConfig.arbiter)
    , m_faceFrames(1)   // keep only the newest face request (INV-031)
    , m_faceResults(1)  // keep only the newest face answer
    , m_status(pipelineConfig.statusPublisher)
    , m_people(std::move(pipelineConfig.people))
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
    m_startTime = core::now();  // uptime origin for the status snapshot (FR-11)
    m_captureThread = std::jthread([this](std::stop_token token) { captureLoop(token); });
    m_inferenceThread = std::jthread([this](std::stop_token token) { inferenceLoop(token); });
    m_speechThread = std::jthread([this](std::stop_token token) { speechLoop(token); });
    // The face worker runs only when a recognizer was injected (host/disabled builds
    // pass null and skip face recognition entirely).
    if (m_faceRecognizer != nullptr) {
        m_faceThread = std::jthread([this](std::stop_token token) { faceLoop(token); });
    }
    // The proximity worker runs only when a sensor was injected (host/disabled
    // builds pass null and skip it entirely).
    if (m_proximity != nullptr) {
        m_proximityThread = std::jthread([this](std::stop_token token) { proximityLoop(token); });
    }
    // The status worker runs only when a publisher was injected (FR-11); it is the
    // only place the runtime reports its health, and it never blocks the pipeline.
    if (m_status != nullptr) {
        m_statusThread = std::jthread([this](std::stop_token token) { statusLoop(token); });
    }
    return true;
}

void Pipeline::stop() noexcept
{
    if (!m_running.exchange(false)) {
        return; // never started, or already stopped
    }

    // Ask the loops to stop, then wake them. Closing the arbiter also raises its
    // interrupt flag, so an in-progress utterance aborts promptly.
    m_captureThread.request_stop();
    m_inferenceThread.request_stop();
    m_speechThread.request_stop();
    m_faceThread.request_stop();
    m_proximityThread.request_stop();
    m_statusThread.request_stop();
    m_arbiter.close();
    m_frames.close();
    m_faceFrames.close();
    m_faceResults.close();

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    if (m_inferenceThread.joinable()) {
        m_inferenceThread.join();
    }
    if (m_speechThread.joinable()) {
        m_speechThread.join();
    }
    if (m_faceThread.joinable()) {
        m_faceThread.join();
    }
    if (m_proximityThread.joinable()) {
        m_proximityThread.join();
    }
    if (m_statusThread.joinable()) {
        m_statusThread.join();
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

        // Feed the status snapshot (FR-11): one atomic bump per frame plus a
        // bounded-sample luminance read. Both are tiny and the frame is still
        // intact here (it is moved into the face queue further below).
        m_framesTotal.fetch_add(1, std::memory_order_relaxed);
        m_meanLuma.store(status::meanLuma(frame), std::memory_order_relaxed);

        const std::vector<core::Detection> detections = m_detector->detect(frame);
        // Fuse the freshest front ToF distance (FR-02.4); negative means no reading.
        const float proximity = m_proximityMeters.load(std::memory_order_relaxed);
        const std::optional<float> proximityMeters =
            proximity >= 0.0F ? std::optional<float>(proximity) : std::nullopt;
        const std::optional<alerts::Alert> alert =
            buildSceneAlert(detections, m_config, frame.width, frame.height, frame.capturedAt,
                            proximityMeters);
        if (alert) {
            // Logged for every candidate (accepted or not) so detection latency can
            // be read off the DEBUG stream (INV-051 / docs/PERFORMANCE.md).
            LUMINA_LOG_DEBUG("detection {:.1f} ms after capture -> '{}'",
                             core::msSince(frame.capturedAt),
                             alert->text);
            if (!m_arbiter.submit(*alert, core::now())) {
                // Suppressed by stability/cooldown/gap or a full queue: expected.
                LUMINA_LOG_TRACE("alert suppressed: '{}'", alert->text);
            }
        } else {
            // Nothing to say this frame: break the stability run so a scene that
            // disappeared and came back must be re-observed (matches the old
            // hysteresis reset).
            m_arbiter.clearCandidate();
        }

        // Face recognition (FR-03): dispatch work and collect greetings. `frame`
        // is moved into the face queue here, so nothing below may read it.
        driveFaceRecognition(std::move(frame), detections);

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
    alerts::Alert request;
    while (!stopToken.stop_requested() && m_arbiter.waitPop(request)) {
        LUMINA_LOG_INFO("speech: '{}' (event->speech-start {:.0f} ms)",
                        request.text,
                        core::msSince(request.detectedAt));

        // The arbiter's interrupt flag becomes true if a higher-priority alert
        // arrives while this utterance is being synthesized (INV-032).
        const core::Status status = m_tts->synthesize(request.text, *m_sink, m_arbiter.interruptFlag());
        if (!status) {
            LUMINA_LOG_WARN("speech failed: {}", status.error());
            m_arbiter.finishSpeaking(core::now());
            continue;
        }

        // Let the sentence play out before taking the next one, unless it was cut
        // short by a preemption. On preemption, drop the already-buffered audio so
        // the higher-priority alert starts immediately (AlsaSink re-prepares on the
        // next write).
        if (m_arbiter.interruptFlag().load(std::memory_order_relaxed)) {
            m_sink->stop();
        } else {
            m_sink->drain();
            LUMINA_LOG_INFO("spoken: event->end {:.0f} ms", core::msSince(request.detectedAt));
        }
        m_arbiter.finishSpeaking(core::now());
    }
}

void Pipeline::faceLoop(std::stop_token stopToken)
{
    FaceWork work;
    while (!stopToken.stop_requested() && m_faceFrames.waitPop(work)) {
        FaceResult result;
        result.capturedAt = work.capturedAt;
        // The heavy OpenCV call runs here, off the inference thread, so detection
        // keeps producing frames while a face is recognized (INV-031).
        result.match = m_faceRecognizer->identify(work.frame, work.person);
        m_faceResults.push(std::move(result));
    }
}

void Pipeline::proximityLoop(std::stop_token stopToken)
{
    const auto pollInterval = std::chrono::milliseconds(m_config.proximityPollMs);
    bool close = false;  // hysteresis state

    while (!stopToken.stop_requested()) {
        const std::optional<sensors::ProximityReading> reading = m_proximity->read();
        if (reading.has_value() && reading->valid) {
            m_proximityMeters.store(reading->meters, std::memory_order_relaxed);

            // Hysteresis: latch "close" below the threshold and release it only
            // once the distance climbs back above the release band, so a reading
            // hovering at the threshold cannot flap the alert.
            if (!close && reading->meters <= m_config.proximityThresholdM) {
                close = true;
            } else if (close && reading->meters >= m_config.proximityReleaseM) {
                close = false;
            }

            if (close) {
                // Imminent, class-agnostic obstacle: the highest priority. This is
                // the low-latency safety channel (FR-10); the camera's duplicate
                // warning is suppressed in buildSceneAlert (FR-02.4). The arbiter's
                // per-key cooldown prevents repetition while the obstacle remains.
                alerts::Alert alert;
                alert.priority = alerts::Priority::Safety;
                alert.source = alerts::Source::Proximity;
                alert.text = i18n::proximityAlertPhrase(true);
                alert.dedupKey = "proximity:front";
                alert.detectedAt = core::now();
                alert.preStabilized = true;  // one-shot sensor event, no frame stability
                if (m_arbiter.submit(std::move(alert), core::now())) {
                    LUMINA_LOG_INFO("proximity: obstacle at {:.2f} m", reading->meters);
                }
            }
        } else {
            // No valid reading: clear the fusion input. Do not reset `close`
            // immediately, so a single dropped sample does not clear the latch.
            m_proximityMeters.store(-1.0F, std::memory_order_relaxed);
        }

        // Sleep in short slices so stop() is responsive even for long poll rates.
        const auto deadline = std::chrono::steady_clock::now() + pollInterval;
        while (!stopToken.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

void Pipeline::driveFaceRecognition(core::Frame frame, const std::vector<core::Detection>& detections)
{
    if (m_faceRecognizer == nullptr) {
        return;  // face path not built/configured
    }

    // Collect any finished recognition and feed the greeting policy. This runs on
    // the inference thread; the arbiter is mutex-guarded and the proximity thread
    // is the only other producer (it submits pre-stabilized Safety alerts, which
    // never touch the description stability state).
    FaceResult result;
    while (m_faceResults.tryPop(result)) {
        std::optional<std::string> name;
        if (result.match.has_value()) {
            name = result.match->name;
        }
        const core::TimePoint observedAt = core::now();
        const std::optional<std::string> greeted = m_faceGreeter.observe(name, observedAt);
        if (!greeted.has_value()) {
            continue;
        }

        alerts::Alert alert;
        alert.priority = alerts::Priority::Description;  // never preempts safety
        alert.source = alerts::Source::Face;
        alert.text = i18n::greeting(*greeted);
        alert.dedupKey = "face:" + *greeted;  // greet each person, not each frame
        alert.detectedAt = result.capturedAt;
        alert.preStabilized = true;  // the greeter already enforced its own stability
        LUMINA_LOG_INFO("face: recognized '{}' (similarity {:.3f})", *greeted,
                        result.match->similarity);
        if (m_arbiter.submit(std::move(alert), core::now())) {
            // The greeting was accepted: start the person's cooldown now. Doing it
            // only here (not inside observe()) means a greeting rejected by the
            // arbiter's gap or a higher-priority alert is retried next pulse
            // instead of silencing the person for the whole cooldown (CHG-0071).
            m_faceGreeter.markGreeted(*greeted, observedAt);
        } else {
            LUMINA_LOG_TRACE("face: greeting suppressed: '{}'", *greeted);
        }
    }

    if (!m_config.faceEnabled) {
        return;
    }

    // Find the largest person detection (COCO class 0).
    const core::Detection* person = nullptr;
    float bestArea = 0.0F;
    for (const core::Detection& detection : detections) {
        if (detection.classId != 0) {
            continue;
        }
        const float area = detection.box.area();
        if (area > bestArea) {
            bestArea = area;
            person = &detection;
        }
    }

    if (person == nullptr) {
        // Nobody in view: break any in-progress stability run (reset() has the
        // same effect as observing a nullopt, without an ignored [[nodiscard]]).
        m_faceGreeter.reset();
        return;
    }

    // Skip distant faces: the person must cover a minimum share of the frame.
    const float frameArea = static_cast<float>(frame.width) * static_cast<float>(frame.height);
    const float areaFraction = frameArea > 0.0F ? person->box.area() / frameArea : 0.0F;
    if (areaFraction < m_config.faceMinBoxFraction) {
        return;
    }

    // Throttle: at most one face attempt per faceIntervalMs (protects INV-050).
    // While someone is inside their greeting cooldown, slow it further by
    // faceCooldownBackoffFactor: re-recognizing an already-greeted person at full
    // rate keeps SFace's DNN workspace hot and a core busy for no benefit, which
    // is what pushed the ~447 MB board into swap (CHG-0071).
    const core::TimePoint now = core::now();
    std::chrono::milliseconds interval(m_config.faceIntervalMs);
    if (m_faceGreeter.hasActiveCooldown(now)) {
        interval *= m_config.faceCooldownBackoffFactor;
    }
    if ((now - m_lastFaceAttempt) < interval) {
        return;
    }
    m_lastFaceAttempt = now;

    FaceWork work;
    work.person = *person;
    work.capturedAt = frame.capturedAt;  // read before the move below
    work.frame = std::move(frame);       // transfer the pixels; no copy
    m_faceFrames.push(std::move(work));
}

void Pipeline::statusLoop(std::stop_token stopToken)
{
    // Publish one snapshot per second. The FPS is the number of inference frames
    // since the previous publish divided by the real elapsed time, so it reflects
    // live throughput rather than a long-window average. Sleeping in 100 ms slices
    // keeps stop() responsive without a condition variable (this loop has no queue
    // to wait on).
    core::TimePoint lastWindow = core::now();
    std::uint64_t lastFrames = m_framesTotal.load(std::memory_order_relaxed);

    while (!stopToken.stop_requested()) {
        for (int slice = 0; slice < 10 && !stopToken.stop_requested(); ++slice) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stopToken.stop_requested()) {
            break;
        }

        const core::TimePoint now = core::now();
        const std::uint64_t frames = m_framesTotal.load(std::memory_order_relaxed);
        const double seconds = core::toMilliseconds(now - lastWindow) / 1000.0;

        status::StatusSnapshot snapshot;
        snapshot.running = true;
        snapshot.uptimeSeconds = static_cast<int>(core::toMilliseconds(now - m_startTime) / 1000.0);
        snapshot.fps = seconds > 0.0 ? static_cast<double>(frames - lastFrames) / seconds : 0.0;
        snapshot.rssMb = readRssMb();
        snapshot.meanLuma = m_meanLuma.load(std::memory_order_relaxed);
        snapshot.faceCount = m_people.size();
        snapshot.people = m_people;  // copied once per second; only a few names
        snapshot.sinkReady = m_running.load(std::memory_order_relaxed);
        m_status->publish(snapshot);

        lastWindow = now;
        lastFrames = frames;
    }
}

} // namespace lumina::app
