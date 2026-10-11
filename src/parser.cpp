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
    case TokenKind::KwTypeof:
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

bool Parser::starts_declaration() const {
    const TokenKind kind = peek().kind;
    const bool typedef_name = kind == TokenKind::Identifier && peek(1).kind != TokenKind::Dot &&
                              (peek(1).kind == TokenKind::Star || peek(1).kind == TokenKind::Identifier);
    return kind == TokenKind::KwExport || kind == TokenKind::KwStatic || kind == TokenKind::KwExtern ||
           kind == TokenKind::KwFunc || kind == TokenKind::KwType || is_type_keyword(kind) ||
           starts_qualified_type() || typedef_name;
}


void Parser::parse_trait(TranslationUnit &unit) {
    TraitDecl trait;
    trait.loc = peek().loc;
    eat(TokenKind::KwTrait);
    if (check(TokenKind::Identifier)) {
        trait.name = peek().text;
        trait.loc = peek().loc;
        ++pos_;
    } else {
        error_here("expected an identifier");
    }
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !eof()) {
        const std::size_t before = pos_;
        if (!check(TokenKind::KwFunc)) {
            error_here("expected a declaration");
            ++pos_;
            continue;
        }
        FunctionDecl method = parse_function(false, false, false, false, false);
        if (!method.method_struct.empty()) {
            diags_.error(method.loc, "type mismatch");
        }
        trait.methods.push_back(std::move(method));
        if (pos_ == before) {
            error_here("expected a declaration");
            ++pos_;
        }
    }
    expect(TokenKind::RBrace, "expected '}'");
    expect(TokenKind::Semicolon, "expected ';'");
    TopDecl top;
    top.data = std::move(trait);
    unit.decls.push_back(std::move(top));
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
        if (check(TokenKind::KwTrait)) {
            parse_trait(unit);
            saw_decl_ = true;
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
        error_here("expected a declaration");
        ++pos_;
    }
    return unit;
}

} // namespace tlc
