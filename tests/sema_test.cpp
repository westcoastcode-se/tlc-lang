#include "driver.hpp"
#include "harness.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::string check_program(const std::string &source) {
    static int seq = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("tlc-check-" + std::to_string(++seq) + ".tlc");
    {
        std::ofstream out(path);
        out << source;
    }
    tlc::Options opt;
    opt.mode = tlc::Mode::Check;
    opt.input = path.string();
    const tlc::CompileOutput output = tlc::compile_source(opt);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return output.compiler_stderr;
}

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
            "var acc i32;\n"
            "func add(a i32, b i32) i32;\n"
            "func main() i32 {\n"
            "  var n i32 = add(1, 2);\n"
            "  while (n) {\n"
            "    acc = acc + n;\n"
            "    n = n - 1;\n"
            "  }\n"
            "  return acc;\n"
            "}\n"
            "func add(a i32, b i32) i32 { return a + b; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 { return later(); }\n"
            "func later() i32 { return 1; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("func main() i32 { return missing; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  return x;\n"
            "  var x i32;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32;\n"
            "  var x i32;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func f(a i32) i32 { return a; }\n"
            "func main() i32 { return f(1, 2); }\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "func f() void { return 1; }\n"
            "func main() i32 { return; }\n");
        EXPECT_HAS(check, errors, "return type");
        EXPECT(check, errors.find("return type") != errors.rfind("return type"));
    }

    {
        const std::string errors = diagnose("func main() i32 { break; return 0; }\n");
        EXPECT_HAS(check, errors, "break outside loop");
    }

    {
        const std::string errors = diagnose("func main() i32 { continue; return 0; }\n");
        EXPECT_HAS(check, errors, "continue outside loop");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  for (var i i32 = 0; i < 1; i = i + 1) {}\n"
            "  return i;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main(n i32) i32 {\n"
            "  var n i32;\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose("export extern(abi=\"c\") func printf() i32;\nfunc main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\";\n"
             "func main() i32 {\n"
             "  math.base = 40;\n"
             "  return math.add(math.base, 2);\n"
             "}\n"},
            {"math.tlc",
             "export var base i32;\n"
             "export func add(a i32, b i32) i32 { return a + b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import { add } from \"math\";\n"
             "func main() i32 { return add(40, 2); }\n"},
            {"math.tlc", "export func add(a i32, b i32) i32 { return a + b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\" as m;\n"
             "func main() i32 { return m.add(40, 2); }\n"},
            {"math.tlc", "export func add(a i32, b i32) i32 { return a + b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\" as m;\n"
             "func main() i32 { return math.add(1, 2); }\n"},
            {"math.tlc", "export func add(a i32, b i32) i32 { return a + b; }\n"},
        });
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\" as a;\n"
             "import \"also\" as b;\n"
             "func main() i32 { return a.add(20, 20) + b.sub(3, 1); }\n"},
            {"math.tlc",
             "package math;\n"
             "export func add(a i32, b i32) i32 { return a + b; }\n"},
            {"also.tlc",
             "package math;\n"
             "export func sub(a i32, b i32) i32 { return a - b; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() i32 { return math.secret(); }\n"},
            {"math.tlc",
             "func secret() i32 { return 1; }\n"
             "export func add(a i32, b i32) i32 { return a + b + secret(); }\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
        EXPECT(check, errors.find("undeclared identifier") == std::string::npos);
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() i32 { return secret(); }\n"},
            {"math.tlc", "func secret() i32 { return 1; }\n"},
        });
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import \"math\";\n"
             "import \"also\";\n"
             "func main() i32 { return 0; }\n"},
            {"math.tlc", "package math;\nexport func add(a i32, b i32) i32 { return a + b; }\n"},
            {"also.tlc", "package math;\nexport func sub(a i32, b i32) i32 { return a - b; }\n"},
        });
        EXPECT_HAS(check, errors, "duplicate package name");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { missing, secret } from \"math\";\nfunc main() i32 { return 0; }\n"},
            {"math.tlc", "func secret() i32 { return 1; }\nexport func add(a i32, b i32) i32 { return a + b; }\n"},
        });
        EXPECT_HAS(check, errors, "missing is not exported");
        EXPECT_HAS(check, errors, "secret is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc",
             "import { add } from \"math\";\n"
             "var add i32;\n"
             "func main() i32 { return add; }\n"},
            {"math.tlc", "export func add(a i32, b i32) i32 { return a + b; }\n"},
        });
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 7;\n"
            "  var p *i32 = &n;\n"
            "  var pp **i32 = &p;\n"
            "  pp = &p;\n"
            "  **pp = 42;\n"
            "  return n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32 = null;\n"
            "  p = 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  return *n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { return &1; }\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32 = null;\n"
            "  return p + 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("var p ?*void;\n"
"func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add4(p *i32) i32 {\n"
            "  return p[0] + p[1] + p[2] + p[3];\n"
            "}\n"
            "func main() i32 {\n"
            "  var a [4]i32;\n"
            "  a[0] = 10;\n"
            "  a[1] = 20;\n"
            "  a[2] = 7;\n"
            "  a[3] = 5;\n"
            "  var q *i32 = a;\n"
            "  var s *u8 = \"hi\";\n"
            "  var n i32 = 0;\n"
            "  while (s[n] != 0) {\n"
            "    n = n + 1;\n"
            "  }\n"
            "  return add4(a) + q[0] - a[0] + n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var a [1]i32;\n"
            "  var p *i32 = null;\n"
            "  return a[p];\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32 = \"hi\";\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func make(x i32, y i32) Point {\n"
            "  var p Point;\n"
            "  p.x = x;\n"
            "  p.y = y;\n"
            "  return p;\n"
            "}\n"
            "func sum(p Point) i32 {\n"
            "  return p.x + p.y;\n"
            "}\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p = make(40, 2);\n"
            "  var q *Point = &p;\n"
            "  (*q).y = 2;\n"
            "  var r Point;\n"
            "  r = *q;\n"
            "  return sum(r) + make(0, 0).x;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  return p.z;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func make() Point {\n"
            "  var p Point;\n"
            "  return p;\n"
            "}\n"
            "func main() i32 {\n"
            "  make().x = 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "type Empty struct {}\n"
            "func Empty::id(self this) i32 { return 7; }\n"
            "func main() i32 { var e Empty; return e.id() + sizeof(Empty); }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; x i32; };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"geom\";\n"
"func main() i32 {\n"
"  var p geom.Point;\n"
"  return 0;\n"
"}\n"},
            {"geom.tlc", "package geom;\n"
                       "type Point struct { x i32; y i32; };\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"geom\";\n"
"func main() i32 {\n"
"  var p geom.Point;\n"
"  p.x = 40;\n"
"  p.y = 2;\n"
"  return geom.sum(p);\n"
"}\n"},
            {"geom.tlc", "package geom;\n"
                       "export type Point struct { x i32; y i32; };\n"
                       "export func sum(p Point) i32 { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") func printf(fmt *const u8, ...) i32;\n"
            "extern(abi=\"c\") var slot i32;\n"
            "func main() i32 {\n"
            "  slot = 1;\n"
            "  printf(\"hi\\n\");\n"
            "  printf(\"n %d\\n\", slot);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") func printf(fmt *const u8, ...) i32;\n"
            "func main() i32 {\n"
            "  printf();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") var slot i32;\n"
            "func main() i32 { return slot; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "type Ptr *Point;\n"
            "type Integer i32;\n"
            "type Pair [2]i32;\n"
            "func make(x i32, y i32) Point {\n"
            "  var p Point;\n"
            "  p.x = x;\n"
            "  p.y = y;\n"
            "  return p;\n"
            "}\n"
            "func sum(p Point) i32 { return p.x + p.y; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  var bias Integer;\n"
            "  var extra Pair;\n"
            "  p = make(40, 2);\n"
            "  var q Ptr = Ptr(&p);\n"
            "  bias = Integer(0);\n"
            "  extra[0] = 0;\n"
            "  return sum(*q) + i32(bias) + extra[0];\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Integer i32;\n"
            "type Count i32;\n"
            "func main() i32 {\n"
            "  var value Integer = 10;\n"
            "  var raw i32 = value;\n"
            "  var count Count = value;\n"
            "  return value + 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "expected Integer, found i32");
        EXPECT_HAS(check, errors, "expected i32, found Integer");
        EXPECT_HAS(check, errors, "expected Count, found Integer");
    }

    {
        const std::string errors = diagnose(
            "type Integer i32;\n"
            "type Count i32;\n"
            "func add(a Integer, b Integer) Integer { return a + b; }\n"
            "func main() i32 {\n"
            "  var value Integer = Integer(40);\n"
            "  var count Count = Count(i32(value));\n"
            "  return i32(add(value, Integer(2))) + i32(count);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, Green = 40, Blue };\n"
            "var value Color = Color.Blue;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, Green = 40, Blue };\n"
            "func main() i32 {\n"
            "  Color.Blue = 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, Green = 40, Blue };\n"
            "func main() i32 {\n"
            "  var p *i32;\n"
            "  p = &Color.Blue;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, Green, Red };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "type Red enum { Red };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point i32;\n"
            "var Point i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose("type Color enum {};\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { A = 2147483647, B };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red = 1 };\n"
            "func main() i32 {\n"
            "  var n i32 = Color.Red;\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Tiny enum(i8) { A = 200 };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; }\n"
            "type Color enum { Red }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
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
            "extern(abi=\"c\") type Flag enum(i8) { On = 1 }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "extern enum");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red = 1 }\n"
            "func take(x i32) i32 { return x; }\n"
            "func main() i32 { return take(Color.Red); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") type Flag enum { On = 1 }\n"
            "func take(x u32) i32 { return 0; }\n"
            "func Flag::on(self this) i32 { return 1; }\n"
            "func main() i32 {\n"
            "  n := take(Flag.On);\n"
            "  return n + Flag.On.on();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type One enum { Val };\n"
            "type Two enum { Val };\n"
            "func main() i32 {\n"
            "  value := Two.Val;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Point } from \"geom\";\n"
                       "func main() i32 { return 0; }\n"},
            {"geom.tlc", "package geom;\n"
                       "type Point i32;\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"geom\";\n"
"func main() i32 {\n"
"  var p geom.Alias;\n"
"  p.x = 40;\n"
"  p.y = 2;\n"
"  return geom.sum(p);\n"
"}\n"},
            {"geom.tlc", "package geom;\n"
                       "type Point struct { x i32; y i32; };\n"
                       "export type Alias Point;\n"
                       "export func sum(p Alias) i32 { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Alias, sum } from \"geom\";\n"
"func main() i32 {\n"
"  var p Alias;\n"
"  p.x = 40;\n"
"  p.y = 2;\n"
"  return sum(p);\n"
"}\n"},
            {"geom.tlc", "package geom;\n"
                       "type Point struct { x i32; y i32; };\n"
                       "export type Alias Point;\n"
                       "export func sum(p Alias) i32 { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Color } from \"geom\";\n"
                       "func main() i32 {\n"
                       "  value := Color.Blue;\n"
                       "  return 0;\n"
                       "}\n"},
            {"geom.tlc", "package geom;\n"
                       "export type Color enum { Red, Green = 40, Blue };\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32;\n"
            "  var x i32;\n"
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
            "func main() i32 {\n"
            "  case 1: return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "case outside switch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  default: return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "default outside switch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32;\n"
            "  p = null;\n"
            "  switch (p) {\n"
            "  case 0: return 0;\n"
            "  }\n"
            "  return 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32;\n"
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
            "func main() i32 {\n"
            "  var n i32;\n"
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
            "func main() i32 {\n"
            "  var n i32;\n"
            "  var p *i32;\n"
            "  n = 0;\n"
            "  p = null;\n"
            "  do {\n"
            "    n = 1;\n"
            "  } while (p);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "compare with null");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  switch (1) {\n"
            "  case 1: break;\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32;\n"
            "  var c u8;\n"
            "  n = sizeof(i32);\n"
            "  n = sizeof n;\n"
            "  n = sizeof(n);\n"
            "  n = sizeof(*void);\n"
            "  c = 0;\n"
            "  n = sizeof c;\n"
            "  return n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("func main() i32 { return sizeof(void); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func f() i32 { return 1; }\n"
            "func main() i32 { return sizeof(f); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 0;\n"
            "  var c *u8 = *u8(0);\n"
            "  var v *void = *void(0);\n"
            "  var p *i32 = &n;\n"
            "  n = 42;\n"
            "  c = *u8(n);\n"
            "  v = *void(c);\n"
            "  n = i32(v);\n"
            "  p = &n;\n"
            "  c = *u8(p);\n"
            "  v = *void(c);\n"
            "  p = *i32(v);\n"
            "  return *p;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32;\n"
            "  var v *void;\n"
            "  v = null;\n"
            "  p = v;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var v ?*void = null;\n"
            "  var p *i32 = *i32(v);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var v *void;\n"
            "  v = null;\n"
            "  return *v;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { void(1); return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func id(p ?*void) ?*void { return p; }\n"
            "func main() i32 {\n"
            "  var v ?*void = null;\n"
            "  v = id(v);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var a bool;\n"
            "  var b bool;\n"
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
            "func main() i32 {\n"
            "  var p ?*i32;\n"
            "  var x i32;\n"
            "  x = 0;\n"
            "  p = null;\n"
            "  if (p == null) {\n"
            "    return 1;\n"
            "  }\n"
            "  p = &x;\n"
            "  return p != null;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32 = null;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func id(p *i32) *i32 { return p; }\n"
            "func main() i32 {\n"
            "  var p *i32 = 0;\n"
            "  p = 0;\n"
            "  var inferred = null;\n"
            "  if (p) { return 1; }\n"
            "  return id(0) == null;\n"
            "}\n"
            "func zero() *i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
        EXPECT_HAS(check, errors, "compare with null");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32 = *i32(0);\n"
            "  if (p == null) { return 1; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var c u8;\n"
            "  var s i16;\n"
            "  var n i32;\n"
            "  var u u32;\n"
            "  var a i64;\n"
            "  c = 'A';\n"
            "  s = 1;\n"
            "  n = c + s;\n"
            "  u = 0u32;\n"
            "  u = u - 1u32;\n"
            "  a = 100000;\n"
            "  a = a * 100000;\n"
            "  return n + i32(u) + i32(a);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Word u32;\n"
            "func main() i32 {\n"
            "  var n Word = Word(1u32);\n"
            "  return i32(n);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Integer i32;\n"
            "type Point struct { x i32; y i32; };\n"
            "func add(a i32, b i32) i32 { return a + b; }\n"
            "type BinOp func (i32, i32) i32;\n"
            "const value Integer = Integer(8);\n"
            "const other = Integer(4);\n"
            "const origin Point = {.x = 20, .y = 8};\n"
            "const op BinOp = BinOp(add);\n"
            "func main() i32 {\n"
            "  var back Integer = other;\n"
            "  return i32(value) + i32(back) + origin.x + origin.y + op(1, 1);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Integer i32;\n"
            "const value Integer = 10;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "expected Integer, found i32");
    }

    {
        const std::string errors = diagnose(
            "type Integer i32;\n"
            "const value = Integer(10);\n"
            "func main() i32 {\n"
            "  var raw i32 = value;\n"
            "  return raw;\n"
            "}\n");
        EXPECT_HAS(check, errors, "expected i32, found Integer");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "const origin Point = {.x = 1, .y = 2};\n"
            "func main() i32 {\n"
            "  origin.x = 3;\n"
            "  return origin.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { var n i8 = 200i8; return n; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { return 3000000000; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { return 5000000000u32; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var p *i32;\n"
            "  var n i32;\n"
            "  n = 1;\n"
            "  p = *i64(n);\n"
            "  return i64(p);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  switch (1) {\n"
            "  case 1u32: return 0;\n"
            "  }\n"
            "  return 1;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "type Pair struct { a i32; b i32; };\n"
            "func sum(n i32) i32 { return n; }\n"
            "func Point::make(x i32, y i32) Point {\n"
            "  var p Point;\n"
            "  p.x = x;\n"
            "  p.y = y;\n"
            "  return p;\n"
            "}\n"
            "func Point::sum(self this, extra i32) i32 { return self.x + self.y + extra; }\n"
            "func Pair::sum(self this, extra i32) i32 { return self.a + self.b + extra; }\n"
            "func Point::value(self this) i32;\n"
            "func Point::value(self this) i32 { return self.x; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  var q Pair;\n"
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
            "type Point struct { x i32; y i32; };\n"
            "func Point::make(x i32, y i32) Point {\n"
            "  var p Point;\n"
            "  p.x = x;\n"
            "  p.y = y;\n"
            "  return p;\n"
            "}\n"
            "func main() i32 {\n"
            "  var p = Point.make(40, 2);\n"
            "  return p.x + p.y;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this) i32 { return self.x; }\n"
            "func main() i32 { return Point.sum(); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::make(x i32) Point { var p Point; p.x = x; return p; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p = p.make(1);\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 { return Point.missing(); }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, };\n"
            "func Color::code() i32 { return 7; }\n"
            "func main() i32 {\n"
            "  var c Color = Color.Red;\n"
            "  if (c == Color.Red) { return Color.code(); }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(a i32, b i32) i32 { return a + b; }\n"
            "func take(op func (i32, i32) i32) i32 { return op(40, 2); }\n"
            "type Point struct { x i32; y i32; };\n"
            "func Point::make(x i32, y i32) i32 { return x + y; }\n"
            "func Point::sum(self this) i32 { return self.x + self.y; }\n"
            "func main() i32 {\n"
            "  var method = Point.make;\n"
            "  var free = add;\n"
            "  return take(Point.make) + method(0, 0) + free(0, 0);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(a i32, b i32) i32 { return a + b; }\n"
            "var ptr = add;\n"
            "type Point struct { x i32; };\n"
            "func Point::make(x i32, y i32) i32 { return x + y; }\n"
            "var method = Point.make;\n"
            "func main() i32 { return ptr(40, 2) + method(0, 0); }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this) i32 { return self.x; }\n"
            "func main() i32 {\n"
            "  var f = Point.sum;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func take(op func (i32) i32) i32 { return op(1); }\n"
            "type Point struct { x i32; };\n"
            "func Point::sum(self this) i32 { return self.x; }\n"
            "func main() i32 { return take(Point.sum); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func take(op func (i32) i32) i32 { return op(1); }\n"
            "type Point struct { x i32; };\n"
            "func Point::make(x i32, y i32) i32 { return x + y; }\n"
            "func main() i32 { return take(Point.make); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  var f = Point.missing;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::make(x i32) i32 { return x; }\n"
            "func main() i32 {\n"
            "  var Point i32 = 1;\n"
            "  var f = Point.make;\n"
            "  return Point;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, };\n"
            "func Color::code() i32 { return 1; }\n"
            "func main() i32 {\n"
            "  var f = Color.code;\n"
            "  var c = Color.Red;\n"
            "  if (c == Color.Red) { return f(); }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red, };\n"
            "func Color::code() i32 { return 7; }\n"
            "func main() i32 {\n"
            "  var c = Color.Red;\n"
            "  return c.code();\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose_files({
            {"math/point.tlc", "package math;\n"
                               "type Point struct { x i32; y i32; };\n"
                               "func Point::make(x i32, y i32) Point {\n"
                               "  var p Point;\n"
                               "  p.x = x;\n"
                               "  p.y = y;\n"
                               "  return p;\n"
                               "}\n"},
            {"math/use.tlc", "package math;\n"
                             "export func total() i32 {\n"
                             "  var p = Point.make(40, 2);\n"
                             "  return p.x + p.y;\n"
                             "}\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this, extra i32) i32 { return self.x + extra; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  return p.nope(1);\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this, extra i32) i32 { return self.x + extra; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  return p.sum();\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this, extra i32) i32 { return self.x + extra; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  return p.sum(p);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this) i32 { return self.x; }\n"
            "func Point::sum(self this) i32 { return self.x; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "type Pair struct { a i32; };\n"
            "func Point::sum(self Pair) i32 { return self.a; }\n"
            "func main() i32 {\n"
            "  var pair Pair;\n"
            "  pair.a = 4;\n"
            "  return Point.sum(pair);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func Missing::get(self this) i32 { return 0; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::sum(self this) i32 { return self.x; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  return p.sum;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func Point::sum(self this) i32 { return self.x + self.y; }\n"
            "func Point::add(self *this, dx i32, dy i32) void {\n"
            "  self.x = self.x + dx;\n"
            "  self.y = self.y + dy;\n"
            "}\n"
            "func make() Point {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  p.y = 2;\n"
            "  return p;\n"
            "}\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  p.y = 1;\n"
            "  var q *Point = &p;\n"
            "  q.x = q.x + 2;\n"
            "  q.add(3, 4);\n"
            "  p.add(5, 6);\n"
            "  return q.sum() + make().sum();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::add(self *this, dx i32) void { self.x = self.x + dx; }\n"
            "func make() Point {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  return p;\n"
            "}\n"
            "func main() i32 {\n"
            "  make().add(1);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  var q *Point;\n"
            "  var pp **Point;\n"
            "  p.x = 1;\n"
            "  q = &p;\n"
            "  pp = &q;\n"
            "  return pp.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  var n i32;\n"
            "  var p *i32;\n"
            "  n = 1;\n"
            "  p = &n;\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "  func grow(n i32) void;\n"
            "};\n"
            "type Meas trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Rect struct { w i32; h i32; };\n"
            "type Box struct { n i32; };\n"
            "func Rect::area(self this) i32 { return self.w * self.h; }\n"
            "func Rect::grow(self *this, n i32) void {\n"
            "  self.w = self.w + n;\n"
            "  self.h = self.h + n;\n"
            "}\n"
            "func Box::area(self this) i32 { return self.n; }\n"
            "func Box::grow(self *this, n i32) void { self.n = self.n + n; }\n"
            "func measure(s *Shape) i32 { return s.area(); }\n"
            "func apply(s *Shape, n i32) i32 {\n"
            "  s.grow(n);\n"
            "  return measure(s);\n"
            "}\n"
            "func take(m Meas) i32 { return m.area(); }\n"
            "func make() Rect {\n"
            "  var r Rect;\n"
            "  r.w = 1;\n"
            "  r.h = 1;\n"
            "  return r;\n"
            "}\n"
            "func main() i32 {\n"
            "  var r Rect;\n"
            "  var b Box;\n"
            "  r.w = 1;\n"
            "  r.h = 1;\n"
            "  b.n = 1;\n"
            "  return apply(&r, 1) + take(r) + take(make()) + apply(&b, 1);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Rect struct { w i32; };\n"
            "func use(s Shape) i32 { return s.area(); }\n"
            "func main() i32 {\n"
            "  var r Rect;\n"
            "  r.w = 1;\n"
            "  return use(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Rect struct { w i32; };\n"
            "func Rect::area(self *this) void { self.w = 1; }\n"
            "func use(s Shape) i32 { return 0; }\n"
            "func main() i32 {\n"
            "  var r Rect;\n"
            "  r.w = 1;\n"
            "  return use(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func grow(n i32) void;\n"
            "};\n"
            "type Rect struct { w i32; };\n"
            "func Rect::grow(self *this, n i32) void { self.w = self.w + n; }\n"
            "func use(s Shape) void { s.grow(1); }\n"
            "func main() i32 {\n"
            "  var r Rect;\n"
            "  r.w = 1;\n"
            "  use(r);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "func main() i32 {\n"
            "  var s Shape;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "func main() i32 {\n"
            "  var s *Shape;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Rect struct { w i32; };\n"
            "func Rect::area(self this) i32 { return self.w; }\n"
            "func use(s Shape) i32 { return s.area; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Rect struct { w i32; };\n"
            "func Rect::area(self this) i32 { return self.w; }\n"
            "func apply(s *Shape) i32 { return s.area(); }\n"
            "func main() i32 {\n"
            "  var r Rect;\n"
            "  r.w = 1;\n"
            "  return apply(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Rect struct { w i32; };\n"
            "func Rect::area(self this) i32 { return self.w; }\n"
            "export func use(s Shape) i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Adder trait {\n"
            "  func apply(n i32) i32;\n"
            "};\n"
            "type Meas trait {\n"
            "  func area() i32;\n"
            "};\n"
            "type Shape trait {\n"
            "  func area() i32;\n"
            "  func grow(n i32) void;\n"
            "};\n"
            "type Counter struct { n i32; };\n"
            "type Rect struct { w i32; h i32; };\n"
            "func Rect::area(self this) i32 { return self.w * self.h; }\n"
            "func Rect::grow(self *this, n i32) void {\n"
            "  self.w = self.w + n;\n"
            "  self.h = self.h + n;\n"
            "}\n"
            "func twice(a Adder, n i32) i32 { return a.apply(n) + a.apply(n); }\n"
            "func through(a *Adder, n i32) i32 { return a.apply(n); }\n"
            "func Counter::forward(a *Adder, n i32) i32 { return through(a, n); }\n"
            "func Counter::add(self *this, a Adder) i32 { return self.n + a.apply(1); }\n"
            "func Counter::measure(self this, s *Shape) i32 { return s.area(); }\n"
            "func Counter::read(self this, m Meas) i32 { return m.area(); }\n"
            "func main() i32 {\n"
            "  var c Counter;\n"
            "  var r Rect;\n"
            "  c.n = 1;\n"
            "  r.w = 2;\n"
            "  r.h = 3;\n"
            "  return twice((n) { return n + 1; }, 4) + c.add(func (n i32) i32 { return n; }) +\n"
            "      Counter.forward((n) { return n; }, 1) + c.measure(&r) + c.read(r);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "  func grow(n i32) void;\n"
            "};\n"
            "func use(s Shape) i32 { return s.area(); }\n"
            "func main() i32 { return use((n) { return n; }); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Empty trait {};\n"
            "func use(e Empty) i32 { return 1; }\n"
            "func main() i32 { return use(() { return 1; }); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Adder trait {\n"
            "  func apply(n i32) i32;\n"
            "};\n"
            "func take(a Adder) i32 { return a.apply(1); }\n"
            "func main() i32 { return take((a, b) { return a; }); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Adder trait {\n"
            "  func apply(n i32) i32;\n"
            "};\n"
            "func take(a Adder) i32 { return a.apply(1); }\n"
            "func main() i32 { return take(func (n i32) void {}); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Adder trait {\n"
            "  func apply(n i32) i32;\n"
            "};\n"
            "func inc(n i32) i32 { return n + 1; }\n"
            "func take(a Adder) i32 { return a.apply(1); }\n"
            "func main() i32 { return take(inc); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Adder trait {\n"
            "  func apply(n i32) i32;\n"
            "};\n"
            "func main() i32 {\n"
            "  var a Adder = (n) { return n; };\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Adder trait {\n"
            "  func apply(n i32) i32;\n"
            "};\n"
            "func take(a Adder) i32 {\n"
            "  var x = 1;\n"
            "  return take((n) { return n + x; });\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "type Holder struct { value Point; link *Point; };\n"
            "func make(a Point) Point {\n"
            "  var p Point;\n"
            "  p.x = a.x;\n"
            "  p.y = a.y;\n"
            "  return p;\n"
            "}\n"
            "var origin Point;\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 40;\n"
            "  p.y = 2;\n"
            "  var q *Point = &p;\n"
            "  var box Holder = {.value = *q, .link = q};\n"
            "  origin = make(box.value);\n"
            "  return origin.x + box.link.y + sizeof(Point) - 8;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Node struct { n i32; next *Node; };\n"
            "func main() i32 {\n"
            "  var head Node = {.n = 42, .next = &head};\n"
            "  return head.next.n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "type Point Point;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "type Point struct Point;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "expected '{'");
    }

    {
        const std::string errors = diagnose("type Point struct;\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "expected '{'");
    }

    {
        const std::string errors = diagnose(
            "type Node struct { n i32; next ?*Node; };\n"
            "func main() i32 {\n"
            "  var tail Node;\n"
            "  var head Node;\n"
            "  tail.n = 2;\n"
            "  tail.next = null;\n"
            "  head.n = 40;\n"
            "  head.next = &tail;\n"
            "  var next ?*Node = head.next;\n"
            "  if (next == null) { return 0; }\n"
            "  return head.n + next.n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Inner struct { inner_value i32; };\n"
            "type Box struct { value i32; complex Inner; };\n"
            "var bias Box = {.value = 2};\n"
            "func twenty() i32 { return 20; }\n"
            "func take(box Box) i32 { return box.value + box.complex.inner_value; }\n"
            "func made() Box { return {.value = 10, .complex = {.inner_value = 20}}; }\n"
            "func main() i32 {\n"
            "  var box Box = {.value = twenty(), .complex = {.inner_value=20}};\n"
            "  box = {.complex = {.inner_value = 42}};\n"
            "  var omitted Box = {.value = 1,};\n"
            "  var empty Box = {};\n"
            "  var flipped Box = {.complex = {.inner_value = 1}, .value = 2};\n"
            "  return take({.value = 1}) + made().value + box.value + omitted.value + empty.value + flipped.value + bias.value;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var p Point = {.z = 1};\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var p Point = {.x = 1, .x = 2};\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var p Point = {.x = null};\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var n = {.x = 1};\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  p := {.x = 1};\n"
            "  return p;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var n i32 = {.x = 1};\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  return {.x = 1};\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Link struct { n i32; next ?*Link; };\n"
            "func main() i32 {\n"
            "  var link Link = {.n = 1, .next = null};\n"
            "  link = {.next = null};\n"
            "  if (link.next == null) {\n"
            "    return link.n;\n"
            "  }\n"
            "  return 1;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Buf struct { raw [4]i32; };\n"
            "func main() i32 {\n"
            "  var buf Buf = {.raw = 1};\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var p Point = {1, 2};\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Color enum;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "expected '{'");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") type Handle struct {}\n"
            "assert(sizeof(Handle) == 1, \"handle\");\n"
            "func main() i32 {\n"
            "  var p ?*Handle;\n"
            "  p = null;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"math/a.tlc", "type A struct { b *B; n i32; };\n"
"func take(b *B) i32 {\n"
"  var a A = {.n = 2, .b = b};\n"
"  return a.n + a.b.x;\n"
"}\n"},
            {"math/b.tlc", "type B struct { x i32; };\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Node struct { n Node; };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  var p struct Point;\n"
            "  p.x = 1;\n"
            "  return p.x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "expected a type");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 { return sizeof(struct Point); }\n");
        EXPECT_HAS(check, errors, "expected a type");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "type Point i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 {\n"
            "  var Point i32;\n"
            "  Point = sizeof(Point);\n"
            "  return Point;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Shape struct { x i32; };\n"
            "type Shape trait { func area() i32; };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Point, sum } from \"geom\";\n"
"func main() i32 {\n"
"  var p Point;\n"
"  p.x = 40;\n"
"  p.y = 2;\n"
"  return sum(p);\n"
"}\n"},
            {"geom.tlc", "package geom;\n"
                       "export type Point struct { x i32; y i32; };\n"
                       "export func sum(p Point) i32 { return p.x + p.y; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type A struct { b B; };\n"
            "type B struct { x i32; };\n"
            "func main() i32 {\n"
            "  var a A;\n"
            "  a.b.x = 42;\n"
            "  return a.b.x;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { add } from \"math\";\n"
                       "func main() i32 { return add(20, 2); }\n"},
            {"math/add.tlc", "func secret() i32 { return extra(); }\n"
                           "export func add(a i32, b i32) i32 { return a + b + secret(); }\n"},
            {"math/more.tlc", "func extra() i32 { return 20; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() i32 { return math.secret(); }\n"},
            {"math/add.tlc", "func secret() i32 { return 1; }\n"
                           "export func add(a i32, b i32) i32 { return a + b; }\n"},
            {"math/more.tlc", "func extra() i32 { return 2; }\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() i32 { return 0; }\n"},
            {"math/a.tlc", "static func hidden() i32 { return 1; }\n"
                         "export func add() i32 { return hidden(); }\n"},
            {"math/b.tlc", "func use() i32 { return hidden(); }\n"},
        });
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\nfunc main() i32 { return 0; }\n"},
            {"math/a.tlc", "var value i32;\n"},
            {"math/b.tlc", "var value i32;\n"},
        });
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { a, b } from \"math\";\nfunc main() i32 { return a() + b(); }\n"},
            {"math/a.tlc", "static func hidden() i32 { return 1; }\n"
                         "export func a() i32 { return hidden(); }\n"},
            {"math/b.tlc", "static func hidden() i32 { return 2; }\n"
                         "export func b() i32 { return hidden(); }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { use } from \"math\";\nfunc main() i32 { return use(); }\n"},
            {"math/a.tlc", "type A struct { b B; };\n"
"export func use() i32 {\n"
"  var a A;\n"
"  a.b.x = 42;\n"
"  return a.b.x;\n"
"}\n"},
            {"math/b.tlc", "type B struct { x i32; };\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"math/point.tlc", "package math;\n"
                               "type Point struct { x i32; y i32; };\n"
                               "func Point::sum(self this) i32 { return self.x + self.y; }\n"},
            {"math/use.tlc", "package math;\n"
                             "export func total() i32 {\n"
                             "  var p Point;\n"
                             "  p.x = 40;\n"
                             "  p.y = 2;\n"
                             "  return p.sum();\n"
                             "}\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"math/point.tlc", "package math;\ntype Point struct { x i32; };\n"},
            {"math/sum.tlc", "package math;\nfunc Point::sum(self this) i32 { return self.x; }\n"},
        });
        EXPECT_HAS(check, errors, "method in another file");
    }

    {
        const std::string errors = diagnose_files({
            {"math/color.tlc", "package math;\ntype Color enum { Red, Blue, };\n"},
            {"math/code.tlc", "package math;\nfunc Color::code(self this) i32 { return 0; }\n"},
        });
        EXPECT_HAS(check, errors, "method in another file");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { Point } from \"geom\";\n"
                         "func Point::sum(self this) i32 { return self.x; }\n"
                         "func main() i32 { return 0; }\n"},
            {"geom/point.tlc", "package geom;\nexport type Point struct { x i32; };\n"},
        });
        EXPECT_HAS(check, errors, "method in another file");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  func f(x i32) i32 { return x; }\n"
            "  return f(42);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "var bias i32;\n"
            "func inc(n i32) i32 { return n + 1; }\n"
            "func main() i32 {\n"
            "  bias = 1;\n"
            "  func f(n i32) i32 { return inc(n) + bias; }\n"
            "  {\n"
            "    return f(1);\n"
            "  }\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32 = 1;\n"
            "  func f(x i32) i32 { return x; }\n"
            "  return f(2);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  func even(n i32) i32;\n"
            "  func odd(n i32) i32 {\n"
            "    if (n == 0) { return 0; }\n"
            "    return even(n - 1);\n"
            "  }\n"
            "  func even(n i32) i32 {\n"
            "    if (n == 0) { return 1; }\n"
            "    return odd(n - 1);\n"
            "  }\n"
            "  return even(2);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 0;\n"
            "  for (func f() i32 { return 42; }; n < 1; n = n + 1) {\n"
            "    return f();\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  {\n"
            "    func f() i32 { return 1; }\n"
            "    return f();\n"
            "  }\n"
            "}\n"
            "func other() i32 { return f(); }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  {\n"
            "    func f() i32 { return 1; }\n"
            "  }\n"
            "  {\n"
            "    return f();\n"
            "  }\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32 = f();\n"
            "  func f() i32 { return 1; }\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  func f() i32 { return 1; }\n"
            "  func f() i32 { return 2; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  func f() i32;\n"
            "  func f(n i32) i32 { return n; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  func f() i32;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32 = 40;\n"
            "  func f() i32 { return x; }\n"
            "  return f();\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32 = 1;\n"
            "  func f() i32 { x = 2; return x; }\n"
            "  return f();\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  while (1) {\n"
            "    func f() i32 {\n"
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
            "func main() i32 {\n"
            "  switch (1) {\n"
            "  case 1:\n"
            "    func f() i32 {\n"
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
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  func Point::get(self this) i32 { return self.x; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func note(n i32) void { }\n"
            "type Box struct { v i32; };\n"
            "func Box::put(self this, n i32) void { }\n"
            "func Box::touch(self *this) void { }\n"
            "func main() i32 {\n"
            "  func inner(n i32) void { }\n"
            "  var box Box;\n"
            "  var p *Box = &box;\n"
            "  defer note(1);\n"
            "  defer inner(2);\n"
            "  defer func(v i32) { }(3);\n"
            "  defer box.put(4);\n"
            "  defer p.touch();\n"
            "  return func() i32 { return 0; }();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  defer 1;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var x i32 = 1;\n"
            "  defer func() i32 { return x; }();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func note(n i32) void { }\n"
            "func main() i32 {\n"
            "  defer note();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") func printf(fmt *const u8, ...) i32;\n"
            "func main() i32 {\n"
            "  defer printf(\"hi\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait { func area() i32; };\n"
            "type Rect struct { w i32; };\n"
            "func Rect::area(self this) i32 { return self.w; }\n"
            "func use(s Shape) i32 {\n"
            "  defer s.area();\n"
            "  return 0;\n"
            "}\n"
            "func main() i32 {\n"
            "  var r Rect;\n"
            "  r.w = 1;\n"
            "  return use(r);\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Box struct { v i32; };\n"
            "func Box::split(self this) (i32, i32) { return self.v, self.v + 1; }\n"
            "func add(x i32, y i32) (i32, i32) { return x + y, x; }\n"
            "func main() i32 {\n"
            "  func pair(n i32) (i32, i32) { return n + 2, n; }\n"
            "  sum, orig := add(20, 22);\n"
            "  var extra, base = add(1, 1);\n"
            "  extra, base = add(0, 0);\n"
            "  var box Box;\n"
            "  box.v = 3;\n"
            "  var left, right = box.split();\n"
            "  var z, w = func() (i32, i32) { return 0, 0; }();\n"
            "  p, q := pair(0);\n"
            "  add(1, 2);\n"
            "  defer add(0, 1);\n"
            "  var n i32 = 1;\n"
            "  {\n"
            "    n, m := add(4, 5);\n"
            "    return n + m;\n"
            "  }\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x + y, x; }\n"
            "func main() i32 {\n"
            "  var x i32 = add(1, 2);\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  a, b := add(1, 2);\n"
            "  a, b := add(3, 4);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var a, b = add(1, 2);\n"
            "  var a, c = add(3, 4);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  a, b = add(1, 2);\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose("extern(abi=\"c\") func pair() (i32, i32);\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "extern function cannot return several results");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "extern func parts(p Point) (i32, i32) { return p.x, p.y; }\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 20;\n"
            "  p.y = 22;\n"
            "  a, b := parts(p);\n"
            "  return a + b;\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose("extern func printf(fmt *u8, ...) i32;\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"tlc\") func parts() (i32, i32) { return 20, 22; }\n"
            "extern(abi=\"nope\") func missing() i32;\n"
            "func main() i32 { a, b := parts(); return a + b; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
        EXPECT(check, errors.find("several results") == std::string::npos);
    }

    {
        const std::string errors = diagnose("extern type Handle struct {}\nfunc main() i32 { return sizeof(Handle); }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose("extern type Num union { n i32; }\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Pair struct { a i32; b i32; };\n"
            "extern(abi=\"c\") func sum(p Pair) i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "struct must be extern");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") type Pair struct { a i32; b i32; };\n"
            "extern(abi=\"c\") func sum(p Pair) i32;\n"
            "extern(abi=\"c\") func make() Pair;\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "type Inner struct { n i32; };\n"
            "extern(abi=\"c\") type Outer struct { item Inner; };\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "struct must be extern");
    }

    {
        const std::string errors = diagnose(
            "type Num union { n i32; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type ShapeType enum { Circle, Rectangle }\n"
            "type Shape union(ShapeType) {\n"
            "    Circle struct { radius f32; };\n"
            "    Rectangle struct { width f32; height f32; };\n"
            "}\n"
            "type Circle struct { radius i32; }\n"
            "type Rectangle struct { width i32; height i32; }\n"
            "type Named union(ShapeType) { Circle Circle; Rectangle Rectangle; }\n"
            "func main() i32 {\n"
            "    var s Shape = {.Circle = {.radius = 1.0}};\n"
            "    s.Rectangle.width = 2.0;\n"
            "    var n Named;\n"
            "    n.Circle.radius = 3;\n"
            "    if (ShapeType(s) == ShapeType.Rectangle && ShapeType(n) == ShapeType.Circle) {\n"
            "        return n.Circle.radius;\n"
            "    }\n"
            "    return 0;\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "type Shape union(ShapeType) { Circle Circle; Rectangle Rectangle; }\n"
            "type Rectangle struct { width i32; height i32; }\n"
            "type Circle struct { radius i32; }\n"
            "type ShapeType enum { Rectangle, Circle }\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "type ShapeType enum { Circle, Rectangle }\n"
            "type Shape union(ShapeType) {\n"
            "    Circle struct { radius i32; };\n"
            "    Other struct { n i32; };\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type ShapeType enum { Circle, Rectangle }\n"
            "type Shape union(ShapeType) { Circle i32; Rectangle i32; }\n"
            "type Box struct { n i32; }\n"
            "type Bad union(Box) { A struct { n i32; }; }\n"
            "type Missing union(Nope) { A struct { n i32; }; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") type Kind enum { A }\n"
            "extern(abi=\"c\") type Num union(Kind) { A struct { n i32; }; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose_files({
            {"app/main.tlc",
             "func main() i32 {\n"
             "  var pair Pair = {.B = {.n = 8}};\n"
             "  if (Other(pair) != Other.B) { return 0; }\n"
             "  return pair.B.n;\n"
             "}\n"},
            {"app/shape.tlc",
             "type Other enum { A, B }\n"
             "type Left struct { n i32; }\n"
             "type Right struct { n i32; }\n"
             "type Pair union(Other) { A Left; B Right; }\n"},
        });
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") type Num union { n i32; m i32; }\n"
            "type Inner struct { n i32; };\n"
            "extern(abi=\"c\") type Bad union { item Inner; n i32; }\n"
            "extern(abi=\"c\") type Empty union {}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "struct must be extern");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") type Pair struct;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "expected '{'");
    }

    {
        const std::string errors = diagnose("type Pair trait { func both() (i32, i32); };\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func bad() (*i32, i32) { return 1, 2; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "return type: expected *i32, found i32");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func id(n i32) (i32) { return n; }\n"
            "func trail(n i32) (i32,) { return n; }\n"
            "func tri(n i32) (i32, ?*i32, Point) {\n"
            "  var p Point;\n"
            "  p.x = n;\n"
            "  p.y = n;\n"
            "  return n, null, p;\n"
            "}\n"
            "func narrow() (i16, u8, bool) { return -2, 200, true; }\n"
            "func add(x i32, y i32) (i32, i32);\n"
            "func add(x i32, y i32) (i32, i32) { return x + y, x; }\n"
            "func take(n i32) i32 {\n"
            "  n, extra := add(n, 1);\n"
            "  return n + extra;\n"
            "}\n"
            "func keep(n i32) i32 {\n"
            "  {\n"
            "    n, extra := add(1, 2);\n"
            "  }\n"
            "  return n;\n"
            "}\n"
            "static func hidden() (i32, i32) { return 1, 2; }\n"
            "func main() i32 {\n"
            "  var x = id(1);\n"
            "  y := trail(x);\n"
            "  var kept i32 = 1;\n"
            "  kept, fresh := add(kept, 2);\n"
            "  {\n"
            "    var kept, other = add(4, 5);\n"
            "    y = kept + other;\n"
            "  }\n"
            "  var p ?*i32;\n"
            "  var n i32;\n"
            "  var point Point;\n"
            "  n, p, point = tri(1);\n"
            "  var s i16;\n"
            "  var c u8;\n"
            "  var flag bool;\n"
            "  s, c, flag = narrow();\n"
            "  var outer i32 = 1;\n"
            "  func inner() i32 {\n"
            "    outer, m := add(1, 2);\n"
            "    return outer + m;\n"
            "  }\n"
            "  var h1 i32;\n"
            "  var h2 i32;\n"
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
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  a := add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "return type: expected i32, found (i32, i32)");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var a = add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "return type: expected i32, found (i32, i32)");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  a, b, c := add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  a, a := add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var a, a = add(1, 2);\n"
            "  return a;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  x := 1;\n"
            "  x := 2;\n"
            "  return x;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { var a := 1; return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var p *i32;\n"
            "  var n i32;\n"
            "  p = null;\n"
            "  p, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch: expected *i32, found i32");
    }

    {
        const std::string errors = diagnose(
            "type Color enum { Red = 1 };\n"
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var n i32 = 0;\n"
            "  Red, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var xs [2]i32;\n"
            "  var n i32 = 0;\n"
            "  xs, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var n i32 = 0;\n"
            "  add, n = add(1, 2);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not an lvalue");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var outer i32 = 1;\n"
            "  func inner() i32 {\n"
            "    var n i32 = 0;\n"
            "    outer, n = add(1, 2);\n"
            "    return n;\n"
            "  }\n"
            "  return inner() + outer;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func bad() (void, i32) { return 1; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func bad() (i32, void) { return 1; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func bad() (i32, i32) { return; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func bad() (i32, i32) { return 1, 2, 3; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func bad() i32 { return 1, 2; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func pass() (i32, i32) { return add(1, 2); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var n i32 = sizeof(add(1, 2));\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Num trait { func get() i32; };\n"
            "func pair(n Num) (i32, i32) { return n.get(), 1; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) i32;\n"
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  func pair(n i32) (i32, i32);\n"
            "  func pair(n i32) (i32, *i32) { return n, null; }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func take(n i32) i32 { return n; }\n"
            "func main() i32 {\n"
            "  if (add(1, 2)) {\n"
            "    return 1;\n"
            "  }\n"
            "  return take(add(1, 2));\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  a, b := 1;\n"
            "  c, d := add;\n"
            "  return a + c;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func make() (Point, i32) {\n"
            "  var p Point;\n"
            "  p.x = 1;\n"
            "  return p, 2;\n"
            "}\n"
            "func main() i32 {\n"
            "  var a i32;\n"
            "  var b i32;\n"
            "  a, b = make();\n"
            "  return b;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import \"math\";\n"
                       "func main() i32 {\n"
                       "  a, b := math.pair(20);\n"
                       "  return a + b;\n"
                       "}\n"},
            {"math/pair.tlc", "package math;\n"
                            "export func pair(n i32) (i32, i32) { return n + 1, n; }\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { pair } from \"math\";\n"
                       "func main() i32 {\n"
                       "  a, b := pair(1);\n"
                       "  return a;\n"
                       "}\n"},
            {"math/pair.tlc", "package math;\n"
                            "func pair(n i32) (i32, i32) { return n, n; }\n"},
        });
        EXPECT_HAS(check, errors, "is not exported");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { twice } from \"math\";\n"
                       "func main() i32 { return twice(1); }\n"},
            {"math/pair.tlc", "package math;\n"
                            "func pair(n i32) (i32, i32) { return n + 1, n; }\n"},
            {"math/use.tlc", "package math;\n"
                           "export func twice(n i32) i32 {\n"
                           "  a, b := pair(n);\n"
                           "  return a + b;\n"
                           "}\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose("export extern(abi=\"c\") var value i32;\n"
"func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "export extern");
    }

    {
        const std::string errors = diagnose_files({
            {"main.tlc", "import { add } from \"math\";\nfunc main() i32 { return add(20, 22); }\n"},
            {"math/add.tlc", "package math;\nexport extern(abi=\"c\") func add(a i32, b i32) i32;\n"},
        });
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func inc(n i32) i32 { return n + 1; }\n"
            "func apply(fp i32 (*)(i32), n i32) i32 { return fp(n); }\n"
            "func main() i32 {\n"
            "  var fp func (i32) i32 = inc;\n"
            "  var x f64 = 20.5;\n"
            "  var y f32 = 1.5f;\n"
            "  var z f64 = x + y;\n"
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
            "  var fp func (i32, i32) i32 = pick();\n"
            "  return myfunc(add) + take(id) + fp(1, 1);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func dosomething(op func (i32, i32) i32) i32 { return op(20, 22); }\n"
            "type Box struct { n i32; };\n"
            "func Box::map(self this, op func (i32) i32) i32 { return op(self.n); }\n"
            "func note(p func (i32) void) i32 { p(1); return 1; }\n"
            "func main() i32 {\n"
            "  var full = dosomething(func (a i32, b i32) i32 { return a + b; });\n"
            "  var brief = dosomething((a, b) { return a + b; });\n"
            "  var fp func (i32, i32) i32 = (a, b) { return a + b; };\n"
            "  var box Box;\n"
            "  box.n = 40;\n"
            "  var extra = box.map((n) { return n; });\n"
            "  var quiet = note((n) { });\n"
            "  return full + brief + fp(1, 1) + extra + quiet;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n = (a, b) { return a + b; };\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func take(op func (i32) i32) i32 { return op(1); }\n"
            "func main() i32 { return take((a, b) { return a; }); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func take(op func (i32) i32) i32 {\n"
            "  var x = 1;\n"
            "  return take((n) { return n + x; });\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func make() func (i32) i32 { return (n) { return n + 1; }; }\n"
            "func pair() func (i32) (i32, i32) { return (n) { return n, n + 1; }; }\n"
            "func none() func () i32 { return () { return 1; }; }\n"
            "type Box struct { n i32; };\n"
            "func Box::choose(self this) func (i32, i32) i32 { return (a, b) { return a + b; }; }\n"
            "func Box::full() func (i32) i32 { return func (n i32) i32 { return n; }; }\n"
            "func main() i32 {\n"
            "  var f = make();\n"
            "  var box Box;\n"
            "  var g = box.choose();\n"
            "  a, b := pair()(1);\n"
            "  return f(1) + g(1, 1) + none()() + Box.full()(1) + a + b;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func make(x i32) func (i32) i32 { return (n) { return n + x; }; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func add(n i32) i32 { return n + 1; }\n"
            "func outer(op func (i32) i32) func (i32) i32 {\n"
            "  func pass() func (i32) i32 { return op; }\n"
            "  return pass();\n"
            "}\n"
            "func apply(op func (i32) i32) i32 {\n"
            "  func inner(n i32) i32 { return op(n); }\n"
            "  return inner(1);\n"
            "}\n"
            "func nested(op func (i32) i32) i32 {\n"
            "  func mid() i32 {\n"
            "    func inner() func (i32) i32 { return op; }\n"
            "    return inner()(1);\n"
            "  }\n"
            "  return mid();\n"
            "}\n"
            "type Box struct { n i32; };\n"
            "func Box::wrap(self this, op func (i32) i32) i32 {\n"
            "  func pass() func (i32) i32 { return op; }\n"
            "  return pass()(self.n);\n"
            "}\n"
            "func main() i32 {\n"
            "  var box Box;\n"
            "  return outer(add)(1) + apply(add) + nested(add) + box.wrap(add);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func add(n i32) i32 { return n; }\n"
            "func outer(op func (i32) i32) i32 {\n"
            "  func pass() func (i32) i32 { return op; }\n"
            "  var f = pass;\n"
            "  return f(1);\n"
            "}\n"
            "func main() i32 { return outer(add); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func add(n i32) i32 { return n; }\n"
            "func outer(op func (i32) i32) func (i32) i32 {\n"
            "  return (n) { return op(n); };\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func add(n i32) i32 { return n; }\n"
            "func outer(op func (i32) i32) i32 {\n"
            "  func inner() i32 {\n"
            "    op = add;\n"
            "    return 0;\n"
            "  }\n"
            "  return inner();\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Box struct { n i32; };\n"
            "func Box::bad(self this) i32 {\n"
            "  func inner() i32 { return self.n; }\n"
            "  return inner();\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func make() func (i32) i32 { return (a, b) { return a; }; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
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
            "type PairFn func (i32) (i32, i32);\n"
            "func apply(p PairFn) i32 {\n"
            "  a, b := p(20);\n"
            "  return a + b;\n"
            "}\n"
            "func pick() func (i32) (i32, i32) { return pair; }\n"
            "func main() i32 {\n"
            "  var fp func (i32) (i32, i32) = pick();\n"
            "  x, y := fp(10);\n"
            "  return apply(PairFn(fp)) + x + y;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") func take(p func (i32) (i32, i32)) i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "extern function cannot use several results");
    }

    {
        const std::string errors = diagnose(
            "extern(abi=\"c\") func get() func (i32) (i32, i32);\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "extern function cannot use several results");
    }

    {
        const std::string errors = diagnose(
            "func take(p *i64) i32 { return *p != 0; }\n"
            "func main() i32 {\n"
            "  var n i64 = 1;\n"
            "  var b u8 = 1;\n"
            "  var p *u8 = &b;\n"
            "  return take(&n) + *p;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func take(p *long) i32 { return 0; }\n"
            "func main() i32 {\n"
            "  var n i64 = 1;\n"
            "  return take(&n);\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Address struct { street ?*u8; };\n"
            "type Person struct { nickname ?*u8; home *Address; };\n"
            "func settle(person *Person, home *Address) void { person.home = home; }\n"
            "func bind(person ?*Person, home ?*Address) void {\n"
            "  if (person == null) { return; }\n"
            "  if (home == null) { return; }\n"
            "  settle(person, home);\n"
            "}\n"
            "func main() i32 {\n"
            "  var home Address = {.street = null};\n"
            "  var person Person = {.nickname = null, .home = &home};\n"
            "  person.nickname = null;\n"
            "  person.home = &home;\n"
            "  bind(&person, &home);\n"
            "  var p ?*i32;\n"
            "  if (p == null) { return 0; }\n"
            "  return *p;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Address struct { street ?*u8; };\n"
            "type Person struct { nickname ?*u8; home *Address; };\n"
            "func main() i32 {\n"
            "  var home Address = {.street = null};\n"
            "  var missing Person = {.nickname = null};\n"
            "  var person Person = {.nickname = null, .home = &home};\n"
            "  person.home = null;\n"
            "  var p *i32;\n"
            "  var q ?*i32;\n"
            "  q = null;\n"
            "  return *q + missing.home.street[0];\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func take(p *i32) i32 { return *p; }\n"
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var p ?*i32 = &n;\n"
            "  var q *i32 = &n;\n"
            "  if (p == q) { return take(p); }\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func take(p *i32) i32 { return *p; }\n"
            "func wide(p ?*i32) i32 { return p != null; }\n"
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var p ?*i32 = &n;\n"
            "  if (p == null) { return 0; }\n"
            "  return take(p) + wide(&n);\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var p ?*i32 = null;\n"
            "  var q *i32 = *i32(p);\n"
            "  var bad *i32 = *i32(null);\n"
            "  var ok ?*i32 = ?*i32(null);\n"
            "  return *q + n + *ok;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose("func main() i32 { var p ?i32 = 0; return p; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "extern var slot *i32;\n"
            "func main() i32 { return slot != null; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "var saved func (i32, i32) i32;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var p ?*i32 = &n;\n"
            "  do { return *p; } while (p != null);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point::read(self ?*this) i32 {\n"
            "  if (self == null) { return 0; }\n"
            "  return self.x;\n"
            "}\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  p.x = 4;\n"
            "  var q ?*Point = &p;\n"
            "  return p.read() + q.read();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 3;\n"
            "  var p ?*i32 = &n;\n"
            "  while (p != null) {\n"
            "    n = *p;\n"
            "    break;\n"
            "  }\n"
            "  return n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "var g ?*i32;\n"
            "func main() i32 {\n"
            "  if (g == null) { return 0; }\n"
            "  return *g;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var inner ?*i32 = &n;\n"
            "  var p *?*i32 = &inner;\n"
            "  var q ?**i32 = p;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Box struct { n i32; };\n"
            "func read(p *i32) i32 { return *p; }\n"
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var p ?*i32 = &n;\n"
            "  var box Box;\n"
            "  box.n = 2;\n"
            "  var q ?*Box = &box;\n"
            "  return read(p!) + q!.n;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func read(p *i32) i32 { return *p; }\n"
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var p ?*i32 = &n;\n"
            "  var direct *i32 = &n;\n"
            "  return read(p) + read(direct!) + p!! + n!;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "assert(sizeof(i32) == 4, \"i32 must be 4 bytes\");\n"
            "type Pair struct { a u8; b i32; };\n"
            "assert(sizeof(Pair) == 8, \"pair\");\n"
            "type ShapeType enum { Circle, Rectangle };\n"
            "type Shape union(ShapeType) {\n"
            "    Circle struct { radius f32; };\n"
            "    Rectangle struct { width f32; height f32; };\n"
            "};\n"
            "assert(sizeof(Shape) == 12, \"shape\");\n"
            "func main() i32 {\n"
            "  assert(sizeof(i32) == 4, \"i32 must be 4 bytes\");\n"
            "  assert(sizeof(*i32) == 8, \"pointer\");\n"
            "  assert(1 + 2 == 3, \"add\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "assert(sizeof(i32) == 8, \"i32 must be 4 bytes\");\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "i32 must be 4 bytes");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  assert(n == 1, \"runtime\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose("assert(1, 2);\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func add(a i32, b i32) i32 { return a + b; }\n"
            "test \"add joins its arguments\" {\n"
            "  if (add(2, 2) != 4) { return 1; }\n"
            "}\n"
            "test \"empty passes\" {}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "test \"same\" {}\n"
            "test \"same\" {}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "redefinition");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  test \"inside\" {}\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "test \"void return\" { return; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "return type");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "func add(a i32, b i32) i32 { return a + b; }\n"
            "test \"add joins its arguments\" {\n"
            "  var value = add(2, 2);\n"
            "  if (value != 4) {\n"
            "    return testing.error(\"Expected 4 but was %d\", value);\n"
            "  }\n"
            "}\n"
            "test \"string\" { return testing.error(\"%s\", \"no\"); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = check_program(
            "import { error } from \"std/testing\";\n"
            "test \"string\" { return error(\"%s\", \"no\"); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  return testing.error(\"x\");\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "test \"t\" { return testing.error(\"x\"); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "func main() i32 {\n"
            "  return testing.error(\"x\");\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "test \"t\" { return testing.error(); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "test \"t\" { return testing.error(1); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "test \"t\" { return testing.missing(\"x\"); }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not exported");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "type Point struct { x i32; y i32; }\n"
            "test \"t\" {\n"
            "  var p Point;\n"
            "  return testing.error(\"%d\", p);\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = check_program(
            "import \"std/testing\";\n"
            "test \"t\" {\n"
            "  var f = testing.error;\n"
            "  return 0;\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = check_program(
            "import \"builtin\";\n"
            "func main() i32 {\n"
            "  var p = builtin.va();\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n u8 = 1;\n"
            "  const value = n + 2;\n"
            "  var p *const u8 = &n;\n"
            "  var q *u8 = p;\n"
            "  *p = 3;\n"
            "  value = 4;\n"
            "  return value + *p + q[0];\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = check_program(
            "import \"builtin\";\n"
            "func main() i32 {\n"
            "  var p *const u8 = builtin.trace();\n"
            "  var q *u8 = builtin.trace();\n"
            "  return *p;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n u8 = 1;\n"
            "  var p *u8 = &n;\n"
            "  var q *const u8 = p;\n"
            "  const value = 40 + 2;\n"
            "  return value + *q;\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "func id[T type](value T) T { return value; }\n"
            "func main() i32 { var f = id; return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func weird[T type](value i32) i32 { return value; }\n"
            "func main() i32 { return weird(1); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Shape trait { func area() i32; };\n"
            "type Point struct { x i32; y i32; };\n"
            "func id[T Shape](value T) i32 { return 1; }\n"
            "func main() i32 { var p Point; return id(p); }\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "extern func id[T type](value T) T;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Vec[T type] struct { n i32; };\n"
            "func Vec[T]::get[U type](self *this) i32 { return self.n; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Vec[T type] struct { n T; };\n"
            "func Vec[T]::get(self this) T { return self.n; }\n"
            "func main() i32 {\n"
            "  var v Vec[i32];\n"
            "  v.n = 42;\n"
            "  return v.get();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Buf[N i32, T type] struct { value T; };\n"
            "func Buf[N, T]::len(self this) i32 { return N; }\n"
            "func main() i32 {\n"
            "  var b Buf[4, i32];\n"
            "  b.value = 1;\n"
            "  return b.len();\n"
            "}\n");
        EXPECT_EQ(check, errors, "");
    }

    {
        const std::string errors = diagnose(
            "type Vec[T type] struct { n i32; };\n"
            "func Vec::get(self this) i32 { return self.n; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Vec[T type] struct { n i32; };\n"
            "func Vec[U]::get(self this) i32 { return self.n; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Vec[T type] struct { n i32; };\n"
            "func Vec[T, U]::get(self this) i32 { return self.n; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "type Buf[N i32, T type] struct { value T; };\n"
            "func Buf[T, N]::get(self this) T { return self.value; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func Point[T]::get(self this) i32 { return self.x; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "type Vec[T type] struct { n i32; };\n"
            "func Vec[T type]::get(self this) i32 { return self.n; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose_files({
            {"math/vec.tlc", "package math;\ntype Vec[T type] struct { n i32; };\n"},
            {"math/get.tlc", "package math;\nfunc Vec[T]::get(self this) i32 { return self.n; }\n"},
        });
        EXPECT_HAS(check, errors, "method in another file");
    }

    {
        const std::string errors = diagnose(
            "type Box[T type] struct { value T; };\n"
            "func take(item Box[i32]) i32 { return item.value; }\n"
            "func main() i32 {\n"
            "  var ints Box[i32];\n"
            "  var chars Box[u8];\n"
            "  var floats Box[f32];\n"
            "  ints = floats;\n"
            "  return take(chars);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch: expected Box.spec.i32, found Box.spec.f32");
        EXPECT_HAS(check, errors, "type mismatch: expected Box.spec.i32, found Box.spec.u8");
    }

    {
        const std::string errors = diagnose(
            "type Box[T type] struct { value T; };\n"
            "func pair[T type](a Box[T], b Box[T]) i32 { return 0; }\n"
            "func main() i32 {\n"
            "  var ints Box[i32];\n"
            "  var chars Box[u8];\n"
            "  return pair(ints, chars);\n"
            "}\n");
        EXPECT_HAS(check, errors, "error: type mismatch\n");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var a int;\n"
            "  var b uint;\n"
            "  var c short;\n"
            "  var d long;\n"
            "  var e ulong;\n"
            "  var f char;\n"
            "  var g float;\n"
            "  var h double;\n"
            "  return 0;\n"
            "}\n");
        int count = 0;
        for (std::size_t pos = 0; (pos = errors.find("not supported yet", pos)) != std::string::npos;
             pos += 1) {
            ++count;
        }
        EXPECT(check, count == 8);
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var n i32 = 1;\n"
            "  var t Type = typeof(n);\n"
            "  var text *u8 = t.name();\n"
            "  if (t == typeof(i32) && typeof('A') == typeof(u8) && typeof('A') != typeof(i8)) {\n"
            "    return 0;\n"
            "  }\n"
            "  return text != t.name();\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose("func main() i32 { var t Type; return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var t Type = typeof(i32);\n"
            "  return t < typeof(u8);\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var t Type = typeof(i32);\n"
            "  return t.other();\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var t Type = typeof(i32);\n"
            "  return t.name(1);\n"
            "}\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var t Type = typeof(i32);\n"
            "  var text *u8 = t;\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, errors, "type mismatch: expected *u8, found Type");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 {\n"
            "  var t Type = typeof(i32);\n"
            "  return t.name;\n"
            "}\n");
        EXPECT_HAS(check, errors, "undeclared identifier");
    }

    {
        const std::string errors = diagnose("func main() i32 { return typeof(main); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; };\n"
            "func main() i32 { return typeof(struct Point); }\n");
        EXPECT_HAS(check, errors, "expected a type");
    }

    {
        const std::string errors = diagnose(
            "func keep[...U](args U) U { return args; }\n"
            "func first[...U](args U) typeof(args[0]) { return args[0]; }\n"
            "func id(x i32) typeof(x) { return x; }\n"
            "func main() i32 {\n"
            "  var n = keep(40);\n"
            "  a, b := keep((40, 2));\n"
            "  var row [3]i32 = {10, 20, 12};\n"
            "  var head = first((7, 8));\n"
            "  return n + a + b + row[0] + head + id(1);\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "type Point struct { x i32; y i32; };\n"
            "func main() i32 { var box Point = {10, 20}; return 0; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func first[...U](args U) typeof(args[0]) { return args[0]; }\n"
            "func main() i32 { return first(40); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func f[T type, ...U](a T, args U) T { return a; }\n"
            "func main() i32 { return f(1, 2); }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "func f[...U, T type](args U, a T) T { return a; }\n"
            "func main() i32 { return f(1, 2); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func f[T type, ...U, V type](a T, args U, b V) T { return a; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func main() i32 { var row [3]i32 = {10, 20}; return row[0]; }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    {
        const std::string errors = diagnose(
            "func keep[...U](args U) i32 { args[0] = 1; return 0; }\n"
            "func main() i32 { return keep((1, 2)); }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose("type Box[...U] struct { n i32; };\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, errors, "not supported yet");
    }

    {
        const std::string errors = diagnose(
            "func keep[...U](args U) U { return args; }\n"
            "func main() i32 { return keep(); }\n");
        EXPECT_HAS(check, errors, "argument count");
    }

    {
        const std::string errors = diagnose("func main() i32 { a, b := (40, 2); return a + b; }\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose(
            "func add(x i32, y i32) (i32, i32) { return x, y; }\n"
            "func main() i32 {\n"
            "  var mytuple = add(40, 2);\n"
            "  var value1, value2 = mytuple;\n"
            "  return value1 + value2;\n"
            "}\n");
        EXPECT(check, errors.empty());
    }

    {
        const std::string errors = diagnose("func main() i32 { var a, b, c = (1, 2); return a; }\n");
        EXPECT_HAS(check, errors, "type mismatch: expected 3 results, found 2 results");
    }

    {
        const std::string errors = diagnose(
            "func keep[...U](args U) U { return args; }\n"
            "func main() i32 { return keep[4](1); }\n");
        EXPECT_HAS(check, errors, "type mismatch");
    }

    return check.failed;
}
