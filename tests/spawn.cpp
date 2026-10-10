#include "harness.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tlc::test {
namespace {

void append_fd(int fd, std::string &dest) {
    char buffer[4096];
    while (true) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n > 0) {
            dest.append(buffer, buffer + n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

} // namespace

void Check::fail(const std::string &message) {
    std::cerr << message << '\n';
    ++failed;
}

void Check::expect(bool cond, const char *expr, const char *file, int line) {
    if (!cond) {
        fail(std::string(file) + ":" + std::to_string(line) + ": expected " + expr);
    }
}

void Check::expect_eq(const std::string &got, const std::string &want, const char *file, int line) {
    if (got != want) {
        fail(std::string(file) + ":" + std::to_string(line) + ":\n got: [" + got + "]\n want: [" + want + "]");
    }
}

void Check::expect_contains(const std::string &hay, const std::string &needle, const char *file, int line) {
    if (hay.find(needle) == std::string::npos) {
        fail(std::string(file) + ":" + std::to_string(line) + ": missing [" + needle + "] in [" + hay + "]");
    }
}

Proc spawn(const std::vector<std::string> &args, const std::vector<std::pair<std::string, std::string>> &env) {
    Proc result;
    if (args.empty()) {
        result.code = 127;
        result.err = "spawn: empty argv\n";
        return result;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        result.code = 127;
        result.err = "spawn: pipe failed\n";
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        result.code = 127;
        result.err = "spawn: fork failed\n";
        return result;
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        for (const auto &slot : env) {
            ::setenv(slot.first.c_str(), slot.second.c_str(), 1);
        }
        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    pollfd fds[2] = {
        {out_pipe[0], POLLIN, 0},
        {err_pipe[0], POLLIN, 0},
    };
    int open_fds = 2;
    while (open_fds > 0) {
        const int polled = poll(fds, 2, -1);
        if (polled < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const auto before = result.out.size();
            append_fd(out_pipe[0], result.out);
            if (result.out.size() == before && (fds[0].revents & (POLLHUP | POLLERR))) {
                close(out_pipe[0]);
                fds[0].fd = -1;
                --open_fds;
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            const auto before = result.err.size();
            append_fd(err_pipe[0], result.err);
            if (result.err.size() == before && (fds[1].revents & (POLLHUP | POLLERR))) {
                close(err_pipe[0]);
                fds[1].fd = -1;
                --open_fds;
            }
        }
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        result.code = 127;
        return result;
    }
    if (WIFEXITED(status)) {
        result.code = WEXITSTATUS(status);
    } else {
        result.code = 128;
    }
    return result;
}

} // namespace tlc::test
