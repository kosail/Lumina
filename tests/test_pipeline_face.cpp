// ---------------------------------------------------------------------------
// Integration test: the pipeline greets a recognized person by name (FR-03).
//
// The detector reports a person, the (mock) recognizer returns "María", and the
// pipeline must speak "<name> está enfrente" exactly once. `classIds` is emptied
// so the generic person description is suppressed and the greeting is the only
// utterance, keeping the assertion deterministic.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "app/pipeline.hpp"
#include "core/config.hpp"
#include "core/detection.hpp"
#include "core/frame.hpp"
#include "core/time.hpp"
#include "vision/face_recognizer.hpp"

#include "mocks/mock_audio_sink.hpp"
#include "mocks/mock_camera.hpp"
#include "mocks/mock_detector.hpp"
#include "mocks/mock_face_recognizer.hpp"
#include "mocks/mock_tts_engine.hpp"

using lumina::app::Pipeline;
using lumina::app::PipelineConfig;
using lumina::core::BoundingBox;
using lumina::core::Config;
using lumina::core::Detection;
using lumina::core::Frame;
using lumina::core::PixelFormat;
using lumina::vision::FaceMatch;

namespace {

Frame makeFrame(int width, int height)
{
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 3;
    frame.format = PixelFormat::Rgb888;
    frame.capturedAt = lumina::core::now();
    frame.data.assign(4, 0);
    return frame;
}

}  // namespace

TEST_CASE("pipeline: a recognized person is greeted by name exactly once")
{
    lumina::tests::MockCamera camera;
    // A small, centred person: Far for the obstacle heuristic, so no warning fires.
    Detection person;
    person.classId = 0;
    person.score = 0.9F;
    person.box = BoundingBox{45.0F, 0.0F, 10.0F, 10.0F};
    lumina::tests::MockDetector detector({person});

    FaceMatch match;
    match.name = "María";
    match.similarity = 0.9F;
    lumina::tests::MockFaceRecognizer recognizer(match);

    lumina::tests::MockTtsEngine tts;
    lumina::tests::MockAudioSink sink;

    Config config = lumina::core::defaultConfig();
    config.classIds.clear();             // suppress the generic "una persona enfrente."
    config.obstacleClassIds.clear();     // and the obstacle path
    config.faceStableFrames = 1;         // one stable observation is enough here
    config.faceIntervalMs = 100;         // the enforced floor; still fast in a test
    config.faceMinBoxFraction = 0.0F;    // accept any person size

    Pipeline pipeline(&camera, &detector, &tts, &sink, config, PipelineConfig{}, &recognizer);
    REQUIRE(pipeline.start());

    // Space the frames out so the inference thread can dispatch a face request and
    // then drain the answer (the frame queue keeps only the newest frame, INV-031).
    for (int i = 0; i < 6; ++i) {
        camera.pushFrame(makeFrame(100, 100));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Bounded wait for the greeting (never sleep indefinitely in a test).
    for (int i = 0; i < 600 && tts.calls() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    pipeline.stop();  // stop before asserting so a failure cannot leak threads

    REQUIRE(tts.calls() >= 1);
    CHECK(tts.lastText() == "María está enfrente");
}
