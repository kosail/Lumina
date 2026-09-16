// ---------------------------------------------------------------------------
// libcamera capture source (OV5647) — implementation.
//
// Verified against the *installed* libcamera 0.7.2 headers in the target sysroot
// (`cmake/rpi-sysroot/usr/include/libcamera/`) and upstream documentation
// (INV-001). Sources consulted 2026-09-16:
//   - libcamera application developer guide (v0.7.2):
//     https://git.libcamera.org/libcamera/libcamera.git/tree/Documentation/guides/application-developer.rst?h=v0.7.2
//   - libcamera public API reference: https://libcamera.org/api-html/
//
// IMPORTANT local finding: the convenient `MappedFrameBuffer` helper is declared
// in `include/libcamera/internal/mapped_framebuffer.h`, which is NOT installed by
// the `libcamera-dev` package (the symbol is exported, but the header is
// internal). We therefore map the FrameBuffer planes ourselves with mmap(); see
// MappedBuffer below.
//
// Flow (matches the guide): CameraManager::start -> pick camera -> acquire ->
// generateConfiguration({Viewfinder}) -> choose format/size -> validate ->
// configure -> FrameBufferAllocator::allocate -> mmap buffers -> createRequest +
// addBuffer -> start -> queueRequest. On each completion we copy the newest frame
// into a single-slot hand-off (dropping older ones), then reuse + re-queue the
// request. `requestCompleted` is emitted on libcamera's internal thread.
// ---------------------------------------------------------------------------

#include "capture/libcamera_source.hpp"

#include <libcamera/libcamera.h>
#include <libcamera/version.h>

#include <sys/mman.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/frame.hpp"
#include "core/logging.hpp"
#include "core/time.hpp"

namespace lumina::capture {
namespace {

// Owns one mmap()'d plane and unmaps it in the destructor (RAII).
//
// C++ note (for Java readers): there is no garbage collector for OS resources;
// an object's destructor is the deterministic place to release them. Because a
// MappedBuffer owns a unique mapping, it is move-only (see the deleted copy and
// defined move operations) — never copied.
class MappedBuffer {
public:
    MappedBuffer() = default;

    MappedBuffer(const MappedBuffer&) = delete;
    MappedBuffer& operator=(const MappedBuffer&) = delete;

    MappedBuffer(MappedBuffer&& other) noexcept { *this = std::move(other); }

    MappedBuffer& operator=(MappedBuffer&& other) noexcept {
        if (this != &other) {
            unmap();
            base_ = other.base_;
            mapLength_ = other.mapLength_;
            data_ = other.data_;
            dataLength_ = other.dataLength_;
            other.base_ = MAP_FAILED;
            other.mapLength_ = 0;
            other.data_ = nullptr;
            other.dataLength_ = 0;
        }
        return *this;
    }

    ~MappedBuffer() { unmap(); }

    // Attach a freshly mapped region. `base` is the mmap base, `offset` where the
    // usable pixels start inside it, and `length` how many bytes are usable.
    void reset(void* base, std::size_t mapLength, std::size_t offset, std::size_t length) noexcept {
        base_ = base;
        mapLength_ = mapLength;
        data_ = static_cast<const std::uint8_t*>(base) + offset;
        dataLength_ = length;
    }

    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t length() const noexcept { return dataLength_; }
    [[nodiscard]] bool valid() const noexcept { return base_ != MAP_FAILED && data_ != nullptr; }

private:
    void unmap() noexcept {
        if (base_ != MAP_FAILED) {
            ::munmap(base_, mapLength_);
            base_ = MAP_FAILED;
        }
        data_ = nullptr;
        dataLength_ = 0;
    }

    void* base_ = MAP_FAILED;
    std::size_t mapLength_ = 0;
    const std::uint8_t* data_ = nullptr;
    std::size_t dataLength_ = 0;
};

}  // namespace

// The hidden implementation. Holds all libcamera objects and the latest-frame
// slot; only this translation unit sees libcamera types.
class LibcameraSource::Impl {
public:
    explicit Impl(LibcameraConfig config) : m_config(std::move(config)) {}

    ~Impl() { stop(); }

    [[nodiscard]] bool start();
    [[nodiscard]] bool getLatest(core::Frame& out);
    void stop();

private:
    // Called by libcamera on its internal thread for every completed request.
    void onRequestCompleted(libcamera::Request* request);

    // mmap() every buffer the allocator produced.
    [[nodiscard]] bool mapBuffers();

    // Copy one completed buffer's planes into a core::Frame.
    [[nodiscard]] core::Frame convert(const libcamera::FrameBuffer& buffer) const;

    // Translate a libcamera pixel format to our own enum (Unknown if unmapped).
    [[nodiscard]] static core::PixelFormat mapFormat(const libcamera::PixelFormat& format);

    LibcameraConfig m_config;

    libcamera::CameraManager m_manager;
    std::shared_ptr<libcamera::Camera> m_camera;
    std::unique_ptr<libcamera::CameraConfiguration> m_configuration;
    libcamera::Stream* m_stream = nullptr;
    std::unique_ptr<libcamera::FrameBufferAllocator> m_allocator;
    std::vector<std::unique_ptr<libcamera::Request>> m_requests;

    // Buffer -> its mapped planes. Keyed by the raw FrameBuffer pointer because
    // that is what a completed Request hands back.
    std::unordered_map<const libcamera::FrameBuffer*, std::vector<MappedBuffer>> m_mappings;

    // Latest-frame hand-off (one slot => stale frames are naturally dropped).
    mutable std::mutex m_frameMutex;
    core::Frame m_latest;
    bool m_hasNewFrame = false;

    std::atomic<bool> m_running{false};
    bool m_managerStarted = false;
    bool m_acquired = false;
    bool m_streaming = false;
};

bool LibcameraSource::Impl::start() {
    if (m_running.load()) {
        LUMINA_LOG_WARN("libcamera source already started");
        return true;
    }

    LUMINA_LOG_INFO("libcamera {} ({}:{})", libcamera::CameraManager::version(),
                    LIBCAMERA_VERSION_MAJOR, LIBCAMERA_VERSION_MINOR);

    const int managerResult = m_manager.start();
    if (managerResult != 0) {
        LUMINA_LOG_ERROR("CameraManager::start failed: {}", std::strerror(-managerResult));
        return false;
    }
    m_managerStarted = true;

    if (!m_config.cameraId.empty()) {
        m_camera = m_manager.get(m_config.cameraId);
    } else {
        const std::vector<std::shared_ptr<libcamera::Camera>> cameras = m_manager.cameras();
        if (!cameras.empty()) {
            m_camera = cameras.front();
        }
    }
    if (!m_camera) {
        LUMINA_LOG_ERROR("no libcamera camera found (is the OV5647 connected?)");
        stop();
        return false;
    }
    LUMINA_LOG_INFO("using camera '{}'", m_camera->id());

    const int acquireResult = m_camera->acquire();
    if (acquireResult != 0) {
        LUMINA_LOG_ERROR("Camera::acquire failed: {}", std::strerror(-acquireResult));
        stop();
        return false;
    }
    m_acquired = true;

    m_configuration = m_camera->generateConfiguration({libcamera::StreamRole::Viewfinder});
    if (!m_configuration || m_configuration->empty()) {
        LUMINA_LOG_ERROR("generateConfiguration(Viewfinder) returned nothing");
        stop();
        return false;
    }

    libcamera::StreamConfiguration& streamConfig = m_configuration->at(0);
    // Express a preference for packed RGB (directly usable by the detector) and
    // let validate() adjust it if the pipeline does not support it.
    streamConfig.pixelFormat = libcamera::formats::RGB888;
    streamConfig.size = {static_cast<unsigned int>(m_config.width),
                         static_cast<unsigned int>(m_config.height)};
    streamConfig.bufferCount = m_config.bufferCount;

    const libcamera::CameraConfiguration::Status status = m_configuration->validate();
    if (status == libcamera::CameraConfiguration::Invalid) {
        LUMINA_LOG_ERROR("camera configuration is invalid at {}x{}",
                         m_config.width, m_config.height);
        stop();
        return false;
    }
    if (status == libcamera::CameraConfiguration::Adjusted) {
        LUMINA_LOG_WARN("camera adjusted the configuration to {}",
                        m_configuration->at(0).toString());
    }

    const int configureResult = m_camera->configure(m_configuration.get());
    if (configureResult != 0) {
        LUMINA_LOG_ERROR("Camera::configure failed: {}", std::strerror(-configureResult));
        stop();
        return false;
    }

    m_stream = m_configuration->at(0).stream();
    LUMINA_LOG_INFO("capturing {} at {} ({} buffers)", m_stream->configuration().toString(),
                    m_stream->configuration().pixelFormat.toString(),
                    m_stream->configuration().bufferCount);

    m_allocator = std::make_unique<libcamera::FrameBufferAllocator>(m_camera);
    const int allocateResult = m_allocator->allocate(m_stream);
    if (allocateResult < 0) {
        LUMINA_LOG_ERROR("FrameBufferAllocator::allocate failed: {}",
                         std::strerror(-allocateResult));
        stop();
        return false;
    }

    if (!mapBuffers()) {
        stop();
        return false;
    }

    // Connect before starting so no early completion is missed. `this` is not a
    // libcamera Object, so the functor overload of Signal::connect is used.
    m_camera->requestCompleted.connect(this, [this](libcamera::Request* request) {
        onRequestCompleted(request);
    });

    const std::vector<std::unique_ptr<libcamera::FrameBuffer>>& buffers =
        m_allocator->buffers(m_stream);
    for (const std::unique_ptr<libcamera::FrameBuffer>& buffer : buffers) {
        std::unique_ptr<libcamera::Request> request = m_camera->createRequest();
        if (!request) {
            LUMINA_LOG_ERROR("Camera::createRequest returned null");
            stop();
            return false;
        }
        const int addResult = request->addBuffer(m_stream, buffer.get());
        if (addResult < 0) {
            LUMINA_LOG_ERROR("Request::addBuffer failed: {}", std::strerror(-addResult));
            stop();
            return false;
        }
        m_requests.push_back(std::move(request));
    }

    const int startResult = m_camera->start();
    if (startResult != 0) {
        LUMINA_LOG_ERROR("Camera::start failed: {}", std::strerror(-startResult));
        stop();
        return false;
    }
    m_streaming = true;

    for (std::unique_ptr<libcamera::Request>& request : m_requests) {
        const int queueResult = m_camera->queueRequest(request.get());
        if (queueResult != 0) {
            LUMINA_LOG_ERROR("Camera::queueRequest failed: {}", std::strerror(-queueResult));
            stop();
            return false;
        }
    }

    m_running.store(true);
    LUMINA_LOG_INFO("libcamera streaming started");
    return true;
}

bool LibcameraSource::Impl::mapBuffers() {
    const std::vector<std::unique_ptr<libcamera::FrameBuffer>>& buffers =
        m_allocator->buffers(m_stream);

    for (const std::unique_ptr<libcamera::FrameBuffer>& buffer : buffers) {
        std::vector<MappedBuffer> planes;
        for (const libcamera::FrameBuffer::Plane& plane : buffer->planes()) {
            const int fd = plane.fd.get();
            if (fd < 0) {
                LUMINA_LOG_ERROR("frame buffer plane has no file descriptor");
                return false;
            }
            // mmap requires a page-aligned offset, so map from 0 up to the end of
            // the plane and expose the plane's own window inside that region.
            const std::size_t mapLength =
                static_cast<std::size_t>(plane.offset) + static_cast<std::size_t>(plane.length);
            void* base = ::mmap(nullptr, mapLength, PROT_READ, MAP_SHARED, fd, 0);
            if (base == MAP_FAILED) {
                LUMINA_LOG_ERROR("mmap failed: {}", std::strerror(errno));
                return false;
            }
            MappedBuffer mapping;
            mapping.reset(base, mapLength, plane.offset, plane.length);
            planes.push_back(std::move(mapping));
        }
        m_mappings.emplace(buffer.get(), std::move(planes));
    }

    LUMINA_LOG_INFO("mapped {} frame buffer(s)", m_mappings.size());
    return true;
}

core::Frame LibcameraSource::Impl::convert(const libcamera::FrameBuffer& buffer) const {
    core::Frame frame;
    if (m_stream == nullptr) {
        return frame;
    }

    const libcamera::StreamConfiguration& config = m_stream->configuration();
    frame.width = static_cast<int>(config.size.width);
    frame.height = static_cast<int>(config.size.height);
    frame.stride = static_cast<int>(config.stride);
    frame.format = mapFormat(config.pixelFormat);
    frame.capturedAt = core::now();

    const auto found = m_mappings.find(&buffer);
    if (found == m_mappings.end()) {
        return frame;  // no mapping: empty frame
    }

    const std::vector<MappedBuffer>& planes = found->second;
    std::size_t totalBytes = 0;
    for (const MappedBuffer& plane : planes) {
        totalBytes += plane.length();
    }

    // Concatenate the planes back-to-back. Packed formats have a single plane;
    // multiplanar formats (NV12/YUV420) end up as plane0 | plane1( | plane2).
    frame.data.reserve(totalBytes);
    for (const MappedBuffer& plane : planes) {
        if (plane.valid()) {
            frame.data.insert(frame.data.end(), plane.data(), plane.data() + plane.length());
        }
    }
    return frame;
}

core::PixelFormat LibcameraSource::Impl::mapFormat(const libcamera::PixelFormat& format) {
    if (format == libcamera::formats::RGB888) {
        return core::PixelFormat::Rgb888;
    }
    if (format == libcamera::formats::BGR888) {
        return core::PixelFormat::Bgr888;
    }
    if (format == libcamera::formats::RGBA8888) {
        return core::PixelFormat::Rgba8888;
    }
    if (format == libcamera::formats::NV12) {
        return core::PixelFormat::Nv12;
    }
    if (format == libcamera::formats::YUV420) {
        return core::PixelFormat::Yuv420;
    }
    return core::PixelFormat::Unknown;
}

void LibcameraSource::Impl::onRequestCompleted(libcamera::Request* request) {
    if (request == nullptr || !m_running.load()) {
        return;  // shutting down: do not touch buffers or re-queue
    }
    if (request->status() == libcamera::Request::RequestCancelled) {
        return;  // cancelled by stop(): nothing to recycle, do not re-queue
    }

    libcamera::FrameBuffer* buffer = request->findBuffer(m_stream);
    if (buffer != nullptr) {
        core::Frame frame = convert(*buffer);
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_latest = std::move(frame);
        m_hasNewFrame = true;
    }

    // Return the request (and its buffers) to the camera for the next frame.
    request->reuse(libcamera::Request::ReuseBuffers);
    if (m_camera) {
        m_camera->queueRequest(request);
    }
}

bool LibcameraSource::Impl::getLatest(core::Frame& out) {
    if (!m_running.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (!m_hasNewFrame) {
        return false;
    }
    out = std::move(m_latest);
    m_hasNewFrame = false;
    return true;
}

void LibcameraSource::Impl::stop() {
    // Flip the running flag and detach the completion signal FIRST. libcamera
    // cancels the in-flight requests while transitioning to Stopping and would
    // otherwise still call our slot, which must not re-queue a request then
    // (libcamera logs "Camera in Stopping state trying queueRequest()"). Setting
    // m_running also makes any callback already executing on libcamera's thread
    // return immediately.
    m_running.store(false);
    if (m_camera) {
        m_camera->requestCompleted.disconnect(this);
    }
    if (m_streaming && m_camera) {
        m_camera->stop();
        m_streaming = false;
    }
    if (m_allocator && m_stream) {
        m_allocator->free(m_stream);
    }
    m_requests.clear();
    m_mappings.clear();
    m_allocator.reset();
    if (m_camera && m_acquired) {
        m_camera->release();
        m_acquired = false;
    }
    m_camera.reset();
    m_configuration.reset();
    m_stream = nullptr;
    if (m_managerStarted) {
        m_manager.stop();
        m_managerStarted = false;
    }
}

LibcameraSource::LibcameraSource(LibcameraConfig config)
    : m_impl(std::make_unique<Impl>(std::move(config))) {}

LibcameraSource::~LibcameraSource() = default;

bool LibcameraSource::start() { return m_impl->start(); }

bool LibcameraSource::getLatest(core::Frame& out) { return m_impl->getLatest(out); }

void LibcameraSource::stop() { m_impl->stop(); }

}  // namespace lumina::capture
