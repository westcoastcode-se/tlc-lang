#pragma once

#include "ast.hpp"
#include "diag.hpp"
#include "source.hpp"

#include <string>
#include <vector>

namespace tlc {

struct PackageNode {
    uint32_t file_id = 0;
    std::string path;
    std::string qualifier;
    // Directory of an imported package, or the entry file's own path.
    std::string package_key;
    TranslationUnit unit;
    // The import that loaded this file. The entry file leaves these empty.
    SourceLoc import_loc{};
    uint32_t imported_from = 0;
    std::string import_spec;
};

struct ImportEdge {
    SourceLoc loc{};
    uint32_t from_file = 0;
    uint32_t to_file = 0;
    std::string spec;
};

struct PackageGraph {
    std::vector<PackageNode> nodes;
    std::vector<ImportEdge> edges;
    // Linker arguments from packages loaded out of the vendor root, in import
    // order. Ready to pass to cc. Empty when no such package was loaded.
    std::vector<std::string> link_args;
};

struct ResolveRequest {
    std::string entry;
    std::vector<std::string> package_paths;
    // Searched after `package_paths`. A package loaded from this directory
    // contributes its libraries to `PackageGraph::link_args`.
    std::string vendor_root;
};

// Loads the import graph of `request.entry`. The entry directory is the first
// package root, followed by each path in `package_paths`, then `vendor_root`
// when that directory exists. Returns false when any diagnostic was reported.
// `graph` still contains every file that loaded.
bool resolve_packages(const ResolveRequest &request, SourceSet &sources, DiagnosticEngine &diags,
                      PackageGraph &graph);

} // namespace tlc
