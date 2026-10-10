#include "parser.hpp"

#include <utility>

namespace tlc {
namespace {

template <typename Node>
std::unique_ptr<Expr> make_expr(Node node) {
    auto expr = std::make_unique<Expr>();
    expr->data = std::move(node);
    return expr;
}

template <typename Node>
std::unique_ptr<Stmt> make_stmt(Node node) {
    auto stmt = std::make_unique<Stmt>();
    stmt->data = std::move(node);
    return stmt;
}

std::unique_ptr<Expr> unsupported_expr(SourceLoc loc) {
    Expr::Unsupported node;
    node.loc = loc;
    return make_expr(std::move(node));
}

bool is_int_spec(TokenKind kind) {
    switch (kind) {
    case TokenKind::KwInt:
    case TokenKind::KwChar:
    case TokenKind::KwShort:
    case TokenKind::KwLong:
    case TokenKind::KwSigned:
    case TokenKind::KwUnsigned:
        return true;
    default:
        return false;
    }
}

struct IntSpecs {
    bool saw_signed = false;
    bool saw_unsigned = false;
    bool saw_short = false;
    bool saw_char = false;
    bool saw_int = false;
    int longs = 0;
    bool any = false;
    bool bad = false;
};

void take_int_spec(IntSpecs &specs, TokenKind kind) {
    specs.any = true;
    switch (kind) {
    case TokenKind::KwSigned:
        if (specs.saw_signed || specs.saw_unsigned) {
            specs.bad = true;
        }
        specs.saw_signed = true;
        break;
    case TokenKind::KwUnsigned:
        if (specs.saw_signed || specs.saw_unsigned) {
            specs.bad = true;
        }
        specs.saw_unsigned = true;
        break;
    case TokenKind::KwShort:
        if (specs.saw_short || specs.saw_char || specs.longs != 0) {
            specs.bad = true;
        }
        specs.saw_short = true;
        break;
    case TokenKind::KwLong:
        if (specs.saw_short || specs.saw_char || specs.longs >= 2) {
            specs.bad = true;
        }
        ++specs.longs;
        break;
    case TokenKind::KwChar:
        if (specs.saw_char || specs.saw_short || specs.saw_int || specs.longs != 0) {
            specs.bad = true;
        }
        specs.saw_char = true;
        break;
    case TokenKind::KwInt:
        if (specs.saw_int || specs.saw_char) {
            specs.bad = true;
        }
        specs.saw_int = true;
        break;
    default:
        specs.bad = true;
        break;
    }
}

std::string canonical_int(const IntSpecs &specs) {
    if (specs.bad) {
        return "invalid";
    }
    if (specs.saw_char) {
        if (specs.saw_unsigned) {
            return "unsigned char";
        }
        if (specs.saw_signed) {
            return "signed char";
        }
        return "char";
    }
    if (specs.saw_short) {
        if (specs.saw_unsigned) {
            return "unsigned short";
        }
        return "short";
    }
    if (specs.longs >= 2) {
        if (specs.saw_unsigned) {
            return "unsigned long long";
        }
        return "long long";
    }
    if (specs.longs == 1) {
        if (specs.saw_unsigned) {
            return "unsigned long";
        }
        return "long";
    }
    if (specs.saw_unsigned) {
        return "unsigned int";
    }
    return "int";
}

} // namespace

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
    case TokenKind::KwSigned:
    case TokenKind::KwUnsigned:
    case TokenKind::KwFloat:
    case TokenKind::KwDouble:
    case TokenKind::KwBool:
    case TokenKind::KwUBool:
    case TokenKind::KwStruct:
    case TokenKind::KwEnum:
    case TokenKind::KwUnion:
    case TokenKind::KwTypedef:
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

bool Parser::starts_declaration() const {
    const TokenKind kind = peek().kind;
    const bool typedef_name = kind == TokenKind::Identifier && peek(1).kind != TokenKind::Dot &&
                              (peek(1).kind == TokenKind::Star || peek(1).kind == TokenKind::Identifier);
    return kind == TokenKind::KwExport || kind == TokenKind::KwStatic || kind == TokenKind::KwExtern ||
           kind == TokenKind::KwFunc || is_type_keyword(kind) || starts_qualified_type() || typedef_name;
}

Parser::Specifiers Parser::parse_specifiers(bool file_scope, bool allow_const, bool type_only) {
    Specifiers specs;
    specs.loc = peek().loc;
    bool reported_unsupported = false;
    IntSpecs ints;
    bool saw_void = false;
    bool saw_bool = false;
    while (true) {
        const Token &token = peek();
        if (token.kind == TokenKind::KwExport) {
            if (!file_scope) {
                error_at(token, "export is only valid at file scope");
            }
            specs.exported = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwStatic) {
            if (!file_scope) {
                error_at(token, "static locals are not supported yet");
            }
            specs.is_static = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwExtern) {
            specs.is_extern = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwTypedef) {
            if (!file_scope) {
                error_at(token, "not supported yet");
            }
            specs.is_typedef = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwVoid) {
            if (ints.any || saw_bool) {
                ints.bad = true;
            }
            saw_void = true;
            specs.type_name = "void";
            specs.saw_type = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwBool) {
            if (ints.any || saw_void) {
                ints.bad = true;
            }
            saw_bool = true;
            specs.type_name = "bool";
            specs.saw_type = true;
            ++pos_;
            continue;
        }
        if (is_int_spec(token.kind)) {
            if (saw_void || saw_bool) {
                ints.bad = true;
            }
            take_int_spec(ints, token.kind);
            specs.saw_type = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwConst && allow_const) {
            specs.is_const = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwStruct) {
            ++pos_;
            specs.is_struct = true;
            specs.saw_type = true;
            if (check(TokenKind::Identifier)) {
                specs.type_name = peek().text;
                specs.loc = peek().loc;
                ++pos_;
            } else {
                error_here("expected an identifier");
                specs.type_name = "invalid";
            }
            if (eat(TokenKind::LBrace)) {
                specs.struct_definition = true;
                specs.fields = parse_fields();
                expect(TokenKind::RBrace, "expected '}'");
            }
            break;
        }
        if (token.kind == TokenKind::KwEnum) {
            ++pos_;
            specs.is_enum = true;
            specs.saw_type = true;
            if (check(TokenKind::Identifier)) {
                specs.type_name = peek().text;
                specs.loc = peek().loc;
                ++pos_;
            }
            if (eat(TokenKind::LBrace)) {
                specs.enum_definition = true;
                specs.enumerators = parse_enumerators();
                expect(TokenKind::RBrace, "expected '}'");
            }
            break;
        }
        if (!specs.saw_type && token.kind == TokenKind::Identifier && peek(1).kind == TokenKind::Dot &&
            peek(2).kind == TokenKind::Identifier) {
            specs.type_package = token.text;
            specs.type_name = peek(2).text;
            specs.saw_type = true;
            pos_ += 3;
            break;
        }
        if (!specs.saw_type && token.kind == TokenKind::Identifier && peek(1).kind != TokenKind::Dot &&
            (type_only || peek(1).kind == TokenKind::Star || peek(1).kind == TokenKind::Identifier)) {
            specs.type_name = token.text;
            specs.saw_type = true;
            ++pos_;
            break;
        }
        if (is_type_keyword(token.kind)) {
            if (!reported_unsupported) {
                error_at(token, "not supported yet");
                reported_unsupported = true;
            }
            specs.saw_type = true;
            if (specs.type_name.empty()) {
                specs.type_name = "invalid";
            }
            ++pos_;
            continue;
        }
        break;
    }
    if (ints.any) {
        if (ints.bad || saw_void || saw_bool) {
            diags_.error(specs.loc, "type mismatch");
            specs.type_name = "invalid";
        } else {
            specs.type_name = canonical_int(ints);
        }
    }
    if (!specs.saw_type) {
        error_at(peek(), "expected a type");
        specs.type_name = "invalid";
    }
    if (specs.exported && specs.is_static) {
        diags_.error(specs.loc, "export static");
    }
    return specs;
}

std::vector<Enumerator> Parser::parse_enumerators() {
    std::vector<Enumerator> enumerators;
    while (!check(TokenKind::RBrace) && !eof()) {
        Enumerator enumerator;
        enumerator.loc = peek().loc;
        if (check(TokenKind::Identifier)) {
            enumerator.name = peek().text;
            ++pos_;
        } else {
            error_here("expected an identifier");
            break;
        }
        if (eat(TokenKind::Eq)) {
            if (check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.long_count == 0 &&
                peek().int_value <= 2147483647ULL) {
                enumerator.has_value = true;
                enumerator.value = static_cast<int>(peek().int_value);
                ++pos_;
            } else {
                error_here("not supported yet");
                while (!check(TokenKind::Comma) && !check(TokenKind::RBrace) && !eof()) {
                    ++pos_;
                }
            }
        }
        enumerators.push_back(std::move(enumerator));
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    return enumerators;
}

std::vector<FieldDecl> Parser::parse_fields() {
    std::vector<FieldDecl> fields;
    while (!check(TokenKind::RBrace) && !eof()) {
        const Specifiers specs = parse_specifiers(false);
        if (specs.struct_definition) {
            diags_.error(specs.loc, "not supported yet");
        }
        const Declarator decl = parse_declarator();
        if (decl.is_function || !decl.method_struct.empty()) {
            diags_.error(decl.loc, "not supported yet");
        }
        FieldDecl field;
        field.loc = decl.loc.valid() ? decl.loc : specs.loc;
        field.name = decl.name;
        field.type_name = specs.type_name;
        field.type_package = specs.type_package;
        field.is_struct = specs.is_struct;
        field.is_enum = specs.is_enum;
        field.indirections = decl.indirections;
        field.is_array = decl.is_array;
        field.array_len = decl.array_len;
        fields.push_back(std::move(field));
        expect(TokenKind::Semicolon, "expected ';'");
    }
    return fields;
}

namespace {

bool is_builtin_type_name(const std::string &name) {
    return name.empty() || name == "invalid" || name == "void" || name == "bool" || name == "char" ||
           name == "signed char" || name == "unsigned char" || name == "short" || name == "unsigned short" ||
           name == "int" || name == "unsigned int" || name == "long" || name == "unsigned long" ||
           name == "long long" || name == "unsigned long long" || name == "float" || name == "double";
}

} // namespace

Parser::PlacedType Parser::parse_placed_type(bool allow_const, bool allow_array) {
    PlacedType type;
    const Specifiers specs = parse_specifiers(false, allow_const, true);
    type.loc = specs.loc;
    type.type_name = specs.type_name;
    type.type_package = specs.type_package;
    type.is_struct = specs.is_struct;
    type.is_enum = specs.is_enum;
    type.is_const = specs.is_const;
    type.struct_definition = specs.struct_definition;
    type.enum_definition = specs.enum_definition;
    while (check(TokenKind::Star)) {
        ++type.indirections;
        ++pos_;
    }
    if (check(TokenKind::LBracket)) {
        if (!allow_array) {
            error_here("not supported yet");
            while (eat(TokenKind::LBracket)) {
                while (!check(TokenKind::RBracket) && !check(TokenKind::Semicolon) && !check(TokenKind::Comma) &&
                       !check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                    ++pos_;
                }
                eat(TokenKind::RBracket);
            }
        } else {
            const ArrayMark array = parse_array_suffix(true);
            type.is_array = array.is_array;
            type.array_len = array.length;
        }
    }
    return type;
}

std::vector<Param> Parser::parse_params(bool allow_const, bool &variadic, bool method) {
    std::vector<Param> params;
    variadic = false;
    expect(TokenKind::LParen, "expected '('");
    auto type_starts = [&](TokenKind kind) {
        return kind == TokenKind::Identifier || is_type_keyword(kind);
    };
    auto receiver_form = [&]() {
        if (!check(TokenKind::Identifier) || peek().text != "this" || peek(1).kind != TokenKind::Identifier) {
            return false;
        }
        return type_starts(peek(2).kind);
    };
    auto take_receiver = [&](bool as_receiver) {
        Param param;
        param.loc = peek().loc;
        ++pos_;
        if (check(TokenKind::Identifier)) {
            param.name = peek().text;
            param.loc = peek().loc;
            ++pos_;
        } else {
            error_here("expected an identifier");
        }
        const PlacedType type = parse_placed_type(allow_const, false);
        param.type_name = type.type_name;
        param.type_package = type.type_package;
        param.is_enum = type.is_enum;
        param.is_const = type.is_const;
        param.indirections = type.indirections;
        param.is_array = type.is_array;
        param.array_len = type.array_len;
        param.is_receiver = as_receiver;
        // The receiver names the struct tag. The signature does not write `struct`.
        if (!type.is_enum && type.type_package.empty() && !is_builtin_type_name(type.type_name)) {
            param.is_struct = true;
        } else {
            param.is_struct = type.is_struct;
        }
        if (type.struct_definition || type.enum_definition) {
            diags_.error(type.loc, "not supported yet");
        }
        if (param.indirections > 1) {
            diags_.error(param.loc, "not supported yet");
        }
        return param;
    };
    if (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
            if (check(TokenKind::Ellipsis)) {
                if (params.empty()) {
                    error_here("not supported yet");
                } else {
                    variadic = true;
                }
                ++pos_;
                break;
            }
            const bool want_receiver = method && params.empty();
            if (receiver_form()) {
                if (!want_receiver) {
                    error_here("type mismatch");
                }
                params.push_back(take_receiver(want_receiver));
                if (!eat(TokenKind::Comma)) {
                    break;
                }
                continue;
            }
            if (want_receiver) {
                error_here("type mismatch");
            }
            Param param;
            param.loc = peek().loc;
            if (check(TokenKind::Identifier)) {
                param.name = peek().text;
                param.loc = peek().loc;
                ++pos_;
            } else {
                error_here("expected an identifier");
                break;
            }
            const PlacedType type = parse_placed_type(allow_const, true);
            if (type.struct_definition || type.enum_definition) {
                diags_.error(type.loc, "not supported yet");
            }
            param.type_name = type.type_name;
            param.type_package = type.type_package;
            param.is_struct = type.is_struct;
            param.is_enum = type.is_enum;
            param.is_const = type.is_const;
            param.indirections = type.indirections;
            param.is_array = type.is_array;
            param.array_len = type.array_len;
            params.push_back(std::move(param));
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
    }
    if (method && params.empty()) {
        error_here("type mismatch");
    }
    expect(TokenKind::RParen, "expected ')'");
    return params;
}

void Parser::take_result(FunctionDecl &fn, const PlacedType &type) {
    if (type.struct_definition || type.enum_definition) {
        diags_.error(type.loc, "not supported yet");
    }
    ResultSlot slot;
    slot.type_name = type.type_name;
    slot.type_package = type.type_package;
    slot.is_struct = type.is_struct;
    slot.is_enum = type.is_enum;
    slot.indirections = type.indirections;
    fn.results.push_back(std::move(slot));
}

void Parser::parse_results(FunctionDecl &fn) {
    if (check(TokenKind::LParen)) {
        const SourceLoc loc = peek().loc;
        ++pos_;
        if (check(TokenKind::RParen)) {
            diags_.error(loc, "type mismatch");
            ++pos_;
        } else {
            while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !check(TokenKind::Semicolon) && !eof()) {
                take_result(fn, parse_placed_type(false, false));
                if (!eat(TokenKind::Comma)) {
                    break;
                }
            }
            expect(TokenKind::RParen, "expected ')'");
        }
    } else {
        take_result(fn, parse_placed_type(false, false));
    }
    if (fn.results.empty()) {
        fn.return_type = "invalid";
        return;
    }
    const ResultSlot &first = fn.results.front();
    fn.return_type = first.type_name;
    fn.type_package = first.type_package;
    fn.is_struct = first.is_struct;
    fn.is_enum = first.is_enum;
    fn.indirections = first.indirections;
}

FunctionDecl Parser::parse_function(bool exported, bool is_static, bool is_extern, bool allow_body) {
    FunctionDecl fn;
    fn.exported = exported;
    fn.is_static = is_static;
    fn.is_extern = is_extern;
    fn.loc = peek().loc;
    expect(TokenKind::KwFunc, "expected 'func'");
    if (check(TokenKind::Identifier)) {
        fn.name = peek().text;
        fn.loc = peek().loc;
        ++pos_;
        if (check(TokenKind::ColonColon)) {
            ++pos_;
            fn.method_struct = fn.name;
            if (check(TokenKind::Identifier)) {
                fn.name = peek().text;
                fn.loc = peek().loc;
                ++pos_;
            } else {
                fn.name.clear();
                error_here("expected an identifier");
            }
        }
    } else {
        error_here("expected an identifier");
    }
    fn.params = parse_params(is_extern, fn.variadic, !fn.method_struct.empty());
    parse_results(fn);
    if (check(TokenKind::LBrace)) {
        if (!allow_body) {
            diags_.error(peek().loc, "not supported yet");
        }
        auto body = parse_block();
        if (allow_body) {
            fn.body = std::move(body);
        }
    } else if (!allow_body && eat(TokenKind::Eq)) {
        diags_.error(fn.loc, "type mismatch");
        parse_assignment();
        expect(TokenKind::Semicolon, "expected ';'");
    } else {
        expect(TokenKind::Semicolon, "expected ';'");
    }
    if (fn.body) {
        if (fn.variadic) {
            diags_.error(fn.loc, "not supported yet");
        }
        for (const Param &param : fn.params) {
            if (param.is_const) {
                diags_.error(param.loc, "not supported yet");
            }
        }
    }
    return fn;
}

Parser::Declarator Parser::parse_declarator() {
    Declarator decl;
    while (check(TokenKind::Star)) {
        ++decl.indirections;
        ++pos_;
    }
    if (check(TokenKind::Identifier)) {
        decl.name = peek().text;
        decl.loc = peek().loc;
        ++pos_;
        if (check(TokenKind::ColonColon)) {
            ++pos_;
            decl.method_struct = decl.name;
            if (check(TokenKind::Identifier)) {
                decl.name = peek().text;
                decl.loc = peek().loc;
                ++pos_;
            } else {
                decl.name.clear();
                error_here("expected an identifier");
            }
        }
    } else {
        decl.loc = peek().loc;
        error_here("expected an identifier");
    }
    if (check(TokenKind::LParen)) {
        decl.is_function = true;
        int depth = 0;
        while (!eof()) {
            if (check(TokenKind::LParen)) {
                ++depth;
            } else if (check(TokenKind::RParen)) {
                --depth;
            }
            ++pos_;
            if (depth == 0) {
                break;
            }
        }
        return decl;
    }
    if (check(TokenKind::LBracket)) {
        const ArrayMark array = parse_array_suffix(false);
        decl.is_array = array.is_array;
        decl.array_len = array.length;
    }
    return decl;
}

Parser::ArrayMark Parser::parse_array_suffix(bool allow_unsized) {
    ArrayMark mark;
    if (!check(TokenKind::LBracket)) {
        return mark;
    }
    mark.is_array = true;
    const SourceLoc loc = peek().loc;
    ++pos_;
    if (check(TokenKind::RBracket)) {
        ++pos_;
        mark.length = -1;
        if (!allow_unsized) {
            diags_.error(loc, "not supported yet");
        }
    } else if (check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.long_count == 0 &&
               peek().int_value > 0 && peek().int_value <= 2147483647ULL) {
        mark.length = static_cast<int>(peek().int_value);
        ++pos_;
        expect(TokenKind::RBracket, "expected ']'");
    } else {
        diags_.error(loc, "not supported yet");
        while (!check(TokenKind::RBracket) && !check(TokenKind::Semicolon) && !check(TokenKind::RParen) &&
               !check(TokenKind::Comma) && !eof()) {
            ++pos_;
        }
        eat(TokenKind::RBracket);
        mark.length = -1;
    }
    if (check(TokenKind::LBracket)) {
        diags_.error(peek().loc, "not supported yet");
        while (eat(TokenKind::LBracket)) {
            while (!check(TokenKind::RBracket) && !check(TokenKind::Semicolon) && !check(TokenKind::RParen) && !eof()) {
                ++pos_;
            }
            eat(TokenKind::RBracket);
        }
    }
    return mark;
}

void Parser::parse_top_declaration(TranslationUnit &unit, bool file_scope_export_ok) {
    (void)file_scope_export_ok;
    const std::size_t marker = pos_;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
    const SourceLoc storage_loc = peek().loc;
    while (true) {
        if (check(TokenKind::KwExport)) {
            exported = true;
            ++pos_;
            continue;
        }
        if (check(TokenKind::KwStatic)) {
            is_static = true;
            ++pos_;
            continue;
        }
        if (check(TokenKind::KwExtern)) {
            is_extern = true;
            ++pos_;
            continue;
        }
        break;
    }
    if (check(TokenKind::KwFunc)) {
        if (exported && is_static) {
            diags_.error(storage_loc, "export static");
        }
        FunctionDecl fn = parse_function(exported, is_static, is_extern, true);
        TopDecl top;
        top.data = std::move(fn);
        unit.decls.push_back(std::move(top));
        saw_decl_ = true;
        return;
    }
    pos_ = marker;
    const Specifiers specs = parse_specifiers(true);
    if (specs.struct_definition) {
        StructDecl item;
        item.loc = specs.loc;
        item.name = specs.type_name;
        item.fields = specs.fields;
        item.exported = specs.exported;
        TopDecl top;
        top.data = std::move(item);
        unit.decls.push_back(std::move(top));
        if (check(TokenKind::Semicolon)) {
            ++pos_;
            saw_decl_ = true;
            return;
        }
    }
    if (specs.enum_definition) {
        EnumDecl item;
        item.loc = specs.loc;
        item.name = specs.type_name;
        item.enumerators = specs.enumerators;
        item.exported = specs.exported;
        TopDecl top;
        top.data = std::move(item);
        unit.decls.push_back(std::move(top));
        if (check(TokenKind::Semicolon)) {
            ++pos_;
            saw_decl_ = true;
            return;
        }
    }
    if (specs.is_struct && !specs.struct_definition && check(TokenKind::Semicolon)) {
        if (specs.is_typedef && !specs.type_name.empty() && specs.type_name != "invalid") {
            StructDecl item;
            item.loc = specs.loc;
            item.name = specs.type_name;
            item.exported = specs.exported;
            item.forward = true;
            TopDecl top;
            top.data = std::move(item);
            unit.decls.push_back(std::move(top));
        } else if (!specs.is_typedef) {
            diags_.error(specs.loc, "struct outside definition");
        }
        ++pos_;
        saw_decl_ = true;
        return;
    }
    while (true) {
        const Declarator decl = parse_declarator();
        if (!decl.method_struct.empty() && !decl.is_function) {
            diags_.error(decl.loc, "type mismatch");
        }
        if (specs.is_typedef) {
            if (decl.is_function) {
                diags_.error(decl.loc, "not supported yet");
                if (check(TokenKind::LBrace)) {
                    parse_block();
                } else {
                    expect(TokenKind::Semicolon, "expected ';'");
                }
                break;
            }
            TypedefDecl item;
            item.loc = decl.loc.valid() ? decl.loc : specs.loc;
            item.name = decl.name;
            item.type_name = specs.type_name;
            item.type_package = specs.type_package;
            item.is_struct = specs.is_struct;
            item.is_enum = specs.is_enum;
            item.indirections = decl.indirections;
            item.is_array = decl.is_array;
            item.array_len = decl.array_len;
            item.exported = specs.exported;
            TopDecl top;
            top.data = std::move(item);
            unit.decls.push_back(std::move(top));
            if (!eat(TokenKind::Comma)) {
                expect(TokenKind::Semicolon, "expected ';'");
                break;
            }
            continue;
        }
        if (decl.is_function) {
            diags_.error(decl.loc, "not supported yet");
            if (check(TokenKind::LBrace)) {
                parse_block();
            } else {
                expect(TokenKind::Semicolon, "expected ';'");
            }
            break;
        }
        VarDecl var;
        var.loc = decl.loc.valid() ? decl.loc : specs.loc;
        var.name = decl.name;
        var.type_name = specs.type_name;
        var.type_package = specs.type_package;
        var.is_struct = specs.is_struct;
        var.is_enum = specs.is_enum;
        var.indirections = decl.indirections;
        var.is_array = decl.is_array;
        var.array_len = decl.array_len;
        var.exported = specs.exported;
        var.is_static = specs.is_static;
        var.is_extern = specs.is_extern;
        if (eat(TokenKind::Eq)) {
            var.init = parse_assignment();
        }
        TopDecl top;
        top.data = std::move(var);
        unit.decls.push_back(std::move(top));
        if (!eat(TokenKind::Comma)) {
            expect(TokenKind::Semicolon, "expected ';'");
            break;
        }
    }
    saw_decl_ = true;
}

std::unique_ptr<Stmt> Parser::parse_decl_stmt(bool file_scope) {
    const std::size_t marker = pos_;
    while (check(TokenKind::KwExport) || check(TokenKind::KwStatic) || check(TokenKind::KwExtern)) {
        ++pos_;
    }
    if (check(TokenKind::KwFunc)) {
        pos_ = marker;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
        const SourceLoc storage_loc = peek().loc;
        while (true) {
            if (eat(TokenKind::KwExport)) {
                exported = true;
                continue;
            }
            if (eat(TokenKind::KwStatic)) {
                is_static = true;
                continue;
            }
            if (eat(TokenKind::KwExtern)) {
                is_extern = true;
                continue;
            }
            break;
        }
        if (exported || is_static || is_extern) {
            diags_.error(storage_loc, "not supported yet");
        }
        FunctionDecl fn = parse_function(exported, is_static, is_extern, true);
        if (!fn.method_struct.empty()) {
            diags_.error(fn.loc, "not supported yet");
        }
        Stmt::Func local;
        local.decl = std::make_unique<FunctionDecl>(std::move(fn));
        return make_stmt(std::move(local));
    }
    pos_ = marker;
    const Specifiers specs = parse_specifiers(file_scope);
    if (specs.struct_definition || specs.enum_definition || specs.is_typedef) {
        diags_.error(specs.loc, "not supported yet");
        if (check(TokenKind::Semicolon)) {
            ++pos_;
            return make_stmt(Stmt::Expression{});
        }
    }
    Stmt::Decl decl;
    decl.loc = specs.loc;
    while (true) {
        const Declarator declarator = parse_declarator();
        if (declarator.is_function) {
            diags_.error(declarator.loc, "nested functions are not supported yet");
            if (check(TokenKind::LBrace)) {
                parse_block();
            } else {
                expect(TokenKind::Semicolon, "expected ';'");
            }
            break;
        } else if (!declarator.method_struct.empty()) {
            diags_.error(declarator.loc, "not supported yet");
            if (check(TokenKind::LBrace)) {
                parse_block();
            }
            break;
        }
        VarDecl var;
        var.loc = declarator.loc;
        var.name = declarator.name;
        var.type_name = specs.type_name;
        var.type_package = specs.type_package;
        var.is_struct = specs.is_struct;
        var.is_enum = specs.is_enum;
        var.indirections = declarator.indirections;
        var.is_array = declarator.is_array;
        var.array_len = declarator.array_len;
        var.exported = specs.exported;
        var.is_static = specs.is_static;
        var.is_extern = specs.is_extern;
        if (eat(TokenKind::Eq)) {
            var.init = parse_assignment();
        }
        decl.vars.push_back(std::move(var));
        if (!eat(TokenKind::Comma)) {
            expect(TokenKind::Semicolon, "expected ';'");
            break;
        }
    }
    return make_stmt(std::move(decl));
}

std::unique_ptr<Stmt> Parser::parse_block() {
    Stmt::Block block;
    block.loc = peek().loc;
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !eof()) {
        const std::size_t before = pos_;
        block.statements.push_back(parse_statement());
        if (pos_ == before) {
            error_here("expected a statement");
            ++pos_;
        }
    }
    expect(TokenKind::RBrace, "expected '}'");
    return make_stmt(std::move(block));
}

std::unique_ptr<Stmt> Parser::parse_if() {
    Stmt::If stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwIf);
    expect(TokenKind::LParen, "expected '('");
    stmt.cond = parse_expr();
    expect(TokenKind::RParen, "expected ')'");
    stmt.then_arm = parse_statement();
    if (eat(TokenKind::KwElse)) {
        stmt.else_arm = parse_statement();
    }
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_while() {
    Stmt::While stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwWhile);
    expect(TokenKind::LParen, "expected '('");
    stmt.cond = parse_expr();
    expect(TokenKind::RParen, "expected ')'");
    stmt.body = parse_statement();
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_do() {
    Stmt::DoWhile stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwDo);
    stmt.body = parse_statement();
    expect(TokenKind::KwWhile, "expected 'while'");
    expect(TokenKind::LParen, "expected '('");
    stmt.cond = parse_expr();
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Semicolon, "expected ';'");
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_for() {
    Stmt::For stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwFor);
    expect(TokenKind::LParen, "expected '('");
    if (eat(TokenKind::Semicolon)) {
        stmt.init = nullptr;
    } else if (starts_declaration()) {
        stmt.init = parse_decl_stmt(false);
        // A declaration consumes its ';'. A function body does not, so the for-loop
        // separator after `func f() int { ... }` is that missing terminator.
        if (stmt.init != nullptr) {
            if (const auto *local = std::get_if<Stmt::Func>(&stmt.init->data)) {
                if (local->decl != nullptr && local->decl->body != nullptr) {
                    eat(TokenKind::Semicolon);
                }
            }
        }
    } else {
        Stmt::Expression expr;
        expr.loc = peek().loc;
        expr.expr = parse_expr();
        expect(TokenKind::Semicolon, "expected ';'");
        stmt.init = make_stmt(std::move(expr));
    }
    if (!check(TokenKind::Semicolon)) {
        stmt.cond = parse_expr();
    }
    expect(TokenKind::Semicolon, "expected ';'");
    if (!check(TokenKind::RParen)) {
        stmt.step = parse_expr();
    }
    expect(TokenKind::RParen, "expected ')'");
    stmt.body = parse_statement();
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_switch() {
    Stmt::Switch stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwSwitch);
    expect(TokenKind::LParen, "expected '('");
    stmt.cond = parse_expr();
    expect(TokenKind::RParen, "expected ')'");
    stmt.body = parse_statement();
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_case() {
    Stmt::Case stmt;
    stmt.loc = peek().loc;
    if (eat(TokenKind::KwDefault)) {
        stmt.is_default = true;
    } else {
        eat(TokenKind::KwCase);
        stmt.value = parse_expr();
    }
    expect(TokenKind::Colon, "expected ':'");
    if (check(TokenKind::RBrace) || eof()) {
        error_here("expected a statement");
        stmt.stmt = make_stmt(Stmt::Expression{});
    } else {
        stmt.stmt = parse_statement();
    }
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_return() {
    Stmt::Return stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwReturn);
    if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
        stmt.value = parse_expr();
        while (eat(TokenKind::Comma)) {
            stmt.extra.push_back(parse_expr());
        }
    }
    expect(TokenKind::Semicolon, "expected ';'");
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_bind(bool is_var) {
    Stmt::Bind stmt;
    stmt.loc = peek().loc;
    stmt.is_var = is_var;
    if (is_var) {
        ++pos_;
    }
    while (check(TokenKind::Identifier)) {
        stmt.names.push_back(peek().text);
        stmt.name_locs.push_back(peek().loc);
        stmt.loc = stmt.names.size() == 1 ? peek().loc : stmt.loc;
        ++pos_;
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    if (stmt.names.empty()) {
        error_here("expected an identifier");
    }
    if (eat(TokenKind::ColonEq)) {
        if (is_var) {
            diags_.error(stmt.loc, "type mismatch");
        }
        stmt.is_define = true;
    } else {
        expect(TokenKind::Eq, "expected '='");
    }
    stmt.value = parse_expr();
    expect(TokenKind::Semicolon, "expected ';'");
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Stmt> Parser::parse_statement() {
    if (check(TokenKind::LBrace)) {
        return parse_block();
    }
    if (check(TokenKind::KwIf)) {
        return parse_if();
    }
    if (check(TokenKind::KwWhile)) {
        return parse_while();
    }
    if (check(TokenKind::KwDo)) {
        return parse_do();
    }
    if (check(TokenKind::KwFor)) {
        return parse_for();
    }
    if (check(TokenKind::KwSwitch)) {
        return parse_switch();
    }
    if (check(TokenKind::KwCase) || check(TokenKind::KwDefault)) {
        return parse_case();
    }
    if (check(TokenKind::KwReturn)) {
        return parse_return();
    }
    if (check(TokenKind::KwBreak)) {
        Stmt::Break stmt;
        stmt.loc = peek().loc;
        ++pos_;
        expect(TokenKind::Semicolon, "expected ';'");
        return make_stmt(std::move(stmt));
    }
    if (check(TokenKind::KwContinue)) {
        Stmt::Continue stmt;
        stmt.loc = peek().loc;
        ++pos_;
        expect(TokenKind::Semicolon, "expected ';'");
        return make_stmt(std::move(stmt));
    }
    if (check(TokenKind::Semicolon)) {
        Stmt::Expression stmt;
        stmt.loc = peek().loc;
        ++pos_;
        return make_stmt(std::move(stmt));
    }
    if (check(TokenKind::KwImport)) {
        error_here("import must appear before declarations");
        ++pos_;
        while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
            ++pos_;
        }
        eat(TokenKind::Semicolon);
        return make_stmt(Stmt::Expression{});
    }
    if (check(TokenKind::KwPackage)) {
        error_here("package must appear before declarations");
        ++pos_;
        while (!check(TokenKind::Semicolon) && !eof()) {
            ++pos_;
        }
        eat(TokenKind::Semicolon);
        return make_stmt(Stmt::Expression{});
    }
    if (check(TokenKind::KwGoto)) {
        error_here("not supported yet");
        ++pos_;
        while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
            if (check(TokenKind::LBrace)) {
                break;
            }
            ++pos_;
        }
        eat(TokenKind::Semicolon);
        return make_stmt(Stmt::Expression{});
    }
    if (check(TokenKind::KwDefer)) {
        Stmt::Defer stmt;
        stmt.loc = peek().loc;
        ++pos_;
        stmt.call = parse_expr();
        expect(TokenKind::Semicolon, "expected ';'");
        return make_stmt(std::move(stmt));
    }
    if (check(TokenKind::KwVar)) {
        return parse_bind(true);
    }
    if (check(TokenKind::Identifier) && (peek(1).kind == TokenKind::Comma || peek(1).kind == TokenKind::ColonEq)) {
        return parse_bind(false);
    }
    if (starts_declaration()) {
        return parse_decl_stmt(false);
    }
    Stmt::Expression stmt;
    stmt.loc = peek().loc;
    stmt.expr = parse_expr();
    expect(TokenKind::Semicolon, "expected ';'");
    return make_stmt(std::move(stmt));
}

std::unique_ptr<Expr> Parser::parse_expr() { return parse_assignment(); }

std::unique_ptr<Expr> Parser::parse_assignment() {
    auto lhs = parse_conditional();
    if (eat(TokenKind::Eq)) {
        auto rhs = parse_assignment();
        Expr::Assign node;
        node.loc = expr_loc(*lhs);
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        return make_expr(std::move(node));
    }
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::PlusEq || kind == TokenKind::MinusEq || kind == TokenKind::StarEq ||
        kind == TokenKind::SlashEq || kind == TokenKind::PercentEq || kind == TokenKind::AmpEq ||
        kind == TokenKind::PipeEq || kind == TokenKind::CaretEq || kind == TokenKind::LtLtEq ||
        kind == TokenKind::GtGtEq) {
        error_here("not supported yet");
        ++pos_;
        parse_assignment();
        return unsupported_expr(expr_loc(*lhs));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_conditional() {
    auto cond = parse_or();
    if (eat(TokenKind::Question)) {
        error_here("not supported yet");
        parse_expr();
        expect(TokenKind::Colon, "expected ':'");
        parse_conditional();
        return unsupported_expr(expr_loc(*cond));
    }
    return cond;
}

std::unique_ptr<Expr> Parser::parse_or() {
    auto lhs = parse_and();
    while (check(TokenKind::PipePipe)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_and();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_and() {
    auto lhs = parse_bit_or();
    while (check(TokenKind::AmpAmp)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_equality();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_bit_or() {
    auto lhs = parse_bit_xor();
    while (check(TokenKind::Pipe)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_bit_xor();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_bit_xor() {
    auto lhs = parse_bit_and();
    while (check(TokenKind::Caret)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_bit_and();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_bit_and() {
    auto lhs = parse_equality();
    while (check(TokenKind::Amp)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_equality();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_equality() {
    auto lhs = parse_relational();
    while (check(TokenKind::EqEq) || check(TokenKind::BangEq)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_relational();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_relational() {
    auto lhs = parse_shift();
    while (check(TokenKind::Lt) || check(TokenKind::Gt) || check(TokenKind::Le) || check(TokenKind::Ge)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_shift();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_shift() {
    auto lhs = parse_additive();
    while (check(TokenKind::LtLt) || check(TokenKind::GtGt)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_additive();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_additive() {
    auto lhs = parse_multiplicative();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_multiplicative();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parse_multiplicative() {
    auto lhs = parse_unary();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        const TokenKind op = peek().kind;
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto rhs = parse_unary();
        Expr::Binary node;
        node.loc = loc;
        node.op = op;
        node.lhs = std::move(lhs);
        node.rhs = std::move(rhs);
        lhs = make_expr(std::move(node));
    }
    return lhs;
}

bool Parser::starts_type_paren() const {
    if (peek().kind != TokenKind::LParen) {
        return false;
    }
    std::size_t index = 1;
    const TokenKind kind = peek(index).kind;
    if (is_int_spec(kind)) {
        while (is_int_spec(peek(index).kind)) {
            ++index;
        }
    } else if (kind == TokenKind::KwVoid || kind == TokenKind::KwBool) {
        ++index;
    } else if (kind == TokenKind::KwStruct || kind == TokenKind::KwEnum) {
        if (peek(index + 1).kind != TokenKind::Identifier) {
            return false;
        }
        index += 2;
    } else if (kind == TokenKind::Identifier && peek(index + 1).kind == TokenKind::Dot &&
               peek(index + 2).kind == TokenKind::Identifier) {
        index += 3;
    } else if (kind == TokenKind::Identifier && peek(index + 1).kind == TokenKind::Star) {
        ++index;
    } else {
        return false;
    }
    while (peek(index).kind == TokenKind::Star) {
        ++index;
    }
    return peek(index).kind == TokenKind::RParen;
}

Parser::ParenType Parser::parse_paren_type() {
    ParenType type;
    eat(TokenKind::LParen);
    type.loc = peek().loc;
    if (is_int_spec(peek().kind)) {
        IntSpecs ints;
        while (is_int_spec(peek().kind)) {
            take_int_spec(ints, peek().kind);
            ++pos_;
        }
        if (ints.bad) {
            diags_.error(type.loc, "type mismatch");
            type.name = "invalid";
        } else {
            type.name = canonical_int(ints);
        }
    } else if (eat(TokenKind::KwVoid)) {
        type.name = "void";
    } else if (eat(TokenKind::KwBool)) {
        type.name = "bool";
    } else if (eat(TokenKind::KwStruct)) {
        type.is_struct = true;
        if (check(TokenKind::Identifier)) {
            type.name = peek().text;
            type.loc = peek().loc;
            ++pos_;
        } else {
            error_here("expected an identifier");
            type.name = "invalid";
        }
    } else if (eat(TokenKind::KwEnum)) {
        type.is_enum = true;
        if (check(TokenKind::Identifier)) {
            type.name = peek().text;
            type.loc = peek().loc;
            ++pos_;
        } else {
            error_here("expected an identifier");
            type.name = "invalid";
        }
    } else if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Dot &&
               peek(2).kind == TokenKind::Identifier) {
        type.package = peek().text;
        type.name = peek(2).text;
        pos_ += 3;
    } else if (check(TokenKind::Identifier)) {
        type.name = peek().text;
        ++pos_;
    }
    while (eat(TokenKind::Star)) {
        ++type.indirections;
    }
    expect(TokenKind::RParen, "expected ')'");
    return type;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwSizeof) {
        Expr::Sizeof node;
        node.loc = peek().loc;
        ++pos_;
        if (starts_type_paren()) {
            const ParenType type = parse_paren_type();
            node.on_type = true;
            node.type_name = type.name;
            node.type_package = type.package;
            node.is_struct = type.is_struct;
            node.is_enum = type.is_enum;
            node.indirections = type.indirections;
        } else {
            node.expr = parse_unary();
        }
        return make_expr(std::move(node));
    }
    if (kind == TokenKind::LParen && starts_type_paren()) {
        Expr::Cast node;
        node.loc = peek().loc;
        const ParenType type = parse_paren_type();
        node.type_name = type.name;
        node.type_package = type.package;
        node.is_struct = type.is_struct;
        node.is_enum = type.is_enum;
        node.indirections = type.indirections;
        node.expr = parse_unary();
        return make_expr(std::move(node));
    }
    if (kind == TokenKind::Plus || kind == TokenKind::Minus || kind == TokenKind::Bang || kind == TokenKind::Tilde ||
        kind == TokenKind::Amp || kind == TokenKind::Star) {
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto expr = parse_unary();
        Expr::Unary node;
        node.loc = loc;
        node.op = kind;
        node.expr = std::move(expr);
        return make_expr(std::move(node));
    }
    if (kind == TokenKind::PlusPlus || kind == TokenKind::MinusMinus) {
        const SourceLoc loc = peek().loc;
        error_here("not supported yet");
        ++pos_;
        parse_unary();
        return unsupported_expr(loc);
    }
    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix() {
    auto expr = parse_primary();
    while (true) {
        if (eat(TokenKind::LParen)) {
            Expr::Call call;
            call.loc = expr_loc(*expr);
            call.callee = std::move(expr);
            if (!check(TokenKind::RParen)) {
                while (!check(TokenKind::RParen) && !eof()) {
                    call.args.push_back(parse_assignment());
                    if (!eat(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            expect(TokenKind::RParen, "expected ')'");
            expr = make_expr(std::move(call));
            continue;
        }
        if (eat(TokenKind::Dot)) {
            Expr::Selector selector;
            selector.loc = expr_loc(*expr);
            selector.base = std::move(expr);
            if (check(TokenKind::Identifier)) {
                selector.field = peek().text;
                ++pos_;
            } else {
                error_here("expected an identifier");
            }
            expr = make_expr(std::move(selector));
            continue;
        }
        if (eat(TokenKind::LBracket)) {
            Expr::Index index;
            index.loc = expr_loc(*expr);
            index.base = std::move(expr);
            index.index = parse_expr();
            expect(TokenKind::RBracket, "expected ']'");
            expr = make_expr(std::move(index));
            continue;
        }
        if (check(TokenKind::Arrow) || check(TokenKind::PlusPlus) || check(TokenKind::MinusMinus)) {
            error_here("not supported yet");
            ++pos_;
            expr = unsupported_expr(expr_loc(*expr));
            continue;
        }
        break;
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    if (check(TokenKind::Identifier)) {
        Expr::Name name;
        name.loc = peek().loc;
        name.text = peek().text;
        ++pos_;
        return make_expr(std::move(name));
    }
    if (check(TokenKind::IntLiteral)) {
        Expr::Int literal;
        literal.loc = peek().loc;
        literal.value = peek().int_value;
        literal.suffix = peek().suffix;
        ++pos_;
        return make_expr(std::move(literal));
    }
    if (check(TokenKind::StringLiteral)) {
        Expr::String literal;
        literal.loc = peek().loc;
        literal.text = peek().text;
        ++pos_;
        return make_expr(std::move(literal));
    }
    if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
        Expr::Bool literal;
        literal.loc = peek().loc;
        literal.value = peek().kind == TokenKind::KwTrue;
        ++pos_;
        return make_expr(std::move(literal));
    }
    if (check(TokenKind::KwNullptr)) {
        Expr::Nullptr literal;
        literal.loc = peek().loc;
        ++pos_;
        return make_expr(std::move(literal));
    }
    if (check(TokenKind::CharLiteral)) {
        Expr::Char literal;
        literal.loc = peek().loc;
        literal.value = peek().int_value;
        ++pos_;
        return make_expr(std::move(literal));
    }
    if (check(TokenKind::FloatLiteral)) {
        const SourceLoc loc = peek().loc;
        error_here("not supported yet");
        ++pos_;
        return unsupported_expr(loc);
    }
    if (eat(TokenKind::LParen)) {
        auto expr = parse_expr();
        expect(TokenKind::RParen, "expected ')'");
        return expr;
    }
    if (check(TokenKind::KwFunc)) {
        return parse_anon_function();
    }
    const SourceLoc loc = peek().loc;
    error_here("expected an expression");
    if (!eof() && !check(TokenKind::Semicolon) && !check(TokenKind::RParen) && !check(TokenKind::RBrace) &&
        !check(TokenKind::Comma)) {
        ++pos_;
    }
    return unsupported_expr(loc);
}

std::unique_ptr<Expr> Parser::parse_anon_function() {
    FunctionDecl fn;
    fn.loc = peek().loc;
    expect(TokenKind::KwFunc, "expected 'func'");
    if (!check(TokenKind::LParen)) {
        error_here("expected '('");
    }
    fn.params = parse_params(false, fn.variadic, false);
    if (check(TokenKind::LBrace)) {
        fn.return_type = "void";
        ResultSlot slot;
        slot.type_name = "void";
        fn.results.push_back(std::move(slot));
        fn.body = parse_block();
    } else {
        parse_results(fn);
        if (check(TokenKind::LBrace)) {
            fn.body = parse_block();
        } else {
            error_here("expected '{'");
        }
    }
    if (fn.variadic) {
        diags_.error(fn.loc, "not supported yet");
    }
    for (const Param &param : fn.params) {
        if (param.is_const) {
            diags_.error(param.loc, "not supported yet");
        }
    }
    Expr::Func node;
    node.loc = fn.loc;
    node.decl = std::make_unique<FunctionDecl>(std::move(fn));
    return make_expr(std::move(node));
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
        FunctionDecl method = parse_function(false, false, false, false);
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
