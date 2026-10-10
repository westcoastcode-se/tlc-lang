#include "harness.hpp"
#include "pkg.hpp"

#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

class TempTree {
  public:
    TempTree() {
        char buf[] = "/tmp/tlc-pkg-XXXXXX";
        if (mkdtemp(buf) == nullptr) {
            return;
        }
        path_ = buf;
    }

    ~TempTree() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

    TempTree(const TempTree &) = delete;
    TempTree &operator=(const TempTree &) = delete;

    bool ok() const { return !path_.empty(); }

    std::string directory() const {
        std::string text = path_.string();
        if (!text.empty() && text.back() != '/') {
            text.push_back('/');
        }
        return text;
    }

    void write(const std::string &rel, std::string_view text) const {
        const fs::path full = path_ / rel;
        std::error_code ec;
        fs::create_directories(full.parent_path(), ec);
        std::ofstream out(full, std::ios::binary);
        out << text;
    }

    std::string file(const std::string &rel) const { return (path_ / rel).string(); }

  private:
    fs::path path_;
};

struct Resolved {
    tlc::PackageGraph graph;
    std::string errors;
    bool ok = false;
};

Resolved resolve_at(const std::string &entry, const std::vector<std::string> &roots = {}) {
    tlc::SourceSet sources;
    tlc::DiagnosticEngine diags(sources);
    tlc::ResolveRequest request;
    request.entry = entry;
    request.package_paths = roots;
    Resolved result;
    result.ok = tlc::resolve_packages(request, sources, diags, result.graph);
    result.errors = diags.render();
    return result;
}

const tlc::PackageNode *find_suffix(const tlc::PackageGraph &graph, std::string_view suffix) {
    for (const tlc::PackageNode &node : graph.nodes) {
        if (node.path.ends_with(suffix)) {
            return &node;
        }
    }
    return nullptr;
}

bool has_edge(const tlc::PackageGraph &graph, uint32_t from, uint32_t to, std::string_view spec) {
    for (const tlc::ImportEdge &edge : graph.edges) {
        if (edge.from_file == from && edge.to_file == to && edge.spec == spec) {
            return true;
        }
    }
    return false;
}

} // namespace

int run_pkg_tests() {
    tlc::test::Check check;

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "func main() int { return 0; }\n");
        tree.write("sibling.tlc", "int side;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, resolved.ok);
        EXPECT(check, resolved.graph.nodes.size() == 1);
        EXPECT(check, resolved.graph.edges.empty());
        EXPECT_EQ(check, resolved.graph.nodes[0].qualifier, "main");
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"math\";\nfunc main() int { return 0; }\n");
        tree.write("math/add.tlc", "func add(a int, b int) int { return a + b; }\n");
        tree.write("math/more.tlc", "func extra() int { return 1; }\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, resolved.ok);
        EXPECT_EQ(check, resolved.errors, "");
        EXPECT(check, resolved.graph.nodes.size() == 3);
        EXPECT(check, resolved.graph.edges.size() == 1);
        const tlc::PackageNode *add = find_suffix(resolved.graph, "/math/add.tlc");
        const tlc::PackageNode *more = find_suffix(resolved.graph, "/math/more.tlc");
        EXPECT(check, add != nullptr && more != nullptr);
        if (add != nullptr && more != nullptr) {
            EXPECT_EQ(check, add->qualifier, "math");
            EXPECT_EQ(check, more->qualifier, "math");
            EXPECT(check, add->package_key == more->package_key);
            EXPECT(check, !add->package_key.empty());
            EXPECT_EQ(check, add->import_spec, "math");
            EXPECT_EQ(check, more->import_spec, "math");
            EXPECT(check, has_edge(resolved.graph, resolved.graph.nodes[0].file_id, add->file_id, "math"));
            EXPECT(check, !has_edge(resolved.graph, resolved.graph.nodes[0].file_id, more->file_id, "math"));
        }
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"lib/vec\";\nfunc main() int { return 0; }\n");
        tree.write("lib/vec/len.tlc", "int len;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, resolved.ok);
        const tlc::PackageNode *vec = find_suffix(resolved.graph, "/lib/vec/len.tlc");
        EXPECT(check, vec != nullptr);
        if (vec != nullptr) {
            EXPECT_EQ(check, vec->qualifier, "vec");
            EXPECT_EQ(check, vec->import_spec, "lib/vec");
        }
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"math.tlc\";\nfunc main() int { return 0; }\n");
        tree.write("math.tlc", "func add(a int, b int) int { return a + b; }\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "unknown package");
        EXPECT(check, resolved.graph.nodes.size() == 1);
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"math\";\nfunc main() int { return 0; }\n");
        tree.write("math/a.tlc", "package math;\nint a;\n");
        tree.write("math/b.tlc", "package other;\nint b;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "package mismatch");
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"nested/a\";\nfunc main() int { return 0; }\n");
        tree.write("nested/a/a.tlc", "import \"./sib\";\nint value;\n");
        tree.write("nested/a/sib/sib.tlc", "int sib;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, resolved.ok);
        EXPECT_EQ(check, resolved.errors, "");
        const tlc::PackageNode *nested = find_suffix(resolved.graph, "/nested/a/a.tlc");
        const tlc::PackageNode *sib = find_suffix(resolved.graph, "/nested/a/sib/sib.tlc");
        EXPECT(check, nested != nullptr && sib != nullptr);
        if (nested != nullptr && sib != nullptr) {
            EXPECT_EQ(check, sib->qualifier, "sib");
            EXPECT_EQ(check, sib->import_spec, "./sib");
            EXPECT(check, sib->imported_from == nested->file_id);
            EXPECT(check, has_edge(resolved.graph, nested->file_id, sib->file_id, "./sib"));
        }
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("sub/main.tlc", "import \"../sib\";\nfunc main() int { return 0; }\n");
        tree.write("sib/sib.tlc", "int sib;\n");
        const Resolved resolved = resolve_at(tree.file("sub/main.tlc"));
        EXPECT(check, resolved.ok);
        const tlc::PackageNode *sib = find_suffix(resolved.graph, "/sib/sib.tlc");
        EXPECT(check, sib != nullptr);
        if (sib != nullptr) {
            EXPECT(check, !sib->path.ends_with("/sub/sib/sib.tlc"));
            EXPECT_EQ(check, sib->import_spec, "../sib");
            EXPECT_EQ(check, sib->qualifier, "sib");
        }
    }

    {
        TempTree entry;
        TempTree extra;
        EXPECT(check, entry.ok());
        EXPECT(check, extra.ok());
        entry.write("main.tlc", "import \"math\";\nfunc main() int { return 0; }\n");
        extra.write("math/add.tlc", "package math;\nfunc add(a int, b int) int { return a + b; }\n");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {extra.directory()});
        EXPECT(check, resolved.ok);
        const tlc::PackageNode *math = find_suffix(resolved.graph, "/math/add.tlc");
        EXPECT(check, math != nullptr);
        if (math != nullptr) {
            EXPECT(check, math->path.starts_with(extra.directory()));
            EXPECT_EQ(check, math->qualifier, "math");
        }
    }

    {
        TempTree entry;
        TempTree extra;
        EXPECT(check, entry.ok());
        EXPECT(check, extra.ok());
        entry.write("main.tlc", "import \"math\";\nfunc main() int { return 0; }\n");
        entry.write("math/add.tlc", "int from_entry;\n");
        extra.write("math/add.tlc", "int from_extra;\n");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {extra.directory()});
        EXPECT(check, resolved.ok);
        const tlc::PackageNode *math = find_suffix(resolved.graph, "/math/add.tlc");
        EXPECT(check, math != nullptr);
        if (math != nullptr) {
            EXPECT(check, math->path.starts_with(entry.directory()));
        }
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"missing\";\nfunc main() int { return 0; }\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "unknown package");
        EXPECT(check, resolved.graph.nodes.size() == 1);
        EXPECT(check, resolved.graph.edges.empty());
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"\";\nimport \"/abs\";\nfunc main() int { return 0; }\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "empty package path");
        EXPECT_HAS(check, resolved.errors, "absolute package path");
        EXPECT(check, resolved.graph.nodes.size() == 1);
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"a\";\nfunc main() int { return 0; }\n");
        tree.write("a/a.tlc", "import \"b\";\nint from_a;\n");
        tree.write("b/b.tlc", "import \"a\";\nint from_b;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "circular import a -> b -> a");
        EXPECT(check, find_suffix(resolved.graph, "/a/a.tlc") != nullptr);
        EXPECT(check, find_suffix(resolved.graph, "/b/b.tlc") != nullptr);
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"a\";\nimport \"b\";\nfunc main() int { return 0; }\n");
        tree.write("a/a.tlc", "import \"c\";\nint from_a;\n");
        tree.write("b/b.tlc", "import \"c\";\nint from_b;\n");
        tree.write("c/c.tlc", "int shared;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, resolved.ok);
        EXPECT_EQ(check, resolved.errors, "");
        const tlc::PackageNode *shared = find_suffix(resolved.graph, "/c/c.tlc");
        const tlc::PackageNode *a = find_suffix(resolved.graph, "/a/a.tlc");
        const tlc::PackageNode *b = find_suffix(resolved.graph, "/b/b.tlc");
        EXPECT(check, shared != nullptr && a != nullptr && b != nullptr);
        if (shared != nullptr && a != nullptr && b != nullptr) {
            int copies = 0;
            for (const tlc::PackageNode &node : resolved.graph.nodes) {
                if (node.path.ends_with("/c/c.tlc")) {
                    ++copies;
                }
            }
            EXPECT(check, copies == 1);
            EXPECT(check, has_edge(resolved.graph, a->file_id, shared->file_id, "c"));
            EXPECT(check, has_edge(resolved.graph, b->file_id, shared->file_id, "c"));
        }
    }

    return check.failed;
}
