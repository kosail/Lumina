// ---------------------------------------------------------------------------
// Integration test: the pipeline wires capture -> inference -> arbiter -> TTS.
//
// Uses the host mocks (AGENTS §9); timing is bounded with a poll loop so the test
// cannot hang. Full on-device behaviour (real audio, preemption) is verified in
// WP7; the arbiter's preemption policy is covered deterministically in
// test_arbiter.cpp.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "app/pipeline.hpp"
#include "core/config.hpp"
#include "core/detection.hpp"
#include "core/frame.hpp"
#include "core/time.hpp"

#include "mocks/mock_audio_sink.hpp"
#include "mocks/mock_camera.hpp"
#include "mocks/mock_detector.hpp"
#include "mocks/mock_tts_engine.hpp"

using lumina::app::Pipeline;
using lumina::app::PipelineConfig;
using lumina::core::BoundingBox;
using lumina::core::Config;
using lumina::core::Detection;
using lumina::core::Frame;
using lumina::core::PixelFormat;

namespace {

Frame makeFrame(int width, int height)
{
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = PixelFormat::Rgb888;
    frame.capturedAt = lumina::core::now();
    frame.data.assign(4, 0); // contents are irrelevant to the mock detector
    return frame;
}

Detection personDetection()
{
    Detection detection;
    detection.classId = 0; // person
    detection.score = 0.9F;
    detection.box = BoundingBox{45.0F, 0.0F, 10.0F, 10.0F}; // small, centred
    return detection;
}

} // namespace

TEST_CASE("pipeline: a stable description reaches the TTS engine")
{
    lumina::tests::MockCamera camera;
    lumina::tests::MockDetector detector({personDetection()});
    lumina::tests::MockTtsEngine tts;
    lumina::tests::MockAudioSink sink;

    const Config config = lumina::core::defaultConfig();
    PipelineConfig pipelineConfig;
    pipelineConfig.arbiter.descriptionStableFrames = 1; // one frame is enough here

    Pipeline pipeline(&camera, &detector, &tts, &sink, config, pipelineConfig);
    REQUIRE(pipeline.start());
    camera.pushFrame(makeFrame(100, 100));
    camera.pushFrame(makeFrame(100, 100));

    // Bounded wait for the speech worker (never sleep indefinitely in a test).
    for (int i = 0; i < 400 && tts.calls() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    pipeline.stop(); // stop before asserting so a failure cannot leak threads

    CHECK(tts.calls() >= 1);
    CHECK(tts.lastText() == "una persona enfrente.");
}
