#include "harness.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

std::string diagnose(const std::string &source) {
    tlc::SourceSet sources;
    tlc::SourceFile &file = sources.add("mem.tlc", source);
    tlc::DiagnosticEngine diags(sources);
    tlc::Lexer lexer(file, diags);
    tlc::Parser parser(lexer.tokenize(), diags);
    const tlc::TranslationUnit unit = parser.parse();
    if (!diags.ok()) {
        return diags.render();
    }
    tlc::analyze(unit, diags);
    return diags.render();
}

std::string package_key_for(const std::string &path) {
    const auto slash = path.rfind('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(0, slash);
}

std::string stem_qualifier(const std::string &path, const tlc::TranslationUnit &unit) {
    if (unit.package && !unit.package->name.empty()) {
        return unit.package->name;
    }
    const auto slash = path.rfind('/');
    if (slash != std::string::npos) {
        const std::string parent = path.substr(0, slash);
        const auto prev = parent.rfind('/');
        return prev == std::string::npos ? parent : parent.substr(prev + 1);
    }
    std::string stem = path;
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    return stem;
}

const tlc::PackageNode *find_path(const tlc::PackageGraph &graph, const std::string &path) {
    for (const tlc::PackageNode &node : graph.nodes) {
        if (node.path == path) {
            return &node;
        }
    }
    return nullptr;
}

std::string diagnose_files(const std::vector<std::pair<std::string, std::string>> &files) {
    tlc::SourceSet sources;
    tlc::DiagnosticEngine diags(sources);
    tlc::PackageGraph graph;
    for (const auto &[path, text] : files) {
        tlc::SourceFile &file = sources.add(path, text);
        tlc::Lexer lexer(file, diags);
        tlc::Parser parser(lexer.tokenize(), diags);
        tlc::PackageNode node;
        node.file_id = file.id();
        node.path = path;
        node.unit = parser.parse();
        node.package_key = package_key_for(path);
        node.qualifier = stem_qualifier(path, node.unit);
        graph.nodes.push_back(std::move(node));
    }
    if (!diags.ok()) {
        return diags.render();
    }
    for (const tlc::PackageNode &node : graph.nodes) {
        for (const tlc::ImportDecl &import : node.unit.imports) {
            const tlc::PackageNode *target = nullptr;
            for (const tlc::PackageNode &candidate : graph.nodes) {
                if (candidate.package_key == import.path) {
                    target = &candidate;
                    break;
                }
            }
            if (target == nullptr) {
                const std::string direct = import.path.ends_with(".tlc") ? import.path : import.path + ".tlc";
                const std::string mod = import.path.ends_with(".tlc") ? std::string{} : import.path + "/mod.tlc";
                target = find_path(graph, direct);
                const tlc::PackageNode *other = mod.empty() ? nullptr : find_path(graph, mod);
                if (target != nullptr && other != nullptr) {
                    continue;
                }
                if (target == nullptr) {
                    target = other;
                }
            }
            if (target == nullptr) {
                continue;
            }
            graph.edges.push_back(tlc::ImportEdge{import.loc, node.file_id, target->file_id, import.path});
        }
    }
    tlc::analyze(graph, diags);
    return diags.render();
}

} // namespace

int run_sema_tests() {
    tlc::test::Check check;

    {
        const std::string errors = diagnose(
            "int acc;\n"
            "func add(a int, b int) int;\n"
            "func main() int {\n"
            "  int n = add(1, 2);\n"
            "  while (n) {\n"
            "    acc = acc + n;\n"
            "    n = n - 1;\n"
            "  }\n"
            "  return acc;\n"
            "}\n"
            "func add(a int, b int) int { return a + b; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int { return later(); }\n"
            "func later() int { return 1; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("func main() int { return missing; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  return x;\n"
            "  int x;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x;\n"
            "  int x;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func f(a int) int { return a; }\n"
            "func main() int { return f(1, 2); }\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "func f() void { return 1; }\n"
            "func main() int { return; }\n");
        EXPECT_HAS(check, errors, "return type");
        EXPECT(check, errors.find("return type") != errors.rfind("return type"));
    }

    {
        const std::string errors = diagnose("func main() int { break; return 0; }\n");
        EXPECT_HAS(check, errors, "break outside loop");
    }

    {
        const std::string errors = diagnose("func main() int { continue; return 0; }\n");
        EXPECT_HAS(check, errors, "continue outside loop");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  for (int i = 0; i < 1; i = i + 1) {}\n"
            "  return i;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main(n int) int {\n"
            "  int n;\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose("export extern func printf() int;\nfunc main() int { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\";\n"
             "func main() int {\n"
             "  math.base = 40;\n"
             "  return math.add(math.base, 2);\n"
             "}\n"},
            {"math.tlc",
             "export int base;\n"
             "export func add(a int, b int) int { return a + b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import { add } from \"math\";\n"
             "func main() int { return add(40, 2); }\n"},
            {"math.tlc", "export func add(a int, b int) int { return a + b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\" as m;\n"
             "func main() int { return m.add(40, 2); }\n"},
            {"math.tlc", "export func add(a int, b int) int { return a + b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\" as m;\n"
             "func main() int { return math.add(1, 2); }\n"},
            {"math.tlc", "export func add(a int, b int) int { return a + b; }\n"},
        });
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\" as a;\n"
             "import \"also\" as b;\n"
             "func main() int { return a.add(20, 20) + b.sub(3, 1); }\n"},
            {"math.tlc",
             "package math;\n"
             "export func add(a int, b int) int { return a + b; }\n"},
            {"also.tlc",
             "package math;\n"
             "export func sub(a int, b int) int { return a - b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() int { return math.secret(); }\n"},
            {"math.tlc",
             "func secret() int { return 1; }\n"
             "export func add(a int, b int) int { return a + b + secret(); }\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
        EXPECT(check, errors.find("undeclared identifier") == std::string::npos);
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() int { return secret(); }\n"},
            {"math.tlc", "func secret() int { return 1; }\n"},
        });
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\";\n"
             "import \"also\";\n"
             "func main() int { return 0; }\n"},
            {"math.tlc", "package math;\nexport func add(a int, b int) int { return a + b; }\n"},
            {"also.tlc", "package math;\nexport func sub(a int, b int) int { return a - b; }\n"},
        });
        EXPECT_HAS(check, errors, "duplicate package name");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { missing, secret } from \"math\";\nfunc main() int { return 0; }\n"},
            {"math.tlc", "func secret() int { return 1; }\nexport func add(a int, b int) int { return a + b; }\n"},
        });
        EXPECT_HAS(check, errors, "missing is not exported");
        EXPECT_HAS(check, errors, "secret is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import { add } from \"math\";\n"
             "int add;\n"
             "func main() int { return add; }\n"},
            {"math.tlc", "export func add(a int, b int) int { return a + b; }\n"},
        });
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n = 7;\n"
            "  int *p = 0;\n"
            "  int **pp = &p;\n"
            "  p = &n;\n"
            "  pp = &p;\n"
            "  **pp = 42;\n"
            "  return n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p = 0;\n"
            "  p = 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n = 1;\n"
            "  return *n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() int { return &1; }\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p = 0;\n"
            "  return p + 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("void *p;\nfunc main() int { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add4(p int *) int {\n"
            "  return p[0] + p[1] + p[2] + p[3];\n"
            "}\n"
            "func main() int {\n"
            "  int a[4];\n"
            "  a[0] = 10;\n"
            "  a[1] = 20;\n"
            "  a[2] = 7;\n"
            "  a[3] = 5;\n"
            "  int *q = a;\n"
            "  char *s = \"hi\";\n"
            "  int n = 0;\n"
            "  while (s[n] != 0) {\n"
            "    n = n + 1;\n"
            "  }\n"
            "  return add4(a) + q[0] - a[0] + n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int a[1];\n"
            "  int *p = 0;\n"
            "  return a[p];\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p = \"hi\";\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "func make(x int, y int) Point {\n"
            "  Point p;\n"
            "  p.x = x;\n"
            "  p.y = y;\n"
            "  return p;\n"
            "}\n"
            "func sum(p Point) int {\n"
            "  return p.x + p.y;\n"
            "}\n"
            "func main() int {\n"
            "  Point p;\n"
            "  Point *q;\n"
            "  p = make(40, 2);\n"
            "  q = &p;\n"
            "  (*q).y = 2;\n"
            "  Point r;\n"
            "  r = *q;\n"
            "  return sum(r) + make(0, 0).x;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func main() int {\n"
            "  Point p;\n"
            "  return p.z;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func make() Point {\n"
            "  Point p;\n"
            "  return p;\n"
            "}\n"
            "func main() int {\n"
            "  make().x = 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose("struct Empty {};\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int x; };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"geom\";\n"
                       "func main() int {\n"
                       "  geom.Point p;\n"
                       "  return 0;\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "struct Point { int x; int y; };\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"geom\";\n"
                       "func main() int {\n"
                       "  geom.Point p;\n"
                       "  p.x = 40;\n"
                       "  p.y = 2;\n"
                       "  return geom.sum(p);\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "export struct Point { int x; int y; };\n"
                       "export func sum(p Point) int { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern func printf(fmt const char *, ...) int;\n"
            "extern int slot;\n"
            "func main() int {\n"
            "  slot = 1;\n"
            "  printf(\"hi\\n\");\n"
            "  printf(\"n %d\\n\", slot);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern func printf(fmt const char *, ...) int;\n"
            "func main() int {\n"
            "  printf();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "extern int slot;\n"
            "func main() int { return slot; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "typedef Point *Ptr;\n"
            "typedef int Integer;\n"
            "typedef int Pair[2];\n"
            "func make(x int, y int) Point {\n"
            "  Point p;\n"
            "  p.x = x;\n"
            "  p.y = y;\n"
            "  return p;\n"
            "}\n"
            "func sum(p Point) int { return p.x + p.y; }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  Ptr q;\n"
            "  Integer bias;\n"
            "  Pair extra;\n"
            "  p = make(40, 2);\n"
            "  q = &p;\n"
            "  bias = 0;\n"
            "  extra[0] = 0;\n"
            "  return sum(*q) + bias + extra[0];\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red, Green = 40, Blue };\n"
            "enum Color value = Color.Blue;\n"
            "func main() int { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red, Green = 40, Blue };\n"
            "func main() int {\n"
            "  Color.Blue = 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red, Green = 40, Blue };\n"
            "func main() int {\n"
            "  int *p;\n"
            "  p = &Color.Blue;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red, Green, Red };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "typedef enum { Red } Red;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "typedef int Point;\n"
            "int Point;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose("enum Color {};\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "enum Color { A = 2147483647, B };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red = 1 };\n"
            "func main() int {\n"
            "  int n = Color.Red;\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "enum Tiny i8 { A = 200 };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; }\n"
            "enum Color { Red }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  if (Color.Red == Color.Red) {\n"
            "    return p.x;\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern enum Flag i8 { On = 1 }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "extern enum");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red = 1 }\n"
            "func take(x int) int { return x; }\n"
            "func main() int { return take(Color.Red); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "extern enum Flag { On = 1 }\n"
            "func take(x u32) int { return 0; }\n"
            "func Flag::on(this self Flag) int { return 1; }\n"
            "func main() int {\n"
            "  n := take(Flag.On);\n"
            "  return n + Flag.On.on();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "enum One { Val };\n"
            "enum Two { Val };\n"
            "func main() int {\n"
            "  value := Two.Val;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Point } from \"geom\";\n"
                       "func main() int { return 0; }\n"},
            {"geom.tlc", "package geom;\n"
                       "typedef int Point;\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"geom\";\n"
                       "func main() int {\n"
                       "  geom.Alias p;\n"
                       "  p.x = 40;\n"
                       "  p.y = 2;\n"
                       "  return geom.sum(p);\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "struct Point { int x; int y; };\n"
                       "export typedef Point Alias;\n"
                       "export func sum(p Alias) int { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Alias, sum } from \"geom\";\n"
                       "func main() int {\n"
                       "  Alias p;\n"
                       "  p.x = 40;\n"
                       "  p.y = 2;\n"
                       "  return sum(p);\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "struct Point { int x; int y; };\n"
                       "export typedef Point Alias;\n"
                       "export func sum(p Alias) int { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Color } from \"geom\";\n"
                       "func main() int {\n"
                       "  value := Color.Blue;\n"
                       "  return 0;\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "export enum Color { Red, Green = 40, Blue };\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n;\n"
            "  int x;\n"
            "  n = 1;\n"
            "  x = 0;\n"
            "  switch (n) {\n"
            "  case 1:\n"
            "    x = 2;\n"
            "  case 2:\n"
            "    x = x + 40;\n"
            "    break;\n"
            "  default:\n"
            "    x = 7;\n"
            "    break;\n"
            "  }\n"
            "  return x;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  case 1: return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "case outside switch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  default: return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "default outside switch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p;\n"
            "  p = 0;\n"
            "  switch (p) {\n"
            "  case 0: return 0;\n"
            "  }\n"
            "  return 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n;\n"
            "  n = 1;\n"
            "  switch (n) {\n"
            "  case 1: return 1;\n"
            "  case 1: return 2;\n"
            "  default: return 3;\n"
            "  default: return 4;\n"
            "  }\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n;\n"
            "  n = 1;\n"
            "  switch (n) {\n"
            "  case n: return 0;\n"
            "  }\n"
            "  return 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n;\n"
            "  int *p;\n"
            "  n = 0;\n"
            "  p = 0;\n"
            "  do {\n"
            "    n = 1;\n"
            "  } while (p);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  switch (1) {\n"
            "  case 1: break;\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n;\n"
            "  char c;\n"
            "  n = sizeof(int);\n"
            "  n = sizeof n;\n"
            "  n = sizeof(n);\n"
            "  n = sizeof(void *);\n"
            "  c = 0;\n"
            "  n = sizeof c;\n"
            "  return n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("func main() int { return sizeof(void); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func f() int { return 1; }\n"
            "func main() int { return sizeof(f); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n;\n"
            "  char *c;\n"
            "  void *v;\n"
            "  int *p;\n"
            "  n = 42;\n"
            "  c = char*(n);\n"
            "  v = void*(c);\n"
            "  n = int(v);\n"
            "  p = &n;\n"
            "  c = char*(p);\n"
            "  v = void*(c);\n"
            "  p = int*(v);\n"
            "  return *p;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p;\n"
            "  void *v;\n"
            "  v = 0;\n"
            "  p = v;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p;\n"
            "  void *v;\n"
            "  v = 0;\n"
            "  p = int*(v);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  void *v;\n"
            "  v = 0;\n"
            "  return *v;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() int { void(1); return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func id(p void *) void * { return p; }\n"
            "func main() int {\n"
            "  void *v;\n"
            "  v = 0;\n"
            "  v = id(v);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  bool a;\n"
            "  bool b;\n"
            "  a = true;\n"
            "  b = false;\n"
            "  b = 8;\n"
            "  if (a && !b) {\n"
            "    return 1;\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p;\n"
            "  int x;\n"
            "  x = 0;\n"
            "  p = nullptr;\n"
            "  if (p == nullptr) {\n"
            "    return 1;\n"
            "  }\n"
            "  p = &x;\n"
            "  return p != nullptr;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x = nullptr;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  char c;\n"
            "  short s;\n"
            "  int n;\n"
            "  unsigned int u;\n"
            "  long long a;\n"
            "  c = 'A';\n"
            "  s = 1;\n"
            "  n = c + s;\n"
            "  u = 0u32;\n"
            "  u = u - 1u32;\n"
            "  a = 100000;\n"
            "  a = a * 100000;\n"
            "  return n + int(u) + int(a);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "typedef unsigned int u32;\n"
            "func main() int {\n"
            "  u32 n;\n"
            "  n = 1u32;\n"
            "  return n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("func main() int { i8 n = 200i8; return n; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() int { return 3000000000; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() int { return 5000000000u32; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int *p;\n"
            "  int n;\n"
            "  n = 1;\n"
            "  p = long*(n);\n"
            "  return long(p);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  switch (1) {\n"
            "  case 1u32: return 0;\n"
            "  }\n"
            "  return 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "struct Pair { int a; int b; };\n"
            "func sum(n int) int { return n; }\n"
            "func Point::sum(this self Point, extra int) int { return self.x + self.y + extra; }\n"
            "func Pair::sum(this self Pair, extra int) int { return self.a + self.b + extra; }\n"
            "func Point::value(this self Point) int;\n"
            "func Point::value(this self Point) int { return self.x; }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  Pair q;\n"
            "  p.x = 20;\n"
            "  p.y = 20;\n"
            "  q.a = 1;\n"
            "  q.b = 1;\n"
            "  return p.sum(1) + q.sum(0) + p.value() + sum(0);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func Point::sum(this self Point, extra int) int { return self.x + extra; }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  return p.nope(1);\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func Point::sum(this self Point, extra int) int { return self.x + extra; }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  return p.sum();\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func Point::sum(this self Point, extra int) int { return self.x + extra; }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  return p.sum(p);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func Point::sum(this self Point) int { return self.x; }\n"
            "func Point::sum(this self Point) int { return self.x; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "struct Pair { int a; };\n"
            "func Point::sum(this self Pair) int { return 0; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func Missing::get(this self Missing) int { return 0; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func Point::sum(this self Point) int { return self.x; }\n"
            "func main() int {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  return p.sum;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "func Point::sum(this self Point) int { return self.x + self.y; }\n"
            "func Point::add(this self Point *, dx int, dy int) void {\n"
            "  self.x = self.x + dx;\n"
            "  self.y = self.y + dy;\n"
            "}\n"
            "func make() Point {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  p.y = 2;\n"
            "  return p;\n"
            "}\n"
            "func main() int {\n"
            "  Point p;\n"
            "  Point *q;\n"
            "  p.x = 1;\n"
            "  p.y = 1;\n"
            "  q = &p;\n"
            "  q.x = q.x + 2;\n"
            "  q.add(3, 4);\n"
            "  p.add(5, 6);\n"
            "  return q.sum() + make().sum();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func Point::add(this self Point *, dx int) void { self.x = self.x + dx; }\n"
            "func make() Point {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  return p;\n"
            "}\n"
            "func main() int {\n"
            "  make().add(1);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func main() int {\n"
            "  Point p;\n"
            "  Point *q;\n"
            "  Point **pp;\n"
            "  p.x = 1;\n"
            "  q = &p;\n"
            "  pp = &q;\n"
            "  return pp.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func main() int {\n"
            "  int n;\n"
            "  int *p;\n"
            "  n = 1;\n"
            "  p = &n;\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "  func grow(n int) void;\n"
            "};\n"
            "trait Meas {\n"
            "  func area() int;\n"
            "};\n"
            "struct Rect { int w; int h; };\n"
            "struct Box { int n; };\n"
            "func Rect::area(this self Rect) int { return self.w * self.h; }\n"
            "func Rect::grow(this self Rect *, n int) void {\n"
            "  self.w = self.w + n;\n"
            "  self.h = self.h + n;\n"
            "}\n"
            "func Box::area(this self Box) int { return self.n; }\n"
            "func Box::grow(this self Box *, n int) void { self.n = self.n + n; }\n"
            "func measure(s Shape *) int { return s.area(); }\n"
            "func apply(s Shape *, n int) int {\n"
            "  s.grow(n);\n"
            "  return measure(s);\n"
            "}\n"
            "func take(m Meas) int { return m.area(); }\n"
            "func make() Rect {\n"
            "  Rect r;\n"
            "  r.w = 1;\n"
            "  r.h = 1;\n"
            "  return r;\n"
            "}\n"
            "func main() int {\n"
            "  Rect r;\n"
            "  Box b;\n"
            "  r.w = 1;\n"
            "  r.h = 1;\n"
            "  b.n = 1;\n"
            "  return apply(&r, 1) + take(r) + take(make()) + apply(&b, 1);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "struct Rect { int w; };\n"
            "func use(s Shape) int { return s.area(); }\n"
            "func main() int {\n"
            "  Rect r;\n"
            "  r.w = 1;\n"
            "  return use(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "struct Rect { int w; };\n"
            "func Rect::area(this self Rect *) void { self.w = 1; }\n"
            "func use(s Shape) int { return 0; }\n"
            "func main() int {\n"
            "  Rect r;\n"
            "  r.w = 1;\n"
            "  return use(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func grow(n int) void;\n"
            "};\n"
            "struct Rect { int w; };\n"
            "func Rect::grow(this self Rect *, n int) void { self.w = self.w + n; }\n"
            "func use(s Shape) void { s.grow(1); }\n"
            "func main() int {\n"
            "  Rect r;\n"
            "  r.w = 1;\n"
            "  use(r);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "func main() int {\n"
            "  Shape s;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "func main() int {\n"
            "  Shape *s;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "struct Rect { int w; };\n"
            "func Rect::area(this self Rect) int { return self.w; }\n"
            "func use(s Shape) int { return s.area; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "struct Rect { int w; };\n"
            "func Rect::area(this self Rect) int { return self.w; }\n"
            "func apply(s Shape *) int { return s.area(); }\n"
            "func main() int {\n"
            "  Rect r;\n"
            "  r.w = 1;\n"
            "  return apply(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "trait Shape {\n"
            "  func area() int;\n"
            "};\n"
            "struct Rect { int w; };\n"
            "func Rect::area(this self Rect) int { return self.w; }\n"
            "export func use(s Shape) int;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "struct Holder { Point value; Point *link; };\n"
            "func make(a Point) Point {\n"
            "  Point p;\n"
            "  p.x = a.x;\n"
            "  p.y = a.y;\n"
            "  return p;\n"
            "}\n"
            "Point origin;\n"
            "func main() int {\n"
            "  Point p;\n"
            "  Point *q;\n"
            "  Holder box;\n"
            "  p.x = 40;\n"
            "  p.y = 2;\n"
            "  q = &p;\n"
            "  box.value = *q;\n"
            "  box.link = q;\n"
            "  origin = make(box.value);\n"
            "  return origin.x + box.link.y + sizeof(Point) - 8;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Node { int n; Node *next; };\n"
            "func main() int {\n"
            "  Node head;\n"
            "  head.n = 42;\n"
            "  head.next = &head;\n"
            "  return head.next.n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "typedef Point Point;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "typedef struct Point Point;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose("struct Point;\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "struct outside definition");
    }

    {
        const std::string errors = diagnose(
            "typedef struct Node;\n"
            "struct Node { int n; Node *next; };\n"
            "func main() int {\n"
            "  Node tail;\n"
            "  Node head;\n"
            "  tail.n = 2;\n"
            "  tail.next = 0;\n"
            "  head.n = 40;\n"
            "  head.next = &tail;\n"
            "  return head.n + head.next.n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "typedef struct Node;\n"
            "func main() int {\n"
            "  Node n;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "typedef struct Node;\n"
            "func main() int {\n"
            "  Node *p;\n"
            "  p = 0;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"math/fwd.tlc", "typedef struct B;\n"
                           "struct A { B *b; int n; };\n"
                           "func take(b B *) int {\n"
                           "  A a;\n"
                           "  a.n = 2;\n"
                           "  a.b = b;\n"
                           "  return a.n + a.b.x;\n"
                           "}\n"},
            {"math/def.tlc", "struct B { int x; };\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Node { Node n; };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func main() int {\n"
            "  struct Point p;\n"
            "  p.x = 1;\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "struct outside definition");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func main() int { return sizeof(struct Point); }\n");
        EXPECT_HAS(check, errors, "struct outside definition");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "typedef int Point;\n"
            "func main() int {\n"
            "  Point n;\n"
            "  struct Point p;\n"
            "  n = 42;\n"
            "  p.x = n;\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "func main() int {\n"
            "  int Point;\n"
            "  Point = sizeof(Point);\n"
            "  return Point;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct Shape { int x; };\n"
            "trait Shape { func area() int; };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Point, sum } from \"geom\";\n"
                       "func main() int {\n"
                       "  Point p;\n"
                       "  p.x = 40;\n"
                       "  p.y = 2;\n"
                       "  return sum(p);\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "export struct Point { int x; int y; };\n"
                       "export func sum(p Point) int { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "struct A { B b; };\n"
            "struct B { int x; };\n"
            "func main() int {\n"
            "  A a;\n"
            "  a.b.x = 42;\n"
            "  return a.b.x;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { add } from \"math\";\n"
                       "func main() int { return add(20, 2); }\n"},
            {"math/add.tlc", "func secret() int { return extra(); }\n"
                           "export func add(a int, b int) int { return a + b + secret(); }\n"},
            {"math/more.tlc", "func extra() int { return 20; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() int { return math.secret(); }\n"},
            {"math/add.tlc", "func secret() int { return 1; }\n"
                           "export func add(a int, b int) int { return a + b; }\n"},
            {"math/more.tlc", "func extra() int { return 2; }\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() int { return 0; }\n"},
            {"math/a.tlc", "static func hidden() int { return 1; }\n"
                         "export func add() int { return hidden(); }\n"},
            {"math/b.tlc", "func use() int { return hidden(); }\n"},
        });
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() int { return 0; }\n"},
            {"math/a.tlc", "int value;\n"},
            {"math/b.tlc", "int value;\n"},
        });
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { a, b } from \"math\";\nfunc main() int { return a() + b(); }\n"},
            {"math/a.tlc", "static func hidden() int { return 1; }\n"
                         "export func a() int { return hidden(); }\n"},
            {"math/b.tlc", "static func hidden() int { return 2; }\n"
                         "export func b() int { return hidden(); }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { use } from \"math\";\nfunc main() int { return use(); }\n"},
            {"math/a.tlc", "struct A { B b; };\n"
                         "export func use() int {\n"
                         "  A a;\n"
                         "  a.b.x = 42;\n"
                         "  return a.b.x;\n"
                         "}\n"},
            {"math/b.tlc", "struct B { int x; };\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  func f(x int) int { return x; }\n"
            "  return f(42);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "int bias;\n"
            "func inc(n int) int { return n + 1; }\n"
            "func main() int {\n"
            "  bias = 1;\n"
            "  func f(n int) int { return inc(n) + bias; }\n"
            "  {\n"
            "    return f(1);\n"
            "  }\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x = 1;\n"
            "  func f(x int) int { return x; }\n"
            "  return f(2);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  func even(n int) int;\n"
            "  func odd(n int) int {\n"
            "    if (n == 0) { return 0; }\n"
            "    return even(n - 1);\n"
            "  }\n"
            "  func even(n int) int {\n"
            "    if (n == 0) { return 1; }\n"
            "    return odd(n - 1);\n"
            "  }\n"
            "  return even(2);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int n = 0;\n"
            "  for (func f() int { return 42; }; n < 1; n = n + 1) {\n"
            "    return f();\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  {\n"
            "    func f() int { return 1; }\n"
            "    return f();\n"
            "  }\n"
            "}\n"
            "func other() int { return f(); }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  {\n"
            "    func f() int { return 1; }\n"
            "  }\n"
            "  {\n"
            "    return f();\n"
            "  }\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x = f();\n"
            "  func f() int { return 1; }\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  func f() int { return 1; }\n"
            "  func f() int { return 2; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  func f() int;\n"
            "  func f(n int) int { return n; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  func f() int;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x = 40;\n"
            "  func f() int { return x; }\n"
            "  return f();\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x = 1;\n"
            "  func f() int { x = 2; return x; }\n"
            "  return f();\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  while (1) {\n"
            "    func f() int {\n"
            "      break;\n"
            "      return 1;\n"
            "    }\n"
            "    return f();\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "break outside loop");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  switch (1) {\n"
            "  case 1:\n"
            "    func f() int {\n"
            "      case 2: return 2;\n"
            "      return 1;\n"
            "    }\n"
            "    return f();\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "case outside switch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func main() int {\n"
            "  func Point::get(this self Point) int { return self.x; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func note(n int) void { }\n"
            "struct Box { int v; };\n"
            "func Box::put(this self Box, n int) void { }\n"
            "func Box::touch(this self Box *) void { }\n"
            "func main() int {\n"
            "  func inner(n int) void { }\n"
            "  Box box;\n"
            "  Box *p = &box;\n"
            "  defer note(1);\n"
            "  defer inner(2);\n"
            "  defer func(v int) { }(3);\n"
            "  defer box.put(4);\n"
            "  defer p.touch();\n"
            "  return func() int { return 0; }();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  defer 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  int x = 1;\n"
            "  defer func() int { return x; }();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func note(n int) void { }\n"
            "func main() int {\n"
            "  defer note();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "extern func printf(fmt const char *, ...) int;\n"
            "func main() int {\n"
            "  defer printf(\"hi\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "trait Shape { func area() int; };\n"
            "struct Rect { int w; };\n"
            "func Rect::area(this self Rect) int { return self.w; }\n"
            "func use(s Shape) int {\n"
            "  defer s.area();\n"
            "  return 0;\n"
            "}\n"
            "func main() int {\n"
            "  Rect r;\n"
            "  r.w = 1;\n"
            "  return use(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "struct Box { int v; };\n"
            "func Box::split(this self Box) (int, int) { return self.v, self.v + 1; }\n"
            "func add(x int, y int) (int, int) { return x + y, x; }\n"
            "func main() int {\n"
            "  func pair(n int) (int, int) { return n + 2, n; }\n"
            "  sum, orig := add(20, 22);\n"
            "  var extra, base = add(1, 1);\n"
            "  extra, base = add(0, 0);\n"
            "  Box box;\n"
            "  box.v = 3;\n"
            "  var left, right = box.split();\n"
            "  var z, w = func() (int, int) { return 0, 0; }();\n"
            "  p, q := pair(0);\n"
            "  add(1, 2);\n"
            "  defer add(0, 1);\n"
            "  int n = 1;\n"
            "  {\n"
            "    n, m := add(4, 5);\n"
            "    return n + m;\n"
            "  }\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x + y, x; }\n"
            "func main() int {\n"
            "  int x = add(1, 2);\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  a, b := add(1, 2);\n"
            "  a, b := add(3, 4);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  var a, b = add(1, 2);\n"
            "  var a, c = add(3, 4);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  a, b = add(1, 2);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose("extern func pair() (int, int);\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "extern function cannot return several results");
    }

    {
        const std::string errors = diagnose(
            "struct Pair { int a; int b; };\n"
            "extern func sum(p Pair) int;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "struct must be extern");
    }

    {
        const std::string errors = diagnose(
            "extern struct Pair { int a; int b; };\n"
            "extern func sum(p Pair) int;\n"
            "extern func make() Pair;\n"
            "func main() int { return 0; }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "struct Inner { int n; };\n"
            "extern struct Outer { Inner item; };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "struct must be extern");
    }

    {
        const std::string errors = diagnose(
            "typedef struct Pair;\n"
            "extern struct Pair { int a; int b; };\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "extern struct");
    }

    {
        const std::string errors = diagnose("trait Pair { func both() (int, int); };\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func bad() (int *, int) { return 1, 2; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "return type: expected int*, found int");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; int y; };\n"
            "func id(n int) (int) { return n; }\n"
            "func trail(n int) (int,) { return n; }\n"
            "func tri(n int) (int, int *, Point) {\n"
            "  Point p;\n"
            "  p.x = n;\n"
            "  p.y = n;\n"
            "  return n, nullptr, p;\n"
            "}\n"
            "func narrow() (short, char, bool) { return -2, 200, true; }\n"
            "func add(x int, y int) (int, int);\n"
            "func add(x int, y int) (int, int) { return x + y, x; }\n"
            "func take(n int) int {\n"
            "  n, extra := add(n, 1);\n"
            "  return n + extra;\n"
            "}\n"
            "func keep(n int) int {\n"
            "  {\n"
            "    n, extra := add(1, 2);\n"
            "  }\n"
            "  return n;\n"
            "}\n"
            "static func hidden() (int, int) { return 1, 2; }\n"
            "func main() int {\n"
            "  var x = id(1);\n"
            "  y := trail(x);\n"
            "  int kept = 1;\n"
            "  kept, fresh := add(kept, 2);\n"
            "  {\n"
            "    var kept, other = add(4, 5);\n"
            "    y = kept + other;\n"
            "  }\n"
            "  int *p;\n"
            "  int n;\n"
            "  Point point;\n"
            "  n, p, point = tri(1);\n"
            "  short s;\n"
            "  char c;\n"
            "  bool flag;\n"
            "  s, c, flag = narrow();\n"
            "  int outer = 1;\n"
            "  func inner() int {\n"
            "    outer, m := add(1, 2);\n"
            "    return outer + m;\n"
            "  }\n"
            "  int h1;\n"
            "  int h2;\n"
            "  h1, h2 = hidden();\n"
            "  add(1, 2);\n"
            "  for (i, k := add(1, 2); i < 2; i = i + 1) {\n"
            "    y = k;\n"
            "  }\n"
            "  return x + y + fresh + take(1) + keep(1) + n + s + c + flag + inner() + outer + h1 + h2;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  a := add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch: expected 1 result, found 2 results");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  var a = add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  a, b, c := add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  a, a := add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  var a, a = add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  x := 1;\n"
            "  x := 2;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() int { var a := 1; return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  int *p;\n"
            "  int n;\n"
            "  p = nullptr;\n"
            "  p, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch: expected int*, found int");
    }

    {
        const std::string errors = diagnose(
            "enum Color { Red = 1 };\n"
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  int n = 0;\n"
            "  Red, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  int xs[2];\n"
            "  int n = 0;\n"
            "  xs, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  int n = 0;\n"
            "  add, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  int outer = 1;\n"
            "  func inner() int {\n"
            "    int n = 0;\n"
            "    outer, n = add(1, 2);\n"
            "    return n;\n"
            "  }\n"
            "  return inner() + outer;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func bad() (void, int) { return 1; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func bad() (int, void) { return 1; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func bad() (int, int) { return; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func bad() (int, int) { return 1, 2, 3; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func bad() int { return 1, 2; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func bad() (int, int) { return add(1, 2); }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  int n = sizeof(add(1, 2));\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "trait Num { func get() int; };\n"
            "func pair(n Num) (int, int) { return n.get(), 1; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) int;\n"
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() int {\n"
            "  func pair(n int) (int, int);\n"
            "  func pair(n int) (int, int *) { return n, nullptr; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func take(n int) int { return n; }\n"
            "func main() int {\n"
            "  if (add(1, 2)) {\n"
            "    return 1;\n"
            "  }\n"
            "  return take(add(1, 2));\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x int, y int) (int, int) { return x, y; }\n"
            "func main() int {\n"
            "  a, b := 1;\n"
            "  c, d := add;\n"
            "  return a + c;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "struct Point { int x; };\n"
            "func make() (Point, int) {\n"
            "  Point p;\n"
            "  p.x = 1;\n"
            "  return p, 2;\n"
            "}\n"
            "func main() int {\n"
            "  int a;\n"
            "  int b;\n"
            "  a, b = make();\n"
            "  return b;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\n"
                       "func main() int {\n"
                       "  a, b := math.pair(20);\n"
                       "  return a + b;\n"
                       "}\n"},
            {"math/pair.tlc", "package math;\n"
                            "export func pair(n int) (int, int) { return n + 1, n; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { pair } from \"math\";\n"
                       "func main() int {\n"
                       "  a, b := pair(1);\n"
                       "  return a;\n"
                       "}\n"},
            {"math/pair.tlc", "package math;\n"
                            "func pair(n int) (int, int) { return n, n; }\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { twice } from \"math\";\n"
                       "func main() int { return twice(1); }\n"},
            {"math/pair.tlc", "package math;\n"
                            "func pair(n int) (int, int) { return n + 1, n; }\n"},
            {"math/use.tlc", "package math;\n"
                           "export func twice(n int) int {\n"
                           "  a, b := pair(n);\n"
                           "  return a + b;\n"
                           "}\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("export extern int value;\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, errors, "export extern");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { add } from \"math\";\nfunc main() int { return add(20, 22); }\n"},
            {"math/add.tlc", "package math;\nexport extern func add(a int, b int) int;\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func inc(n int) int { return n + 1; }\n"
            "func apply(fp int (*)(int), n int) int { return fp(n); }\n"
            "func main() int {\n"
            "  int (*fp)(int);\n"
            "  fp = inc;\n"
            "  f64 x = 20.5;\n"
            "  f32 y = 1.5f;\n"
            "  double z = x + y;\n"
            "  if (z != 22.0) {\n"
            "    return 0;\n"
            "  }\n"
            "  return apply(fp, 41);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(a i32, b i32) i32 { return a + b; }\n"
            "func myfunc(ptr func (i32, i32) i32) i32 { return ptr(10, 20); }\n"
            "func id(n i32) i32 { return n; }\n"
            "func take(p func (i32) (i32)) i32 { return p(1); }\n"
            "func pick() func (i32, i32) i32 { return add; }\n"
            "func main() i32 {\n"
            "  func (i32, i32) i32 fp = pick();\n"
            "  return myfunc(add) + take(id) + fp(1, 1);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func f() (func (i32) i32, i32) { return 0, 0; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func pair(n i32) (i32, i32) { return n, n + 2; }\n"
            "typedef func (i32) (i32, i32) PairFn;\n"
            "func apply(p PairFn) i32 {\n"
            "  a, b := p(20);\n"
            "  return a + b;\n"
            "}\n"
            "func pick() func (i32) (i32, i32) { return pair; }\n"
            "func main() i32 {\n"
            "  func (i32) (i32, i32) fp = pick();\n"
            "  x, y := fp(10);\n"
            "  return apply(fp) + x + y;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern func take(p func (i32) (i32, i32)) i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "extern function cannot use several results");
    }

    {
        const std::string errors = diagnose(
            "extern func get() func (i32) (i32, i32);\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "extern function cannot use several results");
    }

    {
        const std::string errors = diagnose(
            "func take(p long long *) int { return *p != 0; }\n"
            "func main() int {\n"
            "  i64 n = 1;\n"
            "  u8 b = 1;\n"
            "  unsigned char *p = &b;\n"
            "  return take(&n) + *p;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func take(p long *) int { return 0; }\n"
            "func main() int {\n"
            "  i64 n = 1;\n"
            "  return take(&n);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    return check.failed;
}
