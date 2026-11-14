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
    // The newline belongs to the line it ends. An empty file and a trailing newline have a last line.
    EXPECT(check, file.line_col(1).line == 1 && file.line_col(1).column == 2);
    EXPECT(check, file.line_col(2).line == 2 && file.line_col(2).column == 1);
    const tlc::SourceFile &empty = sources.add("empty", "");
    EXPECT(check, empty.line_col(0).line == 1 && empty.line_col(0).column == 1);
    const tlc::SourceFile &ends = sources.add("ends", "x\n\n");
    EXPECT(check, ends.line_col(3).line == 3 && ends.line_col(3).column == 1);

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
