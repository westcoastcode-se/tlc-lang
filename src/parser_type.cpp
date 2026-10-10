// Types: specifiers, struct and enum bodies, function types, and declarators.
#include "parser.hpp"
#include "parse_util.hpp"

#include <utility>

namespace tlc {

Parser::Specifiers Parser::parse_specifiers(bool file_scope, bool allow_const, bool type_only) {
    Specifiers specs;
    specs.loc = peek().loc;
    bool reported_unsupported = false;
    IntSpecs ints;
    bool saw_void = false;
    bool saw_bool = false;
    bool saw_float = false;
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
            take_extern(specs.is_extern, specs.extern_c);
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
            if (ints.any || saw_void || saw_float) {
                ints.bad = true;
            }
            saw_bool = true;
            specs.type_name = "bool";
            specs.saw_type = true;
            ++pos_;
            continue;
        }
        if (token.kind == TokenKind::KwFloat || token.kind == TokenKind::KwDouble) {
            if (ints.any || saw_void || saw_bool || saw_float) {
                ints.bad = true;
            }
            saw_float = true;
            specs.type_name = token.kind == TokenKind::KwFloat ? "float" : "double";
            specs.saw_type = true;
            ++pos_;
            continue;
        }
        if (is_int_spec(token.kind)) {
            if (saw_void || saw_bool || saw_float) {
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
        if (token.kind == TokenKind::KwStruct || token.kind == TokenKind::KwUnion) {
            const bool as_union = token.kind == TokenKind::KwUnion;
            ++pos_;
            specs.saw_type = true;
            if (check(TokenKind::LParen)) {
                if (as_union) {
                    diags_.error(peek().loc, "not supported yet");
                    int depth = 0;
                    do {
                        if (check(TokenKind::LParen)) {
                            ++depth;
                        } else if (check(TokenKind::RParen)) {
                            --depth;
                        }
                        ++pos_;
                    } while (depth > 0 && !eof());
                } else {
                    specs.align = parse_struct_align();
                }
            }
            if (check(TokenKind::Identifier)) {
                specs.type_name = peek().text;
                specs.loc = peek().loc;
                ++pos_;
            } else {
                error_here("expected an identifier");
                specs.type_name = "invalid";
            }
            if (eat(TokenKind::LBrace)) {
                if (as_union) {
                    specs.is_union = true;
                    specs.union_definition = true;
                } else {
                    specs.is_struct = true;
                    specs.struct_definition = true;
                }
                specs.fields = parse_fields();
                expect(TokenKind::RBrace, "expected '}'");
            } else {
                // `union Name` outside a definition is the same mistake as `struct Name`.
                specs.is_struct = true;
                specs.is_union = as_union;
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
            if (take_enum_underlying(specs.underlying) || check(TokenKind::LBrace)) {
                if (eat(TokenKind::LBrace)) {
                    specs.enum_definition = true;
                    specs.enumerators = parse_enumerators();
                    expect(TokenKind::RBrace, "expected '}'");
                }
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
            // `signed` and `unsigned` are not keywords. The names are `i` and `u` types.
            if (token.text == "signed" || token.text == "unsigned") {
                diags_.error(token.loc, "type mismatch");
                specs.type_name = "invalid";
            } else {
                specs.type_name = token.text;
            }
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
        if (ints.bad || saw_void || saw_bool || saw_float) {
            diags_.error(specs.loc, "type mismatch");
            specs.type_name = "invalid";
        } else {
            specs.type_name = canonical_int(ints);
            if (specs.type_name == "invalid") {
                diags_.error(specs.loc, "type mismatch");
            }
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

bool Parser::take_enum_underlying(std::string &out) {
    if (check(TokenKind::LBrace) || check(TokenKind::Semicolon) || eof()) {
        return false;
    }
    auto brace_after = [&](std::size_t begin) {
        for (std::size_t ahead = begin; ahead < 6; ++ahead) {
            const TokenKind kind = peek(ahead).kind;
            if (kind == TokenKind::LBrace) {
                return true;
            }
            if (kind == TokenKind::Semicolon || kind == TokenKind::Comma || kind == TokenKind::RBrace ||
                kind == TokenKind::End || kind == TokenKind::Eq) {
                return false;
            }
        }
        return false;
    };
    if (!brace_after(0)) {
        return false;
    }
    const auto alias = [&](const std::string &name) {
        return name == "i8" || name == "u8" || name == "i16" || name == "u16" || name == "i32" || name == "u32" ||
               name == "i64" || name == "u64" || name == "uint" || name == "ulong";
    };
    if (check(TokenKind::Identifier) && alias(peek().text) && peek(1).kind == TokenKind::LBrace) {
        out = peek().text;
        ++pos_;
        return true;
    }
    if (check(TokenKind::KwBool) && peek(1).kind == TokenKind::LBrace) {
        out = "bool";
        ++pos_;
        return true;
    }
    if (check(TokenKind::KwFloat) || check(TokenKind::KwDouble)) {
        error_here("type mismatch");
        ++pos_;
        return true;
    }
    if (is_int_spec(peek().kind)) {
        IntSpecs ints;
        while (is_int_spec(peek().kind)) {
            take_int_spec(ints, peek().kind);
            ++pos_;
        }
        if (!check(TokenKind::LBrace) || ints.bad) {
            error_here("type mismatch");
            out = "invalid";
            return true;
        }
        out = canonical_int(ints);
        if (out == "invalid") {
            error_here("type mismatch");
        }
        return true;
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::LBrace) {
        error_here("type mismatch");
        ++pos_;
        return true;
    }
    return false;
}

bool Parser::take_extern(bool &is_extern, bool &extern_c) {
    if (!eat(TokenKind::KwExtern)) {
        return false;
    }
    is_extern = true;
    if (!eat(TokenKind::LParen)) {
        return true;
    }
    auto finish = [&]() {
        if (check(TokenKind::Comma)) {
            diags_.error(peek().loc, "not supported yet");
            while (!check(TokenKind::RParen) && !eof()) {
                ++pos_;
            }
        }
        expect(TokenKind::RParen, "expected ')'");
    };
    if (!check(TokenKind::Identifier) || peek().text != "abi") {
        if (check(TokenKind::Identifier)) {
            diags_.error(peek().loc, "not supported yet");
            ++pos_;
        } else {
            error_here("expected 'abi'");
        }
        while (!check(TokenKind::RParen) && !check(TokenKind::Comma) && !eof()) {
            ++pos_;
        }
        finish();
        return true;
    }
    ++pos_;
    if (!eat(TokenKind::Eq)) {
        error_here("expected '='");
        while (!check(TokenKind::RParen) && !eof()) {
            ++pos_;
        }
        expect(TokenKind::RParen, "expected ')'");
        return true;
    }
    if (!check(TokenKind::StringLiteral)) {
        error_here("expected a string");
        while (!check(TokenKind::RParen) && !check(TokenKind::Comma) && !eof()) {
            ++pos_;
        }
        finish();
        return true;
    }
    if (peek().text == "c") {
        extern_c = true;
    } else if (peek().text == "tlc") {
        extern_c = false;
    } else {
        diags_.error(peek().loc, "not supported yet");
    }
    ++pos_;
    finish();
    return true;
}

int Parser::parse_struct_align() {
    expect(TokenKind::LParen, "expected '('");
    int value = 0;
    if (!check(TokenKind::Identifier) || peek().text != "align") {
        if (check(TokenKind::Identifier)) {
            diags_.error(peek().loc, "not supported yet");
            ++pos_;
        } else {
            error_here("expected 'align'");
        }
        while (!check(TokenKind::RParen) && !eof()) {
            ++pos_;
        }
        expect(TokenKind::RParen, "expected ')'");
        return 0;
    }
    ++pos_;
    if (!eat(TokenKind::Eq)) {
        error_here("expected '='");
    }
    const bool plain = check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.bits == 0;
    const uint64_t raw = plain ? peek().int_value : 0;
    const bool power = raw >= 1 && raw <= 4096 && (raw & (raw - 1)) == 0;
    if (!plain || !power) {
        error_here("type mismatch");
        if (check(TokenKind::IntLiteral) || check(TokenKind::Identifier)) {
            ++pos_;
        }
    } else {
        value = static_cast<int>(raw);
        ++pos_;
    }
    if (check(TokenKind::Comma)) {
        diags_.error(peek().loc, "not supported yet");
        while (!check(TokenKind::RParen) && !eof()) {
            ++pos_;
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    return value;
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
            if (check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.bits != 8 &&
                peek().suffix.bits != 16 && peek().suffix.bits != 64 && peek().int_value <= 2147483647ULL) {
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
    // A field is `name type`, and the type uses the same Go order as `var`.
    std::vector<FieldDecl> fields;
    while (!check(TokenKind::RBrace) && !eof()) {
        FieldDecl field;
        field.loc = peek().loc;
        if (!check(TokenKind::Identifier)) {
            error_here("expected an identifier");
            while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
                ++pos_;
            }
            eat(TokenKind::Semicolon);
            continue;
        }
        field.name = peek().text;
        field.loc = peek().loc;
        ++pos_;
        const PlacedType type = parse_go_type();
        if (type.struct_definition || type.enum_definition || type.ptr_to_array) {
            diags_.error(type.loc.valid() ? type.loc : field.loc, "not supported yet");
        }
        field.type_name = type.type_name;
        field.type_package = type.type_package;
        field.is_struct = type.is_struct;
        field.is_enum = type.is_enum;
        field.indirections = type.indirections;
        field.is_array = type.is_array;
        field.array_len = type.array_len;
        field.is_fn_ptr = type.is_fn_ptr;
        field.ret_indirections = type.ret_indirections;
        field.fn_params = type.fn_params;
        field.fn_variadic = type.fn_variadic;
        field.fn_results = type.fn_results;
        if (field.is_array && field.array_len < 0) {
            diags_.error(field.loc, "not supported yet");
        }
        fields.push_back(std::move(field));
        expect(TokenKind::Semicolon, "expected ';'");
    }
    return fields;
}


bool Parser::at_func_type(std::size_t ahead) const {
    return peek(ahead).kind == TokenKind::KwFunc && peek(ahead + 1).kind == TokenKind::LParen;
}

Parser::PlacedType Parser::parse_func_type() {
    PlacedType type;
    type.loc = peek().loc;
    type.is_fn_ptr = true;
    // The value is the function pointer. Stars on the result stay in ret_indirections.
    type.indirections = 1;
    expect(TokenKind::KwFunc, "expected 'func'");
    expect(TokenKind::LParen, "expected '('");
    while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !check(TokenKind::Semicolon) && !eof()) {
        if (check(TokenKind::Ellipsis)) {
            diags_.error(peek().loc, "not supported yet");
            ++pos_;
            break;
        }
        const std::size_t before = pos_;
        type.fn_params.push_back(fn_param_from(parse_placed_type(false, false)));
        if (pos_ == before) {
            ++pos_;
        }
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RParen, "expected ')'");

    std::vector<PlacedType> results;
    if (check(TokenKind::LParen)) {
        const SourceLoc list_loc = peek().loc;
        ++pos_;
        if (check(TokenKind::RParen)) {
            diags_.error(list_loc, "type mismatch");
            ++pos_;
            type.type_name = "invalid";
            type.fn_params.clear();
            return type;
        }
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !check(TokenKind::Semicolon) && !eof()) {
            const std::size_t before = pos_;
            results.push_back(parse_placed_type(false, false));
            if (pos_ == before) {
                ++pos_;
            }
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RParen, "expected ')'");
    } else {
        results.push_back(parse_placed_type(false, false));
    }
    bool reject = false;
    SourceLoc reject_loc = type.loc;
    for (const PlacedType &result : results) {
        if (result.is_fn_ptr || result.struct_definition || result.enum_definition) {
            reject = true;
            reject_loc = result.loc.valid() ? result.loc : reject_loc;
        }
    }
    if (reject || results.empty()) {
        if (reject) {
            diags_.error(reject_loc, "not supported yet");
        }
        type.type_name = "invalid";
        type.fn_params.clear();
        return type;
    }
    const PlacedType &first = results.front();
    type.type_name = first.type_name;
    type.type_package = first.type_package;
    type.is_struct = first.is_struct;
    type.is_enum = first.is_enum;
    type.ret_indirections = first.indirections;
    if (results.size() > 1) {
        type.fn_results.reserve(results.size());
        for (const PlacedType &result : results) {
            type.fn_results.push_back(fn_param_from(result));
        }
    }
    return type;
}

void Parser::parse_func_vars(std::vector<VarDecl> &vars, bool exported, bool is_static, bool is_extern,
                             bool extern_c) {
    reject_c_var(peek().loc);
    const PlacedType type = parse_func_type();
    while (true) {
        VarDecl var;
        var.loc = peek().loc;
        if (check(TokenKind::Identifier)) {
            var.name = peek().text;
            var.loc = peek().loc;
            ++pos_;
        } else {
            error_here("expected an identifier");
        }
        var.type_name = type.type_name;
        var.type_package = type.type_package;
        var.is_struct = type.is_struct;
        var.is_enum = type.is_enum;
        var.indirections = type.indirections;
        var.is_fn_ptr = type.is_fn_ptr;
        var.ret_indirections = type.ret_indirections;
        var.fn_params = type.fn_params;
        var.fn_variadic = type.fn_variadic;
        var.fn_results = type.fn_results;
        var.exported = exported;
        var.is_static = is_static;
        var.is_extern = is_extern;
        var.extern_c = extern_c;
        if (eat(TokenKind::Eq)) {
            var.init = parse_assignment();
        }
        vars.push_back(std::move(var));
        if (!eat(TokenKind::Comma)) {
            expect(TokenKind::Semicolon, "expected ';'");
            break;
        }
    }
}

Parser::PlacedType Parser::parse_placed_type(bool allow_const, bool allow_array) {
    if (at_func_type()) {
        return parse_func_type();
    }
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
    int stars = 0;
    while (check(TokenKind::Star)) {
        ++stars;
        ++pos_;
    }
    if (check(TokenKind::LParen) && peek(1).kind == TokenKind::Star) {
        type.is_fn_ptr = true;
        type.ret_indirections = stars;
        std::string name;
        SourceLoc name_loc;
        parse_fn_pointer(type.indirections, name, name_loc, type.fn_params, type.fn_variadic);
        if (!name.empty()) {
            diags_.error(name_loc, "expected ')'");
        }
        return type;
    }
    type.indirections = stars;
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

Parser::PlacedType Parser::parse_go_type() {
    auto take_base = [](PlacedType &type, const Specifiers &specs) {
        if (!type.loc.valid()) {
            type.loc = specs.loc;
        }
        type.type_name = specs.type_name;
        type.type_package = specs.type_package;
        type.is_struct = specs.is_struct;
        type.is_enum = specs.is_enum;
        type.is_const = specs.is_const;
        type.struct_definition = specs.struct_definition;
        type.enum_definition = specs.enum_definition;
    };
    if (at_func_type()) {
        return parse_func_type();
    }
    PlacedType type;
    type.loc = peek().loc;
    int outer = 0;
    while (check(TokenKind::Star)) {
        ++outer;
        ++pos_;
    }
    if (!check(TokenKind::LBracket)) {
        if (at_func_type()) {
            PlacedType fn = parse_func_type();
            if (outer > 0) {
                diags_.error(type.loc, "not supported yet");
                fn.indirections += outer;
            }
            return fn;
        }
        take_base(type, parse_specifiers(false, false, true));
        type.indirections = outer;
        return type;
    }
    // One star outside the brackets is a pointer to the array: `*[4]i32`.
    if (outer > 1) {
        diags_.error(type.loc, "not supported yet");
    } else if (outer == 1) {
        type.ptr_to_array = true;
    }
    const ArrayMark array = parse_array_suffix(true);
    type.is_array = array.is_array;
    type.array_len = array.length;
    int inner = 0;
    while (check(TokenKind::Star)) {
        ++inner;
        ++pos_;
    }
    if (check(TokenKind::LBracket)) {
        diags_.error(peek().loc, "not supported yet");
        while (eat(TokenKind::LBracket)) {
            while (!check(TokenKind::RBracket) && !check(TokenKind::Semicolon) && !check(TokenKind::Comma) &&
                   !check(TokenKind::RParen) && !check(TokenKind::Eq) && !check(TokenKind::LBrace) && !eof()) {
                ++pos_;
            }
            eat(TokenKind::RBracket);
        }
    }
    if (at_func_type()) {
        diags_.error(peek().loc, "not supported yet");
        PlacedType fn = parse_func_type();
        fn.is_array = type.is_array;
        fn.array_len = type.array_len;
        fn.indirections += inner;
        return fn;
    }
    take_base(type, parse_specifiers(false, false, true));
    type.indirections = inner;
    return type;
}

std::vector<Param> Parser::parse_params(bool allow_const, bool &variadic, const std::string &method_struct) {
    std::vector<Param> params;
    variadic = false;
    expect(TokenKind::LParen, "expected '('");
    const bool method = !method_struct.empty();
    auto receiver_form = [&]() {
        if (!check(TokenKind::Identifier)) {
            return false;
        }
        std::size_t ahead = 1;
        while (peek(ahead).kind == TokenKind::Star) {
            ++ahead;
        }
        if (peek(ahead).kind != TokenKind::Identifier || peek(ahead).text != "this") {
            return false;
        }
        const TokenKind after = peek(ahead + 1).kind;
        return after == TokenKind::Comma || after == TokenKind::RParen || after == TokenKind::Ellipsis;
    };
    auto take_receiver = [&](bool as_receiver) {
        Param param;
        param.loc = peek().loc;
        param.name = peek().text;
        ++pos_;
        while (eat(TokenKind::Star)) {
            ++param.indirections;
        }
        ++pos_;
        param.type_name = method_struct;
        param.is_struct = true;
        param.is_receiver = as_receiver;
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
            param.is_fn_ptr = type.is_fn_ptr;
            param.ret_indirections = type.ret_indirections;
            param.fn_params = type.fn_params;
            param.fn_variadic = type.fn_variadic;
        param.fn_results = type.fn_results;
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
    slot.is_fn_ptr = type.is_fn_ptr;
    slot.ret_indirections = type.ret_indirections;
    slot.fn_params = type.fn_params;
    slot.fn_variadic = type.fn_variadic;
    slot.fn_results = type.fn_results;
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

FunctionDecl Parser::parse_function(bool exported, bool is_static, bool is_extern, bool extern_c, bool allow_body) {
    FunctionDecl fn;
    fn.exported = exported;
    fn.is_static = is_static;
    fn.is_extern = is_extern;
    fn.extern_c = extern_c;
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
    fn.params = parse_params(extern_c, fn.variadic, fn.method_struct);
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

FnParam Parser::fn_param_from(const PlacedType &type) const {
    FnParam param;
    param.type_name = type.type_name;
    param.type_package = type.type_package;
    param.is_struct = type.is_struct;
    param.is_enum = type.is_enum;
    param.indirections = type.indirections;
    param.is_fn_ptr = type.is_fn_ptr;
    param.ret_indirections = type.ret_indirections;
    param.params = type.fn_params;
    param.variadic = type.fn_variadic;
    param.results = type.fn_results;
    return param;
}

void Parser::parse_fn_pointer(int &fn_depth, std::string &name, SourceLoc &loc, std::vector<FnParam> &params,
                              bool &variadic) {
    expect(TokenKind::LParen, "expected '('");
    fn_depth = 0;
    while (eat(TokenKind::Star)) {
        ++fn_depth;
    }
    if (fn_depth < 1) {
        error_here("expected '*'");
    }
    if (check(TokenKind::Identifier)) {
        name = peek().text;
        loc = peek().loc;
        ++pos_;
    }
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::LParen, "expected '('");
    if (!check(TokenKind::RParen)) {
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
            if (check(TokenKind::Ellipsis)) {
                variadic = true;
                ++pos_;
                break;
            }
            params.push_back(fn_param_from(parse_placed_type(false, false)));
            if (check(TokenKind::Identifier)) {
                ++pos_;
            }
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
    }
    expect(TokenKind::RParen, "expected ')'");
}

Parser::Declarator Parser::parse_declarator() {
    Declarator decl;
    int stars = 0;
    while (check(TokenKind::Star)) {
        ++stars;
        ++pos_;
    }
    if (check(TokenKind::LParen) && peek(1).kind == TokenKind::Star) {
        decl.is_fn_ptr = true;
        decl.ret_indirections = stars;
        parse_fn_pointer(decl.indirections, decl.name, decl.loc, decl.fn_params, decl.fn_variadic);
        if (decl.name.empty()) {
            error_here("expected an identifier");
        }
        return decl;
    }
    decl.indirections = stars;
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
    } else if (check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.bits != 8 &&
               peek().suffix.bits != 16 && peek().suffix.bits != 64 && peek().int_value > 0 &&
               peek().int_value <= 2147483647ULL) {
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

} // namespace tlc
