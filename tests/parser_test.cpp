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
            "func main() int {\n"
            "  return 1 + 2 * 3;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (fn main (params) int\n"
                  "    (block\n"
                  "      (return (+ (int 1) (* (int 2) (int 3))))\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  return 1 | 2 & 3 ^ 4;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return (| (int 1) (^ (& (int 2) (int 3)) (int 4))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  return a = b = 1;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(return (= (name a) (= (name b) (int 1))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main(n int) int {\n"
            "  if (n) return -n; else return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (fn main (params (n int)) int\n"
                  "    (block\n"
                  "      (if (name n)\n"
                  "        (return (- (name n)))\n"
                  "        (return (int 0))\n"
                  "      )\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main(n int) int {\n"
            "  while (n) n = n - 1;\n"
            "  return n;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(while (name n)\n");
        EXPECT_HAS(check, parsed.dump, "(expr (= (name n) (- (name n) (int 1))))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  var s int = 0;\n"
            "  for (var i int = 0; i < 10; i = i + 1) s = s + i;\n"
            "  for (;;) break;\n"
            "  return s;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (fn main (params) int\n"
                  "    (block\n"
                  "      (decl\n"
                  "        (var s int (int 0))\n"
                  "      )\n"
                  "      (for\n"
                  "        (decl\n"
                  "          (var i int (int 0))\n"
                  "        )\n"
                  "        (< (name i) (int 10))\n"
                  "        (= (name i) (+ (name i) (int 1)))\n"
                  "        (expr (= (name s) (+ (name s) (name i))))\n"
                  "      )\n"
                  "      (for\n"
                  "        (empty)\n"
                  "        (empty)\n"
                  "        (empty)\n"
                  "        (break)\n"
                  "      )\n"
                  "      (return (name s))\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
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
            "export func add(a int, b int) int {\n"
            "  return a + b;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_EQ(check, parsed.dump,
                  "(tu\n"
                  "  (package math)\n"
                  "  (import \"a\")\n"
                  "  (import-as \"lib/vec\" v)\n"
                  "  (import-from \"math\" add Point)\n"
                  "  (fn add (params (a int) (b int)) int export\n"
                  "    (block\n"
                  "      (return (+ (name a) (name b)))\n"
                  "    )\n"
                  "  )\n"
                  ")\n");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int { return 0; }\n"
            "import \"math\";\n");
        EXPECT_HAS(check, parsed.errors, "import must appear before declarations");
    }

    {
        const Parsed parsed = parse_source(
            "var x double;\n"
            "var y f32;\n"
            "func main() int {\n"
            "  var fp func (int, f64) int;\n"
            "  return 1.5;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var x double");
        EXPECT_HAS(check, parsed.dump, "(var y f32");
        EXPECT_HAS(check, parsed.dump, "(var fp int* fnptr");
        EXPECT_HAS(check, parsed.dump, "(double 1.5)");
    }

    {
        const Parsed parsed = parse_source(
            "typedef func (i32, i32) i32 BinOp;\n"
            "var saved func (i32) i32;\n"
            "struct Box {\n"
            "  func (i32, i32) i32 op;\n"
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
        EXPECT_HAS(check, parsed.dump, "(typedef BinOp i32* fnptr");
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
        const Parsed parsed = parse_source("func main( {\n  return 1\n}\n");
        EXPECT_HAS(check, parsed.errors, "expected ')'");
        EXPECT_HAS(check, parsed.errors, "expected ';'");
    }

    {
        const Parsed parsed = parse_source(
            "var p *int;\n"
            "var q **int;\n"
            "func f(a int *) int *;\n"
            "func main() int {\n"
            "  p = &q;\n"
            "  return *p;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var p int*");
        EXPECT_HAS(check, parsed.dump, "(var q int**");
        EXPECT_HAS(check, parsed.dump, "(fn f (params (a int*)) int*");
        EXPECT_HAS(check, parsed.dump, "(expr (= (name p) (& (name q))))");
        EXPECT_HAS(check, parsed.dump, "(return (* (name p)))");
    }

    {
        const Parsed parsed = parse_source(
            "var a [4]int;\n"
            "var b [2]*int;\n"
            "var s [3]char;\n"
            "func f(xs int[4], t char *) int;\n"
            "func main() int {\n"
            "  return \"hi\"[0];\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var a int[4]");
        EXPECT_HAS(check, parsed.dump, "(var b int*[2]");
        EXPECT_HAS(check, parsed.dump, "(var s char[3]");
        EXPECT_HAS(check, parsed.dump, "(fn f (params (xs int[4]) (t char*)) int");
        EXPECT_HAS(check, parsed.dump, "(return ([] (string \"hi\") (int 0)))");
    }

    {
        const Parsed parsed = parse_source("func main() int { var a [n]int; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("var a []int;\n"
"func main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "export struct Point {\n"
            "  int x;\n"
            "  int *y;\n"
            "};\n"
            "var p struct Point;\n"
            "var q geom.Point;\n"
            "func sum(p struct Point) int {\n"
            "  return p.x;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Point export\n");
        EXPECT_HAS(check, parsed.dump, "(field x int)\n");
        EXPECT_HAS(check, parsed.dump, "(field y int*)\n");
        EXPECT_HAS(check, parsed.dump, "(var p struct Point)\n");
        EXPECT_HAS(check, parsed.dump, "(var q geom.Point)\n");
        EXPECT_HAS(check, parsed.dump, "(fn sum (params (p struct Point)) int\n");
        EXPECT_HAS(check, parsed.dump, "(return (. (name p) x))\n");
    }

    {
        const Parsed parsed = parse_source("func main() int { struct Point { int x; }; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "extern func printf(fmt const char *, ...) int;\n"
            "func main() int {\n"
            "  printf(\"hi\\n\");\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn printf (params (fmt char*) ...) int extern\n");
        EXPECT_HAS(check, parsed.dump, "(expr (call (name printf) (string \"hi\\n\")))\n");
    }

    {
        const Parsed parsed = parse_source("var x const int;\n"
"func main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func f(x const int) int { return x; }\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("extern func f(x const int) int { return x; }\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "export enum Color {\n"
            "  Red,\n"
            "  Green = 40,\n"
            "  Blue,\n"
            "};\n"
            "typedef struct Point Point;\n"
            "typedef int *Ptr;\n"
            "typedef int Pair[2];\n"
            "var c enum Color;\n"
            "func main() int { return Blue; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(enum Color export\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Red)\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Green 40)\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Blue)\n");
        EXPECT_HAS(check, parsed.dump, "(typedef Point struct Point)\n");
        EXPECT_HAS(check, parsed.dump, "(typedef Ptr int*)\n");
        EXPECT_HAS(check, parsed.dump, "(typedef Pair int[2])\n");
        EXPECT_HAS(check, parsed.dump, "(var c enum Color)\n");
    }

    {
        const Parsed parsed = parse_source(
            "typedef struct Node;\n"
            "struct Node;\n"
            "enum Color;\n"
            "extern enum Flag;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected '{'");
        EXPECT(check, parsed.dump.find("forward") == std::string::npos);
    }

    {
        const Parsed parsed = parse_source(
            "extern struct Pair { int a; int b; };\n"
            "export extern struct Node;\n"
            "func main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected '{'");
        EXPECT_HAS(check, parsed.dump, "(struct Pair extern\n");
        EXPECT(check, parsed.dump.find("(struct Node") == std::string::npos);
    }

    {
        const Parsed parsed = parse_source(
            "export extern struct Handle {}\n"
            "func main() int { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Handle export extern\n");
    }

    {
        const Parsed parsed = parse_source(
            "struct Point { int x; }\n"
            "enum Color { Red, Blue = 2 }\n"
            "func main() int { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(struct Point\n");
        EXPECT_HAS(check, parsed.dump, "(enum Color\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Blue 2)\n");
    }

    {
        const Parsed parsed = parse_source("enum Tiny i8 { A, B = 2 };\nfunc main() int { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(enum Tiny i8\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator B 2)\n");
    }

    {
        const Parsed parsed = parse_source(
            "export extern enum Color {\n"
            "  Red,\n"
            "  Blue = 2,\n"
            "}\n"
            "func main() int { return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(enum Color export extern\n");
        EXPECT_HAS(check, parsed.dump, "(enumerator Blue 2)\n");
    }

    {
        const Parsed parsed = parse_source("enum Color { Green = -1 };\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() int { typedef int T; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() int { enum E { A }; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("typedef int F(int);\nfunc main() int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  var n int;\n"
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
        const Parsed parsed = parse_source("func main() int { goto end; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  var n int;\n"
            "  n = sizeof(int) + sizeof(int *) + sizeof n + sizeof(n);\n"
            "  n = int(char*(void*(n)));\n"
            "  return (n);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(sizeof int)");
        EXPECT_HAS(check, parsed.dump, "(sizeof int*)");
        EXPECT_HAS(check, parsed.dump, "(sizeof (name n))");
        EXPECT_HAS(check, parsed.dump, "(cast int (cast char* (cast void* (name n))))");
        EXPECT_HAS(check, parsed.dump, "(return (name n))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  var b bool;\n"
            "  var p *int;\n"
            "  b = true;\n"
            "  b = false;\n"
            "  p = nullptr;\n"
            "  return b;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var b bool");
        EXPECT_HAS(check, parsed.dump, "(= (name b) (true))");
        EXPECT_HAS(check, parsed.dump, "(= (name b) (false))");
        EXPECT_HAS(check, parsed.dump, "(= (name p) (nullptr))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  var n u64;\n"
            "  var w i64;\n"
            "  var u uint;\n"
            "  var s short;\n"
            "  var sc i8;\n"
            "  var c char;\n"
            "  c = 'A';\n"
            "  c = '\\n';\n"
            "  n = 1u64;\n"
            "  return int(c) + long(s);\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n u64");
        EXPECT_HAS(check, parsed.dump, "(var w i64");
        EXPECT_HAS(check, parsed.dump, "(var u uint");
        EXPECT_HAS(check, parsed.dump, "(var s short");
        EXPECT_HAS(check, parsed.dump, "(var sc i8");
        EXPECT_HAS(check, parsed.dump, "(char 65)");
        EXPECT_HAS(check, parsed.dump, "(char 10)");
        EXPECT_HAS(check, parsed.dump, "(int 1 u64)");
        EXPECT_HAS(check, parsed.dump, "(cast int (name c))");
        EXPECT_HAS(check, parsed.dump, "(cast long (name s))");
    }

    {
        const Parsed parsed = parse_source("func main() int { var n unsigned; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func main() int { var n signed; var m long long; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func main() int { var n uint; var m ulong; return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n uint");
        EXPECT_HAS(check, parsed.dump, "(var m ulong");
    }

    {
        const Parsed parsed = parse_source("var x short long;\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("var x long long long;\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "struct Point {\n"
            "  int x;\n"
            "};\n"
            "func Point::sum(this self Point, extra int) int {\n"
            "  return self.x + extra;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn Point::sum (params (self struct Point receiver) (extra int)) int");
        EXPECT_HAS(check, parsed.dump, "(return (+ (. (name self) x) (name extra)))");
    }

    {
        const Parsed parsed = parse_source("func f(this self Point) int { return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("func Point::sum(x int) int { return x; }\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source("int Point::x;\n");
        EXPECT_HAS(check, parsed.errors, "type mismatch");
    }

    {
        const Parsed parsed = parse_source(
            "func Point::add(this self Point *, dx int) void {\n"
            "  return;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn Point::add (params (self struct Point* receiver) (dx int)) void");
    }

    {
        const Parsed parsed = parse_source("func Point::add(this self Point **) void { return; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() int { p->x; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "trait Shape {\n"
            "  func area() int;\n"
            "  func grow(n int) void;\n"
            "};\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(trait Shape\n");
        EXPECT_HAS(check, parsed.dump, "(fn area (params) int");
        EXPECT_HAS(check, parsed.dump, "(fn grow (params (n int)) void");
    }

    {
        const Parsed parsed = parse_source(
            "trait Shape {\n"
            "  func area(this self Shape) int { return 1; }\n"
            "};\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "trait Shape {\n"
            "  func grow(this self Shape **) void;\n"
            "};\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  func id(p int *) int * {\n"
            "    return p;\n"
            "  }\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn main (params) int");
        EXPECT_HAS(check, parsed.dump, "(fn id (params (p int*)) int*");
        EXPECT_HAS(check, parsed.dump, "(return (name p))");
    }

    {
        const Parsed parsed = parse_source("func main() int { int f(); return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "nested functions are not supported yet");
    }

    {
        const Parsed parsed = parse_source("func main() int { export func f() int { return 1; } return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  defer func(value int) {\n"
            "  }(10);\n"
            "  defer func() {\n"
            "  }();\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(defer ");
        EXPECT_HAS(check, parsed.dump, "(anon (params (value int)) void");
        EXPECT_HAS(check, parsed.dump, "(int 10)");
        EXPECT_HAS(check, parsed.dump, "(anon (params) void");
    }

    {
        const Parsed parsed = parse_source(
            "struct Point { int x; };\n"
            "func Point::method(this self Point, a int, b int, c int) void {\n"
            "}\n"
            "func main() int {\n"
            "  var myval Point;\n"
            "  defer myval.method(1, 2, 3);\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(defer (call (. (name myval) method) (int 1) (int 2) (int 3)))");
    }

    {
        const Parsed parsed = parse_source(
            "func add(x int, y int) (int, int) {\n"
            "  return x + y, x;\n"
            "}\n"
            "func id(n int) (int) { return n; }\n"
            "func main() int {\n"
            "  sum, orig := add(20, 22);\n"
            "  var extra, base = add(1, 1);\n"
            "  extra, base = add(0, 0);\n"
            "  return sum;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(fn add (params (x int) (y int)) (int, int)");
        EXPECT_HAS(check, parsed.dump, "(return (+ (name x) (name y)) (name x))");
        EXPECT_HAS(check, parsed.dump, "(fn id (params (n int)) int");
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
            "func main() int {\n"
            "  var z, w = func() (int, int) { return 0, 0; }();\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(anon (params) (int, int)");
        EXPECT_HAS(check, parsed.dump, "(bind var z w");
    }

    {
        const Parsed parsed = parse_source(
            "struct Box { int v; };\n"
            "func Box::both(this self Box *, p math.Point *) (int, Box*, math.Point, ulong) {\n"
            "  return self.v, self, p, 1;\n"
            "}\n"
            "func id(n int) (int) { return n; }\n"
            "func trail(n int) (int,) { return n; }\n"
            "func pair(n int) (int, int,) { return n, n; }\n"
            "func main() int {\n"
            "  var x = id(1);\n"
            "  y := 2;\n"
            "  return x + y;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump,
                   "(fn Box::both (params (self struct Box* receiver) (p math.Point*)) "
                   "(int, Box*, math.Point, ulong)");
        EXPECT_HAS(check, parsed.dump, "(return (. (name self) v) (name self) (name p) (int 1))");
        EXPECT_HAS(check, parsed.dump, "(fn id (params (n int)) int");
        EXPECT_HAS(check, parsed.dump, "(fn trail (params (n int)) int");
        EXPECT_HAS(check, parsed.dump, "(fn pair (params (n int)) (int, int)");
        EXPECT_HAS(check, parsed.dump, "(bind var x (call (name id) (int 1)))");
        EXPECT_HAS(check, parsed.dump, "(bind def y (int 2))");
        EXPECT_HAS(check, parsed.dump, "(return (+ (name x) (name y)))");
    }

    {
        const Parsed parsed = parse_source("func main() int { var a := 1; return 0; }\n");
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
        const Parsed parsed = parse_source("func main() int { var = 1; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected an identifier");
    }

    {
        const Parsed parsed = parse_source("func main() int { var a, = 1; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "expected an identifier");
    }

    {
        const Parsed parsed = parse_source(
            "func f() (int, int) { return 1, 2; }\n"
            "func main() int {\n"
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
        const Parsed parsed = parse_source("int value = 1;\n");
        EXPECT_HAS(check, parsed.errors, "expected var");
    }

    {
        const Parsed parsed = parse_source("func main() int { int value = 1; return value; }\n");
        EXPECT_HAS(check, parsed.errors, "expected var");
    }

    {
        const Parsed parsed = parse_source(
            "n := 40;\n"
            "func main() int {\n"
            "  var extra i32 = 2;\n"
            "  return n + extra;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n := (int 40))");
        EXPECT_HAS(check, parsed.dump, "(var extra i32 (int 2))");
    }

    {
        const Parsed parsed = parse_source("var n = 1;\nfunc main() int { return n; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var n := (int 1))");
    }

    {
        const Parsed parsed = parse_source(
            "func main() int {\n"
            "  var p *i32 = 0;\n"
            "  var buf [32]u8;\n"
            "  var ps [2]*int;\n"
            "  return 0;\n"
            "}\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var p i32*");
        EXPECT_HAS(check, parsed.dump, "(var buf u8[32]");
        EXPECT_HAS(check, parsed.dump, "(var ps int*[2]");
    }

    {
        const Parsed parsed = parse_source("func main() int { var arr *[4]i32 = 0; return 0; }\n");
        EXPECT(check, parsed.errors.empty());
        EXPECT_HAS(check, parsed.dump, "(var arr i32[4] ptrarr");
    }

    {
        const Parsed parsed = parse_source("func main() int { var arr **[4]i32; return 0; }\n");
        EXPECT_HAS(check, parsed.errors, "not supported yet");
    }

    return check.failed;
}
