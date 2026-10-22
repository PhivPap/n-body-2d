#include "Hub/Subprocess.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "fmt/format.h"


namespace Hub {
Subprocess::Subprocess(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw std::runtime_error("No program to start");
    }
    // Close-on-exec: the child keeps only its own ends, dup2()'ed onto stdout and stderr
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        throw std::runtime_error(fmt::format("Failed to create a pipe: {}", std::strerror(errno)));
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    std::vector<char*> args;
    for (const std::string& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);
    const int error = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (error != 0) {
        close(fds[0]);
        throw std::runtime_error(
                fmt::format("Failed to start `{}`: {}", argv[0], std::strerror(error)));
    }
    output = fds[0];
    fcntl(output, F_SETFL, fcntl(output, F_GETFL) | O_NONBLOCK);
}

Subprocess::~Subprocess() {
    terminate();
    if (output >= 0) {
        close(output);
    }
}

std::vector<std::string> Subprocess::poll() {
    char buffer[4096];
    for (size_t read_bytes = 0; output >= 0 && read_bytes < READ_MAX_BYTES;) {
        const ssize_t n = read(output, buffer, sizeof(buffer));
        if (n > 0) {
            partial.append(buffer, static_cast<size_t>(n));
            read_bytes += static_cast<size_t>(n);
        }
        else if (n < 0 && errno == EINTR) {
            continue;
        }
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;  // nothing more for now
        }
        else {
            close(output);  // the output ended
            output = -1;
        }
    }
    std::vector<std::string> lines;
    size_t begin = 0;
    for (size_t end; (end = partial.find('\n', begin)) != std::string::npos; begin = end + 1) {
        lines.emplace_back(partial, begin, end - begin);
    }
    partial.erase(0, begin);
    if (output < 0 && !partial.empty()) {
        lines.push_back(std::move(partial));
        partial.clear();
    }
    return lines;
}

void Subprocess::reap(bool wait) {
    int child_status;
    pid_t reaped;
    do {
        reaped = waitpid(pid, &child_status, wait ? 0 : WNOHANG);
    } while (reaped < 0 && errno == EINTR);
    if (reaped == pid) {
        status = child_status;
    }
}

bool Subprocess::running() {
    if (!status) {
        reap(false);
    }
    return !status;
}

bool Subprocess::finished() {
    return !running() && output < 0;
}

std::optional<int> Subprocess::exit_code() {
    if (running()) {
        return std::nullopt;
    }
    return WIFEXITED(*status) ? WEXITSTATUS(*status) : 128 + WTERMSIG(*status);
}

void Subprocess::terminate() {
    using namespace std::chrono_literals;
    if (!running()) {
        return;
    }
    kill(pid, SIGTERM);
    for (int waited_ms = 0; waited_ms < TERMINATE_TIMEOUT_MS && running(); waited_ms += 10) {
        std::this_thread::sleep_for(10ms);
    }
    if (running()) {
        kill(pid, SIGKILL);
        reap(true);
    }
}
}  // namespace Hub
