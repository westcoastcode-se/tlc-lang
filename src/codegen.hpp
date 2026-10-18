#pragma once

#include "diag.hpp"
#include "pkg.hpp"

#include <string>

namespace tlc {

// What LLVM runs after the module is built.
// Default matches the reference: a dead store is removed, and nothing else runs.
enum class OptLevel {
    Default,
    // `-O0`. Every store stays.
    None,
    // `-O` and `-O1`.
    O1,
    O2,
    O3,
};

struct CodegenOutput {
    bool ok = false;
    std::string ir;
    std::string error;
};

CodegenOutput codegen(const PackageGraph &graph, DiagnosticEngine &diags, const std::string &object_path,
                      bool testing, bool debug_info, OptLevel opt_level);

} // namespace tlc
