// ---------------------------------------------------------------------------
// Text-to-speech abstraction.
//
// ITtsEngine turns Spanish text into PCM and streams it to an IAudioSink. The
// beta implementation is PiperTts (libpiper + espeak-ng). Kept behind an
// interface so logic is host-testable and the GPL engine is isolated (INV-030,
// INV-060, AGENTS §5).
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <string_view>

#include "audio/audio_sink.hpp"
#include "core/result.hpp"

namespace lumina::audio {

// Text-to-speech engine.
//
// Threading: synthesize() is called by the single speech worker and is not
// re-entrant. `stop` is written by other pipeline threads and read here; speech
// must stop soon after it becomes true so a safety alert can preempt (INV-032).
class ITtsEngine {
public:
    virtual ~ITtsEngine() = default;

    // Synthesize `text` and stream the audio to `sink`.
    //   - returns success once the whole utterance was written,
    //   - returns failure(message) if the engine or sink failed,
    //   - stops early (still success) when `stop` becomes true.
    [[nodiscard]] virtual core::Status synthesize(std::string_view text,
                                                  IAudioSink& sink,
                                                  const std::atomic<bool>& stop) = 0;
};

} // namespace lumina::audio
