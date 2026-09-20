// ---------------------------------------------------------------------------
// Enrollment image discovery implementation. See image_list.hpp.
// ---------------------------------------------------------------------------

#include "vision/image_list.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "core/logging.hpp"

namespace lumina::vision {

bool isSupportedImageExtension(const std::string& extension)
{
    // Compare case-insensitively so ".JPG" and ".jpg" both match.
    std::string lowered = extension;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered == ".jpg" || lowered == ".jpeg" || lowered == ".png" || lowered == ".bmp" ||
           lowered == ".webp";
}

std::vector<std::string> listImageFiles(const std::string& directory)
{
    std::vector<std::string> files;

    // Use the error_code overloads so a bad path logs a warning instead of
    // throwing (the tool must never crash on a typo'd folder).
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        LUMINA_LOG_WARN("enroll: '{}' is not a readable directory", directory);
        return files;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;  // stop on the first iteration error rather than looping forever
        }
        if (!entry.is_regular_file(error)) {
            continue;
        }
        if (isSupportedImageExtension(entry.path().extension().string())) {
            files.push_back(entry.path().string());
        }
    }

    std::sort(files.begin(), files.end());  // deterministic enrollment order
    return files;
}

}  // namespace lumina::vision
