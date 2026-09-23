// ---------------------------------------------------------------------------
// Unit tests for the companion agent's pure logic (FR-11): JSON helpers, telemetry
// parsing/serialization, the amixer parsers, enrollment parsing/orchestration and
// base64 decoding. No sockets, no hardware.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "agent/amixer.hpp"
#include "agent/bind_retry.hpp"
#include "agent/enroll.hpp"
#include "agent/face_names.hpp"
#include "agent/json.hpp"
#include "agent/runtime_state.hpp"
#include "agent/telemetry.hpp"
#include "vision/face_store.hpp"

using namespace lumina::agent;

namespace {

// ICommandRunner double: the test sets canned replies or streamed lines.
class FakeCommandRunner final : public ICommandRunner {
public:
    CommandResult run(const std::vector<std::string>& argv) override {
        m_lastArgv = argv;
        if (m_responder) {
            return m_responder(argv);
        }
        return {};
    }
    int runLines(const std::vector<std::string>& argv,
                 const std::function<void(const std::string&)>& onLine,
                 const std::atomic<bool>* /*cancel*/ = nullptr) override {
        m_lastArgv = argv;
        for (const std::string& line : m_lines) {
            onLine(line);
        }
        return m_linesExit;
    }

    std::function<CommandResult(const std::vector<std::string>&)> m_responder;
    std::vector<std::string> m_lines;
    int m_linesExit = 0;
    std::vector<std::string> m_lastArgv;
};

// IRuntimeControl double: records whether stop()/start() were called.
class FakeRuntimeControl final : public IRuntimeControl {
public:
    bool stop() override {
        ++stops;
        return true;
    }
    bool start() override {
        ++starts;
        return true;
    }
    [[nodiscard]] bool isRunning() override { return running; }
    int stops = 0;
    int starts = 0;
    bool running = true;
};

}  // namespace

TEST_CASE("agent json: escape and read flat fields")
{
    CHECK(escapeJson("a\"b\\c\nd") == "a\\\"b\\\\c\\nd");

    const std::string line =
        R"({"t":"volume.set","token":"abc","value":42,"mute":true,"images":["AA","BB"]})";
    CHECK(jsonGetString(line, "t").value() == "volume.set");
    CHECK(jsonGetString(line, "token").value() == "abc");
    CHECK(jsonGetInt(line, "value").value() == 42);
    CHECK(jsonGetBool(line, "mute").value() == true);
    CHECK(jsonGetStringArray(line, "images") == std::vector<std::string>{"AA", "BB"});
    CHECK_FALSE(jsonGetString(line, "missing").has_value());
    // A UTF-8 name survives a round trip through escape/parse.
    const std::string name = R"({"name":"David Solís"})";
    CHECK(jsonGetString(name, "name").value() == "David Solís");
}

TEST_CASE("agent telemetry: parse the runtime status block")
{
    const std::string block =
        "running=1\nuptime_s=62\nfps=4.3\nrss_mb=218.0\nmean_luma=0.420\nface_count=2\n"
        "sink_ready=1\nperson=Ana\nperson=Luis\n";
    const auto status = parseStatusBlock(block);
    REQUIRE(status.has_value());
    CHECK(status->running);
    CHECK(status->uptimeSeconds == 62);
    CHECK(status->fps == doctest::Approx(4.3));
    CHECK(status->faceCount == 2);
    CHECK(status->sinkReady);
    CHECK(status->people == std::vector<std::string>{"Ana", "Luis"});

    CHECK_FALSE(parseStatusBlock("").has_value());
    CHECK_FALSE(parseStatusBlock("garbage\n").has_value());  // no "running" key
}

TEST_CASE("agent telemetry: parse OS stat contents")
{
    CHECK(parseThermalC("46160\n") == doctest::Approx(46.16));
    CHECK(parseThermalC("nope") < 0.0);
    CHECK(parseMemAvailableKb("MemTotal: 100 kB\nMemAvailable:  65536 kB\n") == 65536);
    CHECK(parseMemAvailableKb("MemTotal: 100 kB\n") == -1);
    CHECK(parseLoad1("1.80 1.20 0.90 1/200 1234\n") == doctest::Approx(1.80));
    CHECK(deriveDayNight(0.9F) == "day");
    CHECK(deriveDayNight(0.1F) == "night");
    CHECK(deriveDayNight(-1.0F) == "unknown");
}

TEST_CASE("agent telemetry: datagram is valid-shaped JSON")
{
    TelemetryState state;
    state.ts = 1690000000;
    state.runtimeReachable = true;
    state.running = true;
    state.uptimeS = 62;
    state.sink = "ready";
    state.faceCount = 1;
    state.fps = 4.3;
    state.rssMb = 218.0;
    state.tempC = 46.2;
    state.load1 = 1.8;
    state.volume = 70;
    state.luma = 0.10F;
    state.people = {"David Solís"};
    const std::string json = buildTelemetryJson(state);
    CHECK(json.find("\"t\":\"status\"") != std::string::npos);
    CHECK(json.find("\"proto\":1") != std::string::npos);
    CHECK(json.find("\"dayNight\":\"night\"") != std::string::npos);  // luma 0.10
    CHECK(json.find("\"people\":[\"David Solís\"]") != std::string::npos);
    CHECK(json.find("\"volume\":70") != std::string::npos);
    CHECK(json.find("\"initializing\":false") != std::string::npos);  // default
}

TEST_CASE("agent telemetry: an unknown volume is the -1 sentinel, not null")
{
    TelemetryState state;
    state.volume = -1;  // the mixer could not be read
    const std::string json = buildTelemetryJson(state);
    CHECK(json.find("\"volume\":-1") != std::string::npos);
    CHECK(json.find("\"volume\":null") == std::string::npos);
}

TEST_CASE("agent telemetry: the initializing flag is serialized")
{
    TelemetryState state;
    state.runtimeReachable = false;
    state.running = false;
    state.initializing = true;  // active but not yet reporting a fresh running status
    const std::string json = buildTelemetryJson(state);
    CHECK(json.find("\"running\":false") != std::string::npos);
    CHECK(json.find("\"initializing\":true") != std::string::npos);
}

TEST_CASE("agent bind retry: bounded attempts")
{
    CHECK(shouldRetryControlBind(1, 3));
    CHECK(shouldRetryControlBind(2, 3));
    CHECK_FALSE(shouldRetryControlBind(3, 3));  // the last attempt is not retried
    CHECK_FALSE(shouldRetryControlBind(1, 1));  // a single attempt disables retries
    CHECK_FALSE(shouldRetryControlBind(1, 0));
    CHECK_FALSE(shouldRetryControlBind(5, 1));
}

TEST_CASE("agent runtime readiness: probe scheduling and the initializing flag")
{
    using std::chrono::seconds;

    // Unknown always probes; Active probes every 5 s; Inactive every 30 s (F1).
    CHECK(runtimeProbeDue(RuntimeLiveness::Unknown, seconds{0}));
    CHECK_FALSE(runtimeProbeDue(RuntimeLiveness::Active, seconds{4}));
    CHECK(runtimeProbeDue(RuntimeLiveness::Active, seconds{5}));
    CHECK_FALSE(runtimeProbeDue(RuntimeLiveness::Inactive, seconds{29}));
    CHECK(runtimeProbeDue(RuntimeLiveness::Inactive, seconds{30}));
    // Custom intervals are honored.
    CHECK(runtimeProbeDue(RuntimeLiveness::Inactive, seconds{10},
                          RuntimeProbeIntervals{seconds{5}, seconds{10}}));

    const auto expect = [](RuntimeReadiness got, RuntimeLiveness wantLiveness,
                           bool wantInitializing) {
        CHECK(got.liveness == wantLiveness);
        CHECK(got.initializing == wantInitializing);
    };

    // A fresh running status means the start finished: active, not initializing.
    expect(evaluateRuntimeReadiness(RuntimeReadinessInput{RuntimeLiveness::Unknown, true, false, false}),
           RuntimeLiveness::Active, false);
    // Active + due probe + still active -> initializing.
    expect(evaluateRuntimeReadiness(RuntimeReadinessInput{RuntimeLiveness::Active, false, true, true}),
           RuntimeLiveness::Active, true);
    // Active + due probe + now inactive -> stopped, not initializing.
    expect(evaluateRuntimeReadiness(RuntimeReadinessInput{RuntimeLiveness::Active, false, true, false}),
           RuntimeLiveness::Inactive, false);
    // Inactive + due probe + active again -> an external start is detected (F1).
    expect(evaluateRuntimeReadiness(
               RuntimeReadinessInput{RuntimeLiveness::Inactive, false, true, true}),
           RuntimeLiveness::Active, true);
    // Inactive + no probe due -> stays inactive and quiet.
    expect(evaluateRuntimeReadiness(
               RuntimeReadinessInput{RuntimeLiveness::Inactive, false, false, false}),
           RuntimeLiveness::Inactive, false);
    // Active + no probe due -> keeps initializing without probing.
    expect(evaluateRuntimeReadiness(RuntimeReadinessInput{RuntimeLiveness::Active, false, false, false}),
           RuntimeLiveness::Active, true);
}

TEST_CASE("agent face names: read enrolled names from the face store")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "lumina_agent_face_names_test.bin";
    std::filesystem::remove(path);

    // Missing file -> no names (treated the same as an empty store).
    CHECK(readEnrolledNames(path.string()).empty());

    // Write a real store through the runtime's own FaceStore, then read it back.
    lumina::vision::FaceStore store;
    store.addEmbedding("Ana", std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});
    store.addEmbedding("Luis Solís", std::vector<float>{0.0F, 1.0F, 0.0F, 0.0F});
    // A second embedding for an existing person must not duplicate the name.
    store.addEmbedding("Ana", std::vector<float>{0.9F, 0.1F, 0.0F, 0.0F});
    REQUIRE(store.save(path.string()));

    CHECK(readEnrolledNames(path.string()) == std::vector<std::string>{"Ana", "Luis Solís"});

    // A truncated file is rejected wholesale, never partially read.
    {
        std::ifstream in(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
        REQUIRE(bytes.size() > 4);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
    }
    CHECK(readEnrolledNames(path.string()).empty());

    // A file with the wrong magic is rejected.
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "NOTASTORE-and-then-some-padding-bytes";
    }
    CHECK(readEnrolledNames(path.string()).empty());

    std::filesystem::remove(path);
}

TEST_CASE("agent amixer: parse controls, volume and mute")
{
    CHECK(parseAmixerControls("Simple mixer control 'TWS A2DP',0\n") ==
          std::vector<std::string>{"TWS A2DP"});
    CHECK(parseAmixerControls("") .empty());
    CHECK(parseAmixerVolume("  Front Left: Playback 100 [79%] [on]\n") == 79);
    CHECK(parseAmixerVolume("no percent here") == std::nullopt);
    CHECK(parseAmixerMuted("  Mono: Playback 0 [0%] [off]\n"));
    CHECK_FALSE(parseAmixerMuted("  Mono: Playback 0 [0%] [on]\n"));
}

TEST_CASE("agent amixer: AmixerVolume drives the mixer through the runner")
{
    FakeCommandRunner runner;
    runner.m_responder = [](const std::vector<std::string>& argv) -> CommandResult {
        CommandResult result;
        result.exitCode = 0;
        const std::string action = argv.size() > 3 ? argv[3] : "";
        if (action == "scontrols") {
            result.output = "Simple mixer control 'TWS A2DP',0\n";
        } else if (action == "sget") {
            result.output = "  Front Left: Playback 100 [79%] [on]\n";
        }
        return result;
    };

    AmixerVolume volume(runner);
    const AmixerVolume::State state = volume.read();
    CHECK(state.percent == 79);
    CHECK_FALSE(state.muted);
    CHECK(volume.control() == "TWS A2DP");

    CHECK(volume.setPercent(50));
    CHECK(runner.m_lastArgv == std::vector<std::string>{"amixer", "-D", "bluealsa", "sset",
                                                       "TWS A2DP", "50%"});
    CHECK_FALSE(volume.setPercent(150));  // out of range
    CHECK(volume.setMuted(true));
    CHECK(runner.m_lastArgv.back() == "mute");
}

TEST_CASE("agent enroll: parse the tool output lines")
{
    CHECK(parseEnrollLine("Enrolling 'Ana'.") == std::nullopt);

    const auto captured = parseEnrollLine("  captured embedding 3/10");
    REQUIRE(captured.has_value());
    CHECK(captured->kind == EnrollEvent::Kind::Captured);
    CHECK(captured->captured == 3);
    CHECK(captured->total == 10);

    const auto photo = parseEnrollLine("  captured embedding 2 from 'a.jpg'");
    REQUIRE(photo.has_value());
    CHECK(photo->kind == EnrollEvent::Kind::Captured);
    CHECK(photo->captured == 2);

    const auto noFace = parseEnrollLine("  (no face detected — keep looking at the camera)");
    REQUIRE(noFace.has_value());
    CHECK(noFace->kind == EnrollEvent::Kind::NoFace);

    const auto done = parseEnrollLine("Enrolled 'Ana' with 10 embedding(s) into 'x.bin'.");
    REQUIRE(done.has_value());
    CHECK(done->kind == EnrollEvent::Kind::Enrolled);
    CHECK(done->captured == 10);

    const auto error = parseEnrollLine("enroll: no face was captured; no data written");
    REQUIRE(error.has_value());
    CHECK(error->kind == EnrollEvent::Kind::Error);
}

TEST_CASE("agent base64: decode standard input")
{
    const auto bytes = decodeBase64("SGVsbG8=");
    REQUIRE(bytes.has_value());
    CHECK(std::string(bytes->begin(), bytes->end()) == "Hello");
    CHECK_FALSE(decodeBase64("not*base64").has_value());
}

TEST_CASE("agent enroll: camera route always restarts the runtime")
{
    FakeCommandRunner runner;
    runner.m_lines = {"  captured embedding 1/10", "  captured embedding 2/10",
                      "Enrolled 'Ana' with 2 embedding(s) into 'x.bin'."};
    runner.m_linesExit = 0;
    FakeRuntimeControl runtime;

    EnrollOrchestrator::Paths paths;
    paths.binary = "/home/lumina/lumina_enroll";
    paths.modelDir = "/home/lumina/models/face";
    paths.store = "/home/lumina/models/face/embeddings.bin";
    EnrollOrchestrator orchestrator(runner, runtime, paths);

    std::vector<EnrollEvent> events;
    const int exitCode =
        orchestrator.enrollFromCamera("Ana", 10, [&](const EnrollEvent& event) { events.push_back(event); },
                                      nullptr);

    CHECK(exitCode == 0);
    CHECK(runtime.stops == 1);
    CHECK(runtime.starts == 1);  // restarted on success
    REQUIRE(!events.empty());
    CHECK(events.front().kind == EnrollEvent::Kind::Phase);
    CHECK(events.front().message == "stopping_runtime");
    CHECK(events.back().kind == EnrollEvent::Kind::Phase);
    CHECK(events.back().message == "starting_runtime");
    // The camera target is always the requested frame count.
    const std::vector<std::string>& argv = runner.m_lastArgv;
    CHECK(std::find(argv.begin(), argv.end(), "--camera") != argv.end());
    CHECK(std::find(argv.begin(), argv.end(), "10") != argv.end());
}

TEST_CASE("agent enroll: runtime is restarted even when the tool fails")
{
    FakeCommandRunner runner;
    runner.m_lines = {"enroll: no face was captured; no data written"};
    runner.m_linesExit = 2;
    FakeRuntimeControl runtime;

    EnrollOrchestrator orchestrator(runner, runtime, {"/home/lumina/lumina_enroll",
                                                      "/home/lumina/models/face",
                                                      "/home/lumina/models/face/embeddings.bin", ""});
    const int exitCode = orchestrator.enrollFromCamera("Ana", 10, [](const EnrollEvent&) {}, nullptr);
    CHECK(exitCode == 2);
    CHECK(runtime.starts == 1);  // the finally always runs
}

TEST_CASE("agent enroll: frame count is clamped into range")
{
    CHECK(clampFrameCount(10, 10, 10) == 10);
    CHECK(clampFrameCount(5, 10, 10) == 5);
    CHECK(clampFrameCount(0, 10, 10) == 10);        // omitted -> fallback
    CHECK(clampFrameCount(-3, 10, 10) == 10);       // bad -> fallback
    CHECK(clampFrameCount(999999, 10, 10) == 10);   // huge -> max
    CHECK(clampFrameCount(7, 3, 10) == 7);
    CHECK(clampFrameCount(0, 0, 0) == 1);           // max floor cannot go below 1
}

TEST_CASE("agent json: quoted text inside a value is not a key")
{
    CHECK(jsonGetString(R"({"t":"volume.get"})", "t").value() == "volume.get");
    // The value "t" is quoted text, not the key, so the key must not be found.
    CHECK_FALSE(jsonGetString(R"({"value":"t"})", "t").has_value());
}

TEST_CASE("agent telemetry: negative face count is clamped")
{
    const auto status = parseStatusBlock("running=1\nface_count=-4\n");
    REQUIRE(status.has_value());
    CHECK(status->faceCount == 0);
}
