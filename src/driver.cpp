#include "driver.hpp"

#include "codegen.hpp"
#include "diag.hpp"
#include "pkg.hpp"
#include "sema.hpp"

#include "llvm/Config/llvm-config.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tlc {

std::string version_text() {
    return std::string("tlc ") + kVersion + "\nLLVM " + LLVM_VERSION_STRING + "\n";
}

std::string help_text() {
    return
        "usage: tlc [options] <entry.tlc>\n"
        "       tlc --version\n"
        "       tlc --help\n"
        "\n"
        "options:\n"
        "  --check         parse and typecheck, do not emit code\n"
        "  --emit-llvm     print LLVM IR\n"
        "  --emit-obj      write an object file\n"
        "  --run           compile, link, and run\n"
        "  -o <file>       output path\n"
        "  -P <dir>        package search root, after the entry directory\n"
        "  -l <lib>        link lib<lib> when producing an executable\n"
        "  -L <dir>        library search directory\n"
        "  -Bstatic        link later -l libraries from static archives\n"
        "  -Bdynamic       link later -l libraries from shared libraries\n"
        "  -static         link all libraries statically\n"
        "  -Wl,<opt>       pass <opt> to the linker\n";
}

namespace {

bool is_library_file(const std::string &path) {
    auto ends_with = [&](const std::string &suffix) {
        return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(".a") || ends_with(".so") || ends_with(".dylib")) {
        return true;
    }
    return path.find(".so.") != std::string::npos;
}

} // namespace

Options parse_args(int argc, char **argv) {
    Options opt;
    bool saw_mode = false;
    bool saw_input = false;
    auto set_mode = [&](Mode mode) {
        if (saw_mode && opt.mode != mode) {
            opt.mode = Mode::Error;
            opt.error = "conflicting mode flags";
            return;
        }
        saw_mode = true;
        opt.mode = mode;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (opt.mode == Mode::Error) {
            break;
        }
        if (arg == "--version") {
            set_mode(Mode::Version);
        } else if (arg == "--help" || arg == "-h") {
            set_mode(Mode::HelpStdout);
        } else if (arg == "--check") {
            set_mode(Mode::Check);
        } else if (arg == "--emit-llvm") {
            set_mode(Mode::EmitLlvm);
        } else if (arg == "--emit-obj") {
            set_mode(Mode::EmitObj);
        } else if (arg == "--run") {
            set_mode(Mode::Run);
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                opt.mode = Mode::Error;
                opt.error = "missing path after -o";
                break;
            }
            opt.output = argv[++i];
        } else if (arg == "-P") {
            if (i + 1 >= argc) {
                opt.mode = Mode::Error;
                opt.error = "missing directory after -P";
                break;
            }
            opt.package_paths.emplace_back(argv[++i]);
        } else if (arg == "-l" || arg == "-L") {
            if (i + 1 >= argc) {
                opt.mode = Mode::Error;
                opt.error = arg == "-l" ? "missing library after -l" : "missing directory after -L";
                break;
            }
            opt.link_args.push_back(arg);
            opt.link_args.push_back(argv[++i]);
        } else if ((arg.starts_with("-l") || arg.starts_with("-L")) && arg.size() > 2) {
            opt.link_args.push_back(arg);
        } else if (arg.starts_with("-Wl,")) {
            opt.link_args.push_back(arg);
        } else if (arg == "-Bstatic" || arg == "-Bdynamic") {
            // cc does not accept -Bstatic itself. The linker option is passed through -Wl.
            opt.link_args.push_back("-Wl," + arg);
        } else if (arg == "-static") {
            opt.link_args.push_back(arg);
        } else if (arg.starts_with('-')) {
            opt.mode = Mode::Error;
            opt.error = "unknown option: " + arg;
            break;
        } else if (is_library_file(arg)) {
            opt.link_args.push_back(arg);
        } else if (saw_input) {
            opt.mode = Mode::Error;
            opt.error = "multiple input files";
            break;
        } else {
            opt.input = arg;
            saw_input = true;
        }
    }

    if (opt.mode == Mode::Error) {
        return opt;
    }
    if (!saw_mode && !saw_input) {
        opt.mode = Mode::HelpStderr;
        return opt;
    }
    if (opt.mode == Mode::Version || opt.mode == Mode::HelpStdout) {
        return opt;
    }
    if (!saw_input) {
        opt.mode = Mode::Error;
        opt.error = "missing input file";
        return opt;
    }
    if (!saw_mode) {
        opt.mode = Mode::Compile;
    }
    return opt;
}

namespace {

struct ProcessOutput {
    int code = 1;
    std::string out;
    std::string err;
};

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

ProcessOutput run_process(const std::vector<std::string> &args) {
    ProcessOutput result;
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        result.err = "pipe failed\n";
        return result;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        result.err = "fork failed\n";
        return result;
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        std::vector<char *> argv;
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
    pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    int open_fds = 2;
    while (open_fds > 0) {
        if (poll(fds, 2, -1) < 0) {
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
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return result;
}

std::string make_temp(const std::string &suffix) {
    std::string pattern = "/tmp/tlcXXXXXX" + suffix;
    const int fd = mkstemps(pattern.data(), static_cast<int>(suffix.size()));
    if (fd < 0) {
        return {};
    }
    close(fd);
    return pattern;
}

struct TempPath {
    std::string path;
    ~TempPath() {
        if (!path.empty()) {
            std::filesystem::remove(path);
        }
    }
};

} // namespace

CompileOutput compile_source(const Options &opt) {
    CompileOutput output;
    if (!read_file(opt.input)) {
        output.compiler_stderr = "error: cannot read " + opt.input + "\n";
        return output;
    }
    SourceSet sources;
    DiagnosticEngine diags(sources);
    ResolveRequest request;
    request.entry = opt.input;
    request.package_paths = opt.package_paths;
    PackageGraph graph;
    if (resolve_packages(request, sources, diags, graph) && diags.ok()) {
        analyze(graph, diags);
    }
    if (!diags.ok()) {
        output.compiler_stderr = diags.render();
        return output;
    }
    if (opt.mode == Mode::Check) {
        output.compiled = true;
        output.status = 0;
        return output;
    }

    TempPath object;
    TempPath executable;
    std::string object_path;
    if (opt.mode == Mode::EmitObj) {
        object_path = opt.output.empty() ? "a.o" : opt.output;
    } else if (opt.mode == Mode::Compile || opt.mode == Mode::Run) {
        object.path = make_temp(".o");
        object_path = object.path;
        if (object_path.empty()) {
            output.compiler_stderr = "error: cannot create a temporary object file\n";
            return output;
        }
    }

    const CodegenOutput generated = codegen(graph, diags, object_path);
    output.ir = generated.ir;
    if (!generated.ok || !diags.ok()) {
        output.compiler_stderr = diags.render();
        if (output.compiler_stderr.empty()) {
            output.compiler_stderr = generated.error;
        }
        return output;
    }
    if (opt.mode == Mode::EmitLlvm || opt.mode == Mode::EmitObj) {
        output.compiled = true;
        output.status = 0;
        return output;
    }

    std::string exe_path;
    if (opt.mode == Mode::Compile) {
        exe_path = opt.output.empty() ? "a.out" : opt.output;
    } else {
        executable.path = make_temp("");
        exe_path = executable.path;
        if (exe_path.empty()) {
            output.compiler_stderr = "error: cannot create a temporary executable\n";
            return output;
        }
    }
    std::vector<std::string> link_cmd = {"cc", "-o", exe_path, object_path};
    link_cmd.insert(link_cmd.end(), opt.link_args.begin(), opt.link_args.end());
    const ProcessOutput linked = run_process(link_cmd);
    if (linked.code != 0) {
        output.compiler_stderr = linked.err.empty() ? linked.out : linked.err;
        if (output.compiler_stderr.empty()) {
            output.compiler_stderr = "error: link failed\n";
        }
        return output;
    }
    output.compiled = true;
    if (opt.mode == Mode::Compile) {
        output.status = 0;
        return output;
    }
    const ProcessOutput ran = run_process({exe_path});
    output.program_stdout = ran.out;
    output.program_stderr = ran.err;
    output.status = ran.code;
    return output;
}

int execute(const Options &opt) {
    switch (opt.mode) {
    case Mode::Version:
        std::cout << version_text();
        return 0;
    case Mode::HelpStdout:
        std::cout << help_text();
        return 0;
    case Mode::HelpStderr:
        std::cerr << help_text();
        return 1;
    case Mode::Error:
        std::cerr << "error: " << opt.error << "\n" << help_text();
        return 1;
    case Mode::Check:
    case Mode::EmitLlvm:
    case Mode::EmitObj:
    case Mode::Compile:
    case Mode::Run: {
        const CompileOutput output = compile_source(opt);
        if (opt.mode == Mode::EmitLlvm) {
            std::cout << output.ir;
        }
        std::cout << output.program_stdout;
        std::cerr << output.compiler_stderr;
        std::cerr << output.program_stderr;
        return output.status;
    }
    }
    return 1;
}

} // namespace tlc
