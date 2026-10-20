#include "diag/diag.hpp"
#include "harness.hpp"

int run_diag_tests() {
    tlc::test::Check check;

    tlc::SourceSet sources;
    tlc::SourceFile &file = sources.add("mem", "a\nabc");
    EXPECT(check, file.line_col(0).line == 1);
    EXPECT(check, file.line_col(0).column == 1);
    EXPECT(check, file.line_col(4).line == 2);
    EXPECT(check, file.line_col(4).column == 3);
    EXPECT(check, file.line_col(100).line == 2);
    EXPECT(check, file.line_col(100).column == 4);

    tlc::DiagnosticEngine diags(sources);
    diags.error(tlc::SourceLoc{file.id(), 0}, "first");
    diags.error(tlc::SourceLoc{file.id(), 4}, "second");
    EXPECT(check, !diags.ok());
    EXPECT(check, diags.size() == 2);
    EXPECT_EQ(check, diags.render(), "mem:1:1: error: first\nmem:2:3: error: second\n");

    diags.error(tlc::SourceLoc{}, "no location");
    EXPECT_EQ(check, diags.items()[2].message, "no location");
    EXPECT_HAS(check, diags.render(), "error: no location\n");

    return check.failed;
}
