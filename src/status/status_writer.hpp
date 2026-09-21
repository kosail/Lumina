// ---------------------------------------------------------------------------
// File-backed status publisher (FR-11).
//
// Writes the snapshot to a small file under /run (tmpfs, so no SD-card wear) that
// the separate `lumina_agent` reads at ~1 Hz. Writes are atomic (temp file +
// rename) so the agent never observes a half-written block. This is the runtime's
// only status output; it performs no network I/O (INV-003).
// ---------------------------------------------------------------------------

#pragma once

#include <string>

#include "status/status.hpp"

namespace lumina::status {

// Publishes snapshots to `path` as the `key=value` block from toKeyValue().
// Thread-safety: publish() is called from the Pipeline's single status thread, so
// no locking is required here.
class FileStatusWriter final : public IStatusPublisher {
public:
    // Final destination path. The temporary file is written next to it, so the
    // parent directory must exist and be writable by the runtime user.
    explicit FileStatusWriter(std::string path);

    void publish(const StatusSnapshot& snapshot) override;

private:
    std::string m_path;
    bool m_warned = false;  // log only the first failure; publish() runs every second
};

}  // namespace lumina::status
