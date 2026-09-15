// ---------------------------------------------------------------------------
// Lúmina beta runtime — program entry point.
//
// This is a placeholder: the capture -> inference -> alert -> TTS pipeline is not
// wired yet. It exists so the project configures and builds end to end while the
// first modules are implemented (see RAW_PLAN.md §3 and §8).
//
// When implemented, main() will:
//   1. Load configuration (src/core/config.*).
//   2. Construct the concrete implementations (libcamera, NCNN, Piper, BlueALSA).
//   3. Inject them into the pipeline and start the worker threads.
//   4. Block until shutdown, then stop the threads cleanly.
//
// C++ note (for Java readers): the entry point is a free function named `main`
// returning `int`, not a method on a class. Returning 0 signals success to the
// operating system; a non-zero value is an error code the shell can inspect.
// ---------------------------------------------------------------------------

#include <cstdio>

int main()
{
    // TODO(core): wire the runtime pipeline — RAW_PLAN.md §3 / CHG-0006.
    std::fprintf(stderr,
                 "Lumina beta runtime: pipeline not implemented yet.\n"
                 "See RAW_PLAN.md and AGENTS.md before continuing.\n");
    return 1; // Non-zero: the runtime is intentionally not functional yet.
}
