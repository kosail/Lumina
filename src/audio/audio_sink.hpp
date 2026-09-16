// ---------------------------------------------------------------------------
// Audio output abstraction.
//
// The runtime reaches speakers only through IAudioSink so the pipeline can run on
// the host against a mock (INV-030, AGENTS §5). The concrete implementation for
// the beta is AlsaSink, which writes to the `bluealsa` PCM provided by the
// bluealsa ALSA plugin (INV-022, RAW_PLAN §9).
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>

namespace lumina::audio {

// A destination for mono 32-bit float PCM (the format Piper produces).
//
// Threading: implementations need NOT be thread-safe. The speech worker owns the
// sink; only stop() may be called from another thread to interrupt playback
// (INV-032).
class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    // Open the underlying device. Returns false after logging the reason; never
    // throws across the API boundary.
    [[nodiscard]] virtual bool open() = 0;

    // Play `count` mono float samples at `sampleRate` Hz. Blocking: returns once
    // the samples have been handed to the device. Returns false if the sink was
    // stopped or a fatal device error occurred (the caller aborts this utterance).
    //
    // C++ note (for Java readers): `const float*` is a non-owning view into the
    // caller's buffer. The callee must copy what it needs and must not retain the
    // pointer after returning (no Java-like garbage collector keeps it alive).
    [[nodiscard]] virtual bool write(const float* samples, std::size_t count, int sampleRate) = 0;

    // Block until every previously written sample has played out. Called at the
    // end of an utterance so the next one does not cut it off. Errors are logged,
    // not thrown; a no-op when the sink is not open.
    virtual void drain() noexcept = 0;

    // Abort any buffered audio immediately. Called from the speech worker, and
    // possibly from another thread to interrupt speech (INV-032); must be quick.
    virtual void stop() noexcept = 0;

    // Release the device. Safe to call more than once, and to open() again after.
    virtual void close() noexcept = 0;
};

} // namespace lumina::audio
