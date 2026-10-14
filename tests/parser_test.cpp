#include "harness.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <string>

namespace {

struct Parsed {
    std::string dump;
    std::string errors;
};

Parsed parse_source(const std::string &source) {
    tlc::SourceSet sources;
    tlc::SourceFile &file = sources.add("mem.tlc", source);
    tlc::DiagnosticEngine diags(sources);
    tlc::Lexer lexer(file, diags);
    tlc::Parser parser(lexer.tokenize(), diags);
    const tlc::TranslationUnit unit = parser.parse();
    return Parsed{tlc::dump_translation_unit(unit), diags.render()};
}

} // namespace

int run_parser_tests() {
    tlc::test::Check check;

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  return 1 + 2 * 3;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (fn main (params) i32\n"
                  "    (block\n"
                  "      (return (+ (int 1) (* (int 2) (int 3))))\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  return 1 | 2 & 3 ^ 4;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return (| (int 1) (^ (& (int 2) (int 3)) (int 4))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  return a = b = 1;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return (= (name a) (= (name b) (int 1))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main(n i32) i32 {\n"
            "  if (n) { return -n; } else if (n) { return 1; } else { return 0; }\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (fn main (params (n i32)) i32\n"
                  "    (block\n"
                  "      (if (name n)\n"
                  "        (block\n"
                  "          (return (- (name n)))\n"
                  "        )\n"
                  "        (if (name n)\n"
                  "          (block\n"
                  "            (return (int 1))\n"
                  "          )\n"
                  "          (block\n"
                  "            (return (int 0))\n"
                  "          )\n"
                  "        )\n"
                  "      )\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main(n i32) i32 {\n"
            "  if (n) return -n;\n"
            "  if (n) { return -n; } else return 0;\n"
            "  while (n) n = n - 1;\n"
            "  for (;;) break;\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, parsed.errors, "expected '{'");
    }

    {
        const Parsed parsed = parse_source(
            "func main(n i32) i32 {\n"
            "  while (n) { n = n - 1; }\n"
            "  return n;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(while (name n)\n");
        EXPECT_HAS(check, parsed.dump, "(expr (= (name n) (- (name n) (int 1))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var s i32 = 0;\n"
            "  for (var i i32 = 0; i < 10; i = i + 1) { s = s + i; }\n"
            "  for (;;) { break; }\n"
            "  return s;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (fn main (params) i32\n"
                  "    (block\n"
                  "      (decl\n"
                  "        (var s i32 (int 0))\n"
                  "      )\n"
                  "      (for\n"
                  "        (decl\n"
                  "          (var i i32 (int 0))\n"
                  "        )\n"
                  "        (< (name i) (int 10))\n"
                  "        (= (name i) (+ (name i) (int 1)))\n"
                  "        (block\n"
                  "          (expr (= (name s) (+ (name s) (name i))))\n"
                  "        )\n"
                  "      )\n"
                  "      (for\n"
                  "        (empty)\n"
                  "        (empty)\n"
                  "        (empty)\n"
                  "        (block\n"
                  "          (break)\n"
                  "        )\n"
                  "      )\n"
                  "      (return (name s))\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  continue;\n"
            "  math.add(1, 2);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(continue)");
        EXPECT_HAS(check, parsed.dump, "(expr (call (. (name math) add) (int 1) (int 2)))");
    }

    {
        const Parsed parsed = parse_source(
            "package math;\n"
            "import \"a\";\n"
            "import \"lib/vec\" as v;\n"
            "import { add, Point } from \"math\";\n"
            "export func add(a i32, b i32) i32 {\n"
            "  return a + b;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (package math)\n"
                  "  (import \"a\")\n"
                  "  (import-as \"lib/vec\" v)\n"
                  "  (import-from \"math\" add Point)\n"
                  "  (fn add (params (a i32) (b i32)) i32 export\n"
                  "    (block\n"
                  "      (return (+ (name a) (name b)))\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 { return 0; }\n"
            "import \"math\";\n");
        EXPECT_HAS(check, parsed.errors, "import must appear before declarations");
    }

    {
        const Parsed parsed = parse_source(
            "var x f64;\n"
            "var y f32;\n"
            "func main() i32 {\n"
            "  var fp func (i32, f64) i32;\n"
            "  return 1.5;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var x f64");
        EXPECT_HAS(check, parsed.dump, "(var y f32");
        EXPECT_HAS(check, parsed.dump, "(var fp i32* fnptr");
        EXPECT_HAS(check, parsed.dump, "(double 1.5)");
    }

    {
        const Parsed parsed = parse_source(
            "type BinOp func (i32, i32) i32;\n"
            "var saved func (i32) i32;\n"
            "type Box struct {\n"
            "  op func (i32, i32) i32;\n"
            "};\n"
            "func myfunc(ptr func (i32, i32) i32) i32 {\n"
            "  return ptr(10, 20);\n"
            "}\n"
            "func pick() func (i32) i32;\n"
            "func main() i32 {\n"
            "  var fp func (i32, i32) i32 = saved;\n"
            "  return myfunc(fp);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(type BinOp i32* fnptr");
        EXPECT_HAS(check, parsed.dump, "(var saved i32* fnptr");
        EXPECT_HAS(check, parsed.dump, "(field op i32* fnptr");
        EXPECT_HAS(check, parsed.dump, "(fn myfunc (params (ptr i32* fnptr)) i32");
        EXPECT_HAS(check, parsed.dump, "(fn pick (params) i32* fnptr");
        EXPECT_HAS(check, parsed.dump, "(var fp i32* fnptr");
        EXPECT_HAS(check, parsed.dump, "(return (call (name ptr) (int 10) (int 20)))");
    }

    {
        const Parsed parsed = parse_source("func f(p func (x i32) i32) i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected ')'");
    }

    {
        const Parsed parsed = parse_source("func f(p func (i32) (i32, i32)) i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(p i32* fnptr (i32, i32))");
    }

    {
        const Parsed parsed = parse_source("func f(p func (i32) func (i32) i32) i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func add(n i32) i32 { return n; }\n"
            "func main() i32 {\n"
            "  var raw *void = *void(add);\n"
            "  return func (i32) i32(raw)(41);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(call (cast i32* fnptr (name raw)) (int 41))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  return func (n i32) i32 { return n; }(41);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT(check, parsed.dump.find("(cast ") == std::string::npos);
        EXPECT_HAS(check, parsed.dump, "(call (anon");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  return func (i32) func (i32) i32(raw);\n"
            "}\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main( {\n  return 1\n}\n");
        EXPECT_HAS(check, parsed.errors, "expected ')'");
        EXPECT_HAS(check, parsed.errors, "expected ';'");
    }

    {
        const Parsed parsed = parse_source(
            "var p *i32;\n"
            "var q **i32;\n"
            "func f(a *i32) *i32;\n"
            "func main() i32 {\n"
            "  p = &q;\n"
            "  return *p;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var p i32*");
        EXPECT_HAS(check, parsed.dump, "(var q i32**");
        EXPECT_HAS(check, parsed.dump, "(fn f (params (a i32*)) i32*");
        EXPECT_HAS(check, parsed.dump, "(expr (= (name p) (& (name q))))");
        EXPECT_HAS(check, parsed.dump, "(return (* (name p)))");
    }

    {
        const Parsed parsed = parse_source(
            "type Person struct { nickname ?*u8; home *Address; };\n"
            "func bind(person ?*Person, home ?*Address) void { return; }\n"
            "func main() i32 {\n"
            "  var p ?*i32;\n"
            "  var q *?*i32;\n"
            "  var r ?**i32;\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(field nickname u8?*)");
        EXPECT_HAS(check, parsed.dump, "(field home Address*)");
        EXPECT_HAS(check, parsed.dump, "(fn bind (params (person Person?*) (home Address?*)) void");
        EXPECT_HAS(check, parsed.dump, "(var p i32?*");
        EXPECT_HAS(check, parsed.dump, "(var q i32*?*");
        EXPECT_HAS(check, parsed.dump, "(var r i32?**");
    }

    {
        const Parsed parsed = parse_source(
            "test \"add joins its arguments\" {\n"
            "  return 1;\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(test \"add joins its arguments\"");
        EXPECT_HAS(check, parsed.dump, "(return (int 1))");
    }

    {
        const Parsed parsed = parse_source(
            "assert(sizeof(i32) == 4, \"i32 must be 4 bytes\");\n"
            "func main() i32 {\n"
            "  assert(1, \"kept\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(assert (== (sizeof i32) (int 4)) \"i32 must be 4 bytes\")");
        EXPECT_HAS(check, parsed.dump, "(assert (int 1) \"kept\")");
    }

    {
        const Parsed parsed = parse_source("var p ?i32;\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var p ?*i32;\n"
            "  var n = !p;\n"
            "  return p!.n;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(! (name p))");
        EXPECT_HAS(check, parsed.dump, "(. (!post (name p)) n)");
    }

    {
        const Parsed parsed = parse_source(
            "var a [4]i32;\n"
            "var b [2]*i32;\n"
            "var s [3]u8;\n"
            "func f(xs [4]i32, t *u8) i32;\n"
            "func main() i32 {\n"
            "  return \"hi\"[0];\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var a i32[4]");
        EXPECT_HAS(check, parsed.dump, "(var b i32*[2]");
        EXPECT_HAS(check, parsed.dump, "(var s u8[3]");
        EXPECT_HAS(check, parsed.dump, "(fn f (params (xs i32[4]) (t u8*)) i32");
        EXPECT_HAS(check, parsed.dump, "(return ([] (string \"hi\") (int 0)))");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { return row[1:3]; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return ([:] (name row) (int 1) (int 3)))");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { return row[1:]; }\n");
        EXPECT_HAS(check, parsed.errors, "expected an expression");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var a [n]i32; return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var a i32[n]");
    }

    {
        const Parsed parsed = parse_source("var a []i32;\n"
"func main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var a i32[])");
    }

    {
        const Parsed parsed = parse_source(
            "export type Point struct {\n"
            "  x i32;\n"
            "  y *i32;\n"
            "};\n"
            "var p Point;\n"
            "var q geom.Point;\n"
            "func sum(p Point) i32 {\n"
            "  return p.x;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Point export\n");
        EXPECT_HAS(check, parsed.dump, "(field x i32)\n");
        EXPECT_HAS(check, parsed.dump, "(field y i32*)\n");
        EXPECT_HAS(check, parsed.dump, "(var p Point)\n");
        EXPECT_HAS(check, parsed.dump, "(var q geom.Point)\n");
        EXPECT_HAS(check, parsed.dump, "(fn sum (params (p Point)) i32\n");
        EXPECT_HAS(check, parsed.dump, "(return (. (name p) x))\n");
    }

    {
        const Parsed parsed = parse_source(
            "type Entity struct {\n"
            "  id i32;\n"
            "  var next i32 = 1;\n"
            "  const spare = 3;\n"
            "  export var shown = 2;\n"
            "};\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "export type Entity struct {\n"
            "  id i32;\n"
            "  export shown i32;\n"
            "  body export struct { k i32; export n i32; };\n"
            "  local struct { n i32; };\n"
            "};\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(field id i32)\n");
        EXPECT_HAS(check, parsed.dump, "(field export shown i32)\n");
        EXPECT_HAS(check, parsed.dump, "(field export body struct\n");
        EXPECT_HAS(check, parsed.dump, "(field export k i32)\n");
        EXPECT_HAS(check, parsed.dump, "(field export n i32)\n");
        EXPECT_HAS(check, parsed.dump, "(field local struct\n");
        EXPECT_HAS(check, parsed.dump, "(field n i32)\n");
    }

    {
        const Parsed parsed = parse_source(
            "type Entity struct {\n"
            "  id i32;\n"
            "};\n"
            "const Entity::spare = 3;\n"
            "type Box[T type] struct {\n"
            "  value T;\n"
            "};\n"
            "const Box[T]::limit i32 = 4;\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(field id i32)\n");
        EXPECT_HAS(check, parsed.dump, "(var Entity::spare := (int 3))\n");
        EXPECT_HAS(check, parsed.dump, "(var Box[T]::limit i32 (int 4))\n");
    }

    {
        const Parsed parsed = parse_source(
            "type Entity struct {\n"
            "  id i32;\n"
            "  const next i32 = 1;\n"
            "  export const shown = 2;\n"
            "};\n"
            "type Box[T type] struct {\n"
            "  value T;\n"
            "  const extra i32 = 1;\n"
            "};\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
        EXPECT_HAS(check, parsed.dump, "(field id i32)\n");
        EXPECT(check, parsed.dump.find("(var Entity::next") == std::string::npos);
        EXPECT(check, parsed.dump.find("(var Entity::shown") == std::string::npos);
        EXPECT(check, parsed.dump.find("(var Box[T]::extra") == std::string::npos);
    }

    {
        const Parsed parsed = parse_source("func main() i32 { type Point struct { x i32; }; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "extern(abi=\"c\") func printf(fmt *const u8, ...) i32;\n"
            "func main() i32 {\n"
            "  printf(\"hi\\n\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn printf (params (fmt const u8*) ...) i32 extern(abi=\"c\")\n");
        EXPECT_HAS(check, parsed.dump, "(expr (call (name printf) (string \"hi\\n\")))\n");
    }

    {
        const Parsed parsed = parse_source(
            "extern func add(a i32, b i32) i32;\n"
            "extern(other) func nope() i32;\n");
        EXPECT_HAS(check, parsed.dump, "(fn add (params (a i32) (b i32)) i32 extern\n");
        EXPECT(check, parsed.dump.find("extern(abi=\"c\")") == std::string::npos);
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "extern(abi=\"tlc\") func add(a i32, b i32) i32;\n"
            "type Pair struct(align=16) { a i32; b i32; }\n"
            "type Bad struct(align=3) { n i32; }\n");
        EXPECT(check, parsed.errors.find("type mismatch") != std::string::npos);
        EXPECT_HAS(check, parsed.dump, "(fn add (params (a i32) (b i32)) i32 extern\n");
        EXPECT(check, parsed.dump.find("abi=") == std::string::npos);
        EXPECT_HAS(check, parsed.dump, "(struct Pair align 16\n");
    }

    {
        const Parsed parsed = parse_source("var x const i32;\n"
"func main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func f(x const i32) i32 { return x; }\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("extern(abi=\"c\") func f(x const i32) i32 { return x; }\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "export type Color enum {\n"
            "  Red,\n"
            "  Green = 40,\n"
            "  Blue,\n"
            "};\n"
            "type Ptr *i32;\n"
            "type Pair [2]i32;\n"
            "var c Color;\n"
            "func main() i32 { return Blue; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(enum Color export\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Red)\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Green 40)\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Blue)\n");
        EXPECT_HAS(check, parsed.dump, "(type Ptr i32*)\n");
        EXPECT_HAS(check, parsed.dump, "(type Pair i32[2])\n");
        EXPECT_HAS(check, parsed.dump, "(var c Color)\n");
    }

    {
        const Parsed parsed = parse_source(
            "type Node struct;\n"
            "type Color enum;\n"
            "extern(abi=\"c\") type Flag enum;\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected '{'");
        EXPECT_HAS(check, parsed.dump, "(struct Node\n");
        EXPECT(check, parsed.dump.find("forward") == std::string::npos);
    }

    {
        const Parsed parsed = parse_source(
            "extern(abi=\"c\") type Pair struct { a i32; b i32; };\n"
            "export extern(abi=\"c\") type Node struct;\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Pair extern(abi=\"c\")\n");
        EXPECT_HAS(check, parsed.dump, "(struct Node export extern(abi=\"c\")\n");
    }

    {
        const Parsed parsed = parse_source(
            "export extern(abi=\"c\") type Handle struct {}\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Handle export extern(abi=\"c\")\n");
    }

    {
        const Parsed parsed = parse_source(
            "extern(abi=\"c\") type Num union { n i32; m i32; }\n"
            "type Plain union { n i32; }\n"
            "extern type Tlc union { n i32; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
        EXPECT_HAS(check, parsed.dump, "(union Num extern(abi=\"c\")\n");
        EXPECT_HAS(check, parsed.dump, "(field n i32)\n");
        EXPECT(check, parsed.dump.find("(union Plain") == std::string::npos);
        EXPECT(check, parsed.dump.find("(union Tlc") == std::string::npos);
    }

    {
        const Parsed parsed = parse_source(
            "type ShapeType enum { Circle, Rectangle }\n"
            "type Shape union(ShapeType) {\n"
            "    Circle struct { radius f32; };\n"
            "    Rectangle struct { width f32; height f32; };\n"
            "}\n"
            "type Circle struct { radius f32; }\n"
            "type Rectangle struct { width f32; height f32; }\n"
            "type Named union(ShapeType) { Circle Circle; Rectangle Rectangle; }\n"
            "func tag(s Shape) ShapeType { return ShapeType(s); }\n"
            "extern(abi=\"c\") type Ext union(ShapeType) { Circle struct { radius f32; }; }\n"
            "extern type Plain union(ShapeType) { Circle Circle; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
        EXPECT_HAS(check, parsed.dump, "(union Shape ShapeType\n");
        EXPECT_HAS(check, parsed.dump, "(field Circle struct\n");
        EXPECT_HAS(check, parsed.dump, "(field radius f32)\n");
        EXPECT_HAS(check, parsed.dump, "(field Rectangle struct\n");
        EXPECT_HAS(check, parsed.dump, "(field width f32)\n");
        EXPECT_HAS(check, parsed.dump, "(field height f32)\n");
        EXPECT_HAS(check, parsed.dump, "(union Named ShapeType\n");
        EXPECT_HAS(check, parsed.dump, "(field Circle Circle)\n");
        EXPECT_HAS(check, parsed.dump, "(field Rectangle Rectangle)\n");
        EXPECT_HAS(check, parsed.dump, "(cast ShapeType (name s))");
        EXPECT(check, parsed.dump.find("(struct Shape.") == std::string::npos);
        EXPECT(check, parsed.dump.find("(union Ext") == std::string::npos);
        EXPECT(check, parsed.dump.find("(union Plain") == std::string::npos);
    }

    {
        const Parsed parsed = parse_source(
            "type Box struct { p *[4]i32; }\n"
            "func main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "type Point struct { x i32; }\n"
            "type Color enum { Red, Blue = 2 }\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Point\n");
        EXPECT_HAS(check, parsed.dump, "(enum Color\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Blue 2)\n");
    }

    {
        const Parsed parsed = parse_source("type Tiny enum(i8) { A, B = 2 };\nfunc main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(enum Tiny i8\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator B 2)\n");
    }

    {
        const Parsed parsed = parse_source(
            "export extern(abi=\"c\") type Color enum {\n"
            "  Red,\n"
            "  Blue = 2,\n"
            "}\n"
            "func main() i32 { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(enum Color export extern(abi=\"c\")\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Blue 2)\n");
    }

    {
        const Parsed parsed = parse_source("type Color enum { Green = -1 };\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { type T i32; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { type E enum { A }; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("type F i32(i32);\nfunc main() i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected ';'");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var n i32;\n"
            "  n = 1;\n"
            "  switch (n) {\n"
            "  case 1:\n"
            "    n = 2;\n"
            "  case 2:\n"
            "    break;\n"
            "  default:\n"
            "    n = 0;\n"
            "    break;\n"
            "  }\n"
            "  do {\n"
            "    n = n + 1;\n"
            "  } while (n < 3);\n"
            "  return n;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(switch (name n)\n");
        EXPECT_HAS(check, parsed.dump, "(case (int 1)\n");
        EXPECT_HAS(check, parsed.dump, "(case (int 2)\n");
        EXPECT_HAS(check, parsed.dump, "(default\n");
        EXPECT_HAS(check, parsed.dump, "(do\n");
        EXPECT_HAS(check, parsed.dump, "(< (name n) (int 3))\n");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { goto end; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var n i32;\n"
            "  n = sizeof(i32) + sizeof(*i32) + sizeof n + sizeof(n);\n"
            "  n = i32(*u8(*void(n)));\n"
            "  return (n);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(sizeof i32)");
        EXPECT_HAS(check, parsed.dump, "(sizeof i32*)");
        EXPECT_HAS(check, parsed.dump, "(sizeof (name n))");
        EXPECT_HAS(check, parsed.dump, "(cast i32 (cast u8* (cast void* (name n))))");
        EXPECT_HAS(check, parsed.dump, "(return (name n))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var b bool;\n"
            "  var p *i32;\n"
            "  b = true;\n"
            "  b = false;\n"
            "  p = null;\n"
            "  return b;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var b bool");
        EXPECT_HAS(check, parsed.dump, "(= (name b) (true))");
        EXPECT_HAS(check, parsed.dump, "(= (name b) (false))");
        EXPECT_HAS(check, parsed.dump, "(= (name p) (null))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var n u64;\n"
            "  var w i64;\n"
            "  var u u32;\n"
            "  var s i16;\n"
            "  var sc i8;\n"
            "  var c u8;\n"
            "  c = 'A';\n"
            "  c = '\\n';\n"
            "  n = 1u64;\n"
            "  return i32(c) + i64(s);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n u64");
        EXPECT_HAS(check, parsed.dump, "(var w i64");
        EXPECT_HAS(check, parsed.dump, "(var u u32");
        EXPECT_HAS(check, parsed.dump, "(var s i16");
        EXPECT_HAS(check, parsed.dump, "(var sc i8");
        EXPECT_HAS(check, parsed.dump, "(char 65)");
        EXPECT_HAS(check, parsed.dump, "(char 10)");
        EXPECT_HAS(check, parsed.dump, "(int 1 u64)");
        EXPECT_HAS(check, parsed.dump, "(cast i32 (name c))");
        EXPECT_HAS(check, parsed.dump, "(cast i64 (name s))");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var n unsigned; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var n signed; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var m long long; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected ';'");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var n u32; var m u64; return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n u32");
        EXPECT_HAS(check, parsed.dump, "(var m u64");
    }

    {
        const Parsed parsed = parse_source("var x short long;\n");
        EXPECT_HAS(check, parsed.errors, "expected ';'");
    }

    {
        const Parsed parsed = parse_source("var x long long long;\n");
        EXPECT_HAS(check, parsed.errors, "expected ';'");
    }

    {
        const Parsed parsed = parse_source(
            "type Point struct {\n"
            "  x i32;\n"
            "};\n"
            "func Point::sum(self this, extra i32) i32 {\n"
            "  return self.x + extra;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn Point::sum (params (self struct Point receiver) (extra i32)) i32");
        EXPECT_HAS(check, parsed.dump, "(return (+ (. (name self) x) (name extra)))");
    }

    {
        const Parsed parsed = parse_source(
            "type Vec[T type] struct { n i32; };\n"
            "func Vec[T]::get(self *this, i i32) T { return self.data[i]; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn Vec[T]::get (params (self struct Vec* receiver) (i i32)) T");
    }

    {
        const Parsed parsed = parse_source("func Vec[T type]::get(self this) i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "type Inner struct {\n"
            "  inner_value i32;\n"
            "};\n"
            "type Box struct {\n"
            "  value i32;\n"
            "  complex Inner;\n"
            "};\n"
            "func main() i32 {\n"
            "  var box Box = {.value = 10, .complex = {.inner_value=20}};\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump,
                   "(init (.value (int 10)) (.complex (init (.inner_value (int 20)))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var box Point = {.x = 1,};\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var box Point (init (.x (int 1))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var box Point = {10, 20};\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(init (int 10) (int 20))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var p = Point{};\n"
            "  var q = Point{.x = 1};\n"
            "  var r = geom.Point{};\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(init Point)");
        EXPECT_HAS(check, parsed.dump, "(init Point (.x (int 1)))");
        EXPECT_HAS(check, parsed.dump, "(init geom.Point)");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var row = [_]i32{10, 20, 12};\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(array i32 (int 10) (int 20) (int 12))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var row = []i32{10, 20, 12};\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, parsed.errors, "expected '_'");
    }

    {
        const Parsed parsed = parse_source("func keep[...U](args U) U { return args; }\n");
        EXPECT(check, parsed.errors.empty());
    }

    {
        const Parsed parsed = parse_source("func keep[T type, ...U](a T, args U) T { return a; }\n");
        EXPECT(check, parsed.errors.empty());
    }

    {
        const Parsed parsed = parse_source("func keep[...U, T type](args U, a T) U { return args; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var box Point = {. = 1};\n"
            "  return 0;\n"
            "}\n");
        EXPECT_HAS(check, parsed.errors, "expected an identifier");
    }

    {
        const Parsed parsed = parse_source("func f(self this) i32 { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func Point::sum(x i32) i32 { return x; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn Point::sum (params (x i32)) i32");
    }

    {
        const Parsed parsed = parse_source("i32 Point::x;\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "func Point::add(self *this, dx i32) void {\n"
            "  return;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn Point::add (params (self struct Point* receiver) (dx i32)) void");
    }

    {
        const Parsed parsed = parse_source("func Point::add(self **this) void { return; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { p->x; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "type Shape trait {\n"
            "  func area() i32;\n"
            "  func grow(n i32) void;\n"
            "};\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(trait Shape\n");
        EXPECT_HAS(check, parsed.dump, "(fn area (params) i32");
        EXPECT_HAS(check, parsed.dump, "(fn grow (params (n i32)) void");
    }

    {
        const Parsed parsed = parse_source(
            "type Shape trait {\n"
            "  w i32;\n"
            "  func area() i32;\n"
            "};\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(field w i32");
        EXPECT_HAS(check, parsed.dump, "(fn area (params) i32");
    }

    {
        const Parsed parsed = parse_source(
            "type Shape trait {\n"
            "  func area(self this) i32 { return 1; }\n"
            "};\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "type Shape trait {\n"
            "  func grow(self **this) void;\n"
            "};\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  func id(p *i32) *i32 {\n"
            "    return p;\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn main (params) i32");
        EXPECT_HAS(check, parsed.dump, "(fn id (params (p i32*)) i32*");
        EXPECT_HAS(check, parsed.dump, "(return (name p))");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { i32 f(); return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "nested functions are not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { export func f() i32 { return 1; } return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  defer func(value i32) {\n"
            "  }(10);\n"
            "  defer func() {\n"
            "  }();\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(defer ");
        EXPECT_HAS(check, parsed.dump, "(anon (params (value i32)) void");
        EXPECT_HAS(check, parsed.dump, "(int 10)");
        EXPECT_HAS(check, parsed.dump, "(anon (params) void");
    }

    {
        const Parsed parsed = parse_source(
            "func dosomething(op func (i32, i32) i32) i32 { return op(1, 2); }\n"
            "func main() i32 {\n"
            "  return dosomething(func (arg1 i32, arg2 i32) i32 { return arg1 + arg2; })\n"
            "      + dosomething((arg1, arg2) { return arg1 + arg2; });\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(anon (params (arg1 i32) (arg2 i32)) i32");
        EXPECT_HAS(check, parsed.dump, "(anon inferred (params (arg1) (arg2))");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { return a && b | c && d; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(&& (&& (name a) (| (name b) (name c))) (name d))");
    }

    {
        const Parsed parsed = parse_source(
            "type Point struct { x i32; };\n"
            "func Point::method(self this, a i32, b i32, c i32) void {\n"
            "}\n"
            "func main() i32 {\n"
            "  var myval Point;\n"
            "  defer myval.method(1, 2, 3);\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(defer (call (. (name myval) method) (int 1) (int 2) (int 3)))");
    }

    {
        const Parsed parsed = parse_source(
            "func add(x i32, y i32) (i32, i32) {\n"
            "  return x + y, x;\n"
            "}\n"
            "func id(n i32) (i32) { return n; }\n"
            "func main() i32 {\n"
            "  sum, orig := add(20, 22);\n"
            "  var extra, base = add(1, 1);\n"
            "  extra, base = add(0, 0);\n"
            "  return sum;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn add (params (x i32) (y i32)) (i32, i32)");
        EXPECT_HAS(check, parsed.dump, "(return (+ (name x) (name y)) (name x))");
        EXPECT_HAS(check, parsed.dump, "(fn id (params (n i32)) i32");
        EXPECT_HAS(check, parsed.dump, "(bind def sum orig (call (name add) (int 20) (int 22)))");
        EXPECT_HAS(check, parsed.dump, "(bind var extra base (call (name add) (int 1) (int 1)))");
        EXPECT_HAS(check, parsed.dump, "(bind set extra base (call (name add) (int 0) (int 0)))");
    }

    {
        const Parsed parsed = parse_source("func f() () { return; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var z, w = func() (i32, i32) { return 0, 0; }();\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(anon (params) (i32, i32)");
        EXPECT_HAS(check, parsed.dump, "(bind var z w");
    }

    {
        const Parsed parsed = parse_source(
            "type Box struct { v i32; };\n"
            "func Box::both(self *this, p *math.Point) (i32, *Box, math.Point, u64) {\n"
            "  return self.v, self, p, 1;\n"
            "}\n"
            "func id(n i32) (i32) { return n; }\n"
            "func trail(n i32) (i32,) { return n; }\n"
            "func pair(n i32) (i32, i32,) { return n, n; }\n"
            "func main() i32 {\n"
            "  var x = id(1);\n"
            "  y := 2;\n"
            "  return x + y;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump,
                   "(fn Box::both (params (self struct Box* receiver) (p math.Point*)) "
                   "(i32, Box*, math.Point, u64)");
        EXPECT_HAS(check, parsed.dump, "(return (. (name self) v) (name self) (name p) (int 1))");
        EXPECT_HAS(check, parsed.dump, "(fn id (params (n i32)) i32");
        EXPECT_HAS(check, parsed.dump, "(fn trail (params (n i32)) i32");
        EXPECT_HAS(check, parsed.dump, "(fn pair (params (n i32)) (i32, i32)");
        EXPECT_HAS(check, parsed.dump, "(bind var x (call (name id) (int 1)))");
        EXPECT_HAS(check, parsed.dump, "(bind def y (int 2))");
        EXPECT_HAS(check, parsed.dump, "(return (+ (name x) (name y)))");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var a := 1; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("var a, b = 1;\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("a, b := 1;\n");
        EXPECT_HAS(check, parsed.errors, "expected a declaration");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var = 1; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected an identifier");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var a, = 1; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected an identifier");
    }

    {
        const Parsed parsed = parse_source(
            "func f() (i32, i32) { return 1, 2; }\n"
            "func main() i32 {\n"
            "  for (a, b := f(); ;) {\n"
            "    return a;\n"
            "  }\n"
            "  for (var x, y = f(); x; x = x + 1) {\n"
            "    return x;\n"
            "  }\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(bind def a b (call (name f)))");
        EXPECT_HAS(check, parsed.dump, "(bind var x y (call (name f)))");
    }

    {
        const Parsed parsed = parse_source("i32 value = 1;\n");
        EXPECT_HAS(check, parsed.errors, "expected var");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { i32 value = 1; return value; }\n");
        EXPECT_HAS(check, parsed.errors, "expected var");
    }

    {
        const Parsed parsed = parse_source(
            "n := 40;\n"
            "func main() i32 {\n"
            "  var extra i32 = 2;\n"
            "  return n + extra;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n := (int 40))");
        EXPECT_HAS(check, parsed.dump, "(var extra i32 (int 2))");
    }

    {
        const Parsed parsed = parse_source("var n = 1;\nfunc main() i32 { return n; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n := (int 1))");
    }

    {
        const Parsed parsed = parse_source(
            "var Entity::next i32 = 1;\n"
            "var Box[T]::extra i32;\n"
            "var Buf[N, T]::count = 4;\n"
            "func main() i32 { return Entity.next + Box[i32].extra + Buf[4, i32].count; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var Entity::next i32 (int 1))");
        EXPECT_HAS(check, parsed.dump, "(var Box[T]::extra i32)");
        EXPECT_HAS(check, parsed.dump, "(var Buf[N, T]::count := (int 4))");
        EXPECT_HAS(check, parsed.dump, "(. (name Entity) next)");
        EXPECT_HAS(check, parsed.dump, "(. ([] (name Box) (name i32)) extra)");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var p *i32 = 0;\n"
            "  var buf [32]u8;\n"
            "  var ps [2]*i32;\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var p i32*");
        EXPECT_HAS(check, parsed.dump, "(var buf u8[32]");
        EXPECT_HAS(check, parsed.dump, "(var ps i32*[2]");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var arr *[4]i32 = 0; return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var arr i32[4] ptrarr");
    }

    {
        const Parsed parsed = parse_source("func main() i32 { var arr **[4]i32; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func old(p i32 *) i32 { return 0; }\n"
            "type Name *i32;\n"
            "func main() i32 { return sizeof(i32 *); }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "func sized(xs i32[4]) i32 { return 0; }\n"
            "func named(xs i32[N]) i32 { return 0; }\n"
            "func open(xs i32[]) i32 { return 0; }\n"
            "func result() i32[2] { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func head(a []i32) []i32 { return a; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn head (params (a i32[])) i32");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var n i32;\n"
            "  n = i32*(n);\n"
            "  return n;\n"
            "}\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "type Point struct { x i32; };\n"
            "func main() i32 {\n"
            "  var p Point;\n"
            "  var q *Point = *Point(&p);\n"
            "  return q.x;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(cast Point*");
    }

    {
        const Parsed parsed = parse_source(
            "func f() *i32 {\n"
            "  var n i32;\n"
            "  return &n;\n"
            "}\n"
            "func main() i32 { return *f(); }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return (* (call (name f))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  return typeof(i32) == typeof(n);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return (== (typeof i32) (typeof (name n))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() i32 {\n"
            "  var n i32;\n"
            "  var text *u8 = typeof(n).name();\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(call (. (typeof (name n)) name))");
    }

    return check.failed;
}
