// ---------------------------------------------------------------------------
// Enrollment image discovery (FR-04).
//
// Small, pure helper used by the enrollment tool to turn a directory into a
// sorted list of image files. Kept separate from the tool (and free of OpenCV)
// so it is unit-tested on the host (AGENTS §9).
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

namespace lumina::vision {

// Image extensions the enrollment tool understands (lower-case, with the dot).
// Listed here so callers and tests agree on the set.
[[nodiscard]] bool isSupportedImageExtension(const std::string& extension);

// Return the regular files directly inside `directory` whose extension is a
// supported image type, sorted by full path so enrollment is deterministic.
// Does not recurse. Returns an empty vector (after logging a warning) when the
// directory is missing or not a directory. Never throws.
[[nodiscard]] std::vector<std::string> listImageFiles(const std::string& directory);

}  // namespace lumina::vision
