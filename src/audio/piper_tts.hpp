// ---------------------------------------------------------------------------
// Piper TTS engine (libpiper C API).
//
// Piper is GPL-3.0 (INV-060). It is linked as a shared library and hidden behind
// ITtsEngine, so the rest of the runtime never sees <piper.h> (AGENTS §5). The
// pimpl keeps the C header out of every other translation unit.
// ---------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>

#include "audio/tts_engine.hpp"

namespace lumina::audio {

// Configuration for PiperTts. Paths are used as given (absolute, or relative to
// the process working directory).
struct PiperConfig {
    std::string modelPath;      // e.g. models/voices/es_MX-<name>.onnx
    std::string configPath;     // empty -> modelPath + ".json"
    std::string espeakDataDir;  // espeak-ng-data directory (phonemizer data)
};

// Concrete ITtsEngine backed by libpiper. Not copyable: one instance is owned by
// the process and shared with the single speech worker.
class PiperTts final : public ITtsEngine {
public:
    explicit PiperTts(PiperConfig config);
    ~PiperTts() override;

    PiperTts(const PiperTts&) = delete;
    PiperTts& operator=(const PiperTts&) = delete;

    // Create the synthesizer. Separate from the constructor because constructors
    // cannot cleanly report failure (same pattern as NcnnDetector::load()).
    [[nodiscard]] bool load();

    [[nodiscard]] core::Status synthesize(std::string_view text,
                                          IAudioSink& sink,
                                          const std::atomic<bool>& stop) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;  // RAII: frees the Piper engine on destruction
};

} // namespace lumina::audio
