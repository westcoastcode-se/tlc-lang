#include "driver.hpp"
#include "harness.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef TLC_ROOT
#error "TLC_ROOT must point at the project root"
#endif

namespace {

std::string read_all(const std::filesystem::path &path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

struct Expectation {
    bool compile_error = false;
    std::string error_text;
    bool has_exit = false;
    int exit_code = 0;
    bool has_stdout = false;
    std::string stdout_text;
};

Expectation parse_expect(const std::string &text) {
    Expectation expect;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        constexpr std::string_view kExit = "exit:";
        constexpr std::string_view kError = "compile-error:";
        constexpr std::string_view kStdout = "stdout:";
        if (line.starts_with(kExit)) {
            expect.has_exit = true;
            expect.exit_code = std::stoi(line.substr(kExit.size()));
        } else if (line.starts_with(kError)) {
            expect.compile_error = true;
            expect.error_text = line.substr(kError.size());
            const auto first = expect.error_text.find_first_not_of(' ');
            if (first != std::string::npos) {
                expect.error_text.erase(0, first);
            }
        } else if (line.starts_with(kStdout)) {
            expect.has_stdout = true;
            expect.stdout_text = line.substr(kStdout.size());
            if (!expect.stdout_text.empty() && expect.stdout_text.front() == ' ') {
                expect.stdout_text.erase(expect.stdout_text.begin());
            }
            std::string rest;
            const auto pos = static_cast<std::streamoff>(lines.tellg());
            if (pos >= 0) {
                rest = text.substr(static_cast<std::size_t>(pos));
            }
            if (!rest.empty()) {
                if (!expect.stdout_text.empty()) {
                    expect.stdout_text.push_back('\n');
                }
                expect.stdout_text += rest;
            }
            break;
        }
    }
    return expect;
}

} // namespace

int run_program_tests() {
    tlc::test::Check check;
    const std::filesystem::path root = std::filesystem::path(TLC_ROOT) / "tests" / "programs";
    EXPECT(check, std::filesystem::is_directory(root));
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const auto expect_path = entry.path() / "expect.txt";
        const auto main_path = entry.path() / "main.c";
        if (!std::filesystem::is_regular_file(expect_path) || !std::filesystem::is_regular_file(main_path)) {
            check.fail(name + ": missing main.c or expect.txt");
            continue;
        }
        const Expectation expect = parse_expect(read_all(expect_path));
        tlc::Options opt;
        opt.input = main_path.string();
        opt.mode = expect.compile_error ? tlc::Mode::Check : tlc::Mode::Run;
        const tlc::CompileOutput output = tlc::compile_source(opt);
        if (expect.compile_error) {
            if (output.compiled || output.status == 0) {
                check.fail(name + ": expected a compile error\n" + output.compiler_stderr);
            } else if (output.compiler_stderr.find(expect.error_text) == std::string::npos) {
                check.fail(name + ": missing [" + expect.error_text + "] in [" + output.compiler_stderr + "]");
            }
            continue;
        }
        if (!output.compiled) {
            check.fail(name + ": compile failed\n" + output.compiler_stderr);
            continue;
        }
        if (expect.has_exit && output.status != expect.exit_code) {
            check.fail(name + ": exit " + std::to_string(output.status) + " wanted " +
                       std::to_string(expect.exit_code) + "\nstdout [" + output.program_stdout + "]\nstderr [" +
                       output.program_stderr + "]\nir:\n" + output.ir);
        }
        if (expect.has_stdout && output.program_stdout != expect.stdout_text) {
            check.fail(name + ": stdout [" + output.program_stdout + "] wanted [" + expect.stdout_text + "]");
        }
    }
    return check.failed;
}
