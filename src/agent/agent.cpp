// ---------------------------------------------------------------------------
// Companion agent implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/agent.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "agent/json.hpp"
#include "core/logging.hpp"

namespace lumina::agent {

namespace {

// Current wall-clock time in seconds. Best effort: the Pi has no RTC, so this can
// jump once after boot when NTP corrects the clock (see docs/APP_PROTOCOL.md).
long long epochSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Age of a file in seconds, or nullopt when it does not exist.
std::optional<double> fileAgeSeconds(const std::string& path)
{
    struct stat info;
    if (::stat(path.c_str(), &info) != 0) {
        return std::nullopt;
    }
    const auto now = std::chrono::system_clock::now();
    const auto modified = std::chrono::system_clock::from_time_t(info.st_mtime);
    return std::chrono::duration<double>(now - modified).count();
}

// Read a whole file, or nullopt when it cannot be opened.
std::optional<std::string> readWholeFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::string boolJson(bool value)
{
    return value ? "true" : "false";
}

// Compare two strings without an early exit, so the time taken does not reveal the
// position of the first differing byte. The length is compared first (the token
// length is not a secret); equal lengths are then folded byte by byte.
bool constantTimeEquals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

}  // namespace

Agent::Agent(AgentConfig config, ICommandRunner& runner)
    : m_config(std::move(config))
    , m_runner(runner)
    , m_runtime(runner)
    , m_volume(runner)
    , m_enroller(runner, m_runtime, m_config.enroll)
    , m_udp(m_config.telemetryPort, m_config.broadcastAddress)
{
}

Agent::~Agent()
{
    stop();
}

bool Agent::start()
{
    if (!m_udp.open()) {
        return false;
    }
    m_control = std::make_unique<ControlServer>(
        m_config.controlPort, m_config.bindAddress,
        [this](const std::string& line, const ReplyFn& reply) { return handleRequest(line, reply); });
    if (!m_control->start()) {
        m_udp.close();
        return false;
    }
    m_udpThread = std::jthread([this](std::stop_token token) { udpLoop(token); });
    m_telemetryThread = std::jthread([this](std::stop_token token) { telemetryLoop(token); });
    return true;
}

void Agent::stop()
{
    // Abort any in-flight enrollment so shutdown does not wait for the 60 s
    // deadline; the orchestrator still restarts the runtime afterwards.
    m_enrollCancel.store(true, std::memory_order_relaxed);
    if (m_telemetryThread.joinable()) {
        m_telemetryThread.request_stop();
    }
    if (m_udpThread.joinable()) {
        m_udpThread.request_stop();
    }
    if (m_control) {
        m_control->stop();
    }
    // Join the threads before closing the socket they may still be using.
    if (m_telemetryThread.joinable()) {
        m_telemetryThread.join();
    }
    if (m_udpThread.joinable()) {
        m_udpThread.join();
    }
    m_udp.close();
}

void Agent::telemetryLoop(std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        // Sleep in slices so stop() is responsive.
        for (int slice = 0; slice < 10 && !stopToken.stop_requested(); ++slice) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stopToken.stop_requested()) {
            break;
        }
        publishTelemetry();
    }
}

void Agent::udpLoop(std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        std::string payload;
        struct sockaddr_in from;
        if (!m_udp.receive(payload, from, 200)) {
            continue;  // timeout: re-check stop_requested
        }
        // Only a subscription datagram matters; anything else is ignored.
        if (payload.find("\"subscribe\"") == std::string::npos) {
            continue;
        }

        {
            const std::lock_guard<std::mutex> lock(m_subscribersMutex);
            bool found = false;
            for (Subscriber& subscriber : m_subscribers) {
                if (subscriber.address.sin_addr.s_addr == from.sin_addr.s_addr &&
                    subscriber.address.sin_port == from.sin_port) {
                    subscriber.lastSeen = std::chrono::steady_clock::now();
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (m_subscribers.size() >= m_config.maxSubscribers && !m_subscribers.empty()) {
                    // Evict the oldest rather than refusing new clients.
                    m_subscribers.erase(m_subscribers.begin());
                }
                m_subscribers.push_back(Subscriber{from, std::chrono::steady_clock::now()});
                LUMINA_LOG_INFO("agent: telemetry subscriber added ({} live)",
                                m_subscribers.size());
            }
        }
        // Reply at once so the app paints without waiting for the next 1 Hz tick.
        m_udp.sendTo(from, buildTelemetryJson(collectState()));
    }
}

void Agent::publishTelemetry()
{
    const std::string payload = buildTelemetryJson(collectState());
    m_udp.sendBroadcast(payload);

    const auto now = std::chrono::steady_clock::now();
    const auto ttl = std::chrono::seconds(m_config.subscriberTtlSeconds);
    std::vector<Subscriber> live;
    {
        const std::lock_guard<std::mutex> lock(m_subscribersMutex);
        for (const Subscriber& subscriber : m_subscribers) {
            if (now - subscriber.lastSeen <= ttl) {
                live.push_back(subscriber);
            }
        }
        m_subscribers = live;  // drop expired subscribers
    }
    for (const Subscriber& subscriber : live) {
        m_udp.sendTo(subscriber.address, payload);
    }
}

std::optional<RuntimeStatus> Agent::readRuntimeStatus()
{
    const std::optional<std::string> text = readWholeFile(m_config.statusPath);
    if (!text.has_value()) {
        return std::nullopt;
    }
    return parseStatusBlock(*text);
}

TelemetryState Agent::collectState()
{
    TelemetryState state;
    state.ts = epochSeconds();

    const std::optional<RuntimeStatus> status = readRuntimeStatus();
    const std::optional<double> age = fileAgeSeconds(m_config.statusPath);
    const bool fresh = age.has_value() && *age <= static_cast<double>(m_config.statusStaleSeconds);
    state.runtimeReachable = fresh && status.has_value();
    if (status.has_value() && state.runtimeReachable) {
        state.running = status->running;
        state.uptimeS = status->uptimeSeconds;
        state.fps = status->fps;
        state.rssMb = status->rssMb;
        state.luma = status->meanLuma;
        state.faceCount = status->faceCount;
        state.people = status->people;
        state.sink = status->sinkReady ? "ready" : "waiting";
    } else {
        state.sink = "absent";
    }

    if (const std::optional<std::string> thermal = readWholeFile(m_config.thermalPath)) {
        state.tempC = parseThermalC(*thermal);
    }
    if (const std::optional<std::string> meminfo = readWholeFile(m_config.meminfoPath)) {
        state.memAvailableKb = parseMemAvailableKb(*meminfo);
    }
    if (const std::optional<std::string> loadavg = readWholeFile(m_config.loadavgPath)) {
        state.load1 = parseLoad1(*loadavg);
    }

    {
        // The telemetry thread also touches the mixer, so serialize with control.
        const std::lock_guard<std::mutex> lock(m_volumeMutex);
        const AmixerVolume::State volume = m_volume.read();
        state.volume = volume.percent;
        state.muted = volume.muted;
    }

    {
        const std::lock_guard<std::mutex> lock(m_stateMutex);
        state.enrollActive = m_enrollActive;
        state.enrollPhase = m_enrollPhase;
        state.enrollCaptured = m_enrollCaptured;
        state.enrollTotal = m_enrollTotal;
    }
    return state;
}

bool Agent::handleRequest(const std::string& line, const ReplyFn& reply)
{
    // Authentication first: every request must carry the shared token. The compare
    // is length-checked then constant-time.
    const std::string token = jsonGetString(line, "token").value_or("");
    if (m_config.token.empty() || !constantTimeEquals(token, m_config.token)) {
        reply("{\"t\":\"error\",\"code\":\"unauthorized\",\"message\":\"bad or missing token\"}");
        return false;  // close the connection
    }

    const std::string type = jsonGetString(line, "t").value_or("");
    if (type == "volume.get") {
        handleVolumeGet(reply);
    } else if (type == "volume.set") {
        const std::optional<long long> value = jsonGetInt(line, "value");
        if (!value.has_value()) {
            reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"volume.set needs 'value'\"}");
        } else {
            handleVolumeSet(*value, reply);
        }
    } else if (type == "volume.mute") {
        const std::optional<bool> value = jsonGetBool(line, "value");
        if (!value.has_value()) {
            reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"volume.mute needs 'value'\"}");
        } else {
            handleVolumeMute(*value, reply);
        }
    } else if (type == "people.list") {
        handlePeopleList(reply);
    } else if (type == "runtime.state") {
        handleRuntimeState(reply);
    } else if (type == "runtime.start") {
        handleRuntimeStart(reply);
    } else if (type == "runtime.stop") {
        handleRuntimeStop(reply);
    } else if (type == "enroll.camera.start") {
        const std::optional<std::string> name = jsonGetString(line, "name");
        if (!name.has_value() || name->empty()) {
            reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"enroll needs 'name'\"}");
        } else {
            const long long frames = jsonGetInt(line, "frames").value_or(m_config.enrollFrames);
            handleEnrollCamera(*name, frames, reply);
        }
    } else if (type == "enroll.camera.cancel") {
        m_enrollCancel.store(true, std::memory_order_relaxed);
        reply("{\"t\":\"enroll.cancelled\"}");
    } else if (type == "enroll.images") {
        const std::optional<std::string> name = jsonGetString(line, "name");
        if (!name.has_value() || name->empty()) {
            reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"enroll needs 'name'\"}");
        } else {
            handleEnrollImages(*name, line, reply);
        }
    } else {
        reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"unknown command\"}");
    }
    return true;
}

void Agent::handleVolumeGet(const ReplyFn& reply, bool ok)
{
    const std::lock_guard<std::mutex> lock(m_volumeMutex);
    const AmixerVolume::State state = m_volume.read();
    reply(std::format("{{\"t\":\"volume.state\",\"ok\":{},\"value\":{},\"muted\":{}}}", boolJson(ok),
                      state.percent, boolJson(state.muted)));
}

void Agent::handleVolumeSet(long long value, const ReplyFn& reply)
{
    if (value < 0 || value > 100) {
        reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"volume must be 0..100\"}");
        return;
    }
    bool ok = false;
    {
        const std::lock_guard<std::mutex> lock(m_volumeMutex);
        ok = m_volume.setPercent(static_cast<int>(value));
    }
    handleVolumeGet(reply, ok);
}

void Agent::handleVolumeMute(bool value, const ReplyFn& reply)
{
    bool ok = false;
    {
        const std::lock_guard<std::mutex> lock(m_volumeMutex);
        ok = m_volume.setMuted(value);
    }
    handleVolumeGet(reply, ok);
}

void Agent::handlePeopleList(const ReplyFn& reply)
{
    const std::optional<RuntimeStatus> status = readRuntimeStatus();
    std::string names = "[";
    if (status.has_value()) {
        for (std::size_t i = 0; i < status->people.size(); ++i) {
            if (i > 0) {
                names += ",";
            }
            names += "\"" + escapeJson(status->people[i]) + "\"";
        }
    }
    names += "]";
    reply(std::format("{{\"t\":\"people\",\"names\":{}}}", names));
}

void Agent::handleRuntimeState(const ReplyFn& reply)
{
    const bool running = m_runtime.isRunning();
    const std::optional<RuntimeStatus> status = readRuntimeStatus();
    std::string sink = "absent";
    if (running && status.has_value()) {
        sink = status->sinkReady ? "ready" : "waiting";
    }
    reply(std::format("{{\"t\":\"runtime.state\",\"running\":{},\"sink\":\"{}\"}}", boolJson(running),
                      sink));
}

void Agent::handleRuntimeStart(const ReplyFn& reply)
{
    m_runtime.start();
    handleRuntimeState(reply);
}

void Agent::handleRuntimeStop(const ReplyFn& reply)
{
    m_runtime.stop();
    handleRuntimeState(reply);
}

void Agent::handleEnrollCamera(const std::string& name, long long frames, const ReplyFn& reply)
{
    // One enrollment at a time: a second request gets a clear "busy" answer instead
    // of fighting over the camera and the runtime.
    std::unique_lock<std::mutex> enrollLock(m_enrollMutex, std::try_to_lock);
    if (!enrollLock.owns_lock()) {
        reply("{\"t\":\"error\",\"code\":\"busy\",\"message\":\"an enrollment is already running\"}");
        return;
    }

    // Never trust the requested count: clamp it to [1, enrollFrames]. This bounds
    // the work and the store growth, and keeps the value safe to cast to int.
    const int frameTarget = clampFrameCount(frames, m_config.enrollFrames, m_config.enrollFrames);
    m_enrollCancel.store(false, std::memory_order_relaxed);
    {
        const std::lock_guard<std::mutex> lock(m_stateMutex);
        m_enrollActive = true;
        m_enrollPhase = "stopping_runtime";
        m_enrollCaptured = 0;
        m_enrollTotal = frameTarget;
    }

    int captured = 0;
    std::string errorMessage;
    const auto onEvent = [&](const EnrollEvent& event) {
        switch (event.kind) {
        case EnrollEvent::Kind::Phase:
            {
                const std::lock_guard<std::mutex> lock(m_stateMutex);
                m_enrollPhase = event.message;
            }
            reply(std::format("{{\"t\":\"enroll.progress\",\"phase\":\"{}\"}}",
                              escapeJson(event.message)));
            break;
        case EnrollEvent::Kind::Captured:
            captured = event.captured;
            {
                const std::lock_guard<std::mutex> lock(m_stateMutex);
                m_enrollCaptured = event.captured;
                if (event.total > 0) {
                    m_enrollTotal = event.total;
                }
                m_enrollPhase = "capturing";
            }
            reply(std::format("{{\"t\":\"enroll.progress\",\"phase\":\"capturing\",\"captured\":{},"
                              "\"total\":{}}}",
                              event.captured, event.total > 0 ? event.total : frameTarget));
            break;
        case EnrollEvent::Kind::NoFace:
            reply(std::format(
                "{{\"t\":\"enroll.progress\",\"phase\":\"capturing\",\"captured\":{},\"total\":{},"
                "\"message\":\"no face detected\"}}",
                captured, frameTarget));
            break;
        case EnrollEvent::Kind::Enrolled:
            captured = event.captured;
            break;
        case EnrollEvent::Kind::Error:
            errorMessage = event.message;
            break;
        }
    };

    const int exitCode = m_enroller.enrollFromCamera(name, frameTarget, onEvent, &m_enrollCancel);

    {
        const std::lock_guard<std::mutex> lock(m_stateMutex);
        m_enrollActive = false;
        m_enrollPhase.clear();
    }
    enrollLock.unlock();

    if (exitCode == 0) {
        const std::optional<RuntimeStatus> status = readRuntimeStatus();
        const std::size_t personCount = status.has_value() ? status->faceCount : 0;
        reply(std::format("{{\"t\":\"enroll.done\",\"ok\":true,\"personCount\":{},\"embeddingsAdded\":{}}}",
                          personCount, captured));
    } else {
        const bool running = m_runtime.isRunning();
        reply(std::format(
            "{{\"t\":\"enroll.error\",\"exitCode\":{},\"message\":\"{}\",\"runtime\":\"{}\"}}",
            exitCode, escapeJson(errorMessage.empty() ? "enrollment failed" : errorMessage),
            running ? "started" : "absent"));
    }
}

void Agent::handleEnrollImages(const std::string& name, const std::string& imagesJson,
                               const ReplyFn& reply)
{
    std::unique_lock<std::mutex> enrollLock(m_enrollMutex, std::try_to_lock);
    if (!enrollLock.owns_lock()) {
        reply("{\"t\":\"error\",\"code\":\"busy\",\"message\":\"an enrollment is already running\"}");
        return;
    }

    const std::vector<std::string> encoded = jsonGetStringArray(imagesJson, "images");
    if (encoded.empty()) {
        reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"enroll.images needs 'images'\"}");
        return;
    }
    if (encoded.size() > m_config.maxEnrollImages) {
        reply(std::format(
            "{{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"too many images (max {})\"}}",
            m_config.maxEnrollImages));
        return;
    }

    // Write each decoded image to a disk scratch directory the enrollment tool can
    // read. Staging lives on disk (not /run tmpfs) so a batch of JPEGs cannot eat
    // the board's RAM (INV-052).
    const std::filesystem::path directory =
        std::filesystem::path(m_config.enrollTempDir) / ("images-" + std::to_string(epochSeconds()));
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        reply("{\"t\":\"error\",\"code\":\"internal\",\"message\":\"cannot create temp dir\"}");
        return;
    }

    std::vector<std::string> paths;
    std::size_t totalBytes = 0;
    bool tooLarge = false;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const std::optional<std::vector<std::uint8_t>> bytes = decodeBase64(encoded[i]);
        if (!bytes.has_value()) {
            continue;  // skip a malformed frame
        }
        if (bytes->size() > m_config.maxEnrollImageBytes ||
            totalBytes + bytes->size() > m_config.maxEnrollImageBytes) {
            tooLarge = true;
            break;
        }
        const std::filesystem::path path = directory / ("frame-" + std::to_string(i) + ".jpg");
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            continue;
        }
        output.write(reinterpret_cast<const char*>(bytes->data()),
                     static_cast<std::streamsize>(bytes->size()));
        totalBytes += bytes->size();
        paths.push_back(path.string());
    }

    if (tooLarge) {
        std::filesystem::remove_all(directory, filesystemError);
        reply(std::format("{{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"images exceed {} "
                          "bytes total\"}}",
                          m_config.maxEnrollImageBytes));
        return;
    }
    if (paths.empty()) {
        std::filesystem::remove_all(directory, filesystemError);
        reply("{\"t\":\"error\",\"code\":\"bad_request\",\"message\":\"no decodable images\"}");
        return;
    }

    // Optional: stop the runtime while the tool loads its own copy of the models,
    // to protect RAM on the board. Off by default (the runtime keeps running).
    const bool stopRuntime = m_config.enrollImagesStopRuntime;
    if (stopRuntime) {
        m_runtime.stop();
    }

    {
        const std::lock_guard<std::mutex> lock(m_stateMutex);
        m_enrollActive = true;
        m_enrollPhase = "capturing";
        m_enrollCaptured = 0;
        m_enrollTotal = static_cast<int>(paths.size());
    }

    int captured = 0;
    std::string errorMessage;
    const auto onEvent = [&](const EnrollEvent& event) {
        switch (event.kind) {
        case EnrollEvent::Kind::Captured:
            captured = event.captured;
            reply(std::format("{{\"t\":\"enroll.progress\",\"phase\":\"capturing\",\"captured\":{},"
                              "\"total\":{}}}",
                              captured, static_cast<int>(paths.size())));
            break;
        case EnrollEvent::Kind::Enrolled:
            captured = event.captured;
            break;
        case EnrollEvent::Kind::Error:
            errorMessage = event.message;
            break;
        default:
            break;  // NoFace/Phase have no meaning on the image route
        }
    };

    const int exitCode = m_enroller.enrollFromImages(name, paths, onEvent, &m_enrollCancel);
    std::filesystem::remove_all(directory, filesystemError);
    if (stopRuntime) {
        m_runtime.start();  // always restore the runtime if we stopped it
    }

    {
        const std::lock_guard<std::mutex> lock(m_stateMutex);
        m_enrollActive = false;
        m_enrollPhase.clear();
    }

    if (exitCode == 0) {
        const std::optional<RuntimeStatus> status = readRuntimeStatus();
        const std::size_t personCount = status.has_value() ? status->faceCount : 0;
        reply(std::format("{{\"t\":\"enroll.done\",\"ok\":true,\"personCount\":{},\"embeddingsAdded\":{}}}",
                          personCount, captured));
    } else {
        reply(std::format(
            "{{\"t\":\"enroll.error\",\"exitCode\":{},\"message\":\"{}\",\"runtime\":\"{}\"}}",
            exitCode, escapeJson(errorMessage.empty() ? "enrollment failed" : errorMessage),
            stopRuntime ? "started" : "unchanged"));
    }
}

}  // namespace lumina::agent
