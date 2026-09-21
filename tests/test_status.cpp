// ---------------------------------------------------------------------------
// Unit tests for the status snapshot helpers (FR-11): the key=value formatting,
// the frame-luminance measurement, and the atomic file writer. Pure logic, no
// hardware.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "core/frame.hpp"
#include "status/status.hpp"
#include "status/status_writer.hpp"

using lumina::core::Frame;
using lumina::core::PixelFormat;
using lumina::status::FileStatusWriter;
using lumina::status::meanLuma;
using lumina::status::StatusSnapshot;
using lumina::status::toKeyValue;

// The status module is compiled only when LUMINA_ENABLE_STATUS is ON (default).
// Guard so a build with the hook disabled still links the test binary.
#if defined(LUMINA_HAS_STATUS)

namespace {

// Build a frame with the given geometry and pixel bytes. `stride` defaults to a
// tightly packed row so tests can reason about exact byte offsets.
Frame makeFrame(int width, int height, PixelFormat format, std::vector<std::uint8_t> data,
                int stride = 0)
{
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.format = format;
    frame.data = std::move(data);
    frame.stride = stride > 0 ? stride : width;  // planar callers pass stride explicitly
    return frame;
}

// Read a whole file into a string (test helper).
std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("status: snapshot formats as a stable key=value block")
{
    StatusSnapshot snapshot;
    snapshot.running = true;
    snapshot.uptimeSeconds = 62;
    snapshot.fps = 4.0;
    snapshot.rssMb = 218.0;
    snapshot.meanLuma = 0.5F;
    snapshot.faceCount = 2;
    snapshot.sinkReady = true;
    snapshot.people = {"Ana", "Luis"};

    const std::string text = toKeyValue(snapshot);
    CHECK(text.find("running=1\n") != std::string::npos);
    CHECK(text.find("uptime_s=62\n") != std::string::npos);
    CHECK(text.find("fps=4.0\n") != std::string::npos);        // one decimal
    CHECK(text.find("rss_mb=218.0\n") != std::string::npos);   // one decimal
    CHECK(text.find("mean_luma=0.500\n") != std::string::npos); // three decimals
    CHECK(text.find("face_count=2\n") != std::string::npos);
    CHECK(text.find("sink_ready=1\n") != std::string::npos);
    CHECK(text.find("person=Ana\n") != std::string::npos);
    CHECK(text.find("person=Luis\n") != std::string::npos);
    // The block must end in a newline so the agent can split on lines.
    CHECK(text.back() == '\n');
}

TEST_CASE("status: meanLuma handles black and white RGB frames")
{
    // 2x2 RGB888: all black -> 0.
    const Frame black = makeFrame(2, 2, PixelFormat::Rgb888, std::vector<std::uint8_t>(12, 0), 6);
    CHECK(meanLuma(black) == doctest::Approx(0.0F));

    // 2x2 RGB888: all white (255) -> 1.
    const Frame white = makeFrame(2, 2, PixelFormat::Rgb888, std::vector<std::uint8_t>(12, 255), 6);
    CHECK(meanLuma(white) == doctest::Approx(1.0F));
}

TEST_CASE("status: meanLuma respects RGB vs BGR channel order")
{
    // A "pure red" pixel is 0.299 luminance under Rec.601; a pure blue is 0.114.
    // Getting the order wrong would swap these two numbers.
    // RGB888 red: R=255,G=0,B=0.
    const Frame rgbRed = makeFrame(1, 1, PixelFormat::Rgb888, {255, 0, 0}, 3);
    CHECK(meanLuma(rgbRed) == doctest::Approx(0.299F).epsilon(0.005));

    // BGR888 red: B=0,G=0,R=255.
    const Frame bgrRed = makeFrame(1, 1, PixelFormat::Bgr888, {0, 0, 255}, 3);
    CHECK(meanLuma(bgrRed) == doctest::Approx(0.299F).epsilon(0.005));

    // BGR888 blue: B=255,G=0,R=0.
    const Frame bgrBlue = makeFrame(1, 1, PixelFormat::Bgr888, {255, 0, 0}, 3);
    CHECK(meanLuma(bgrBlue) == doctest::Approx(0.114F).epsilon(0.005));
}

TEST_CASE("status: meanLuma reads the Y plane of a planar frame")
{
    // NV12: the first width*height bytes are the Y (luma) plane. A 2x2 frame with
    // Y=128 everywhere should read ~128/255 regardless of the UV bytes after it.
    std::vector<std::uint8_t> nv12;
    nv12.resize(2 * 2 + 2, 128);  // Y plane + one interleaved UV pair
    const Frame frame = makeFrame(2, 2, PixelFormat::Nv12, std::move(nv12), 2);
    CHECK(meanLuma(frame) == doctest::Approx(128.0F / 255.0F).epsilon(0.001));
}

TEST_CASE("status: meanLuma rejects frames without usable pixels")
{
    CHECK(meanLuma(Frame{}) == doctest::Approx(-1.0F));  // empty / default

    Frame unknown = makeFrame(2, 2, PixelFormat::Unknown, std::vector<std::uint8_t>(12, 255), 6);
    CHECK(meanLuma(unknown) == doctest::Approx(-1.0F));

    // A buffer too small for even one pixel must not read past the end.
    Frame truncated = makeFrame(4, 4, PixelFormat::Rgb888, std::vector<std::uint8_t>(2, 255), 12);
    CHECK(meanLuma(truncated) == doctest::Approx(-1.0F));
}

TEST_CASE("status: FileStatusWriter writes the snapshot atomically")
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lumina-status-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "status";

    FileStatusWriter writer(file.string());
    StatusSnapshot snapshot;
    snapshot.running = true;
    snapshot.fps = 3.0;
    writer.publish(snapshot);

    CHECK(std::filesystem::exists(file));
    CHECK(readFile(file) == toKeyValue(snapshot));
    // The temp file must be renamed away, never left behind.
    CHECK_FALSE(std::filesystem::exists(dir / "status.tmp"));

    std::filesystem::remove_all(dir);
}

#endif  // LUMINA_HAS_STATUS
