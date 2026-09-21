// ---------------------------------------------------------------------------
// SystemCommandRunner implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/command_runner.hpp"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "core/logging.hpp"

// The C runtime's environment block, so a spawned child inherits the agent's
// environment (PATH, HOME, ...). Provided by the C library; declared here rather
// than relying on a feature-test macro to expose it.
extern char** environ;

namespace lumina::agent {

namespace {

// Launch `argv` with stdout+stderr redirected into a pipe, returning the read end
// and setting `pid`, or -1 (after logging) on failure.
//
// posix_spawnp is the multi-thread-safe way to start a child. The agent has several
// threads, and fork()+execvp() would risk the child deadlocking on a lock another
// thread held at fork time (execvp is not async-signal-safe). posix_spawnp avoids
// that entirely and is the recommended API for exactly this situation.
int spawn(const std::vector<std::string>& argv, pid_t& pid)
{
    // Build the argv array before spawning (posix_spawnp does not modify it).
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const std::string& argument : argv) {
        cargv.push_back(const_cast<char*>(argument.c_str()));
    }
    cargv.push_back(nullptr);

    int fds[2];
    if (::pipe(fds) != 0) {
        LUMINA_LOG_WARN("agent: pipe() failed: {}", std::strerror(errno));
        return -1;
    }

    // File actions wire the child's stdout+stderr to the pipe's write end and close
    // both original pipe descriptors in the child, so only the dup'ed descriptors
    // survive exec (no descriptor leaks into amixer/systemctl/lumina_enroll).
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, fds[0]);
    ::posix_spawn_file_actions_addclose(&actions, fds[1]);

    const int spawnError = ::posix_spawnp(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawnError != 0) {
        LUMINA_LOG_WARN("agent: posix_spawnp('{}') failed: {}", argv[0], std::strerror(spawnError));
        ::close(fds[0]);
        ::close(fds[1]);
        return -1;
    }
    ::close(fds[1]);  // the parent keeps only the read end
    return fds[0];
}

// Block until `pid` exits and return its exit code (-1 for a signal).
int reap(pid_t pid)
{
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

}  // namespace

CommandResult SystemCommandRunner::run(const std::vector<std::string>& argv)
{
    CommandResult result;
    if (argv.empty()) {
        return result;
    }

    pid_t pid = 0;
    const int fd = spawn(argv, pid);
    if (fd < 0) {
        return result;
    }
    char buffer[4096];
    ssize_t count = 0;
    while ((count = ::read(fd, buffer, sizeof(buffer))) > 0) {
        result.output.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(fd);
    result.exitCode = reap(pid);
    return result;
}

int SystemCommandRunner::runLines(const std::vector<std::string>& argv,
                                  const std::function<void(const std::string&)>& onLine,
                                  const std::atomic<bool>* cancel)
{
    if (argv.empty()) {
        return -1;
    }

    pid_t pid = 0;
    const int fd = spawn(argv, pid);
    if (fd < 0) {
        return -1;
    }

    std::string pending;   // bytes read but not yet terminated by '\n'
    bool cancelled = false;
    while (true) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            break;
        }
        // Poll with a timeout so cancellation is noticed even while the child is
        // quiet (e.g. waiting for a face during enrollment).
        struct pollfd descriptor;
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        const int ready = ::poll(&descriptor, 1, 200);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;  // timeout: loop and re-check cancellation
        }

        char buffer[1024];
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) {
            break;  // EOF
        }
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            break;
        }
        pending.append(buffer, static_cast<std::size_t>(count));

        // Emit every complete line.
        std::size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos) {
            onLine(pending.substr(0, newline));
            pending.erase(0, newline + 1);
        }
    }

    // Flush a trailing partial line (the tool normally ends with a newline).
    if (!pending.empty()) {
        onLine(pending);
    }

    if (cancelled) {
        ::kill(pid, SIGTERM);
        ::kill(pid, SIGKILL);  // ensure it dies even if it ignores SIGTERM
    }
    ::close(fd);
    return reap(pid);
}

}  // namespace lumina::agent
