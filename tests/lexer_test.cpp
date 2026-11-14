#include "harness.hpp"
#include "lex/lexer.hpp"

#include <string>
#include <vector>

namespace {

std::string dump_tokens(const std::vector<tlc::Token> &tokens) {
    std::string out;
    for (const tlc::Token &token : tokens) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += tlc::token_kind_name(token.kind);
        if (token.kind == tlc::TokenKind::Identifier || token.kind == tlc::TokenKind::StringLiteral ||
            token.kind == tlc::TokenKind::FloatLiteral) {
            out.push_back('(');
            out += token.text;
            out.push_back(')');
        } else if (token.kind == tlc::TokenKind::IntLiteral) {
            out.push_back('(');
            out += std::to_string(token.int_value);
            if (token.suffix.bits != 0) {
                out.push_back(token.suffix.is_unsigned ? 'u' : 'i');
                out += std::to_string(token.suffix.bits);
            }
            out.push_back(')');
        } else if (token.kind == tlc::TokenKind::CharLiteral) {
            out.push_back('(');
            out += std::to_string(token.int_value);
            out.push_back(')');
        }
    }
    return out;
}

struct LexResult {
    std::string dump;
    std::string errors;
};

LexResult lex(const std::string &source) {
    tlc::SourceSet sources;
    tlc::SourceFile &file = sources.add("mem.tlc", source);
    tlc::DiagnosticEngine diags(sources);
    tlc::Lexer lexer(file, diags);
    LexResult result;
    result.dump = dump_tokens(lexer.tokenize());
    result.errors = diags.render();
    return result;
}

} // namespace

int run_lexer_tests() {
    tlc::test::Check check;

    {
        const LexResult result = lex(
            "func main() int {\n"
            "  // comment\n"
            "  return a->b + 1 <= 2 && c++ != d--;\n"
            "}\n");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump,
                  "func ident(main) ( ) ident(int) { return ident(a) -> ident(b) + int-lit(1) <= int-lit(2) && "
                  "ident(c) ++ != ident(d) -- ; } eof");
    }

    {
        const LexResult result = lex("0b1010 1'000 0xFFu32 010 20i8 400u16");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump,
                  "int-lit(10) int-lit(1000) int-lit(255u32) int-lit(8) int-lit(20i8) int-lit(400u16) eof");
    }

    {
        const LexResult result = lex("1u 1l 1i 1i80");
        EXPECT_HAS(check, result.errors, "invalid integer suffix");
    }

    {
        const LexResult result = lex("1.5f .5 1e-2 0x1p+4");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "float(1.5f) float(.5) float(1e-2) float(0x1p+4) eof");
    }

    {
        const LexResult result = lex(std::string("\"a\\n\" '\\n' \"\\x41\""));
        EXPECT(check, result.errors.empty());
        EXPECT(check, result.dump == "string(a\n) char(10) string(A) eof");
    }

    {
        const LexResult result = lex("\"abc\nint x;");
        EXPECT_HAS(check, result.errors, "unterminated string");
        EXPECT_HAS(check, result.dump, "ident(int) ident(x) ; eof");
    }

    {
        const LexResult result = lex("`ab\ncd` `a\\n\"`");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "string(ab\ncd) string(a\\n\") eof");
    }

    {
        const LexResult result = lex("`abc");
        EXPECT_HAS(check, result.errors, "unterminated string");
    }

    {
        const LexResult result = lex("/* hello");
        EXPECT_HAS(check, result.errors, "unterminated comment");
        EXPECT_EQ(check, result.dump, "eof");
    }

    {
        const LexResult result = lex("'");
        EXPECT_HAS(check, result.errors, "unterminated character constant");
    }

    {
        const LexResult result = lex("#include <stdio.h>\n#define X 1\nvar x int;\n");
        EXPECT_HAS(check, result.errors, "preprocessor directives are not supported");
        EXPECT(check, result.errors.find("preprocessor directives are not supported") !=
                          result.errors.rfind("preprocessor directives are not supported"));
        EXPECT_EQ(check, result.dump, "var ident(x) ident(int) ; eof");
    }

    {
        const LexResult result = lex("var as int = 1;\n"
"var from int = 2;\n"
"import export package\n");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump,
                  "var ident(as) ident(int) = int-lit(1) ; var ident(from) ident(int) = int-lit(2) ; import export package eof");
    }

    {
        const LexResult result = lex("int f(...); x <<= 1 >>= 2;");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "ident(int) ident(f) ( ... ) ; ident(x) <<= int-lit(1) >>= int-lit(2) ; eof");
    }

    {
        const LexResult result = lex("\\u0041");
        EXPECT_HAS(check, result.errors, "universal character names are not supported");
    }

    {
        const LexResult result = lex("Point::sum : a");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "ident(Point) :: ident(sum) : ident(a) eof");
    }

    {
        const LexResult result = lex("trait Shape impl Rect for Shape");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "trait ident(Shape) ident(impl) ident(Rect) for ident(Shape) eof");
    }

    {
        const LexResult result = lex("var a := b : c :: d\ncase 1:\n");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump,
                  "var ident(a) := ident(b) : ident(c) :: ident(d) case int-lit(1) : eof");
    }

    {
        // A character literal is one code point, written as one or more UTF-8 bytes.
        const LexResult result = lex("'A' '\xC3\xA5' '\xE2\x82\xAC' '\xF0\x9F\x98\x80'");
        EXPECT_EQ(check, result.errors, std::string());
        EXPECT_EQ(check, result.dump, std::string("char(65) char(229) char(8364) char(128512) eof"));
    }
    {
        // A hex escape past U+10FFFF stops growing. Seventeen digits do not wrap around to `'A'`.
        const LexResult result = lex("'\\x10000000000000041'");
        EXPECT_EQ(check, result.dump, std::string("char(1114112) eof"));
    }
    {
        // A lone continuation byte, a cut sequence, an overlong form, and a surrogate are not UTF-8.
        for (const char *bad : {"'\x80'", "'\xC3'", "'\xC0\x80'", "'\xED\xA0\x80'", "'\xF4\x90\x80\x80'"}) {
            EXPECT_HAS(check, lex(bad).errors, "unexpected character");
        }
    }
    {
        // After an unexpected character the comment, the whitespace, and the line break are not code.
        const LexResult result = lex("a @ // note\n  b \xC3\xA9 /* x */ c");
        EXPECT_EQ(check, result.errors,
                  std::string("mem.tlc:1:3: error: unexpected character\nmem.tlc:2:5: error: unexpected character\n"));
        EXPECT_EQ(check, result.dump, std::string("ident(a) ident(b) ident(c) eof"));
    }
    {
        // A stray `\` is an unexpected character. `\u` is still a universal character name.
        EXPECT_HAS(check, lex("a \\ b").errors, "unexpected character");
        EXPECT_HAS(check, lex("\\u00e5").errors, "universal character names are not supported");
    }
    {
        // `\` at the end of a line or of the file leaves the literal open. That is reported once.
        // CRLF is the same line end as LF.
        const std::string lf = lex("\"ab\\\nc").errors;
        EXPECT_EQ(check, lf, std::string("mem.tlc:1:1: error: unterminated string\n"));
        EXPECT_EQ(check, lex("\"ab\\\r\nc").errors, lf);
        EXPECT_EQ(check, lex("\"ab\\").errors, lf);
        const std::string ch = lex("'\\\nc").errors;
        EXPECT_EQ(check, ch, std::string("mem.tlc:1:1: error: unterminated character constant\n"));
        EXPECT_EQ(check, lex("'\\\r\nc").errors, ch);
        EXPECT_EQ(check, lex("\"ab\r\nc\"").errors, lex("\"ab\nc\"").errors);
    }
    return check.failed;
}
