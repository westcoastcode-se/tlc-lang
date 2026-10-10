#include "harness.hpp"
#include "lexer.hpp"

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
            if (token.suffix.is_unsigned) {
                out.push_back('u');
            }
            for (int i = 0; i < token.suffix.long_count; ++i) {
                out.push_back('l');
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
                  "func ident(main) ( ) int { return ident(a) -> ident(b) + int-lit(1) <= int-lit(2) && "
                  "ident(c) ++ != ident(d) -- ; } eof");
    }

    {
        const LexResult result = lex("0b1010 1'000 0xFFu 010");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "int-lit(10) int-lit(1000) int-lit(255u) int-lit(8) eof");
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
        EXPECT_HAS(check, result.dump, "int ident(x) ; eof");
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
        const LexResult result = lex("#include <stdio.h>\n#define X 1\nint x;\n");
        EXPECT_HAS(check, result.errors, "preprocessor directives are not supported");
        EXPECT(check, result.errors.find("preprocessor directives are not supported") !=
                          result.errors.rfind("preprocessor directives are not supported"));
        EXPECT_EQ(check, result.dump, "int ident(x) ; eof");
    }

    {
        const LexResult result = lex("int as = 1;\nint from = 2;\nimport export package\n");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump,
                  "int ident(as) = int-lit(1) ; int ident(from) = int-lit(2) ; import export package eof");
    }

    {
        const LexResult result = lex("int f(...); x <<= 1 >>= 2;");
        EXPECT(check, result.errors.empty());
        EXPECT_EQ(check, result.dump, "int ident(f) ( ... ) ; ident(x) <<= int-lit(1) >>= int-lit(2) ; eof");
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

    return check.failed;
}
