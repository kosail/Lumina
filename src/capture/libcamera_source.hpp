#pragma once

#include <memory>
#include <string>

#include "capture/camera.hpp"

namespace lumina::capture {

// Settings for the libcamera capture source. Kept free of libcamera types so this
// header stays thin and nothing else in the project has to include libcamera
// (AGENTS.md §6: keep headers thin). `width`/`height` are the *requested* frame
// size; libcamera may adjust them to something the sensor/pipeline supports.
struct LibcameraConfig {
    int width = 640;
    int height = 480;
    // Number of buffers the pipeline cycles through. At least 2 is required for
    // continuous streaming; more buffers smooth out scheduling jitter.
    unsigned int bufferCount = 4;
    // Specific camera id from `cam -l`; empty means "first camera found".
    std::string cameraId;
};

// ICamera backed by libcamera, targeting the OV5647 sensor (INV-011).
//
// Thread-safety: `start()`/`stop()` are called from the main thread; libcamera
// delivers completed frames on its own internal thread, where they are stashed
// as the latest frame under a mutex. `getLatest()` is called from the pipeline
// thread and never blocks on capture (INV-031).
//
// C++ note (for Java readers): the implementation is hidden behind a
// `std::unique_ptr<Impl>` ("pimpl" idiom). This is the C++ way to keep a heavy
// third-party header (libcamera) out of our public header; the edit-compile
// cycle stays fast and callers are not forced to link libcamera just to see this
// type. `Impl` is defined in the .cpp file.
class LibcameraSource final : public ICamera {
public:
    explicit LibcameraSource(LibcameraConfig config);

    // Defined in the .cpp because `Impl` is incomplete here; stops the camera if
    // it is still running.
    ~LibcameraSource() override;

    // The source owns a camera and a mutex: copying is meaningless.
    LibcameraSource(const LibcameraSource&) = delete;
    LibcameraSource& operator=(const LibcameraSource&) = delete;

    // Acquire the camera, configure a Viewfinder stream, allocate buffers and
    // start streaming. Returns false (after logging) if any step fails.
    [[nodiscard]] bool start() override;

    // Non-blocking: returns the most recent frame that has not been handed out
    // yet. Returns false when no new frame is available.
    [[nodiscard]] bool getLatest(core::Frame& out) override;

    // Stop streaming and release the camera. Idempotent.
    void stop() override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace lumina::capture
