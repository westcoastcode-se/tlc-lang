// Types: specifiers, struct and enum bodies, function types, and declarators.
#include "parse/parser.hpp"
#include "parse/parse_util.hpp"

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
        if (token.kind == TokenKind::KwExtern) {
            take_extern(specs.is_extern, specs.extern_c, specs.link_name, specs.name_loc);
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
        // `var s struct { value i32; }` is a struct. `struct Point` is not a type spelling.
        if (type_only && token.kind == TokenKind::KwStruct && peek(1).kind == TokenKind::LBrace) {
            const SourceLoc loc = token.loc;
            ++pos_;
            ++pos_;
            const std::string name = ".struct." + std::to_string(++anonymous_struct_count_);
            StructMembers members = parse_struct_members(name);
            expect(TokenKind::RBrace, "expected '}'");
            StructDecl item;
            item.loc = loc;
            item.name = name;
            item.fields = std::move(members.fields);
            item.anonymous = true;
            anonymous_structs_.insert(anonymous_structs_.end(), std::make_move_iterator(members.anonymous.begin()),
                                      std::make_move_iterator(members.anonymous.end()));
            anonymous_structs_.push_back(std::move(item));
            specs.saw_type = true;
            specs.type_name = name;
            break;
        }
        // `var u union(Kind) { A struct { n i32; }; }` is a union. `union Shape` is not a type spelling.
        if (type_only && token.kind == TokenKind::KwUnion && peek(1).kind == TokenKind::LParen) {
            const SourceLoc loc = token.loc;
            ++pos_;
            ++pos_;
            std::string tag;
            std::string tag_package;
            if (!parse_union_tag(tag_package, tag) || !eat(TokenKind::LBrace)) {
                if (!tag.empty()) {
                    error_here("expected '{'");
                }
                specs.saw_type = true;
                specs.type_name = "invalid";
                break;
            }
            const std::string name = ".union." + std::to_string(++anonymous_struct_count_);
            UnionBody variants = parse_union_variants(name);
            expect(TokenKind::RBrace, "expected '}'");
            anonymous_structs_.insert(anonymous_structs_.end(), std::make_move_iterator(variants.anonymous.begin()),
                                      std::make_move_iterator(variants.anonymous.end()));
            StructDecl item;
            item.loc = loc;
            item.name = name;
            item.fields = std::move(variants.fields);
            item.anonymous = true;
            item.is_union = true;
            item.tag_name = std::move(tag);
            item.tag_package = std::move(tag_package);
            anonymous_structs_.push_back(std::move(item));
            specs.saw_type = true;
            specs.type_name = name;
            break;
        }
        // `struct`, `union`, and `trait` are only written in the type definition. A variable names the type.
        if (token.kind == TokenKind::KwStruct || token.kind == TokenKind::KwEnum || token.kind == TokenKind::KwUnion ||
            token.kind == TokenKind::KwTrait) {
            const bool definition_keyword = token.kind == TokenKind::KwStruct || token.kind == TokenKind::KwUnion ||
                                            token.kind == TokenKind::KwTrait;
            error_at(token, definition_keyword ? "expected a type" : "not supported yet");
            ++pos_;
            if (check(TokenKind::LParen)) {
                int depth = 0;
                do {
                    if (check(TokenKind::LParen)) {
                        ++depth;
                    } else if (check(TokenKind::RParen)) {
                        --depth;
                    }
                    ++pos_;
                } while (depth > 0 && !eof());
            }
            if (check(TokenKind::Identifier)) {
                ++pos_;
            }
            specs.saw_type = true;
            specs.type_name = "invalid";
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
    if (!specs.type_name.empty() && specs.type_name != "invalid") {
        type_names_.insert(specs.type_name);
    }
    reject_tlc_link_name(specs.extern_c, specs.link_name, specs.name_loc);
    return specs;
}

std::string Parser::parse_enum_underlying() {
    expect(TokenKind::LParen, "expected '('");
    std::string out = "invalid";
    const auto alias = [](const std::string &name) {
        return name == "i8" || name == "u8" || name == "i16" || name == "u16" || name == "i32" || name == "u32" ||
               name == "i64" || name == "u64";
    };
    if (check(TokenKind::Identifier) && alias(peek().text)) {
        out = peek().text;
        ++pos_;
    } else if (check(TokenKind::KwBool)) {
        out = "bool";
        ++pos_;
    } else if (check(TokenKind::KwFloat) || check(TokenKind::KwDouble)) {
        error_here("type mismatch");
        ++pos_;
    } else if (is_int_spec(peek().kind)) {
        IntSpecs ints;
        const SourceLoc spec_loc = peek().loc;
        while (is_int_spec(peek().kind)) {
            take_int_spec(ints, peek().kind);
            ++pos_;
        }
        if (ints.bad) {
            diags_.error(spec_loc, "type mismatch");
        } else {
            out = canonical_int(ints);
            if (out == "invalid") {
                diags_.error(spec_loc, "type mismatch");
            }
        }
    } else if (check(TokenKind::Identifier)) {
        error_here("type mismatch");
        ++pos_;
    } else {
        error_here("expected a type");
    }
    if (check(TokenKind::Comma)) {
        diags_.error(peek().loc, "not supported yet");
        while (!check(TokenKind::RParen) && !eof()) {
            ++pos_;
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    return out;
}

void Parser::reject_tlc_link_name(bool extern_c, const std::string &link_name, SourceLoc name_loc) {
    // `name=` chooses a C object symbol. A tlc library name belongs to the compiler.
    if (link_name.empty() || extern_c) {
        return;
    }
    diags_.error(name_loc.valid() ? name_loc : peek().loc, "name requires abi=\"c\"");
}

bool Parser::take_extern(bool &is_extern, bool &extern_c, std::string &link_name, SourceLoc &name_loc) {
    if (!eat(TokenKind::KwExtern)) {
        return false;
    }
    is_extern = true;
    if (!eat(TokenKind::LParen)) {
        return true;
    }
    // A later `extern` in the same specifier list must keep a name already stored.
    bool saw_abi = false;
    bool saw_name = false;
    bool any = false;
    auto skip_arg = [&]() {
        while (!check(TokenKind::RParen) && !check(TokenKind::Comma) && !eof()) {
            ++pos_;
        }
    };
    while (!check(TokenKind::RParen) && !eof()) {
        any = true;
        if (!check(TokenKind::Identifier)) {
            error_here("expected 'abi'");
            skip_arg();
        } else {
            const std::string key = peek().text;
            const SourceLoc key_loc = peek().loc;
            const bool is_abi = key == "abi";
            const bool is_name = key == "name";
            ++pos_;
            if (!is_abi && !is_name) {
                diags_.error(key_loc, "not supported yet");
                skip_arg();
            } else {
                const bool duplicate = (is_abi && saw_abi) || (is_name && saw_name);
                if (duplicate) {
                    diags_.error(key_loc, "redefinition");
                }
                if (is_abi) {
                    saw_abi = true;
                } else {
                    saw_name = true;
                }
                if (!eat(TokenKind::Eq)) {
                    error_here("expected '='");
                    skip_arg();
                } else if (!check(TokenKind::StringLiteral)) {
                    error_here("expected a string");
                    skip_arg();
                } else {
                    if (!duplicate && is_abi) {
                        if (peek().text == "c") {
                            extern_c = true;
                        } else if (peek().text == "tlc") {
                            extern_c = false;
                        } else {
                            diags_.error(peek().loc, "not supported yet");
                        }
                    } else if (!duplicate) {
                        if (peek().text.empty()) {
                            diags_.error(peek().loc, "type mismatch");
                        } else if (!link_name.empty() && link_name != peek().text) {
                            diags_.error(peek().loc, "redefinition");
                        } else {
                            link_name = peek().text;
                            name_loc = peek().loc;
                        }
                    }
                    ++pos_;
                }
            }
        }
        if (!check(TokenKind::Comma)) {
            break;
        }
        const SourceLoc comma = peek().loc;
        ++pos_;
        if (check(TokenKind::RParen) || eof()) {
            diags_.error(comma, "not supported yet");
            break;
        }
    }
    if (!any) {
        error_here("expected 'abi'");
    }
    expect(TokenKind::RParen, "expected ')'");
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

bool Parser::parse_field(FieldDecl &field, bool extern_c, bool allow_init) {
    // A field is `name type`, and the type uses the same Go order as `var`.
    field.loc = peek().loc;
    if (!check(TokenKind::Identifier)) {
        error_here("expected an identifier");
        while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
            ++pos_;
        }
        eat(TokenKind::Semicolon);
        return false;
    }
    field.name = peek().text;
    field.loc = peek().loc;
    ++pos_;
    // A C union field is an ordinary type. `data union(Kind) { ... }` is a tagged union.
    if (extern_c && check(TokenKind::KwUnion) && peek(1).kind == TokenKind::LParen) {
        diags_.error(peek().loc, "extern(abi=\"c\") on a tagged union");
        ++pos_;
        ++pos_;
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
            ++pos_;
        }
        expect(TokenKind::RParen, "expected ')'");
        if (eat(TokenKind::LBrace)) {
            int depth = 1;
            while (depth > 0 && !eof()) {
                if (check(TokenKind::LBrace)) {
                    ++depth;
                } else if (check(TokenKind::RBrace)) {
                    --depth;
                }
                ++pos_;
            }
        }
        eat(TokenKind::Semicolon);
        return false;
    }
    PlacedType type = parse_go_type(true);
    if (type.struct_definition || type.enum_definition) {
        diags_.error(type.loc.valid() ? type.loc : field.loc, "not supported yet");
    }
    field.type_name = type.type_name;
    field.type_package = type.type_package;
    field.is_struct = type.is_struct;
    field.is_enum = type.is_enum;
    field.indirections = type.indirections;
    field.optional = type.optional;
    field.const_pointee = type.const_pointee;
    field.is_array = type.is_array;
    field.elem_const = type.elem_const;
    field.is_const = type.is_const;
    field.elem_slice = type.elem_slice;
    field.shells = type.shells;
    field.array_len = type.array_len;
    field.array_len_name = type.array_len_name;
    field.type_args = type.type_args;
    field.is_fn_ptr = type.is_fn_ptr;
    field.ret_indirections = type.ret_indirections;
    field.ret_optional = type.ret_optional;
    field.ret_const_pointee = type.ret_const_pointee;
    field.fn_params = type.fn_params;
    field.fn_variadic = type.fn_variadic;
    field.fn_results = type.fn_results;
    field.is_tuple = type.is_tuple;
    field.tuple_parts = std::move(type.tuple_parts);
    // `x i32 = 1` is the value of a field that an initializer does not name.
    // A trait field is `name type;`. A value there is not that syntax.
    if (check(TokenKind::Eq)) {
        if (!allow_init) {
            diags_.error(peek().loc, "expected ';'");
            while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
                ++pos_;
            }
            eat(TokenKind::Semicolon);
            return true;
        }
        ++pos_;
        field.init = parse_expr();
    }
    expect(TokenKind::Semicolon, "expected ';'");
    return true;
}

std::vector<FieldDecl> Parser::parse_fields(bool extern_c) {
    std::vector<FieldDecl> fields;
    while (!check(TokenKind::RBrace) && !eof()) {
        FieldDecl field;
        if (parse_field(field, extern_c)) {
            fields.push_back(std::move(field));
        }
    }
    return fields;
}


bool Parser::at_func_type(std::size_t ahead) const {
    return peek(ahead).kind == TokenKind::KwFunc && peek(ahead + 1).kind == TokenKind::LParen;
}

Parser::PlacedType Parser::parse_func_type(bool as_cast) {
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
        PlacedType param = parse_placed_type(false);
        drop_bare_tuple(param);
        type.fn_params.push_back(fn_param_from(param));
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
            results.push_back(parse_placed_type(false));
            if (pos_ == before) {
                ++pos_;
            }
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RParen, "expected ')'");
    } else {
        results.push_back(parse_placed_type(false, !as_cast));
    }
    bool reject = false;
    SourceLoc reject_loc = type.loc;
    for (PlacedType &result : results) {
        // `((i32, i32))` is one tuple result. `((i32, i32), i32)` stays rejected.
        if (result.is_tuple && results.size() != 1) {
            drop_bare_tuple(result);
        }
    }
    for (const PlacedType &result : results) {
        // One result may be a function pointer. Several results may not include one.
        if (result.struct_definition || result.enum_definition || (result.is_fn_ptr && results.size() != 1)) {
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
    if (first.is_fn_ptr) {
        // The nested pointer is the one entry in `fn_results`. The flattened name is the
        // innermost result, so a later empty check still sees a type. Stars on that name
        // are not this pointer.
        if (first.type_name == "invalid") {
            type.type_name = "invalid";
            type.fn_params.clear();
            return type;
        }
        type.fn_results.push_back(fn_param_from(first));
        type.type_name = first.type_name;
        type.ret_indirections = first.ret_indirections;
        type.ret_optional = first.ret_optional;
        type.ret_const_pointee = first.ret_const_pointee;
        return type;
    }
    if (first.is_tuple) {
        // One tuple result. The element types live on that one entry.
        // Several results are a longer `fn_results` list, with no tuple flag.
        type.fn_results.push_back(fn_param_from(first));
        return type;
    }
    type.type_name = first.type_name;
    type.type_package = first.type_package;
    type.is_struct = first.is_struct;
    type.is_enum = first.is_enum;
    type.ret_indirections = first.indirections;
    type.ret_optional = first.optional;
    type.ret_const_pointee = first.const_pointee;
    // One result keeps its arguments here. `func (i32) Box[i32]` is that result.
    // Several results keep each argument list on `fn_results`.
    if (results.size() == 1) {
        type.type_args = first.type_args;
    }
    if (results.size() > 1) {
        type.fn_results.reserve(results.size());
        for (const PlacedType &result : results) {
            type.fn_results.push_back(fn_param_from(result));
        }
    }
    return type;
}

void Parser::parse_func_vars(std::vector<VarDecl> &vars, bool exported, bool is_static, bool is_extern,
                             bool extern_c, std::string link_name) {
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
        var.optional = type.optional;
        var.const_pointee = type.const_pointee;
        var.is_fn_ptr = type.is_fn_ptr;
        var.ret_optional = type.ret_optional;
        var.ret_const_pointee = type.ret_const_pointee;
        var.ret_indirections = type.ret_indirections;
        var.fn_params = type.fn_params;
        var.fn_variadic = type.fn_variadic;
        var.fn_results = type.fn_results;
        var.type_args = type.type_args;
        var.exported = exported;
        var.is_static = is_static;
        var.is_extern = is_extern;
        var.extern_c = extern_c;
        var.link_name = link_name;
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

Parser::PointerPrefix Parser::parse_pointer_prefix() {
    PointerPrefix prefix;
    while (check(TokenKind::Question) || check(TokenKind::Star)) {
        if (prefix.count >= 31) {
            diags_.error(peek().loc, "not supported yet");
            break;
        }
        bool opt = false;
        if (check(TokenKind::Question)) {
            if (peek(1).kind != TokenKind::Star) {
                diags_.error(peek().loc, "not supported yet");
                ++pos_;
                break;
            }
            ++pos_;
            opt = true;
        }
        if (!eat(TokenKind::Star)) {
            break;
        }
        if (opt) {
            prefix.optional |= 1u << prefix.count;
        }
        if (check(TokenKind::KwConst)) {
            prefix.const_pointee |= 1u << prefix.count;
            ++pos_;
        }
        ++prefix.count;
    }
    return prefix;
}

Parser::PlacedType Parser::parse_tuple_type() {
    const SourceLoc loc = peek().loc;
    expect(TokenKind::LParen, "expected '('");
    std::vector<PlacedType> parts;
    bool saw_comma = false;
    if (!check(TokenKind::RParen)) {
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !check(TokenKind::Semicolon) && !eof()) {
            const std::size_t before = pos_;
            parts.push_back(parse_placed_type(false));
            if (pos_ == before) {
                ++pos_;
            }
            if (!eat(TokenKind::Comma)) {
                break;
            }
            saw_comma = true;
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    // `(i32)` is the type `i32`. A tuple needs two elements. `(i32,)` is not one.
    if (parts.size() == 1 && !saw_comma) {
        return std::move(parts.front());
    }
    if (parts.size() < 2) {
        diags_.error(loc, "type mismatch");
        PlacedType bad;
        bad.loc = loc;
        bad.type_name = "invalid";
        return bad;
    }
    PlacedType type;
    type.loc = loc;
    type.is_tuple = true;
    type.tuple_parts.reserve(parts.size());
    for (PlacedType &part : parts) {
        if (part.is_tuple || part.is_fn_ptr) {
            diags_.error(part.loc.valid() ? part.loc : loc, "not supported yet");
            part.is_tuple = false;
            part.is_fn_ptr = false;
            part.tuple_parts.clear();
            part.type_name = "invalid";
        }
        type.tuple_parts.push_back(slot_from_type(part));
    }
    return type;
}

ResultSlot Parser::slot_from_type(const PlacedType &type) const {
    ResultSlot slot;
    slot.type_name = type.type_name;
    slot.type_package = type.type_package;
    slot.is_struct = type.is_struct;
    slot.is_enum = type.is_enum;
    slot.indirections = type.indirections;
    slot.optional = type.optional;
    slot.const_pointee = type.const_pointee;
    slot.is_fn_ptr = type.is_fn_ptr;
    slot.ret_indirections = type.ret_indirections;
    slot.fn_params = type.fn_params;
    slot.fn_variadic = type.fn_variadic;
    slot.fn_results = type.fn_results;
    slot.is_array = type.is_array;
    slot.elem_const = type.elem_const;
    slot.elem_slice = type.elem_slice;
    slot.shells = type.shells;
    slot.array_len = type.array_len;
    slot.array_len_name = type.array_len_name;
    slot.type_args = type.type_args;
    slot.is_tuple = type.is_tuple;
    slot.tuple_parts = type.tuple_parts;
    return slot;
}

void Parser::drop_bare_tuple(PlacedType &type) {
    if (!type.is_tuple) {
        return;
    }
    diags_.error(type.loc.valid() ? type.loc : peek().loc, "not supported yet");
    type.is_tuple = false;
    type.tuple_parts.clear();
    type.type_name = "invalid";
}

Parser::PlacedType Parser::parse_placed_type(bool allow_const, bool allow_fn_declarator) {
    if (check(TokenKind::LParen)) {
        return parse_tuple_type();
    }
    if (check(TokenKind::LBracket)) {
        return parse_go_type();
    }
    if (check(TokenKind::Star) || (check(TokenKind::Question) && peek(1).kind == TokenKind::Star)) {
        std::size_t ahead = 0;
        while (peek(ahead).kind == TokenKind::Star ||
               (peek(ahead).kind == TokenKind::Question && peek(ahead + 1).kind == TokenKind::Star)) {
            if (peek(ahead).kind == TokenKind::Question) {
                ++ahead;
            }
            ++ahead;
            if (peek(ahead).kind == TokenKind::KwConst) {
                ++ahead;
            }
        }
        if (peek(ahead).kind == TokenKind::LBracket) {
            return parse_go_type();
        }
    }
    if (at_func_type()) {
        return parse_func_type();
    }
    // Pointers are prefix: `*int`, `*Point`, `*pkg.Point`, `*const char`, `**int`.
    auto pointer_prefix = [&]() {
        std::size_t ahead = 0;
        if (peek(ahead).kind == TokenKind::Question) {
            if (peek(ahead + 1).kind != TokenKind::Star) {
                return false;
            }
            ++ahead;
        }
        if (peek(ahead).kind != TokenKind::Star) {
            return false;
        }
        while (peek(ahead).kind == TokenKind::Star || peek(ahead).kind == TokenKind::Question) {
            if (peek(ahead).kind == TokenKind::Question && peek(ahead + 1).kind != TokenKind::Star) {
                break;
            }
            if (peek(ahead).kind == TokenKind::Question) {
                ++ahead;
            }
            if (peek(ahead).kind == TokenKind::Star) {
                ++ahead;
            }
        }
        const Token &next = peek(ahead);
        if (is_int_spec(next.kind) || next.kind == TokenKind::KwVoid || next.kind == TokenKind::KwBool ||
            next.kind == TokenKind::KwFloat || next.kind == TokenKind::KwDouble || next.kind == TokenKind::KwConst ||
            next.kind == TokenKind::KwStruct || next.kind == TokenKind::KwEnum || next.kind == TokenKind::KwUnion) {
            return true;
        }
        return next.kind == TokenKind::Identifier;
    };
    PlacedType type;
    int leading = 0;
    uint32_t leading_optional = 0;
    uint32_t leading_const = 0;
    if (pointer_prefix()) {
        type.loc = peek().loc;
        const PointerPrefix prefix = parse_pointer_prefix();
        leading = prefix.count;
        leading_optional = prefix.optional;
        leading_const = prefix.const_pointee;
    }
    const Specifiers specs = parse_specifiers(false, allow_const, true);
    type.loc = specs.loc;
    type.type_name = specs.type_name;
    type.type_package = specs.type_package;
    type.is_struct = specs.is_struct;
    type.is_enum = specs.is_enum;
    type.is_const = specs.is_const;
    type.struct_definition = specs.struct_definition;
    type.enum_definition = specs.enum_definition;
    if (allow_fn_declarator && check(TokenKind::LParen) && peek(1).kind == TokenKind::Star) {
        // `*int (*fp)(int)`. The stars belong to the return type.
        type.is_fn_ptr = true;
        type.ret_indirections = leading;
        type.ret_optional = leading_optional;
        type.ret_const_pointee = leading_const;
        std::string name;
        SourceLoc name_loc;
        parse_fn_pointer(type.indirections, name, name_loc, type.fn_params, type.fn_variadic);
        if (!name.empty()) {
            diags_.error(name_loc, "expected ')'");
        }
        return type;
    }
    if (check(TokenKind::Star)) {
        diags_.error(peek().loc, "type mismatch");
        while (eat(TokenKind::Star)) {
        }
    }
    type.indirections = leading;
    type.optional = leading_optional;
    type.const_pointee = leading_const;
    if (check(TokenKind::LBracket)) {
        // `Vec[i32]` is a type argument list. An array is prefix only: `[4]i32`.
        const bool named = !primitive_type_name(type.type_name) && !type.type_name.empty() && type.type_name != "invalid";
        if (named) {
            type.type_args = parse_type_args();
        } else {
            diags_.error(peek().loc, "type mismatch");
            while (eat(TokenKind::LBracket)) {
                while (!check(TokenKind::RBracket) && !check(TokenKind::Semicolon) && !check(TokenKind::Comma) &&
                       !check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                    ++pos_;
                }
                eat(TokenKind::RBracket);
            }
        }
    }
    return type;
}

Parser::PlacedType Parser::parse_go_type(bool allow_value_const) {
    auto take_base = [](PlacedType &type, const Specifiers &specs) {
        if (!type.loc.valid()) {
            type.loc = specs.loc;
        }
        type.type_name = specs.type_name;
        type.type_package = specs.type_package;
        type.is_struct = specs.is_struct;
        type.is_enum = specs.is_enum;
        // `kind const Type` set this before the leaf was parsed. Specifiers do not clear it.
        type.is_const = specs.is_const || type.is_const;
        type.struct_definition = specs.struct_definition;
        type.enum_definition = specs.enum_definition;
    };
    // One `[...]` constructor. A further pair is another shell, not a special case.
    auto take_bracket = [&](TypeShell &shell) {
        const SourceLoc loc = peek().loc;
        ++pos_;
        if (check(TokenKind::RBracket)) {
            ++pos_;
            shell.kind = TypeShell::Kind::Slice;
            return;
        }
        if (check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.bits != 8 &&
            peek().suffix.bits != 16 && peek().suffix.bits != 64 && peek().int_value > 0 &&
            peek().int_value <= 2147483647ULL) {
            shell.kind = TypeShell::Kind::Array;
            shell.length = static_cast<int>(peek().int_value);
            ++pos_;
            expect(TokenKind::RBracket, "expected ']'");
            return;
        }
        if (check(TokenKind::Identifier)) {
            shell.kind = TypeShell::Kind::Array;
            shell.length_name = peek().text;
            ++pos_;
            expect(TokenKind::RBracket, "expected ']'");
            return;
        }
        diags_.error(loc, "not supported yet");
        while (!check(TokenKind::RBracket) && !check(TokenKind::Semicolon) && !check(TokenKind::Comma) &&
               !check(TokenKind::RParen) && !check(TokenKind::Eq) && !check(TokenKind::LBrace) && !eof()) {
            ++pos_;
        }
        eat(TokenKind::RBracket);
        shell.kind = TypeShell::Kind::Slice;
    };
    // Flags the older paths still read. A shape they cannot spell stays only in `shells`.
    auto project = [](PlacedType &type) {
        const std::vector<TypeShell> &shells = type.shells;
        if (shells.empty()) {
            return;
        }
        std::size_t index = 0;
        if (shells[0].kind == TypeShell::Kind::Pointer && shells.size() >= 2 &&
            shells[1].kind == TypeShell::Kind::Array && !shells[0].optional && shells.size() == 2) {
            type.ptr_to_array = true;
            if (shells[0].const_pointee) {
                type.const_pointee |= 1u;
            }
            index = 1;
        }
        if (index >= shells.size() || shells[index].kind == TypeShell::Kind::Pointer) {
            return;
        }
        type.is_array = true;
        if (shells[index].kind == TypeShell::Kind::Slice) {
            type.array_len = -1;
        } else {
            type.array_len = shells[index].length;
            type.array_len_name = shells[index].length_name;
        }
        if (index + 1 < shells.size() && shells[index + 1].kind == TypeShell::Kind::Slice && index + 2 == shells.size()) {
            type.elem_slice = true;
        }
    };
    if (at_func_type()) {
        return parse_func_type();
    }
    if (check(TokenKind::LParen)) {
        return parse_tuple_type();
    }
    PlacedType type;
    type.loc = peek().loc;
    while (true) {
        const PointerPrefix prefix = parse_pointer_prefix();
        if (check(TokenKind::LBracket)) {
            for (int star = 0; star < prefix.count; ++star) {
                TypeShell shell;
                shell.kind = TypeShell::Kind::Pointer;
                shell.optional = (prefix.optional & (1u << star)) != 0;
                shell.const_pointee = (prefix.const_pointee & (1u << star)) != 0;
                type.shells.push_back(shell);
            }
            TypeShell shell;
            take_bracket(shell);
            type.shells.push_back(std::move(shell));
            continue;
        }
        if (at_func_type()) {
            if (prefix.count > 0 || !type.shells.empty()) {
                diags_.error(type.loc, "not supported yet");
            }
            return parse_func_type();
        }
        // `[]const u8` marks the element. `kind const Type` marks the field value.
        // `const i32` on a variable, and `[]const [4]T`, are not forms.
        if (check(TokenKind::KwConst)) {
            if (type.shells.empty()) {
                if (!allow_value_const) {
                    diags_.error(peek().loc, "not supported yet");
                } else {
                    type.is_const = true;
                }
                ++pos_;
            } else {
                type.elem_const = true;
                ++pos_;
                if (check(TokenKind::LBracket)) {
                    diags_.error(peek().loc, "not supported yet");
                    while (eat(TokenKind::LBracket)) {
                        while (!check(TokenKind::RBracket) && !eof()) {
                            ++pos_;
                        }
                        eat(TokenKind::RBracket);
                    }
                }
            }
        }
        take_base(type, parse_specifiers(false, false, true));
        type.indirections = prefix.count;
        type.optional = prefix.optional;
        type.const_pointee = prefix.const_pointee;
        if (check(TokenKind::LBracket)) {
            type.type_args = parse_type_args();
        }
        project(type);
        return type;
    }
}

std::vector<Param> Parser::parse_params(bool allow_const, bool &variadic, const std::string &method_struct,
                                    bool comptime_params) {
    std::vector<Param> params;
    variadic = false;
    expect(TokenKind::LParen, "expected '('");
    const bool method = !method_struct.empty();
    auto receiver_form = [&]() {
        if (!check(TokenKind::Identifier)) {
            return false;
        }
        std::size_t ahead = 1;
        while (peek(ahead).kind == TokenKind::Star || peek(ahead).kind == TokenKind::Question) {
            if (peek(ahead).kind == TokenKind::Question && peek(ahead + 1).kind != TokenKind::Star) {
                break;
            }
            if (peek(ahead).kind == TokenKind::Question) {
                ++ahead;
            }
            if (peek(ahead).kind == TokenKind::Star) {
                ++ahead;
            }
            if (peek(ahead).kind == TokenKind::KwConst) {
                ++ahead;
            }
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
        // A receiver is `name this` or `name *this`. A second `*` is where `this` belongs.
        std::size_t ahead = 0;
        int stars = 0;
        SourceLoc extra_star;
        while (peek(ahead).kind == TokenKind::Question || peek(ahead).kind == TokenKind::Star) {
            if (peek(ahead).kind == TokenKind::Question) {
                if (peek(ahead + 1).kind != TokenKind::Star) {
                    break;
                }
                ++ahead;
            }
            if (peek(ahead).kind != TokenKind::Star) {
                break;
            }
            ++stars;
            if (stars == 2) {
                extra_star = peek(ahead).loc;
            }
            ++ahead;
            if (peek(ahead).kind == TokenKind::KwConst) {
                ++ahead;
            }
        }
        if (stars > 1) {
            diags_.error(extra_star, "expected 'this'");
        }
        const PointerPrefix prefix = parse_pointer_prefix();
        param.indirections = prefix.count;
        param.optional = prefix.optional;
        param.const_pointee = prefix.const_pointee;
        ++pos_;
        param.type_name = method_struct;
        param.is_struct = true;
        param.is_receiver = as_receiver;
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
            if (check(TokenKind::KwComptime)) {
                const SourceLoc mark = peek().loc;
                ++pos_;
                if (comptime_params) {
                    param.is_comptime = true;
                } else {
                    diags_.error(mark, "not supported yet");
                }
            }
            const PlacedType type = parse_placed_type(allow_const);
            if (type.struct_definition || type.enum_definition) {
                diags_.error(type.loc, "not supported yet");
            }
            param.type_name = type.type_name;
            param.type_package = type.type_package;
            param.is_struct = type.is_struct;
            param.is_enum = type.is_enum;
            param.is_const = type.is_const;
            param.indirections = type.indirections;
            param.optional = type.optional;
            param.const_pointee = type.const_pointee;
            param.is_array = type.is_array;
            param.elem_const = type.elem_const;
            param.elem_slice = type.elem_slice;
            param.shells = type.shells;
            param.array_len = type.array_len;
            param.is_fn_ptr = type.is_fn_ptr;
            param.ret_indirections = type.ret_indirections;
            param.ret_optional = type.ret_optional;
            param.ret_const_pointee = type.ret_const_pointee;
            param.fn_params = type.fn_params;
            param.fn_variadic = type.fn_variadic;
            param.fn_results = type.fn_results;
            param.array_len_name = type.array_len_name;
            param.type_args = type.type_args;
            param.is_tuple = type.is_tuple;
            param.tuple_parts = type.tuple_parts;
            params.push_back(std::move(param));
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    return params;
}

void Parser::take_result(FunctionDecl &fn, const PlacedType &type) {
    PlacedType written = type;
    if (written.struct_definition || written.enum_definition) {
        diags_.error(written.loc, "not supported yet");
    }
    // `func f() (i32, i32)` is two results. The `(` was already consumed.
    // `((i32, i32))` is one tuple result. `((i32, i32), i32)` keeps that tuple as one result.
    fn.results.push_back(slot_from_type(written));
}

void Parser::parse_results(FunctionDecl &fn) {
    // A body with no written result takes the type from its `return` expressions.
    // A prototype still writes the type: `func add(a i32, b i32);` is `expected a type`.
    if (check(TokenKind::LBrace)) {
        fn.infer_result = true;
        return;
    }
    // `typeof` is not a result. `(typeof(x))` is the same error, from `parse_placed_type`.
    if (check(TokenKind::KwTypeof)) {
        error_here("expected a type");
        parse_unary();
        ResultSlot slot;
        slot.type_name = "invalid";
        fn.results.push_back(std::move(slot));
        fn.return_type = "invalid";
        return;
    }
    if (check(TokenKind::LParen)) {
        const SourceLoc loc = peek().loc;
        ++pos_;
        if (check(TokenKind::RParen)) {
            diags_.error(loc, "type mismatch");
            ++pos_;
        } else {
            while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !check(TokenKind::Semicolon) && !eof()) {
                take_result(fn, parse_placed_type(false));
                if (!eat(TokenKind::Comma)) {
                    break;
                }
            }
            expect(TokenKind::RParen, "expected ')'");
        }
    } else {
        take_result(fn, parse_placed_type(false));
    }
    // Several results are `(i32, i32)`. One tuple result is `((i32, i32))`.
    // `((i32, i32), i32)` keeps the tuple as one of those results.
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
    fn.optional = first.optional;
    fn.const_pointee = first.const_pointee;
}

FunctionDecl Parser::parse_function(bool exported, bool is_static, bool is_extern, bool extern_c, bool allow_body,
                                    std::string link_name, bool comptime_fn) {
    FunctionDecl fn;
    fn.exported = exported;
    fn.is_static = is_static;
    fn.is_extern = is_extern;
    fn.extern_c = extern_c;
    fn.link_name = std::move(link_name);
    fn.is_comptime = comptime_fn;
    fn.loc = peek().loc;
    expect(TokenKind::KwFunc, "expected 'func'");
    if (check(TokenKind::Identifier)) {
        fn.name = peek().text;
        fn.loc = peek().loc;
        ++pos_;
        if (bracket_then_colon_colon()) {
            fn.method_params = parse_method_params();
            fn.method_struct = fn.name;
            expect(TokenKind::ColonColon, "expected '::'");
            if (check(TokenKind::Identifier)) {
                fn.name = peek().text;
                fn.loc = peek().loc;
                ++pos_;
            } else {
                fn.name.clear();
                error_here("expected an identifier");
            }
        } else if (check(TokenKind::ColonColon)) {
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
    if (check(TokenKind::LBracket)) {
        // `func Type::name[T type]` and `func Vec[T]::get[U type]` are method templates.
        fn.type_params = parse_type_params();
    }
    // `comptime` on a parameter is a value known while compiling. A `comptime func` requires
    // every argument to be that kind of value. A `macro` may mix it with a runtime parameter.
    // `(P1, P2)` keeps each type parameter. Substitution fills them in at the call.
    fn.params = parse_params(extern_c, fn.variadic, fn.method_struct, true);
    fn.static_method = !fn.method_struct.empty() && (fn.params.empty() || !fn.params.front().is_receiver);
    parse_results(fn);
    if (check(TokenKind::LBrace)) {
        // A trait method is `func name() type;`. A body belongs on `func Trait::name` outside.
        if (!allow_body) {
            diags_.error(peek().loc, "expected ';'");
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
        for (const Param &param : fn.params) {
            if (param.is_const) {
                diags_.error(param.loc, "not supported yet");
            }
        }
    }
    return fn;
}

static FnParam fn_param_from_result(const ResultSlot &slot) {
    FnParam param;
    param.type_name = slot.type_name;
    param.type_package = slot.type_package;
    param.is_struct = slot.is_struct;
    param.is_enum = slot.is_enum;
    param.indirections = slot.indirections;
    param.optional = slot.optional;
    param.const_pointee = slot.const_pointee;
    param.is_array = slot.is_array && slot.array_len < 0;
    param.array_len = param.is_array ? slot.array_len : 0;
    param.elem_slice = param.is_array && slot.elem_slice;
    param.elem_const = slot.elem_const;
    param.shells = slot.shells;
    param.type_args = slot.type_args;
    if (slot.is_tuple) {
        param.is_tuple = true;
        param.results.reserve(slot.tuple_parts.size());
        for (const ResultSlot &part : slot.tuple_parts) {
            param.results.push_back(fn_param_from_result(part));
        }
        return param;
    }
    param.is_fn_ptr = slot.is_fn_ptr;
    param.ret_indirections = slot.ret_indirections;
    param.ret_optional = slot.ret_optional;
    param.ret_const_pointee = slot.ret_const_pointee;
    param.params = slot.fn_params;
    param.variadic = slot.fn_variadic;
    param.results = slot.fn_results;
    return param;
}

FnParam Parser::fn_param_from(const PlacedType &type) const {
    FnParam param;
    param.type_name = type.type_name;
    param.type_package = type.type_package;
    param.is_struct = type.is_struct;
    param.is_enum = type.is_enum;
    param.indirections = type.indirections;
    param.optional = type.optional;
    param.const_pointee = type.const_pointee;
    param.is_array = type.is_array && type.array_len < 0;
    param.array_len = param.is_array ? type.array_len : 0;
    param.elem_slice = param.is_array && type.elem_slice;
    param.elem_const = type.elem_const;
    param.shells = type.shells;
    param.is_fn_ptr = type.is_fn_ptr;
    param.ret_indirections = type.ret_indirections;
    param.ret_optional = type.ret_optional;
    param.ret_const_pointee = type.ret_const_pointee;
    param.params = type.fn_params;
    param.variadic = type.fn_variadic;
    param.type_args = type.type_args;
    if (type.is_tuple) {
        param.is_tuple = true;
        param.results.reserve(type.tuple_parts.size());
        for (const ResultSlot &slot : type.tuple_parts) {
            param.results.push_back(fn_param_from_result(slot));
        }
        return param;
    }
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
            PlacedType param = parse_placed_type(false);
            drop_bare_tuple(param);
            params.push_back(fn_param_from(param));
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
        const ArrayMark array = parse_array_suffix();
        decl.is_array = array.is_array;
        decl.array_len = array.length;
    }
    return decl;
}

Parser::ArrayMark Parser::parse_array_suffix(bool allow_slice) {
    ArrayMark mark;
    if (!check(TokenKind::LBracket)) {
        return mark;
    }
    mark.is_array = true;
    const SourceLoc loc = peek().loc;
    ++pos_;
    // `[]T` is a slice. `name[]` is still the unsupported C form.
    if (check(TokenKind::RBracket)) {
        ++pos_;
        mark.length = -1;
        if (!allow_slice) {
            diags_.error(loc, "not supported yet");
        }
    } else if (check(TokenKind::IntLiteral) && !peek().suffix.is_unsigned && peek().suffix.bits != 8 &&
               peek().suffix.bits != 16 && peek().suffix.bits != 64 && peek().int_value > 0 &&
               peek().int_value <= 2147483647ULL) {
        mark.length = static_cast<int>(peek().int_value);
        ++pos_;
        expect(TokenKind::RBracket, "expected ']'");
    } else if (check(TokenKind::Identifier)) {
        mark.len_name = peek().text;
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
    // One empty `[]` after the outer brackets is `[][]T` or `[N][]T`.
    // A length, a name, or a third pair stays rejected. A C declarator rejects every extra pair.
    if (allow_slice && check(TokenKind::LBracket) && peek(1).kind == TokenKind::RBracket) {
        mark.elem_slice = true;
        pos_ += 2;
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

bool Parser::looks_like_type_params() const {
    if (!check(TokenKind::LBracket)) {
        return false;
    }
    int depth = 0;
    bool named = false;
    for (std::size_t ahead = 0; peek(ahead).kind != TokenKind::End; ++ahead) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::LBracket) {
            ++depth;
            continue;
        }
        if (kind == TokenKind::RBracket) {
            --depth;
            if (depth == 0) {
                const TokenKind next = peek(ahead + 1).kind;
                // `type Something[K type, V type] (K, V)`. A tuple alias takes the list.
                // `type Name[T type] i32` stays an array length.
                return named && (next == TokenKind::KwStruct || next == TokenKind::KwEnum || next == TokenKind::KwTrait ||
                                 next == TokenKind::KwUnion || next == TokenKind::LParen);
            }
            continue;
        }
        if (depth == 1 && !named) {
            if (kind == TokenKind::Ellipsis) {
                continue;
            }
            if (kind != TokenKind::Identifier) {
                return false;
            }
            named = true;
        }
        if (ahead > 4096) {
            return false;
        }
    }
    return false;
}

bool Parser::bracket_comma_then_dot() const {
    if (!check(TokenKind::LBracket)) {
        return false;
    }
    int depth = 0;
    bool comma = false;
    for (std::size_t ahead = 0; peek(ahead).kind != TokenKind::End; ++ahead) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::LBracket) {
            ++depth;
        } else if (kind == TokenKind::RBracket) {
            --depth;
            if (depth == 0) {
                return comma && peek(ahead + 1).kind == TokenKind::Dot;
            }
        } else if (depth == 1 && kind == TokenKind::Comma) {
            comma = true;
        } else if (depth == 1 && (kind == TokenKind::Semicolon || kind == TokenKind::LBrace || kind == TokenKind::RBrace)) {
            return false;
        }
        if (ahead > 4096) {
            return false;
        }
    }
    return false;
}

bool Parser::bracket_then_colon_colon() const {
    if (!check(TokenKind::LBracket)) {
        return false;
    }
    int depth = 0;
    for (std::size_t ahead = 0; peek(ahead).kind != TokenKind::End; ++ahead) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::LBracket) {
            ++depth;
        } else if (kind == TokenKind::RBracket) {
            --depth;
            if (depth == 0) {
                return peek(ahead + 1).kind == TokenKind::ColonColon;
            }
        } else if (depth == 1 && (kind == TokenKind::Semicolon || kind == TokenKind::LBrace || kind == TokenKind::RBrace)) {
            return false;
        }
        if (ahead > 4096) {
            return false;
        }
    }
    return false;
}

std::vector<std::string> Parser::parse_method_params() {
    std::vector<std::string> names;
    const SourceLoc loc = peek().loc;
    expect(TokenKind::LBracket, "expected '['");
    if (check(TokenKind::RBracket)) {
        diags_.error(loc, "type mismatch");
        ++pos_;
        return names;
    }
    while (!check(TokenKind::RBracket) && !check(TokenKind::LBrace) && !check(TokenKind::LParen) && !eof()) {
        if (!check(TokenKind::Identifier)) {
            error_here("expected an identifier");
            break;
        }
        const std::string name = peek().text;
        const SourceLoc name_loc = peek().loc;
        ++pos_;
        for (const std::string &prior : names) {
            if (prior == name) {
                diags_.error(name_loc, "redefinition");
            }
        }
        // `Vec[T]` names the struct's parameter. `Vec[T type]` declares one, which a method does not do.
        if (!check(TokenKind::Comma) && !check(TokenKind::RBracket)) {
            diags_.error(peek().loc, "type mismatch");
            if (!check(TokenKind::RBracket) && !check(TokenKind::Comma) && !eof()) {
                ++pos_;
            }
        }
        names.push_back(name);
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RBracket, "expected ']'");
    return names;
}

bool Parser::bracket_then_paren() const {
    if (!check(TokenKind::LBracket)) {
        return false;
    }
    int depth = 0;
    for (std::size_t ahead = 0; peek(ahead).kind != TokenKind::End; ++ahead) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::LBracket) {
            ++depth;
        } else if (kind == TokenKind::RBracket) {
            --depth;
            if (depth == 0) {
                return peek(ahead + 1).kind == TokenKind::LParen;
            }
        } else if (depth == 1 && (kind == TokenKind::Semicolon || kind == TokenKind::LBrace || kind == TokenKind::RBrace)) {
            return false;
        }
        if (ahead > 4096) {
            return false;
        }
    }
    return false;
}

bool Parser::bracket_then_brace() const {
    if (!check(TokenKind::LBracket)) {
        return false;
    }
    int depth = 0;
    for (std::size_t ahead = 0; peek(ahead).kind != TokenKind::End; ++ahead) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::LBracket) {
            ++depth;
        } else if (kind == TokenKind::RBracket) {
            --depth;
            if (depth == 0) {
                return peek(ahead + 1).kind == TokenKind::LBrace;
            }
        } else if (depth == 1 && (kind == TokenKind::Semicolon || kind == TokenKind::LBrace || kind == TokenKind::RBrace)) {
            return false;
        }
        if (ahead > 4096) {
            return false;
        }
    }
    return false;
}

bool Parser::primitive_type_name(const std::string &name) const {
    return name == "void" || name == "bool" || name == "i8" || name == "u8" || name == "i16" || name == "u16" ||
           name == "i32" || name == "u32" || name == "i64" || name == "u64" || name == "f32" || name == "f64" ||
           name == "invalid";
}

bool Parser::integer_constraint_name(const std::string &name) const {
    return name == "i8" || name == "u8" || name == "i16" || name == "u16" || name == "i32" || name == "u32" ||
           name == "i64" || name == "u64";
}

std::vector<TypeParam> Parser::parse_type_params() {
    std::vector<TypeParam> params;
    const SourceLoc loc = peek().loc;
    expect(TokenKind::LBracket, "expected '['");
    if (check(TokenKind::RBracket)) {
        diags_.error(loc, "type mismatch");
        ++pos_;
        return params;
    }
    while (!check(TokenKind::RBracket) && !check(TokenKind::LBrace) && !check(TokenKind::LParen) && !eof()) {
        TypeParam param;
        param.loc = peek().loc;
        if (check(TokenKind::Ellipsis)) {
            // `...` is not a type parameter. The list starts with a name.
            const SourceLoc pack_loc = peek().loc;
            diags_.error(pack_loc, "expected an identifier");
            ++pos_;
            if (!check(TokenKind::Identifier)) {
                error_here("expected an identifier");
                break;
            }
            param.name = peek().text;
            param.loc = pack_loc;
            param.is_pack = true;
            param.is_type = true;
            ++pos_;
            for (const TypeParam &prior : params) {
                if (prior.name == param.name) {
                    diags_.error(pack_loc, "redefinition");
                }
            }
            if (!check(TokenKind::Comma) && !check(TokenKind::RBracket)) {
                diags_.error(peek().loc, "type mismatch");
                if (!check(TokenKind::RBracket) && !eof()) {
                    ++pos_;
                }
            }
            params.push_back(std::move(param));
            if (eat(TokenKind::Comma)) {
                while (!check(TokenKind::RBracket) && !eof()) {
                    ++pos_;
                }
            }
            break;
        }
        if (!check(TokenKind::Identifier)) {
            error_here("expected an identifier");
            break;
        }
        param.name = peek().text;
        ++pos_;
        for (const TypeParam &prior : params) {
            if (prior.name == param.name) {
                diags_.error(param.loc, "redefinition");
            }
        }
        if (eat(TokenKind::KwType)) {
            param.is_type = true;
        } else if (check(TokenKind::KwInt) || check(TokenKind::KwChar) || check(TokenKind::KwShort) ||
                   check(TokenKind::KwLong)) {
            param.is_value = true;
            if (check(TokenKind::KwInt)) {
                param.constraint = "int";
            } else if (check(TokenKind::KwChar)) {
                param.constraint = "char";
            } else if (check(TokenKind::KwShort)) {
                param.constraint = "short";
            } else {
                param.constraint = "long";
            }
            ++pos_;
        } else if (check(TokenKind::Identifier) && integer_constraint_name(peek().text)) {
            param.is_value = true;
            param.constraint = peek().text;
            ++pos_;
        } else if (check(TokenKind::Identifier)) {
            param.is_trait = true;
            if (peek(1).kind == TokenKind::Dot && peek(2).kind == TokenKind::Identifier) {
                param.constraint_package = peek().text;
                param.constraint = peek(2).text;
                pos_ += 3;
            } else {
                param.constraint = peek().text;
                ++pos_;
            }
        } else {
            error_here("expected a type");
            break;
        }
        params.push_back(std::move(param));
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RBracket, "expected ']'");
    return params;
}

TypeArg Parser::parse_one_type_arg() {
    TypeArg arg;
    arg.loc = peek().loc;
    if (check(TokenKind::IntLiteral)) {
        arg.is_value = true;
        if (peek().suffix.is_unsigned || peek().suffix.bits == 8 || peek().suffix.bits == 16 || peek().suffix.bits == 64 ||
            peek().int_value > 2147483647ULL) {
            diags_.error(peek().loc, "type mismatch");
        } else {
            arg.has_value = true;
            arg.value = static_cast<int>(peek().int_value);
        }
        ++pos_;
        return arg;
    }
    if (check(TokenKind::LBracket) ||
        ((check(TokenKind::Star) || check(TokenKind::Question)) &&
         [&]() {
             std::size_t ahead = 0;
             while (peek(ahead).kind == TokenKind::Star ||
                    (peek(ahead).kind == TokenKind::Question && peek(ahead + 1).kind == TokenKind::Star)) {
                 if (peek(ahead).kind == TokenKind::Question) {
                     ++ahead;
                 }
                 ++ahead;
                 if (peek(ahead).kind == TokenKind::KwConst) {
                     ++ahead;
                 }
             }
             return peek(ahead).kind == TokenKind::LBracket;
         }())) {
        PlacedType type = parse_go_type();
        drop_bare_tuple(type);
        arg.type_name = type.type_name;
        arg.type_package = type.type_package;
        arg.is_struct = type.is_struct;
        arg.is_enum = type.is_enum;
        arg.indirections = type.indirections;
        arg.optional = type.optional;
        arg.const_pointee = type.const_pointee;
        arg.is_array = type.is_array || !type.shells.empty();
        arg.elem_const = type.elem_const;
        arg.array_len = type.array_len;
        arg.array_len_name = type.array_len_name;
        arg.shells = type.shells;
        arg.args = type.type_args;
        return arg;
    }
    const PointerPrefix prefix = parse_pointer_prefix();
    arg.indirections = prefix.count;
    arg.optional = prefix.optional;
    arg.const_pointee = prefix.const_pointee;
    if (check(TokenKind::KwInt) || check(TokenKind::KwChar) || check(TokenKind::KwShort) || check(TokenKind::KwLong) ||
        check(TokenKind::KwBool) || check(TokenKind::KwFloat) || check(TokenKind::KwDouble) || check(TokenKind::KwVoid)) {
        if (check(TokenKind::KwInt)) {
            arg.type_name = "int";
        } else if (check(TokenKind::KwChar)) {
            arg.type_name = "char";
        } else if (check(TokenKind::KwShort)) {
            arg.type_name = "short";
        } else if (check(TokenKind::KwLong)) {
            arg.type_name = "long";
        } else if (check(TokenKind::KwBool)) {
            arg.type_name = "bool";
        } else if (check(TokenKind::KwFloat)) {
            arg.type_name = "float";
        } else if (check(TokenKind::KwDouble)) {
            arg.type_name = "double";
        } else {
            arg.type_name = "void";
        }
        ++pos_;
    } else if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Dot && peek(2).kind == TokenKind::Identifier) {
        arg.type_package = peek().text;
        arg.type_name = peek(2).text;
        pos_ += 3;
    } else if (check(TokenKind::Identifier)) {
        arg.type_name = peek().text;
        ++pos_;
    } else {
        error_here("expected a type");
        arg.type_name = "invalid";
        return arg;
    }
    if (check(TokenKind::LBracket)) {
        arg.args = parse_type_args();
    }
    return arg;
}

std::vector<TypeArg> Parser::parse_type_args() {
    std::vector<TypeArg> args;
    expect(TokenKind::LBracket, "expected '['");
    if (check(TokenKind::RBracket)) {
        diags_.error(peek().loc, "type mismatch");
        ++pos_;
        return args;
    }
    while (!check(TokenKind::RBracket) && !check(TokenKind::LBrace) && !check(TokenKind::LParen) &&
           !check(TokenKind::Semicolon) && !eof()) {
        const std::size_t before = pos_;
        args.push_back(parse_one_type_arg());
        if (pos_ == before) {
            ++pos_;
        }
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RBracket, "expected ']'");
    return args;
}

} // namespace tlc
