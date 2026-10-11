#pragma once

#include "diag.hpp"
#include "pkg.hpp"

#include <string>

namespace tlc {

struct CodegenOutput {
    bool ok = false;
    std::string ir;
    std::string error;
};

CodegenOutput codegen(const PackageGraph &graph, DiagnosticEngine &diags, const std::string &object_path,
                      bool testing = false);

} // namespace tlc
