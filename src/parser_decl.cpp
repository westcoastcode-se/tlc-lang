// File-scope declarations: functions, variables, structs, enums, and unions.
#include "parser.hpp"
#include "parse_util.hpp"

#include <utility>

namespace tlc {

void Parser::parse_type_decl(TranslationUnit &unit, bool exported) {
    TypedefDecl item;
    item.loc = peek().loc;
    eat(TokenKind::KwType);
    if (check(TokenKind::Identifier)) {
        item.name = peek().text;
        item.loc = peek().loc;
        ++pos_;
    } else {
        error_here("expected an identifier");
    }
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
        parse_type_decl(unit, exported);
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
            // Tagged unions wait. C layout is `extern(abi="c") union`.
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
