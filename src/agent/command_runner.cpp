// ---------------------------------------------------------------------------
// SystemCommandRunner implementation (FR-11).
// ---------------------------------------------------------------------------

#include "agent/command_runner.hpp"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/logging.hpp"

namespace lumina::agent {

namespace {

// Neutralize SIGPIPE for the agent: if the child dies mid-write we want read() to
// return 0 (EOF) rather than killing the whole agent. Ignoring it process-wide is
// safe here because the agent only writes to sockets with MSG_NOSIGNAL-like care.
void ignoreSigpipeOnce()
{
    static const bool done = [] {
        ::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)done;
}

// Fork+exec `argv` with stderr redirected into stdout. On success returns the read
// end of the pipe and sets `pid`; returns -1 (after logging) on failure.
int spawn(const std::vector<std::string>& argv, pid_t& pid)
{
    // Build the argv vector in the parent (no allocation after fork()).
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
    pid = ::fork();
    if (pid < 0) {
        LUMINA_LOG_WARN("agent: fork() failed: {}", std::strerror(errno));
        ::close(fds[0]);
        ::close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        // Child: wire the pipe to stdout+stderr and exec. Only async-signal-safe
        // calls are used after fork().
        ::close(fds[0]);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);
        ::execvp(cargv[0], cargv.data());
        ::_exit(127);  // exec failed; 127 mirrors a shell "command not found"
    }
    ::close(fds[1]);
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
    ignoreSigpipeOnce();

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
    ignoreSigpipeOnce();

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
