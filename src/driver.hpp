#pragma once

#include <string>
#include <vector>

namespace tlc {

inline constexpr const char *kVersion = "0.1.0";

enum class Mode {
    HelpStdout,
    HelpStderr,
    Version,
    Check,
    EmitLlvm,
    EmitObj,
    Compile,
    Run,
    Error,
};

struct Options {
    Mode mode = Mode::HelpStderr;
    std::string input;
    std::string output;
    std::vector<std::string> package_paths;
    // Passed to cc after the object file, in the order given. Used when linking.
    std::vector<std::string> link_args;
    std::string error;
};

struct CompileOutput {
    int status = 1;
    bool compiled = false;
    std::string ir;
    std::string compiler_stderr;
    std::string program_stdout;
    std::string program_stderr;
};

std::string version_text();
std::string help_text();
// Vendor package directory. `TLC_LIB` when that variable is set. Otherwise
// `<prefix>/lib/tlc/vendor`, where prefix is the parent of the directory that
// holds this binary (`bin/tlc` and `build/tlc` both land on `<prefix>/lib/tlc/vendor`).
// Import a package there as `vendor/<name>`.
std::string vendor_library_root();
Options parse_args(int argc, char **argv);
CompileOutput compile_source(const Options &opt);
int execute(const Options &opt);

} // namespace tlc
