#include "harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#ifndef TLC_BIN
#error "TLC_BIN must point at the tlc executable"
#endif

int run_abi_tests() {
    tlc::test::Check check;
    const std::string bin = TLC_BIN;

    struct TempDir {
        std::filesystem::path path;
        ~TempDir() {
            if (!path.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        }
    };
    TempDir dir;
    dir.path = std::filesystem::temp_directory_path() / ("tlc-abi-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir.path);
    auto write_file = [&](const std::string &name, const std::string &text) {
        std::ofstream out(dir.path / name);
        out << text;
    };
    write_file("abi.c",
               "struct Pair { int a; int b; };\n"
               "struct Pair make_pair(int a, int b) { struct Pair p; p.a = a; p.b = b; return p; }\n"
               "int sum_pair(struct Pair p) { return p.a + p.b; }\n"
               "struct Tri { int a; int b; int c; };\n"
               "struct Tri make_tri(int a, int b, int c) {\n"
               "    struct Tri t; t.a = a; t.b = b; t.c = c; return t;\n"
               "}\n"
               "int sum_tri(struct Tri t) { return t.a + t.b + t.c; }\n"
               "struct Big { int a, b, c, d, e, f, g, h; };\n"
               "struct Big make_big(int n) {\n"
               "    struct Big b;\n"
               "    b.a = n; b.b = n + 1; b.c = n + 2; b.d = n + 3;\n"
               "    b.e = n + 4; b.f = n + 5; b.g = n + 6; b.h = n + 7;\n"
               "    return b;\n"
               "}\n"
               "int sum_big(struct Big b) { return b.a + b.b + b.c + b.d + b.e + b.f + b.g + b.h; }\n"
               "struct Mix { float a; int b; };\n"
               "struct Mix make_mix(float a, int b) { struct Mix m; m.a = a; m.b = b; return m; }\n"
               "int mix_b(struct Mix m) { return m.b; }\n"
               "struct One { int a; };\n"
               "struct One make_one(int a) { struct One o; o.a = a; return o; }\n"
               "int one_a(struct One o) { return o.a; }\n"
               "void fill_pair(struct Pair *p, int a, int b) { p->a = a; p->b = b; }\n"
               "int add_pair(struct Pair p);\n"
               "struct Pair make_from_tlc(int a, int b);\n"
               "int call_add(void) { struct Pair p; p.a = 20; p.b = 22; return add_pair(p); }\n"
               "int call_make(void) { struct Pair p = make_from_tlc(20, 22); return p.a + p.b; }\n"
               "struct F2 { float a; float b; };\n"
               "struct F2 make_f2(float a, float b) { struct F2 f; f.a = a; f.b = b; return f; }\n"
               "float sum_f2(struct F2 f) { return f.a + f.b; }\n"
               "#include <stdbool.h>\n"
               "struct Flag { int a; bool b; int c; };\n"
               "struct Flag make_flag(int a, int b, int c) {\n"
               "    struct Flag f; f.a = a; f.b = b; f.c = c; return f;\n"
               "}\n"
               "int flag_c(struct Flag f) { return f.c; }\n"
               "int flag_b(struct Flag f) { return f.b ? 1 : 0; }\n"
               "union Num { int n; int m; };\n"
               "union Num make_num(int n) { union Num u; u.n = n; return u; }\n"
               "int num_m(union Num u) { return u.m; }\n"
               "int read_num(union Num u);\n"
               "int call_read(void) { union Num u; u.n = 40; return read_num(u); }\n"
               "union Fl { float f; int n; };\n"
               "union Fl make_fl(float f) { union Fl u; u.f = f; return u; }\n"
               "int fl_n(union Fl u) { return u.n; }\n"
               "union BigU { char raw[24]; int n; };\n"
               "union BigU make_bigu(int n) { union BigU u; u.n = n; return u; }\n"
               "int bigu_n(union BigU u) { return u.n; }\n"
               "union Hold { struct Pair p; int n; };\n"
               "int hold_a(union Hold h) { return h.p.a; }\n");
    write_file("main.tlc",
               "extern(abi=\"c\") struct Pair { a int; b int; };\n"
               "extern(abi=\"c\") struct Tri { a int; b int; c int; };\n"
               "extern(abi=\"c\") struct Big { a int; b int; c int; d int; e int; f int; g int; h int; };\n"
               "extern(abi=\"c\") struct Mix { a float; b int; };\n"
               "extern(abi=\"c\") struct One { a int; };\n"
               "extern(abi=\"c\") struct F2 { a float; b float; };\n"
               "extern(abi=\"c\") struct Flag { a int; b bool; c int; };\n"
               "extern(abi=\"c\") func make_pair(a int, b int) Pair;\n"
               "extern(abi=\"c\") func sum_pair(p Pair) int;\n"
               "extern(abi=\"c\") func make_tri(a int, b int, c int) Tri;\n"
               "extern(abi=\"c\") func sum_tri(t Tri) int;\n"
               "extern(abi=\"c\") func make_big(n int) Big;\n"
               "extern(abi=\"c\") func sum_big(b Big) int;\n"
               "extern(abi=\"c\") func make_mix(a f32, b int) Mix;\n"
               "extern(abi=\"c\") func mix_b(m Mix) int;\n"
               "extern(abi=\"c\") func make_one(a int) One;\n"
               "extern(abi=\"c\") func one_a(o One) int;\n"
               "extern(abi=\"c\") func fill_pair(p *Pair, a int, b int) void;\n"
               "extern(abi=\"c\") func call_add() int;\n"
               "extern(abi=\"c\") func call_make() int;\n"
               "extern(abi=\"c\") func make_f2(a f32, b f32) F2;\n"
               "extern(abi=\"c\") func sum_f2(f F2) f32;\n"
               "extern(abi=\"c\") func make_flag(a int, b int, c int) Flag;\n"
               "extern(abi=\"c\") func flag_c(f Flag) int;\n"
               "extern(abi=\"c\") func flag_b(f Flag) int;\n"
               "extern(abi=\"c\") union Num { n int; m int; }\n"
               "extern(abi=\"c\") func make_num(n int) Num;\n"
               "extern(abi=\"c\") func num_m(u Num) int;\n"
               "extern(abi=\"c\") func read_num(u Num) int { return u.m; }\n"
               "extern(abi=\"c\") func call_read() int;\n"
               "extern(abi=\"c\") union Fl { f float; n int; }\n"
               "extern(abi=\"c\") func make_fl(f f32) Fl;\n"
               "extern(abi=\"c\") func fl_n(u Fl) int;\n"
               "extern(abi=\"c\") union BigU { raw [24]char; n int; }\n"
               "extern(abi=\"c\") func make_bigu(n int) BigU;\n"
               "extern(abi=\"c\") func bigu_n(u BigU) int;\n"
               "extern(abi=\"c\") union Hold { p Pair; n int; }\n"
               "extern(abi=\"c\") func hold_a(h Hold) int;\n"
               "extern(abi=\"c\") func add_pair(p Pair) int { return p.a + p.b; }\n"
               "extern(abi=\"c\") func make_from_tlc(a int, b int) Pair {\n"
               "    var p Pair;\n"
               "    p.a = a;\n"
               "    p.b = b;\n"
               "    return p;\n"
               "}\n"
               "func main() int {\n"
               "    var p Pair = make_pair(20, 22);\n"
               "    if (p.a != 20 || p.b != 22 || sum_pair(p) != 42) return 1;\n"
               "    if (sum_pair(make_pair(10, 32)) != 42) return 2;\n"
               "    var t Tri = make_tri(20, 10, 12);\n"
               "    if (t.c != 12 || sum_tri(t) != 42) return 3;\n"
               "    var b Big = make_big(1);\n"
               "    if (b.h != 8 || sum_big(b) != 36) return 4;\n"
               "    var m Mix = make_mix(2.5f, 7);\n"
               "    if (m.a != 2.5f || m.b != 7 || mix_b(m) != 7) return 5;\n"
               "    var o One = make_one(42);\n"
               "    if (o.a != 42 || one_a(o) != 42) return 6;\n"
               "    var q Pair;\n"
               "    fill_pair(&q, 10, 5);\n"
               "    if (q.a != 10 || q.b != 5) return 7;\n"
               "    if (call_add() != 42 || call_make() != 42) return 8;\n"
               "    var f F2 = make_f2(1.5f, 2.5f);\n"
               "    if (f.a != 1.5f || f.b != 2.5f || sum_f2(f) != 4.0f) return 9;\n"
               "    var g Flag = make_flag(1, 1, 42);\n"
               "    if (g.a != 1 || g.b != 1 || g.c != 42 || flag_c(g) != 42 || flag_b(g) != 1) return 10;\n"
               "    var num Num = make_num(42);\n"
               "    if (num.n != 42 || num.m != 42 || num_m(num) != 42 || call_read() != 40) return 11;\n"
               "    var fl Fl = make_fl(1.0f);\n"
               "    if (fl.f != 1.0f || fl_n(fl) != 1065353216) return 12;\n"
               "    var bigu BigU = make_bigu(42);\n"
               "    if (bigu.n != 42 || bigu_n(bigu) != 42) return 13;\n"
               "    var hold Hold;\n"
               "    hold.p.a = 20;\n"
               "    hold.p.b = 22;\n"
               "    if (hold.n != 20 || hold_a(hold) != 20) return 14;\n"
               "    return 42;\n"
               "}\n");

    const std::string dir_text = dir.path.string();
    const tlc::test::Proc object =
        tlc::test::spawn({"cc", "-c", dir_text + "/abi.c", "-o", dir_text + "/abi.o"});
    EXPECT(check, object.code == 0);
    const tlc::test::Proc archived =
        tlc::test::spawn({"ar", "rcs", dir_text + "/libabi.a", dir_text + "/abi.o"});
    EXPECT(check, archived.code == 0);
    const tlc::test::Proc ran =
        tlc::test::spawn({bin, "--run", dir_text + "/main.tlc", dir_text + "/libabi.a"});
    EXPECT(check, ran.code == 42);
    EXPECT(check, ran.err.empty());
    return check.failed;
}
