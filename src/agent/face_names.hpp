// ---------------------------------------------------------------------------
// Enrolled-person names read straight from the face store (FR-04/FR-11).
//
// The runtime writes `models/face/embeddings.bin` (see src/vision/face_store.cpp).
// The agent must be able to list the enrolled people even when the runtime is
// stopped, because the runtime only writes `person=` lines into
// /run/lumina/status while it is running. This reader parses the store's name
// table directly so the companion app always sees the registered people.
//
// C++ note (for Java readers): this file is deliberately dependency-free — it does
// not include the OpenCV-backed FaceStore — so it can stay inside the tiny
// lumina_agent_core library. The layout it reads is small and explicit:
//
//   magic[8] "LUMFACE1" | u32 version | u32 dimension | len-prefixed modelId |
//   u32 personCount | per person: len-prefixed name, u32 embeddingCount,
//                    embeddingCount * dimension * 4 bytes of float embeddings
//
// All integers are native little-endian (the Pi and the build host are both LE; see
// face_store.cpp). Only names are returned; the embeddings are skipped, so the
// reader never allocates the (much larger) vector data. Keep this in sync with the
// writer; tests/test_agent.cpp round-trips a real FaceStore file through it.
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

namespace lumina::agent {

// Every enrolled name, in store insertion order.
//
// Returns an empty vector when the file is missing, unreadable, empty, malformed,
// truncated, or has an unknown magic/version. The caller treats that as "no
// enrolments known" (the same as an empty store), so this never throws and never
// distinguishes "no file" from "bad file". Reads at most 64 MiB (the store's own
// sanity cap) and applies the same per-field caps as FaceStore, so a corrupt or
// hostile file cannot trigger a large allocation.
[[nodiscard]] std::vector<std::string> readEnrolledNames(const std::string& storePath);

}  // namespace lumina::agent
