// Runs the battery programs. Included only by programs_test.cpp, so the test target needs no new file. A shape case returns 42. An arithmetic case prints no `FAIL` line and exits 0.
//
// A case that compiles and does anything else is wrong code. That always fails. A case the compiler rejects
// is a known limit when it is listed in `tests/battery/rejected.txt`. A rejection not in that list, or a
// listed case that now compiles, fails, so the list stays exact.
//
// `TLC_BATTERY` picks the cases. Unset runs every 40th shape case and every arithmetic case.
// `full` runs every case. `write` runs every case and rewrites the list of rejected cases.
// `TLC_BATTERY_DUMP=<dir>` writes each program to `<dir>/<battery>/<name>.tlc` and runs nothing.
#pragma once

#include "battery.hpp"
#include "battery_arith.hpp"
#include "battery_shapes.hpp"
#include "harness.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace battery_run {

struct Ran {
    int code = -1;
    std::string out;
    std::string err;
};

// A child that inherits another worker's pipe would keep it open, so every pipe is close-on-exec.
Ran run_tlc(const std::vector<std::string> &args) {
    Ran result;
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe2(out_pipe, O_CLOEXEC) != 0 || ::pipe2(err_pipe, O_CLOEXEC) != 0) {
        result.err = "pipe failed";
        return result;
    }
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        std::vector<char *> argv;
        for (const std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(argv[0], argv.data());
        _exit(127);
    }
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    std::string *sinks[2] = {&result.out, &result.err};
    int open_fds = 2;
    while (open_fds > 0) {
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            char block[4096];
            const ssize_t n = ::read(fds[i].fd, block, sizeof block);
            if (n > 0) {
                sinks[i]->append(block, block + n);
            } else if (n == 0 || errno != EINTR) {
                ::close(fds[i].fd);
                fds[i].fd = -1;
                --open_fds;
            }
        }
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return result;
}

enum class Outcome { Ok, Rejected, Wrong };

std::string first_line(const std::string &text) {
    const std::size_t end = text.find('\n');
    return text.substr(0, end);
}

} // namespace battery_run

int run_battery_tests() {
    using namespace battery_run;
    tlc::test::Check check;
    const char *mode_env = std::getenv("TLC_BATTERY");
    const std::string mode = mode_env == nullptr ? "" : mode_env;
    const bool every = mode == "full" || mode == "write";
    std::vector<tlc::test::BatteryCase> cases;
    {
        std::vector<tlc::test::BatteryCase> shapes = tlc::test::shape_battery_cases();
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            if (every || i % 40 == 0) {
                cases.push_back(std::move(shapes[i]));
            }
        }
        for (tlc::test::BatteryCase &arith : tlc::test::arithmetic_battery_cases()) {
            cases.push_back(std::move(arith));
        }
    }

    namespace fs = std::filesystem;
    if (const char *dump = std::getenv("TLC_BATTERY_DUMP")) {
        for (const tlc::test::BatteryCase &item : tlc::test::shape_battery_cases()) {
            const fs::path dir = fs::path(dump) / item.battery;
            fs::create_directories(dir);
            std::ofstream(dir / (item.name + ".tlc")) << item.source;
        }
        return 0;
    }

    const fs::path list_path = fs::path(TLC_ROOT) / "tests" / "battery" / "rejected.txt";
    std::set<std::string> listed;
    {
        std::ifstream in(list_path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line[0] != '#') {
                listed.insert(line);
            }
        }
    }

    std::error_code ec;
    const fs::path work = fs::temp_directory_path() / ("tlc-battery-" + std::to_string(::getpid()));
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    std::vector<Outcome> outcomes(cases.size(), Outcome::Ok);
    std::vector<std::string> details(cases.size());
    std::atomic<std::size_t> next{0};
    const unsigned workers = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w) {
        pool.emplace_back([&]() {
            while (true) {
                const std::size_t i = next.fetch_add(1);
                if (i >= cases.size()) {
                    return;
                }
                const tlc::test::BatteryCase &item = cases[i];
                const fs::path file = work / (item.battery + "_" + item.name + ".tlc");
                std::ofstream(file) << item.source;
                std::vector<std::string> args = {TLC_BIN, "--run", file.string()};
                if (item.prints) {
                    args.push_back("-lc");
                }
                const Ran ran = run_tlc(args);
                // A diagnostic is `path:line:col: error: ...`. A panic at run time is wrong code, not a limit.
                const bool diagnosed = ran.err.find(": error: ") != std::string::npos;
                const bool passed = item.prints ? ran.code == 0 && ran.out.find("FAIL") == std::string::npos &&
                                                      ran.err.empty()
                                                : ran.code == 42;
                if (passed) {
                    outcomes[i] = Outcome::Ok;
                } else if (diagnosed && ran.code == 1) {
                    outcomes[i] = Outcome::Rejected;
                    details[i] = first_line(ran.err.substr(ran.err.find(": error: ") + 9));
                } else {
                    outcomes[i] = Outcome::Wrong;
                    details[i] = "exit " + std::to_string(ran.code) + " " + first_line(ran.out) + " " +
                                 first_line(ran.err);
                }
            }
        });
    }
    for (std::thread &thread : pool) {
        thread.join();
    }
    fs::remove_all(work, ec);

    std::vector<std::string> rejected;
    std::size_t ok = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const std::string key = cases[i].battery + "/" + cases[i].name;
        switch (outcomes[i]) {
        case Outcome::Wrong:
            check.fail("battery: wrong code in " + key + ": " + details[i] + "\n" + cases[i].source);
            break;
        case Outcome::Rejected:
            rejected.push_back(key);
            if (mode != "write" && !listed.contains(key)) {
                check.fail("battery: " + key + " is now rejected: " + details[i]);
            }
            break;
        case Outcome::Ok:
            ++ok;
            if (mode != "write" && listed.contains(key)) {
                check.fail("battery: " + key + " now compiles. Remove it from tests/battery/rejected.txt");
            }
            break;
        }
    }
    if (mode == "write") {
        std::sort(rejected.begin(), rejected.end());
        std::ofstream out(list_path);
        out << "# Battery cases the compiler rejects today. tests/battery_run.hpp checks this list.\n";
        out << "# TLC_BATTERY=write ./build/tlc_tests rewrites it after a full run.\n";
        for (const std::string &key : rejected) {
            out << key << '\n';
        }
    }
    if (every) {
        std::ostringstream summary;
        summary << "battery: " << cases.size() << " cases, " << ok << " ok, " << rejected.size() << " rejected\n";
        std::cerr << summary.str();
    }
    return check.failed;
}
