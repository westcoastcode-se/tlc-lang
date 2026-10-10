#include "harness.hpp"

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

    return check.failed;
}
