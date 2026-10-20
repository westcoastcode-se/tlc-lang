#pragma once

#include "diag/diag.hpp"
#include "lex/token.hpp"

#include <vector>

namespace tlc {

class Lexer {
  public:
    Lexer(const SourceFile &file, DiagnosticEngine &diags);

    std::vector<Token> tokenize();

  private:
    const SourceFile &file_;
    DiagnosticEngine &diags_;
    uint32_t pos_ = 0;
    bool only_whitespace_since_newline_ = true;

    bool eof_at(uint32_t ahead = 0) const;
    char peek(uint32_t ahead = 0) const;
    char get();
    void error_at(uint32_t offset, const std::string &message);
    Token finish(TokenKind kind, uint32_t start) const;

    void skip_trivia();
    void skip_line_comment();
    void skip_block_comment(uint32_t start);
    void skip_directive(uint32_t start);
    Token lex_token();
    Token lex_identifier(uint32_t start);
    Token lex_number_or_float(uint32_t start);
    Token lex_float(uint32_t start);
    Token lex_string(uint32_t start);
    Token lex_raw_string(uint32_t start);
    Token lex_char(uint32_t start);
    bool looks_like_float(uint32_t start) const;
};

} // namespace tlc
