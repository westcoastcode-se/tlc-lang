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
    // The package directory is `<std_root>/std/seq`. Its extern trait is the iteration trait.
    bool iteration_package = false;
    // `<std_root>/builtin`, or the synthetic `<builtin>` node. Intrinsics apply only here.
    bool compiler_builtin = false;
};

struct ImportEdge {
    SourceLoc loc{};
    uint32_t from_file = 0;
    uint32_t to_file = 0;
    std::string spec;
};

// One field of a named struct, in source order. `type` is the spelling `typeof` uses.
// `arm` is the `builtin.Type` tag: 0 Other, 1 Function, 2 Struct. The flags match that arm.
struct ReflectedField {
    std::string name;
    std::string type;
    int arm = 0;
    bool has_this = false;
    bool generic = false;
    bool variadic = false;
};

// One specialized struct. The checker numbers every concrete struct first, then
// these, in the order each specialization is created. The decl is appended to
// the defining file, which is not that order.
struct StructSpecNote {
    uint32_t file_id = 0;
    std::string name;
};

struct PackageGraph {
    std::vector<PackageNode> nodes;
    std::vector<ImportEdge> edges;
    // Filled by the checker. Keyed by the spelling `typeof` stores.
    // Codegen reads it when it builds a type descriptor. Absent means no fields.
    std::unordered_map<std::string, std::vector<ReflectedField>> reflected_fields;
    // Specialized structs in checker id order. Concrete structs are numbered first.
    std::vector<StructSpecNote> struct_specs;
    // Linker arguments from packages loaded out of the vendor root or the
    // standard library, in import order. Ready to pass to cc. Empty when no
    // such package was loaded.
    std::vector<std::string> link_args;
};

inline const PackageNode *package_node(const PackageGraph *graph, uint32_t file_id) {
    if (graph == nullptr) {
        return nullptr;
    }
    for (const PackageNode &node : graph->nodes) {
        if (node.file_id == file_id) {
            return &node;
        }
    }
    return nullptr;
}

// The file itself, then every other file that shares its package directory.
inline std::vector<uint32_t> package_file_ids(const PackageGraph *graph, uint32_t file_id) {
    std::vector<uint32_t> ids;
    ids.push_back(file_id);
    const PackageNode *self = package_node(graph, file_id);
    if (self == nullptr || self->package_key.empty()) {
        return ids;
    }
    for (const PackageNode &node : graph->nodes) {
        if (node.package_key == self->package_key && node.file_id != file_id) {
            ids.push_back(node.file_id);
        }
    }
    return ids;
}

// One import writes one edge. The first match is that package.
inline const PackageNode *import_target(const PackageGraph *graph, uint32_t from_file, const ImportDecl &import) {
    if (graph == nullptr) {
        return nullptr;
    }
    for (const ImportEdge &edge : graph->edges) {
        if (edge.from_file == from_file && edge.spec == import.path && edge.loc.file == import.loc.file &&
            edge.loc.offset == import.loc.offset) {
            return package_node(graph, edge.to_file);
        }
    }
    return nullptr;
}

inline bool import_selects(const ImportDecl &import, const std::string &name) {
    if (import.form != ImportDecl::Form::Selective) {
        return false;
    }
    for (const std::string &item : import.names) {
        if (item == name) {
            return true;
        }
    }
    return false;
}

struct ResolveRequest {
    std::string entry;
    std::vector<std::string> package_paths;
    // Directory of vendor packages. An import `vendor/<name>` loads
    // `<vendor_root>/<name>`. A package loaded from here contributes its
    // libraries to `PackageGraph::link_args`.
    std::string vendor_root;
    // Directory of standard packages, `<prefix>/lib/tlc`. Searched after the
    // entry directory and each `-P` root. `import "std/testing"` loads
    // `<std_root>/std/testing`. A `link` file there is added to `link_args`,
    // then `link_unix` or `link_windows` for this compiler's family.
    std::string std_root;
    // Replacement text keyed by the path the loader opens. The language server
    // uses this for the unsaved buffer. A missing file is still loaded when its
    // path is a key and its parent directory exists.
    std::unordered_map<std::string, std::string> overlays;
};

// `import "builtin"` is the compiler package. The copy in `<std_root>/builtin` is that
// package. The path `<builtin>` is the same package when the directory is absent.
inline bool is_std_builtin_path(const std::string &path) { return path == "<builtin>"; }

inline bool is_compiler_builtin(const PackageNode *node) {
    return node != nullptr && (node->compiler_builtin || is_std_builtin_path(node->path));
}

// Loads the import graph of `request.entry`. The entry directory is the first
// package root, followed by each path in `package_paths`, then `vendor_root`
// when that directory exists. Returns false when any diagnostic was reported.
// `graph` still contains every file that loaded.
bool resolve_packages(const ResolveRequest &request, SourceSet &sources, DiagnosticEngine &diags,
                      PackageGraph &graph);

} // namespace tlc
