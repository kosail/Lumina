// ---------------------------------------------------------------------------
// Tests for the front proximity path (FR-10, FR-02.4): the null/factory
// behavior, the ToF-vs-camera precedence in buildSceneAlert, and the pipeline's
// Safety alert when the (mock) sensor reports a close obstacle.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include "app/pipeline.hpp"
#include "app/scene.hpp"
#include "core/config.hpp"
#include "core/detection.hpp"
#include "core/time.hpp"
#include "i18n/es.hpp"
#include "sensors/proximity.hpp"

#include "mocks/mock_audio_sink.hpp"
#include "mocks/mock_camera.hpp"
#include "mocks/mock_detector.hpp"
#include "mocks/mock_proximity_sensor.hpp"
#include "mocks/mock_tts_engine.hpp"

using lumina::app::Pipeline;
using lumina::app::PipelineConfig;
using lumina::core::BoundingBox;
using lumina::core::Config;
using lumina::core::Detection;

TEST_CASE("proximity: the factory returns a null sensor on the host build")
{
    const Config config = lumina::core::defaultConfig();
    const auto sensor = lumina::sensors::makeProximitySensor(config);
    REQUIRE(sensor != nullptr);
    CHECK(sensor->init());
    CHECK_FALSE(sensor->read().has_value());
}

TEST_CASE("scene: a close proximity reading takes precedence over the bbox warning")
{
    Config config = lumina::core::defaultConfig();
    config.classIds = {0};
    config.obstacleClassIds = {0};

    Detection person;
    person.classId = 0;
    person.score = 0.9F;
    person.box = BoundingBox{25.0F, 25.0F, 50.0F, 50.0F};  // 25% area, centred => Near
    const std::vector<Detection> detections{person};
    const auto now = lumina::core::now();

    // No proximity input: the bbox heuristic raises the urgent Warning.
    const auto cameraOnly = lumina::app::buildSceneAlert(detections, config, 100, 100, now);
    REQUIRE(cameraOnly.has_value());
    CHECK(cameraOnly->priority == lumina::alerts::Priority::Warning);

    // ToF close: the proximity thread owns the Safety alert, so the camera says
    // nothing this frame (neither warning nor narration).
    const auto fused = lumina::app::buildSceneAlert(detections, config, 100, 100, now, 0.5F);
    CHECK_FALSE(fused.has_value());

    // ToF far: the camera warning is kept (the sensor may have missed the object).
    const auto far = lumina::app::buildSceneAlert(detections, config, 100, 100, now, 2.0F);
    REQUIRE(far.has_value());
    CHECK(far->priority == lumina::alerts::Priority::Warning);
}

TEST_CASE("pipeline: a close proximity reading speaks the safety phrase")
{
    lumina::tests::MockCamera camera;
    lumina::tests::MockDetector detector;  // nothing detected
    lumina::tests::MockTtsEngine tts;
    lumina::tests::MockAudioSink sink;
    lumina::tests::MockProximitySensor proximity;
    proximity.push(0.5F);  // one close reading, then no data

    Config config = lumina::core::defaultConfig();
    config.classIds.clear();
    config.obstacleClassIds.clear();
    config.proximityPollMs = 20;  // fast poll so the test is quick

    Pipeline pipeline(&camera, &detector, &tts, &sink, config, PipelineConfig{}, nullptr,
                      &proximity);
    REQUIRE(pipeline.start());

    for (int i = 0; i < 200 && tts.calls() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    pipeline.stop();  // stop before asserting so a failure cannot leak threads

    REQUIRE(tts.calls() >= 1);
    CHECK(tts.lastText() == lumina::i18n::proximityAlertPhrase(true));
}
