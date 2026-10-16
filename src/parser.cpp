// Parser entry: tokens, imports, and the file walk.
#include "parser.hpp"
#include "parse_util.hpp"

#include <utility>

namespace tlc {

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine &diags)
    : tokens_(std::move(tokens)), diags_(diags) {
    if (tokens_.empty() || tokens_.back().kind != TokenKind::End) {
        Token end;
        end.kind = TokenKind::End;
        if (!tokens_.empty()) {
            const Token &last = tokens_.back();
            end.loc = last.loc;
            end.loc.offset += last.length;
        }
        tokens_.push_back(std::move(end));
    }
}

bool Parser::eof() const { return peek().kind == TokenKind::End; }

const Token &Parser::peek(std::size_t ahead) const {
    const std::size_t index = pos_ + ahead;
    if (index >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[index];
}

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::eat(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    ++pos_;
    return true;
}

void Parser::error_at(const Token &token, const std::string &message) { diags_.error(token.loc, message); }

void Parser::error_here(const std::string &message) { error_at(peek(), message); }

bool Parser::expect(TokenKind kind, const std::string &message) {
    if (eat(kind)) {
        return true;
    }
    error_here(message);
    return false;
}

bool Parser::at_contextual(std::string_view spelling) const {
    return peek().kind == TokenKind::Identifier && peek().text == spelling;
}

bool Parser::is_type_keyword(TokenKind kind) const {
    switch (kind) {
    case TokenKind::KwInt:
    case TokenKind::KwVoid:
    case TokenKind::KwChar:
    case TokenKind::KwShort:
    case TokenKind::KwLong:
    case TokenKind::KwFloat:
    case TokenKind::KwDouble:
    case TokenKind::KwBool:
    case TokenKind::KwUBool:
    case TokenKind::KwStruct:
    case TokenKind::KwEnum:
    case TokenKind::KwUnion:
    case TokenKind::KwConst:
    case TokenKind::KwVolatile:
    case TokenKind::KwRestrict:
    case TokenKind::KwAuto:
    case TokenKind::KwTypeofUnqual:
        return true;
    default:
        return false;
    }
}

bool Parser::starts_qualified_type() const {
    return peek().kind == TokenKind::Identifier && peek(1).kind == TokenKind::Dot &&
           peek(2).kind == TokenKind::Identifier &&
           (peek(3).kind == TokenKind::Star || peek(3).kind == TokenKind::Identifier);
}

bool Parser::continues_declarator() const {
    return check(TokenKind::Identifier) || check(TokenKind::Star) ||
           (check(TokenKind::LParen) && peek(1).kind == TokenKind::Star);
}

void Parser::skip_decl_specifiers(std::size_t &ahead, bool &saw_unsafe) const {
    while (true) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::End) {
            return;
        }
        if (kind == TokenKind::KwExport) {
            ++ahead;
            continue;
        }
        if (kind == TokenKind::KwUnsafe) {
            saw_unsafe = true;
            ++ahead;
            continue;
        }
        if (kind == TokenKind::KwExtern) {
            ++ahead;
            if (peek(ahead).kind != TokenKind::LParen) {
                continue;
            }
            int depth = 0;
            while (peek(ahead).kind != TokenKind::End) {
                if (peek(ahead).kind == TokenKind::LParen) {
                    ++depth;
                } else if (peek(ahead).kind == TokenKind::RParen) {
                    --depth;
                }
                ++ahead;
                if (depth == 0) {
                    break;
                }
            }
            continue;
        }
        return;
    }
}

bool Parser::starts_unsafe_func() const {
    std::size_t ahead = 0;
    bool saw_unsafe = false;
    skip_decl_specifiers(ahead, saw_unsafe);
    return saw_unsafe && peek(ahead).kind == TokenKind::KwFunc;
}

bool Parser::unsupported_unsafe_decl() const {
    std::size_t ahead = 0;
    bool saw_unsafe = false;
    skip_decl_specifiers(ahead, saw_unsafe);
    if (!saw_unsafe || peek(ahead).kind == TokenKind::KwFunc) {
        return false;
    }
    const TokenKind next = peek(ahead).kind;
    return next == TokenKind::KwVar || next == TokenKind::KwConst || next == TokenKind::KwType ||
           next == TokenKind::KwStruct || next == TokenKind::KwEnum || next == TokenKind::KwUnion ||
           next == TokenKind::KwTrait || is_type_keyword(next);
}

void Parser::reject_unsupported_unsafe() {
    diags_.error(peek().loc, "not supported yet");
    int depth = 0;
    while (!eof()) {
        if (check(TokenKind::LBrace)) {
            ++depth;
            ++pos_;
            continue;
        }
        if (check(TokenKind::RBrace)) {
            if (depth == 0) {
                break;
            }
            --depth;
            ++pos_;
            continue;
        }
        if (depth == 0 && check(TokenKind::Semicolon)) {
            ++pos_;
            break;
        }
        ++pos_;
    }
}

bool Parser::starts_declaration() const {
    if (starts_unsafe_func() || unsupported_unsafe_decl()) {
        return true;
    }
    const TokenKind kind = peek().kind;
    const bool typedef_name = kind == TokenKind::Identifier && peek(1).kind != TokenKind::Dot &&
                              (peek(1).kind == TokenKind::Star || peek(1).kind == TokenKind::Identifier);
    return kind == TokenKind::KwExport || kind == TokenKind::KwExtern ||
           kind == TokenKind::KwFunc || kind == TokenKind::KwType || is_type_keyword(kind) ||
           starts_qualified_type() || typedef_name;
}


void Parser::parse_import(TranslationUnit &unit) {
    ImportDecl import;
    import.loc = peek().loc;
    eat(TokenKind::KwImport);
    if (saw_decl_) {
        diags_.error(import.loc, "import must appear before declarations");
    }
    if (check(TokenKind::StringLiteral)) {
        import.path = peek().text;
        ++pos_;
        if (at_contextual("as")) {
            ++pos_;
            import.form = ImportDecl::Form::Alias;
            if (check(TokenKind::Identifier)) {
                import.alias = peek().text;
                ++pos_;
            } else {
                error_here("expected an identifier");
            }
        }
    } else if (eat(TokenKind::LBrace)) {
        import.form = ImportDecl::Form::Selective;
        if (!check(TokenKind::RBrace)) {
            while (!check(TokenKind::RBrace) && !eof()) {
                if (check(TokenKind::Identifier)) {
                    import.names.push_back(peek().text);
                    ++pos_;
                } else {
                    error_here("expected an identifier");
                    break;
                }
                if (!eat(TokenKind::Comma)) {
                    break;
                }
            }
        }
        expect(TokenKind::RBrace, "expected '}'");
        if (at_contextual("from")) {
            ++pos_;
        } else {
            error_here("expected 'from'");
        }
        if (check(TokenKind::StringLiteral)) {
            import.path = peek().text;
            ++pos_;
        } else {
            error_here("expected a string literal");
        }
    } else {
        error_here("expected a string literal");
    }
    expect(TokenKind::Semicolon, "expected ';'");
    unit.imports.push_back(std::move(import));
}

TranslationUnit Parser::parse() {
    TranslationUnit unit;
    while (!eof()) {
        if (!saw_decl_ && check(TokenKind::KwPackage)) {
            if (unit.package) {
                error_here("package must appear before declarations");
            }
            PackageClause package;
            package.loc = peek().loc;
            ++pos_;
            if (check(TokenKind::Identifier)) {
                package.name = peek().text;
                ++pos_;
            } else {
                error_here("expected an identifier");
            }
            expect(TokenKind::Semicolon, "expected ';'");
            if (!unit.package && unit.imports.empty() && unit.decls.empty()) {
                unit.package = std::move(package);
            }
            continue;
        }
        if (check(TokenKind::KwPackage)) {
            error_here("package must appear before declarations");
            ++pos_;
            while (!check(TokenKind::Semicolon) && !eof()) {
                ++pos_;
            }
            eat(TokenKind::Semicolon);
            continue;
        }
        if (check(TokenKind::KwImport)) {
            parse_import(unit);
            continue;
        }
        if (check(TokenKind::KwVar) ||
            (check(TokenKind::Identifier) && peek(1).kind == TokenKind::ColonEq)) {
            parse_top_declaration(unit, true);
            continue;
        }
        if (starts_declaration()) {
            parse_top_declaration(unit, true);
            continue;
        }
        if (check(TokenKind::KwAssert)) {
            TopDecl top;
            top.data = parse_assert();
            unit.decls.push_back(std::move(top));
            saw_decl_ = true;
            continue;
        }
        if (check(TokenKind::KwTest)) {
            TopDecl top;
            top.data = parse_test();
            unit.decls.push_back(std::move(top));
            saw_decl_ = true;
            continue;
        }
        error_here("expected a declaration");
        ++pos_;
    }
    return unit;
}

} // namespace tlc
