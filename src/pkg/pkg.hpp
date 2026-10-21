#pragma once

#include "ast/ast.hpp"
#include "diag/diag.hpp"
#include "source/source.hpp"

#include <string>
#include <unordered_map>
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
    // The package's link file names `<prefix>/lib/libseq.a`. Its extern trait is the iteration trait.
    bool iteration_library = false;
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
    // Linker arguments from packages loaded out of the vendor root or the
    // standard library, in import order. Ready to pass to cc. Empty when no
    // such package was loaded.
    std::vector<std::string> link_args;
};

struct ResolveRequest {
    std::string entry;
    std::vector<std::string> package_paths;
    // Directory of vendor packages. An import `vendor/<name>` loads
    // `<vendor_root>/<name>`. A package loaded from here contributes its
    // libraries to `PackageGraph::link_args`.
    std::string vendor_root;
    // Directory of standard packages, `<prefix>/lib/tlc`. Searched after the
    // entry directory and each `-P` root. `import "std/testing"` loads
    // `<std_root>/std/testing`. A `link` file there is added to `link_args`.
    std::string std_root;
    // Replacement text keyed by the path the loader opens. The language server
    // uses this for the unsaved buffer. A missing file is still loaded when its
    // path is a key and its parent directory exists.
    std::unordered_map<std::string, std::string> overlays;
};

// `import "builtin"` is this package. The compiler supplies it. It has no source file.
inline bool is_std_builtin_path(const std::string &path) { return path == "<builtin>"; }

// Loads the import graph of `request.entry`. The entry directory is the first
// package root, followed by each path in `package_paths`, then `vendor_root`
// when that directory exists. Returns false when any diagnostic was reported.
// `graph` still contains every file that loaded.
bool resolve_packages(const ResolveRequest &request, SourceSet &sources, DiagnosticEngine &diags,
                      PackageGraph &graph);

} // namespace tlc
