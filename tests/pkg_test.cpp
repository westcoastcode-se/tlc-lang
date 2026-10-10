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

Resolved resolve_at(const std::string &entry, const std::vector<std::string> &roots = {},
                    const std::string &vendor = {}) {
    tlc::SourceSet sources;
    tlc::DiagnosticEngine diags(sources);
    tlc::ResolveRequest request;
    request.entry = entry;
    request.package_paths = roots;
    request.vendor_root = vendor;
    Resolved result;
    result.ok = tlc::resolve_packages(request, sources, diags, result.graph);
    result.errors = diags.render();
    return result;
}

std::string join_args(const std::vector<std::string> &args) {
    std::string out;
    for (const std::string &arg : args) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        out += arg;
    }
    return out;
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
        tree.write("sibling.tlc", "var side int;\n");
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
        tree.write("lib/vec/len.tlc", "var len int;\n");
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
        tree.write("math/a.tlc", "package math;\n"
"var a int;\n");
        tree.write("math/b.tlc", "package other;\n"
"var b int;\n");
        const Resolved resolved = resolve_at(tree.file("main.tlc"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "package mismatch");
    }

    {
        TempTree tree;
        EXPECT(check, tree.ok());
        tree.write("main.tlc", "import \"nested/a\";\nfunc main() int { return 0; }\n");
        tree.write("nested/a/a.tlc", "import \"./sib\";\n"
"var value int;\n");
        tree.write("nested/a/sib/sib.tlc", "var sib int;\n");
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
        tree.write("sib/sib.tlc", "var sib int;\n");
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
        entry.write("math/add.tlc", "var from_entry int;\n");
        extra.write("math/add.tlc", "var from_extra int;\n");
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
        tree.write("a/a.tlc", "import \"b\";\n"
"var from_a int;\n");
        tree.write("b/b.tlc", "import \"a\";\n"
"var from_b int;\n");
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
        tree.write("a/a.tlc", "import \"c\";\n"
"var from_a int;\n");
        tree.write("b/b.tlc", "import \"c\";\n"
"var from_b int;\n");
        tree.write("c/c.tlc", "var shared int;\n");
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

    {
        TempTree entry;
        TempTree vendor;
        EXPECT(check, entry.ok());
        EXPECT(check, vendor.ok());
        entry.write("main.tlc", "import \"vendor/answer\";\nimport \"vendor/onlyso\";\nfunc main() int { return 0; }\n");
        vendor.write("answer/answer.tlc", "export extern func answer() int;\n");
        vendor.write("answer/libanswer.so", "");
        vendor.write("answer/libanswer.a", "");
        vendor.write("answer/libother.so.1", "");
        vendor.write("onlyso/onlyso.tlc", "export extern func onlyso() int;\n");
        vendor.write("onlyso/libonly.so.1.2", "");
        vendor.write("onlyso/libonly.so.1", "");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {}, vendor.directory());
        EXPECT(check, resolved.ok);
        const fs::path answer_dir = fs::canonical(fs::path(vendor.file("answer")));
        const fs::path only_dir = fs::canonical(fs::path(vendor.file("onlyso")));
        const std::string args = join_args(resolved.graph.link_args);
        EXPECT_HAS(check, args, (answer_dir / "libanswer.a").string());
        EXPECT(check, args.find("libanswer.so") == std::string::npos);
        EXPECT_HAS(check, args, "-Wl,-rpath," + answer_dir.string());
        EXPECT_HAS(check, args, (answer_dir / "libother.so.1").string());
        EXPECT_HAS(check, args, "-Wl,-rpath," + only_dir.string());
        EXPECT_HAS(check, args, (only_dir / "libonly.so.1").string());
        EXPECT(check, args.find("libonly.so.1.2") == std::string::npos);
        EXPECT(check, resolved.graph.link_args.size() == 5);
    }

    {
        TempTree entry;
        TempTree vendor;
        EXPECT(check, entry.ok());
        EXPECT(check, vendor.ok());
        entry.write("main.tlc", "import \"answer\";\nfunc main() int { return 0; }\n");
        vendor.write("answer/answer.tlc", "export func answer() int { return 1; }\n");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {}, vendor.directory());
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "unknown package");
        EXPECT(check, resolved.graph.link_args.empty());
    }

    {
        TempTree entry;
        TempTree vendor;
        EXPECT(check, entry.ok());
        EXPECT(check, vendor.ok());
        entry.write("main.tlc", "import \"vendor/listed\";\nfunc main() int { return 0; }\n");
        vendor.write("listed/listed.tlc", "export extern func listed() int;\n");
        vendor.write("listed/liblisted.a", "");
        vendor.write("listed/link", "# extra\n-lpthread\n../outside.a\n-L libs\n-Bstatic\n-Wl,-rpath,run\n");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {}, vendor.directory());
        EXPECT(check, resolved.ok);
        const fs::path listed = fs::canonical(fs::path(vendor.file("listed")));
        const std::string args = join_args(resolved.graph.link_args);
        EXPECT_HAS(check, args, (listed / "liblisted.a").string());
        EXPECT_HAS(check, args, "-lpthread");
        EXPECT_HAS(check, args, (listed / "../outside.a").lexically_normal().string());
        EXPECT_HAS(check, args, "-L" + (listed / "libs").lexically_normal().string());
        EXPECT_HAS(check, args, "-Wl,-Bstatic");
        EXPECT_HAS(check, args, "-Wl,-rpath," + (listed / "run").lexically_normal().string());
        EXPECT(check, args.find("\n-Bstatic") == std::string::npos);
    }

    {
        TempTree entry;
        TempTree vendor;
        EXPECT(check, entry.ok());
        EXPECT(check, vendor.ok());
        entry.write("main.tlc", "import \"vendor/math\";\nfunc main() int { return 0; }\n");
        entry.write("vendor/math/add.tlc", "export func add(a int, b int) int { return a + b; }\n");
        entry.write("vendor/math/libmath.a", "");
        vendor.write("math/add.tlc", "export extern func add(a int, b int) int;\n");
        vendor.write("math/libmath.a", "");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {}, vendor.directory());
        EXPECT(check, resolved.ok);
        EXPECT(check, resolved.graph.link_args.empty());
        const tlc::PackageNode *math = find_suffix(resolved.graph, "/math/add.tlc");
        EXPECT(check, math != nullptr);
        if (math != nullptr) {
            EXPECT(check, math->path.starts_with(entry.directory()));
        }
    }

    {
        TempTree entry;
        TempTree extra;
        TempTree vendor;
        EXPECT(check, entry.ok());
        EXPECT(check, extra.ok());
        EXPECT(check, vendor.ok());
        entry.write("main.tlc", "import \"vendor/math\";\nfunc main() int { return 0; }\n");
        extra.write("vendor/math/add.tlc", "export func add(a int, b int) int { return 1; }\n");
        vendor.write("math/add.tlc", "export extern func add(a int, b int) int;\n");
        vendor.write("math/libmath.a", "");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {extra.directory()}, vendor.directory());
        EXPECT(check, resolved.ok);
        EXPECT(check, resolved.graph.link_args.empty());
        const tlc::PackageNode *math = find_suffix(resolved.graph, "/math/add.tlc");
        EXPECT(check, math != nullptr);
        if (math != nullptr) {
            EXPECT(check, math->path.starts_with(extra.directory()));
        }
    }

    {
        TempTree entry;
        TempTree vendor;
        EXPECT(check, entry.ok());
        EXPECT(check, vendor.ok());
        entry.write("main.tlc", "import \"a\";\nimport \"b\";\nfunc main() int { return 0; }\n");
        entry.write("a/a.tlc", "import \"vendor/answer\";\n"
"var from_a int;\n");
        entry.write("b/b.tlc", "import \"vendor/answer\";\n"
"var from_b int;\n");
        vendor.write("answer/answer.tlc", "export extern func answer() int;\n");
        vendor.write("answer/libanswer.a", "");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {}, vendor.directory());
        EXPECT(check, resolved.ok);
        EXPECT(check, resolved.graph.link_args.size() == 1);
        EXPECT(check, resolved.graph.link_args[0].ends_with("/libanswer.a"));
    }

    {
        TempTree entry;
        EXPECT(check, entry.ok());
        entry.write("main.tlc", "import \"vendor/missing\";\nfunc main() int { return 0; }\n");
        const Resolved resolved = resolve_at(entry.file("main.tlc"), {}, entry.file("no-such-vendor"));
        EXPECT(check, !resolved.ok);
        EXPECT_HAS(check, resolved.errors, "unknown package");
        EXPECT(check, resolved.graph.link_args.empty());
    }

    return check.failed;
}
