#include "harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#ifndef TLC_BIN
#error "TLC_BIN must point at the tlc executable"
#endif

int run_cli_tests() {
    tlc::test::Check check;
    const std::string bin = TLC_BIN;

    const tlc::test::Proc version = tlc::test::spawn({bin, "--version"});
    EXPECT(check, version.code == 0);
    EXPECT_HAS(check, version.out, "tlc 0.1.0");
    EXPECT_HAS(check, version.out, "23.1.1");
    EXPECT(check, version.err.empty());

    const tlc::test::Proc none = tlc::test::spawn({bin});
    EXPECT(check, none.code != 0);
    EXPECT_HAS(check, none.err, "usage: tlc");
    EXPECT(check, none.out.empty());

    const tlc::test::Proc help = tlc::test::spawn({bin, "--help"});
    EXPECT(check, help.code == 0);
    EXPECT_HAS(check, help.out, "usage: tlc");
    EXPECT_HAS(check, help.out, "--emit-llvm");
    EXPECT_HAS(check, help.out, "-P <dir>");
    EXPECT_HAS(check, help.out, "-l <lib>");
    EXPECT_HAS(check, help.out, "-Bstatic");
    EXPECT_HAS(check, help.out, "-Bdynamic");

    const tlc::test::Proc unknown = tlc::test::spawn({bin, "--nope"});
    EXPECT(check, unknown.code != 0);
    EXPECT_HAS(check, unknown.err, "unknown option");

#ifndef TLC_ROOT
#error "TLC_ROOT must point at the project root"
#endif
    const std::string ret_const = std::string(TLC_ROOT) + "/tests/programs/ret_const/main.tlc";
    const tlc::test::Proc ran = tlc::test::spawn({bin, "--run", ret_const});
    EXPECT(check, ran.code == 42);
    EXPECT(check, ran.err.empty());

    const tlc::test::Proc ir = tlc::test::spawn({bin, "--emit-llvm", ret_const});
    EXPECT(check, ir.code == 0);
    EXPECT_HAS(check, ir.out, "define i32 @main");

    const std::string import_qual = std::string(TLC_ROOT) + "/tests/programs/import_qual/main.tlc";
    const tlc::test::Proc pkg_ir = tlc::test::spawn({bin, "--emit-llvm", import_qual});
    EXPECT(check, pkg_ir.code == 0);
    EXPECT_HAS(check, pkg_ir.out, "define i32 @main");
    EXPECT_HAS(check, pkg_ir.out, "math.add");

    const std::string hello = std::string(TLC_ROOT) + "/tests/programs/hello_printf/main.tlc";
    const tlc::test::Proc hello_ir = tlc::test::spawn({bin, "--emit-llvm", hello});
    EXPECT(check, hello_ir.code == 0);
    EXPECT_HAS(check, hello_ir.out, "declare i32 @printf(ptr, ...)");
    EXPECT_HAS(check, hello_ir.out, "@slot = external global i32");
    EXPECT(check, hello_ir.out.find("@slot = global") == std::string::npos);
    EXPECT(check, hello_ir.out.find("define i32 @printf") == std::string::npos);

    const tlc::test::Proc missing_lib = tlc::test::spawn({bin, "-l"});
    EXPECT(check, missing_lib.code != 0);
    EXPECT_HAS(check, missing_lib.err, "missing library after -l");

    const tlc::test::Proc missing_dir = tlc::test::spawn({bin, "-L"});
    EXPECT(check, missing_dir.code != 0);
    EXPECT_HAS(check, missing_dir.err, "missing directory after -L");

    const tlc::test::Proc check_with_lib = tlc::test::spawn({bin, "--check", ret_const, "-lm"});
    EXPECT(check, check_with_lib.code == 0);
    EXPECT(check, check_with_lib.err.empty());

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
    dir.path = std::filesystem::temp_directory_path() / ("tlc-link-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir.path);
    auto write_file = [&](const std::string &name, const std::string &text) {
        std::ofstream out(dir.path / name);
        out << text;
    };
    write_file("from_a.c", "int from_a(void) { return 20; }\n");
    write_file("from_so.c", "int from_so(void) { return 22; }\n");
    write_file("pick_a.c", "int pick(void) { return 7; }\n");
    write_file("pick_so.c", "int pick(void) { return 9; }\n");
    write_file("main.tlc",
               "extern func from_a() int;\n"
               "extern func from_so() int;\n"
               "func main() int { return from_a() + from_so(); }\n");
    write_file("pick.tlc", "extern func pick() int;\nfunc main() int { return pick(); }\n");

    const std::string dir_text = dir.path.string();
    auto cc = [&](const std::vector<std::string> &args) {
        return tlc::test::spawn(args);
    };
    const tlc::test::Proc from_a_obj = cc({"cc", "-c", dir_text + "/from_a.c", "-o", dir_text + "/from_a.o"});
    EXPECT(check, from_a_obj.code == 0);
    const tlc::test::Proc from_a_lib = cc({"ar", "rcs", dir_text + "/libfrom_a.a", dir_text + "/from_a.o"});
    EXPECT(check, from_a_lib.code == 0);
    const tlc::test::Proc from_so = cc({"cc", "-shared", "-fPIC", dir_text + "/from_so.c", "-o", dir_text + "/libfrom_so.so"});
    EXPECT(check, from_so.code == 0);
    const tlc::test::Proc pick_a_obj = cc({"cc", "-c", dir_text + "/pick_a.c", "-o", dir_text + "/pick_a.o"});
    EXPECT(check, pick_a_obj.code == 0);
    const tlc::test::Proc pick_a_lib = cc({"ar", "rcs", dir_text + "/libpick.a", dir_text + "/pick_a.o"});
    EXPECT(check, pick_a_lib.code == 0);
    const tlc::test::Proc pick_so = cc({"cc", "-shared", "-fPIC", dir_text + "/pick_so.c", "-o", dir_text + "/libpick.so"});
    EXPECT(check, pick_so.code == 0);

    const tlc::test::Proc mixed = tlc::test::spawn({bin, "--run", dir_text + "/main.tlc", dir_text + "/libfrom_a.a", "-L",
                                                    dir_text, "-Wl,-rpath," + dir_text, "-lfrom_so"});
    EXPECT(check, mixed.code == 42);
    EXPECT(check, mixed.err.empty());

    const tlc::test::Proc static_pick =
        tlc::test::spawn({bin, "--run", dir_text + "/pick.tlc", "-L", dir_text, "-Bstatic", "-lpick", "-Bdynamic"});
    EXPECT(check, static_pick.code == 7);
    EXPECT(check, static_pick.err.empty());

    const tlc::test::Proc shared_pick = tlc::test::spawn(
        {bin, "--run", dir_text + "/pick.tlc", "-L", dir_text, "-Wl,-rpath," + dir_text, "-Bdynamic", "-lpick"});
    EXPECT(check, shared_pick.code == 9);
    EXPECT(check, shared_pick.err.empty());

    return check.failed;
}
