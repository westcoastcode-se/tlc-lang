// File-scope declarations: functions, variables, structs, enums, and unions.
#include "parse/parser.hpp"
#include "parse/parse_util.hpp"

#include <utility>

namespace tlc {

Parser::UnionBody Parser::parse_union_variants(const std::string &union_name, bool extern_c, bool is_extern,
                                               bool publish) {
    UnionBody body;
    while (!check(TokenKind::RBrace) && !eof()) {
        const std::size_t before = pos_;
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
        if (check(TokenKind::KwStruct)) {
            const SourceLoc struct_loc = peek().loc;
            ++pos_;
            if (!eat(TokenKind::LBrace)) {
                diags_.error(struct_loc, "expected '{'");
                while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
                    ++pos_;
                }
                eat(TokenKind::Semicolon);
                continue;
            }
            const std::string arm_name =
                (!union_name.empty() && !field.name.empty()) ? union_name + "." + field.name : std::string{};
            StructMembers nested = parse_struct_members(arm_name, publish, extern_c, is_extern);
            expect(TokenKind::RBrace, "expected '}'");
            expect(TokenKind::Semicolon, "expected ';'");
            field.inline_fields = nested.fields;
            body.anonymous.insert(body.anonymous.end(), std::make_move_iterator(nested.anonymous.begin()),
                                  std::make_move_iterator(nested.anonymous.end()));
            if (!arm_name.empty()) {
                StructDecl anon;
                anon.loc = field.loc;
                anon.name = arm_name;
                anon.fields = field.inline_fields;
                anon.anonymous = true;
                anon.union_payload = true;
                anon.exported = publish;
                anon.extern_c = extern_c;
                anon.is_extern = is_extern || extern_c;
                field.type_name = anon.name;
                body.anonymous.push_back(std::move(anon));
            }
            body.fields.push_back(std::move(field));
        } else {
            const PlacedType type = parse_go_type();
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
            // `n i32 = 4` is the same field form as in a struct. A union field rejects the value in sema.
            if (eat(TokenKind::Eq)) {
                field.init = parse_expr();
            }
            expect(TokenKind::Semicolon, "expected ';'");
            body.fields.push_back(std::move(field));
        }
        if (pos_ == before) {
            ++pos_;
        }
    }
    return body;
}

Parser::StructMembers Parser::parse_struct_members(const std::string &owner, bool publish, bool extern_c,
                                                    bool is_extern) {
    StructMembers body;
    auto skip_member = [&]() {
        while (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !eof()) {
            ++pos_;
        }
        eat(TokenKind::Semicolon);
    };
    auto fill = [](FieldDecl &field, const PlacedType &type) {
        field.type_name = type.type_name;
        field.type_package = type.type_package;
        field.is_struct = type.is_struct;
        field.is_enum = type.is_enum;
        field.indirections = type.indirections;
        field.optional = type.optional;
        field.const_pointee = type.const_pointee;
        field.is_array = type.is_array;
        field.elem_const = type.elem_const;
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
    };
    while (!check(TokenKind::RBrace) && !eof()) {
        const std::size_t before = pos_;
        const bool rejected = check(TokenKind::KwVar) || check(TokenKind::KwConst) || check(TokenKind::KwExtern) ||
                              (check(TokenKind::KwExport) && (peek(1).kind == TokenKind::KwVar ||
                                                              peek(1).kind == TokenKind::KwConst ||
                                                              peek(1).kind == TokenKind::KwExtern));
        if (rejected) {
            // A member is `name type` or `export name type`. It is already a
            // variable, so `var` is not written here. A constant is
            // `const Type::name` beside the type. `var Type::name` is beside it too.
            diags_.error(peek().loc, "not supported yet");
            skip_member();
        } else {
            // An exported inline struct publishes every member. `export` on one of them changes nothing.
            bool exported = publish;
            if (eat(TokenKind::KwExport)) {
                exported = true;
            }
            if (!check(TokenKind::Identifier)) {
                error_here("expected an identifier");
                skip_member();
            } else {
                FieldDecl field;
                field.loc = peek().loc;
                field.name = peek().text;
                field.exported = exported;
                ++pos_;
                if (check(TokenKind::KwExport) &&
                    (peek(1).kind == TokenKind::KwStruct || peek(1).kind == TokenKind::KwUnion)) {
                    field.exported = true;
                    ++pos_;
                }
                if (check(TokenKind::KwStruct) || check(TokenKind::KwUnion)) {
                    const bool as_union = check(TokenKind::KwUnion);
                    const SourceLoc kind_loc = peek().loc;
                    ++pos_;
                    std::string tag;
                    std::string tag_package;
                    bool tagged = false;
                    bool forbid = false;
                    if (as_union && check(TokenKind::LParen)) {
                        // A C struct cannot hold a tagged union. The tag's place is not a C rule.
                        if (extern_c) {
                            diags_.error(kind_loc, "extern(abi=\"c\") on a tagged union");
                            forbid = true;
                        }
                        ++pos_;
                        if (forbid) {
                            while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                                ++pos_;
                            }
                            expect(TokenKind::RParen, "expected ')'");
                        } else {
                            tagged = parse_union_tag(tag_package, tag);
                        }
                    }
                    if (forbid) {
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
                    } else if (!eat(TokenKind::LBrace)) {
                        diags_.error(kind_loc, "expected '{'");
                        skip_member();
                    } else if (as_union && !tagged && check(TokenKind::RBrace)) {
                        diags_.error(kind_loc, "not supported yet");
                        ++pos_;
                        eat(TokenKind::Semicolon);
                    } else {
                        const std::string anon_name = owner.empty() ? field.name : owner + "." + field.name;
                        StructDecl anon;
                        anon.loc = field.loc;
                        anon.name = anon_name;
                        anon.anonymous = true;
                        anon.exported = field.exported;
                        anon.extern_c = extern_c;
                        anon.is_extern = is_extern || extern_c;
                        if (as_union) {
                            UnionBody variants = parse_union_variants(anon_name, extern_c, anon.is_extern, field.exported);
                            expect(TokenKind::RBrace, "expected '}'");
                            expect(TokenKind::Semicolon, "expected ';'");
                            anon.is_union = true;
                            anon.tag_name = tagged ? tag : std::string{};
                            anon.tag_package = tagged ? tag_package : std::string{};
                            anon.fields = std::move(variants.fields);
                            field.inline_fields = anon.fields;
                            body.anonymous.insert(body.anonymous.end(),
                                                  std::make_move_iterator(variants.anonymous.begin()),
                                                  std::make_move_iterator(variants.anonymous.end()));
                        } else {
                            StructMembers nested = parse_struct_members(anon_name, field.exported, extern_c, is_extern);
                            expect(TokenKind::RBrace, "expected '}'");
                            expect(TokenKind::Semicolon, "expected ';'");
                            anon.fields = std::move(nested.fields);
                            field.inline_fields = anon.fields;
                            body.anonymous.insert(body.anonymous.end(),
                                                  std::make_move_iterator(nested.anonymous.begin()),
                                                  std::make_move_iterator(nested.anonymous.end()));
                        }
                        field.type_name = anon.name;
                        body.anonymous.push_back(std::move(anon));
                        body.fields.push_back(std::move(field));
                    }
                } else {
                    const PlacedType type = parse_go_type();
                    if (type.struct_definition || type.enum_definition) {
                        diags_.error(type.loc.valid() ? type.loc : field.loc, "not supported yet");
                    }
                    fill(field, type);
                    // `x i32 = 1` is the value of a field that an initializer does not name.
                    if (eat(TokenKind::Eq)) {
                        field.init = parse_expr();
                    }
                    expect(TokenKind::Semicolon, "expected ';'");
                    body.fields.push_back(std::move(field));
                }
            }
        }
        if (pos_ == before) {
            ++pos_;
        }
    }
    return body;
}

bool Parser::parse_union_tag(std::string &package, std::string &name) {
    package.clear();
    name.clear();
    if (!check(TokenKind::Identifier)) {
        error_here("expected a type");
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
            ++pos_;
        }
        expect(TokenKind::RParen, "expected ')'");
        return false;
    }
    std::string first = peek().text;
    ++pos_;
    // `union(pkg.Kind)` names an enum in another package. `union(Kind)` stays one identifier.
    if (eat(TokenKind::Dot)) {
        if (!check(TokenKind::Identifier)) {
            error_here("expected a type");
            while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                ++pos_;
            }
            expect(TokenKind::RParen, "expected ')'");
            return false;
        }
        package = std::move(first);
        name = peek().text;
        ++pos_;
    } else {
        name = std::move(first);
    }
    expect(TokenKind::RParen, "expected ')'");
    return !name.empty();
}

void Parser::parse_type_def(TranslationUnit &unit, bool exported, bool is_extern, bool extern_c, std::string name,
                            SourceLoc name_loc, std::vector<TypeParam> type_params) {
    const TokenKind kind = peek().kind;
    const SourceLoc kind_loc = peek().loc;
    ++pos_;
    int align = 0;
    std::string underlying;
    std::string tag_name;
    std::string tag_package;
    std::string parent_name;
    std::string parent_package;
    SourceLoc parent_loc{};
    // `union(ShapeType)` is a tlc union. Plain `extern` may mark it.
    // `extern(abi="c")` is rejected: C does not place the tag before or after the payload.
    bool variant_body = false;
    bool push_union = true;
    if (kind == TokenKind::KwStruct && check(TokenKind::LParen)) {
        align = parse_struct_align();
    } else if (kind == TokenKind::KwEnum && check(TokenKind::LParen)) {
        underlying = parse_enum_underlying();
    } else if (kind == TokenKind::KwUnion && check(TokenKind::LParen)) {
        variant_body = true;
        // C does not say whether a tag sits before or after the payload.
        if (extern_c) {
            diags_.error(kind_loc, "extern(abi=\"c\") on a tagged union");
            push_union = false;
            ++pos_;
            while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                ++pos_;
            }
            expect(TokenKind::RParen, "expected ')'");
        } else {
            ++pos_;
            if (!parse_union_tag(tag_package, tag_name)) {
                push_union = false;
            }
        }
    } else if (kind == TokenKind::KwTrait && check(TokenKind::LParen)) {
        // `type Shape trait(Parent)` copies Parent at compile time. One name, no table.
        parent_loc = peek().loc;
        ++pos_;
        if (check(TokenKind::Identifier)) {
            parent_loc = peek().loc;
        }
        parse_union_tag(parent_package, parent_name);
    } else if (check(TokenKind::LParen)) {
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
    }
    if (!eat(TokenKind::LBrace)) {
        // `type Name struct;` is an empty struct, the same type as `type Name struct {}`.
        if (kind == TokenKind::KwStruct && eat(TokenKind::Semicolon)) {
            if (!name.empty()) {
                type_names_.insert(name);
            }
            StructDecl item;
            item.loc = name_loc;
            item.name = std::move(name);
            item.exported = exported;
            item.is_extern = is_extern;
            item.extern_c = extern_c;
            item.align = align;
            item.type_params = std::move(type_params);
            TopDecl top;
            top.data = std::move(item);
            unit.decls.push_back(std::move(top));
            return;
        }
        diags_.error(kind_loc, "expected '{'");
        eat(TokenKind::Semicolon);
        return;
    }
    if (!name.empty()) {
        type_names_.insert(name);
    }
    if (kind == TokenKind::KwTrait) {
        // One `T type`. An integer, a pack, several parameters, and `trait(Parent)` stay rejected.
        bool generic_ok = false;
        if (!type_params.empty()) {
            const TypeParam &param = type_params[0];
            generic_ok = type_params.size() == 1 && param.is_type && !param.is_pack && !param.is_value &&
                         !param.is_trait && parent_name.empty();
            if (!generic_ok) {
                diags_.error(kind_loc, "not supported yet");
                type_params.clear();
            }
        }
        // A C type is one concrete layout. A trait is not a C type.
        // `export` and `extern` are accepted on the one-type-parameter trait. It has no symbol.
        if (extern_c) {
            diags_.error(name_loc, "extern(abi=\"c\") on a trait or a generic");
        } else if ((exported || is_extern) && !generic_ok) {
            diags_.error(name_loc, "not supported yet");
        }
        TraitDecl trait;
        trait.loc = name_loc;
        trait.name = std::move(name);
        trait.exported = generic_ok && exported && !extern_c;
        trait.is_extern = generic_ok && is_extern && !extern_c;
        trait.type_params = std::move(type_params);
        trait.parent_name = std::move(parent_name);
        trait.parent_package = std::move(parent_package);
        trait.parent_loc = parent_loc;
        while (!check(TokenKind::RBrace) && !eof()) {
            const std::size_t before = pos_;
            bool unsafe_fn = false;
            if (check(TokenKind::KwUnsafe)) {
                std::size_t ahead = 0;
                while (peek(ahead).kind == TokenKind::KwUnsafe) {
                    ++ahead;
                }
                // `unsafe` on a field stays rejected. `unsafe func` marks the method.
                if (peek(ahead).kind != TokenKind::KwFunc) {
                    diags_.error(peek().loc, "not supported yet");
                    ++pos_;
                } else {
                    while (eat(TokenKind::KwUnsafe)) {
                        unsafe_fn = true;
                    }
                }
            }
            if (check(TokenKind::KwFunc)) {
                FunctionDecl method = parse_function(false, false, false, false, false);
                method.unsafe = unsafe_fn;
                if (!method.method_struct.empty()) {
                    diags_.error(method.loc, "type mismatch");
                }
                trait.methods.push_back(std::move(method));
            } else if (check(TokenKind::Identifier)) {
                FieldDecl field;
                if (parse_field(field, false, false)) {
                    trait.fields.push_back(std::move(field));
                }
            } else {
                error_here("expected a declaration");
                ++pos_;
            }
            if (pos_ == before) {
                error_here("expected a declaration");
                ++pos_;
            }
        }
        expect(TokenKind::RBrace, "expected '}'");
        eat(TokenKind::Semicolon);
        TopDecl top;
        top.data = std::move(trait);
        unit.decls.push_back(std::move(top));
        return;
    }
    if (kind == TokenKind::KwEnum) {
        EnumDecl item;
        item.loc = name_loc;
        item.name = std::move(name);
        item.underlying = std::move(underlying);
        item.enumerators = parse_enumerators();
        item.exported = exported;
        item.is_extern = is_extern;
        item.extern_c = extern_c;
        expect(TokenKind::RBrace, "expected '}'");
        eat(TokenKind::Semicolon);
        TopDecl top;
        top.data = std::move(item);
        unit.decls.push_back(std::move(top));
        return;
    }
    const bool as_union = kind == TokenKind::KwUnion;
    UnionBody variants;
    std::vector<FieldDecl> fields;
    if (variant_body) {
        variants = parse_union_variants(name);
        fields = std::move(variants.fields);
    } else if (kind == TokenKind::KwStruct) {
        StructMembers members = parse_struct_members(name, false, extern_c, is_extern);
        fields = std::move(members.fields);
        variants.anonymous = std::move(members.anonymous);
    } else {
        fields = parse_fields(extern_c);
    }
    expect(TokenKind::RBrace, "expected '}'");
    eat(TokenKind::Semicolon);
    if (variant_body && !push_union) {
        return;
    }
    for (StructDecl &anon : variants.anonymous) {
        // Nested anonymous structs and unions keep the enclosing type's extern flags.
        // A tagged `extern(abi="c")` union is dropped before this loop.
        anon.is_extern = is_extern;
        anon.extern_c = extern_c;
        TopDecl top;
        top.data = std::move(anon);
        unit.decls.push_back(std::move(top));
    }
    StructDecl item;
    item.loc = name_loc;
    item.name = std::move(name);
    item.fields = std::move(fields);
    item.exported = exported;
    // `extern(abi="c") union` is the C overlay. A plain union is the same layout
    // without that ABI. `union(Enum)` stays a tlc type.
    item.is_extern = is_extern || (as_union && extern_c && !variant_body);
    item.extern_c = extern_c;
    item.align = align;
    item.is_union = as_union;
    item.tag_name = std::move(tag_name);
    item.tag_package = std::move(tag_package);
    item.type_params = std::move(type_params);
    TopDecl top;
    top.data = std::move(item);
    unit.decls.push_back(std::move(top));
}

void Parser::parse_type_decl(TranslationUnit &unit, bool exported, bool is_extern, bool extern_c,
                             std::string link_name) {
    if (extern_c && !link_name.empty()) {
        diags_.error(peek().loc, "not supported yet");
    }
    eat(TokenKind::KwType);
    std::string name;
    SourceLoc name_loc = peek().loc;
    if (check(TokenKind::Identifier)) {
        name = peek().text;
        name_loc = peek().loc;
        ++pos_;
    } else {
        error_here("expected an identifier");
    }
    std::vector<TypeParam> type_params;
    if (looks_like_type_params()) {
        type_params = parse_type_params();
    }
    if (check(TokenKind::KwStruct) || check(TokenKind::KwEnum) || check(TokenKind::KwTrait) ||
        check(TokenKind::KwUnion)) {
        // A trait takes one `T type`. An enum, a union, and an alias do not.
        if (!type_params.empty() && peek().kind != TokenKind::KwStruct && peek().kind != TokenKind::KwTrait) {
            diags_.error(peek().loc, "not supported yet");
            type_params.clear();
        }
        parse_type_def(unit, exported, is_extern, extern_c, std::move(name), name_loc, std::move(type_params));
        return;
    }
    if (!type_params.empty()) {
        diags_.error(name_loc, "not supported yet");
    }
    TypedefDecl item;
    item.loc = name_loc;
    item.name = std::move(name);
    const PlacedType type = parse_go_type();
    if (type.struct_definition || type.enum_definition || type.ptr_to_array) {
        diags_.error(type.loc.valid() ? type.loc : item.loc, "not supported yet");
    }
    item.type_name = type.type_name;
    item.type_package = type.type_package;
    item.is_struct = type.is_struct;
    item.is_enum = type.is_enum;
    item.indirections = type.indirections;
    item.optional = type.optional;
    item.const_pointee = type.const_pointee;
    item.is_array = type.is_array;
    item.elem_const = type.elem_const;
    item.elem_slice = type.elem_slice;
    item.shells = type.shells;
    item.array_len = type.array_len;
    item.array_len_name = type.array_len_name;
    item.type_args = type.type_args;
    item.is_fn_ptr = type.is_fn_ptr;
    item.ret_indirections = type.ret_indirections;
    item.ret_optional = type.ret_optional;
    item.ret_const_pointee = type.ret_const_pointee;
    item.fn_params = type.fn_params;
    item.fn_variadic = type.fn_variadic;
    item.fn_results = type.fn_results;
    item.exported = exported;
    if (!item.name.empty()) {
        type_names_.insert(item.name);
    }
    TopDecl top;
    top.data = std::move(item);
    unit.decls.push_back(std::move(top));
    expect(TokenKind::Semicolon, "expected ';'");
}

void Parser::parse_top_declaration(TranslationUnit &unit, bool file_scope_export_ok) {
    (void)file_scope_export_ok;
    if (unsupported_unsafe_decl()) {
        reject_unsupported_unsafe();
        saw_decl_ = true;
        return;
    }
    const std::size_t marker = pos_;
    bool exported = false;
    bool is_extern = false;
    bool extern_c = false;
    std::string link_name;
    SourceLoc name_loc{};
    bool unsafe_fn = false;
    while (true) {
        if (check(TokenKind::KwExport)) {
            exported = true;
            ++pos_;
            continue;
        }
        if (take_extern(is_extern, extern_c, link_name, name_loc)) {
            continue;
        }
        if (eat(TokenKind::KwUnsafe)) {
            unsafe_fn = true;
            continue;
        }
        break;
    }
    reject_tlc_link_name(extern_c, link_name, name_loc);
    if (check(TokenKind::KwConst) && peek(1).kind == TokenKind::Identifier) {
        // `extern const name` and `extern const Type::name` are file scope.
        VarDecl var = parse_const_decl(exported);
        var.is_extern = is_extern;
        var.extern_c = extern_c;
        var.link_name = link_name;
        TopDecl top;
        top.data = std::move(var);
        unit.decls.push_back(std::move(top));
        saw_decl_ = true;
        return;
    }
    if (at_func_type()) {
        std::vector<VarDecl> vars;
        parse_func_vars(vars, exported, false, is_extern, extern_c, link_name);
        for (VarDecl &var : vars) {
            TopDecl top;
            top.data = std::move(var);
            unit.decls.push_back(std::move(top));
        }
        saw_decl_ = true;
        return;
    }
    if (check(TokenKind::KwType)) {
        parse_type_decl(unit, exported, is_extern, extern_c, link_name);
        saw_decl_ = true;
        return;
    }
    if (check(TokenKind::KwVar)) {
        if (var_is_inferred()) {
            if (is_extern) {
                diags_.error(peek().loc, "not supported yet");
            }
            parse_inferred_global(unit, false, true, exported && !is_extern);
            return;
        }
        if (var_is_bind()) {
            diags_.error(peek().loc, "not supported yet");
            parse_bind(true);
        } else {
            VarDecl var = parse_typed_var(exported, false, is_extern, extern_c, false, link_name);
            TopDecl top;
            top.data = std::move(var);
            unit.decls.push_back(std::move(top));
        }
        saw_decl_ = true;
        return;
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::ColonEq) {
        if (is_extern) {
            diags_.error(peek().loc, "not supported yet");
        }
        parse_inferred_global(unit, false, false, exported && !is_extern);
        return;
    }
    if (check(TokenKind::KwFunc)) {
        FunctionDecl fn = parse_function(exported, false, is_extern, extern_c, true, link_name);
        fn.unsafe = unsafe_fn;
        TopDecl top;
        top.data = std::move(fn);
        unit.decls.push_back(std::move(top));
        saw_decl_ = true;
        return;
    }
    pos_ = marker;
    const Specifiers specs = parse_specifiers(true);
    if (specs.extern_c && !specs.link_name.empty() &&
        (specs.union_definition || specs.struct_definition || specs.enum_definition)) {
        diags_.error(specs.loc, "not supported yet");
    }
    if (specs.union_definition) {
        if (!specs.extern_c) {
            diags_.error(specs.loc, "not supported yet");
        } else {
            StructDecl item;
            item.loc = specs.loc;
            item.name = specs.type_name;
            item.fields = specs.fields;
            item.exported = specs.exported;
            item.is_extern = true;
            item.extern_c = true;
            item.is_union = true;
            TopDecl top;
            top.data = std::move(item);
            unit.decls.push_back(std::move(top));
        }
        if (eat(TokenKind::Semicolon) || !continues_declarator()) {
            saw_decl_ = true;
            return;
        }
    }
    if (specs.struct_definition) {
        StructDecl item;
        item.loc = specs.loc;
        item.name = specs.type_name;
        item.fields = specs.fields;
        item.exported = specs.exported;
        item.is_extern = specs.is_extern;
        item.extern_c = specs.extern_c;
        item.align = specs.align;
        TopDecl top;
        top.data = std::move(item);
        unit.decls.push_back(std::move(top));
        // `}` ends the definition. `;` is optional. A following declarator is
        // `struct Point { x int; } p`.
        if (eat(TokenKind::Semicolon) || !continues_declarator()) {
            saw_decl_ = true;
            return;
        }
    }
    if (specs.enum_definition) {
        EnumDecl item;
        item.loc = specs.loc;
        item.name = specs.type_name;
        item.underlying = specs.underlying;
        item.enumerators = specs.enumerators;
        item.exported = specs.exported;
        item.is_extern = specs.is_extern;
        item.extern_c = specs.extern_c;
        TopDecl top;
        top.data = std::move(item);
        unit.decls.push_back(std::move(top));
        if (eat(TokenKind::Semicolon) || !continues_declarator()) {
            saw_decl_ = true;
            return;
        }
    }
    // `struct Name;`, `extern struct Name;`, and `enum Name;`
    // are not declarations. A body is required. A following declarator is still a C variable.
    if (((specs.is_struct && !specs.struct_definition) || (specs.is_enum && !specs.enum_definition)) &&
        (check(TokenKind::Semicolon) || !continues_declarator())) {
        diags_.error(specs.loc, "expected '{'");
        eat(TokenKind::Semicolon);
        saw_decl_ = true;
        return;
    }
    while (true) {
        const Declarator decl = parse_declarator();
        if (!decl.method_struct.empty() && !decl.is_function) {
            diags_.error(decl.loc, "type mismatch");
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
        reject_c_var(var.loc);
        var.type_name = specs.type_name;
        var.type_package = specs.type_package;
        var.is_struct = specs.is_struct;
        var.is_enum = specs.is_enum;
        var.indirections = decl.indirections;
        var.is_array = decl.is_array;
        var.array_len = decl.array_len;
        var.is_fn_ptr = decl.is_fn_ptr;
        var.ret_indirections = decl.ret_indirections;
        var.fn_params = decl.fn_params;
        var.fn_variadic = decl.fn_variadic;
        var.exported = specs.exported;
        var.is_static = specs.is_static;
        var.is_extern = specs.is_extern;
        var.extern_c = specs.extern_c;
        var.link_name = specs.link_name;
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

} // namespace tlc
