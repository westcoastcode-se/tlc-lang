// File-scope declarations: functions, variables, structs, enums, and unions.
#include "parser.hpp"
#include "parse_util.hpp"

#include <utility>

namespace tlc {

Parser::UnionBody Parser::parse_union_variants(const std::string &union_name) {
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
            field.inline_fields = parse_fields();
            expect(TokenKind::RBrace, "expected '}'");
            expect(TokenKind::Semicolon, "expected ';'");
            if (!union_name.empty() && !field.name.empty()) {
                StructDecl anon;
                anon.loc = field.loc;
                anon.name = union_name + "." + field.name;
                anon.fields = field.inline_fields;
                anon.anonymous = true;
                field.type_name = anon.name;
                body.anonymous.push_back(std::move(anon));
            }
            body.fields.push_back(std::move(field));
        } else {
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
            body.fields.push_back(std::move(field));
            expect(TokenKind::Semicolon, "expected ';'");
        }
        if (pos_ == before) {
            ++pos_;
        }
    }
    return body;
}

void Parser::parse_type_def(TranslationUnit &unit, bool exported, bool is_extern, bool extern_c, std::string name,
                            SourceLoc name_loc) {
    const TokenKind kind = peek().kind;
    const SourceLoc kind_loc = peek().loc;
    ++pos_;
    int align = 0;
    std::string underlying;
    std::string tag_name;
    // `union(ShapeType)` is a tlc union. `extern` keeps the C union and cannot take this form.
    bool variant_body = false;
    bool push_union = true;
    if (kind == TokenKind::KwStruct && check(TokenKind::LParen)) {
        align = parse_struct_align();
    } else if (kind == TokenKind::KwEnum && check(TokenKind::LParen)) {
        underlying = parse_enum_underlying();
    } else if (kind == TokenKind::KwUnion && check(TokenKind::LParen)) {
        variant_body = true;
        if (is_extern || extern_c) {
            diags_.error(peek().loc, "not supported yet");
            push_union = false;
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
            ++pos_;
            if (check(TokenKind::Identifier)) {
                tag_name = peek().text;
                ++pos_;
                if (check(TokenKind::Dot)) {
                    diags_.error(peek().loc, "not supported yet");
                    tag_name.clear();
                    push_union = false;
                    while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                        ++pos_;
                    }
                }
            } else {
                error_here("expected a type");
                push_union = false;
                while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
                    ++pos_;
                }
            }
            expect(TokenKind::RParen, "expected ')'");
            if (tag_name.empty()) {
                push_union = false;
            }
        }
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
        diags_.error(kind_loc, "expected '{'");
        eat(TokenKind::Semicolon);
        return;
    }
    if (!name.empty()) {
        type_names_.insert(name);
    }
    if (kind == TokenKind::KwTrait) {
        if (exported || is_extern) {
            diags_.error(name_loc, "not supported yet");
        }
        TraitDecl trait;
        trait.loc = name_loc;
        trait.name = std::move(name);
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
    } else {
        fields = parse_fields();
    }
    expect(TokenKind::RBrace, "expected '}'");
    eat(TokenKind::Semicolon);
    if (variant_body && !push_union) {
        return;
    }
    if (as_union && !extern_c && !variant_body) {
        diags_.error(kind_loc, "not supported yet");
        return;
    }
    for (StructDecl &anon : variants.anonymous) {
        TopDecl top;
        top.data = std::move(anon);
        unit.decls.push_back(std::move(top));
    }
    StructDecl item;
    item.loc = name_loc;
    item.name = std::move(name);
    item.fields = std::move(fields);
    item.exported = exported;
    // A C union is an extern overlay. A tlc union is an ordinary type.
    item.is_extern = is_extern || (as_union && !variant_body);
    item.extern_c = extern_c;
    item.align = align;
    item.is_union = as_union;
    item.tag_name = std::move(tag_name);
    TopDecl top;
    top.data = std::move(item);
    unit.decls.push_back(std::move(top));
}

void Parser::parse_type_decl(TranslationUnit &unit, bool exported, bool is_extern, bool extern_c) {
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
    if (check(TokenKind::KwStruct) || check(TokenKind::KwEnum) || check(TokenKind::KwTrait) ||
        check(TokenKind::KwUnion)) {
        parse_type_def(unit, exported, is_extern, extern_c, std::move(name), name_loc);
        return;
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
    item.is_array = type.is_array;
    item.array_len = type.array_len;
    item.is_fn_ptr = type.is_fn_ptr;
    item.ret_indirections = type.ret_indirections;
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
    const std::size_t marker = pos_;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
    bool extern_c = false;
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
        if (take_extern(is_extern, extern_c)) {
            continue;
        }
        break;
    }
    if (at_func_type()) {
        if (exported && is_static) {
            diags_.error(storage_loc, "export static");
        }
        std::vector<VarDecl> vars;
        parse_func_vars(vars, exported, is_static, is_extern, extern_c);
        for (VarDecl &var : vars) {
            TopDecl top;
            top.data = std::move(var);
            unit.decls.push_back(std::move(top));
        }
        saw_decl_ = true;
        return;
    }
    if (check(TokenKind::KwType)) {
        if (exported && is_static) {
            diags_.error(storage_loc, "export static");
        }
        parse_type_decl(unit, exported, is_extern, extern_c);
        saw_decl_ = true;
        return;
    }
    if (check(TokenKind::KwVar)) {
        if (exported && is_static) {
            diags_.error(storage_loc, "export static");
        }
        if (var_is_inferred()) {
            if (exported || is_extern) {
                diags_.error(peek().loc, "not supported yet");
            }
            parse_inferred_global(unit, is_static && !exported && !is_extern, true);
            return;
        }
        if (var_is_bind()) {
            diags_.error(peek().loc, "not supported yet");
            parse_bind(true);
        } else {
            VarDecl var = parse_typed_var(exported, is_static, is_extern, extern_c);
            TopDecl top;
            top.data = std::move(var);
            unit.decls.push_back(std::move(top));
        }
        saw_decl_ = true;
        return;
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::ColonEq) {
        if (exported || is_extern) {
            diags_.error(peek().loc, "not supported yet");
        }
        if (exported && is_static) {
            diags_.error(storage_loc, "export static");
        }
        parse_inferred_global(unit, is_static && !exported && !is_extern, false);
        return;
    }
    if (check(TokenKind::KwFunc)) {
        if (exported && is_static) {
            diags_.error(storage_loc, "export static");
        }
        FunctionDecl fn = parse_function(exported, is_static, is_extern, extern_c, true);
        TopDecl top;
        top.data = std::move(fn);
        unit.decls.push_back(std::move(top));
        saw_decl_ = true;
        return;
    }
    pos_ = marker;
    const Specifiers specs = parse_specifiers(true);
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
