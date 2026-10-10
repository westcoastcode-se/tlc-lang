#include "driver.hpp"
#include "harness.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
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
    EXPECT_HAS(check, help.out, "TLC_LIB");

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

    const std::string tlc_extern = std::string(TLC_ROOT) + "/tests/programs/extern_tlc/main.tlc";
    const tlc::test::Proc tlc_ir = tlc::test::spawn({bin, "--emit-llvm", tlc_extern});
    EXPECT(check, tlc_ir.code == 0);
    EXPECT_HAS(check, tlc_ir.out, "@main.parts");
    EXPECT(check, tlc_ir.out.find("@parts(") == std::string::npos);

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
               "extern(abi=\"c\") func from_a() int;\n"
               "extern(abi=\"c\") func from_so() int;\n"
               "func main() int { return from_a() + from_so(); }\n");
    write_file("pick.tlc", "extern(abi=\"c\") func pick() int;\nfunc main() int { return pick(); }\n");

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

    struct EnvGuard {
        std::optional<std::string> old;
        explicit EnvGuard(const char *value) {
            if (const char *cur = std::getenv("TLC_LIB")) {
                old = cur;
            }
            if (value != nullptr) {
                ::setenv("TLC_LIB", value, 1);
            } else {
                ::unsetenv("TLC_LIB");
            }
        }
        ~EnvGuard() {
            if (old) {
                ::setenv("TLC_LIB", old->c_str(), 1);
            } else {
                ::unsetenv("TLC_LIB");
            }
        }
        EnvGuard(const EnvGuard &) = delete;
        EnvGuard &operator=(const EnvGuard &) = delete;
    };
    {
        EnvGuard cleared(nullptr);
        std::error_code ec;
        const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        EXPECT(check, !ec);
        const std::string expect = (exe.parent_path().parent_path() / "lib" / "tlc" / "vendor").string();
        EXPECT_EQ(check, tlc::vendor_library_root(), expect);
    }
    {
        EnvGuard forced("/tmp/tlc-vendor-root");
        EXPECT_EQ(check, tlc::vendor_library_root(), "/tmp/tlc-vendor-root");
    }

    TempDir vendor_root;
    vendor_root.path = std::filesystem::temp_directory_path() / ("tlc-vendor-" + std::to_string(getpid()));
    std::filesystem::create_directories(vendor_root.path / "answer");
    std::filesystem::create_directories(vendor_root.path / "onlyso");
    std::filesystem::create_directories(vendor_root.path / "combo");
    std::filesystem::create_directories(vendor_root.path / "bonus");
    const std::string vendor_text = vendor_root.path.string();
    auto write_at = [](const std::filesystem::path &path, const std::string &text) {
        std::ofstream out(path);
        out << text;
    };
    write_at(vendor_root.path / "answer/answer.c", "int answer(void) { return 20; }\n");
    write_at(vendor_root.path / "answer/answer_so.c", "int answer(void) { return 22; }\n");
    write_at(vendor_root.path / "answer/answer.tlc", "export extern(abi=\"c\") func answer() int;\n");
    write_at(vendor_root.path / "onlyso/onlyso.c", "int onlyso(void) { return 22; }\n");
    write_at(vendor_root.path / "onlyso/onlyso.tlc", "export extern(abi=\"c\") func onlyso() int;\n");
    write_at(vendor_root.path / "combo/base.c", "int base(void) { return 20; }\n");
    write_at(vendor_root.path / "combo/combo.tlc",
             "export extern(abi=\"c\") func base() int;\nexport extern(abi=\"c\") func bonus() int;\n");
    write_at(vendor_root.path / "combo/link", "../bonus/libbonus.a\n");
    write_at(vendor_root.path / "bonus/bonus.c", "int bonus(void) { return 22; }\n");

    const auto built = [&](const std::vector<std::string> &args) {
        const tlc::test::Proc proc = tlc::test::spawn(args);
        EXPECT(check, proc.code == 0);
        return proc.code == 0;
    };
    EXPECT(check, built({"cc", "-c", vendor_text + "/answer/answer.c", "-o", vendor_text + "/answer/answer.o"}));
    EXPECT(check, built({"ar", "rcs", vendor_text + "/answer/libanswer.a", vendor_text + "/answer/answer.o"}));
    EXPECT(check, built({"cc", "-shared", "-fPIC", vendor_text + "/answer/answer_so.c", "-o",
                         vendor_text + "/answer/libanswer.so"}));
    EXPECT(check, built({"cc", "-shared", "-fPIC", vendor_text + "/onlyso/onlyso.c", "-o",
                         vendor_text + "/onlyso/libonlyso.so"}));
    EXPECT(check, built({"cc", "-c", vendor_text + "/combo/base.c", "-o", vendor_text + "/combo/base.o"}));
    EXPECT(check, built({"ar", "rcs", vendor_text + "/combo/libbase.a", vendor_text + "/combo/base.o"}));
    EXPECT(check, built({"cc", "-c", vendor_text + "/bonus/bonus.c", "-o", vendor_text + "/bonus/bonus.o"}));
    EXPECT(check, built({"ar", "rcs", vendor_text + "/bonus/libbonus.a", vendor_text + "/bonus/bonus.o"}));

    TempDir progs;
    progs.path = std::filesystem::temp_directory_path() / ("tlc-vendor-prog-" + std::to_string(getpid()));
    std::filesystem::create_directories(progs.path);
    write_at(progs.path / "answer.tlc", "import \"vendor/answer\";\nfunc main() int { return answer.answer(); }\n");
    write_at(progs.path / "onlyso.tlc", "import \"vendor/onlyso\";\nfunc main() int { return onlyso.onlyso(); }\n");
    write_at(progs.path / "combo.tlc",
             "import \"vendor/combo\";\nfunc main() int { return combo.base() + combo.bonus(); }\n");
    write_at(progs.path / "check.tlc", "import \"vendor/answer\";\nfunc main() int { return 0; }\n");
    const std::vector<std::pair<std::string, std::string>> env = {{"TLC_LIB", vendor_text}};

    const tlc::test::Proc answer_run =
        tlc::test::spawn({bin, "--run", (progs.path / "answer.tlc").string()}, env);
    EXPECT(check, answer_run.code == 20);
    EXPECT(check, answer_run.err.empty());

    const tlc::test::Proc only_run = tlc::test::spawn({bin, "--run", (progs.path / "onlyso.tlc").string()}, env);
    EXPECT(check, only_run.code == 22);
    EXPECT(check, only_run.err.empty());

    const tlc::test::Proc combo_run = tlc::test::spawn({bin, "--run", (progs.path / "combo.tlc").string()}, env);
    EXPECT(check, combo_run.code == 42);
    EXPECT(check, combo_run.err.empty());

    const tlc::test::Proc answer_check =
        tlc::test::spawn({bin, "--check", (progs.path / "check.tlc").string()}, env);
    EXPECT(check, answer_check.code == 0);
    EXPECT(check, answer_check.err.empty());

    TempDir shadow;
    shadow.path = std::filesystem::temp_directory_path() / ("tlc-vendor-shadow-" + std::to_string(getpid()));
    std::filesystem::create_directories(shadow.path / "vendor" / "answer");
    write_at(shadow.path / "vendor/answer/answer.tlc", "export func answer() int { return 5; }\n");
    write_at(shadow.path / "main.tlc", "import \"vendor/answer\";\nfunc main() int { return answer.answer(); }\n");
    const tlc::test::Proc shadow_run = tlc::test::spawn({bin, "--run", (shadow.path / "main.tlc").string()}, env);
    EXPECT(check, shadow_run.code == 5);
    EXPECT(check, shadow_run.err.empty());

    TempDir plain;
    plain.path = std::filesystem::temp_directory_path() / ("tlc-vendor-plain-" + std::to_string(getpid()));
    std::filesystem::create_directories(plain.path / "side");
    write_at(plain.path / "side/side.c", "int side(void) { return 3; }\n");
    write_at(plain.path / "side/side.tlc", "export extern(abi=\"c\") func side() int;\n");
    write_at(plain.path / "main.tlc", "import \"side\";\nfunc main() int { return side.side(); }\n");
    EXPECT(check, built({"cc", "-c", (plain.path / "side/side.c").string(), "-o", (plain.path / "side/side.o").string()}));
    EXPECT(check, built({"ar", "rcs", (plain.path / "side/libside.a").string(), (plain.path / "side/side.o").string()}));
    const tlc::test::Proc plain_run = tlc::test::spawn({bin, "--run", (plain.path / "main.tlc").string()}, env);
    EXPECT(check, plain_run.code != 0);

    return check.failed;
}
