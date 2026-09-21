// ---------------------------------------------------------------------------
// FileStatusWriter implementation (FR-11).
// ---------------------------------------------------------------------------

#include "status/status_writer.hpp"

#include <cstdio>
#include <utility>

#include "core/logging.hpp"

namespace lumina::status {

FileStatusWriter::FileStatusWriter(std::string path) : m_path(std::move(path)) {}

void FileStatusWriter::publish(const StatusSnapshot& snapshot)
{
    // Write "<path>.tmp" then rename it over the real file. rename(2) is atomic on
    // the same filesystem, so a reader either sees the previous snapshot or the new
    // one, never a partial line. Opening with "w" truncates the temp file first;
    // the payload is only a couple hundred bytes.
    const std::string tempPath = m_path + ".tmp";
    std::FILE* file = std::fopen(tempPath.c_str(), "w");
    if (file == nullptr) {
        if (!m_warned) {
            LUMINA_LOG_WARN("status: cannot open '{}' for writing", tempPath);
            m_warned = true;
        }
        return;
    }

    const std::string body = toKeyValue(snapshot);
    const std::size_t written = std::fwrite(body.data(), 1, body.size(), file);
    const int closeResult = std::fclose(file);  // always close, even after a short write
    if (written != body.size() || closeResult != 0) {
        if (!m_warned) {
            LUMINA_LOG_WARN("status: short write to '{}'", tempPath);
            m_warned = true;
        }
        return;  // leave the previous good snapshot in place
    }

    if (std::rename(tempPath.c_str(), m_path.c_str()) != 0) {
        if (!m_warned) {
            LUMINA_LOG_WARN("status: cannot rename '{}' to '{}'", tempPath, m_path);
            m_warned = true;
        }
    }
}

}  // namespace lumina::status
