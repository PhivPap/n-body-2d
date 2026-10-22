#pragma once

#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>


namespace Hub {
// A child process, its stdout and stderr in one pipe that is read without blocking: whoever owns
// it polls it, once a frame, with no thread
class Subprocess {
public:
    // Starts argv[0], found in PATH, with no shell and stdin from /dev/null. Throws
    // std::runtime_error if it can't be started.
    explicit Subprocess(const std::vector<std::string>& argv);
    // Terminates it if it still runs
    ~Subprocess();
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;
    // The lines it wrote since the last call, without their newlines, and its last line once its
    // output ended, newline or not. Reads at most READ_MAX_BYTES a call.
    std::vector<std::string> poll();
    bool running();
    // It ended and its output was read
    bool finished();
    // Once it ended: its exit code, or 128 + the signal that ended it
    std::optional<int> exit_code();
    // SIGTERM, then SIGKILL if it hasn't ended within TERMINATE_TIMEOUT, and waits for it
    void terminate();

private:
    static constexpr size_t READ_MAX_BYTES = 64 * 1024;
    static constexpr int TERMINATE_TIMEOUT_MS = 1000;

    pid_t pid = -1;
    int output = -1;            // the pipe's read end, until the output ended
    std::string partial;        // a line whose newline hasn't come yet
    std::optional<int> status;  // waitpid()'s, once it ended

    void reap(bool wait);
};
}  // namespace Hub
