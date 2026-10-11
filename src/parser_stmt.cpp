// Statements, local variables, and bindings.
#include "parser.hpp"
#include "parse_util.hpp"

#include <utility>

namespace tlc {

std::unique_ptr<Stmt> Parser::parse_decl_stmt(bool file_scope) {
    const std::size_t marker = pos_;
    while (check(TokenKind::KwExport) || check(TokenKind::KwStatic) || check(TokenKind::KwExtern)) {
        if (eat(TokenKind::KwExtern)) {
            if (eat(TokenKind::LParen)) {
                int depth = 1;
                while (depth > 0 && !eof()) {
                    if (check(TokenKind::LParen)) {
                        ++depth;
                    } else if (check(TokenKind::RParen)) {
                        --depth;
                    }
                    ++pos_;
                }
            }
            continue;
        }
        ++pos_;
    }
    if (check(TokenKind::KwType)) {
        diags_.error(peek().loc, "not supported yet");
        ++pos_;
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
                break;
            }
            ++pos_;
        }
        eat(TokenKind::Semicolon);
        return make_stmt(Stmt::Expression{});
    }
    if (at_func_type()) {
        pos_ = marker;
        bool storage = false;
        const SourceLoc storage_loc = peek().loc;
        while (check(TokenKind::KwExport) || check(TokenKind::KwStatic) || check(TokenKind::KwExtern)) {
            storage = true;
            if (check(TokenKind::KwExtern)) {
                bool ignored_extern = false;
                bool ignored_c = false;
                take_extern(ignored_extern, ignored_c);
                continue;
            }
            ++pos_;
        }
        if (storage) {
            diags_.error(storage_loc, "not supported yet");
        }
        Stmt::Decl decl;
        decl.loc = peek().loc;
        parse_func_vars(decl.vars, false, false, false, false);
        return make_stmt(std::move(decl));
    }
    if (check(TokenKind::KwFunc)) {
        pos_ = marker;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
        bool extern_c = false;
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
            if (take_extern(is_extern, extern_c)) {
                continue;
            }
            break;
        }
        if (exported || is_static || is_extern) {
            diags_.error(storage_loc, "not supported yet");
        }
        FunctionDecl fn = parse_function(exported, is_static, is_extern, extern_c, true);
        if (!fn.method_struct.empty()) {
            diags_.error(fn.loc, "not supported yet");
        }
        Stmt::Func local;
        local.decl = std::make_unique<FunctionDecl>(std::move(fn));
        return make_stmt(std::move(local));
    }
    pos_ = marker;
    const Specifiers specs = parse_specifiers(file_scope);
    if (specs.struct_definition || specs.enum_definition || specs.union_definition) {
        diags_.error(specs.loc, "not supported yet");
        eat(TokenKind::Semicolon);
        return make_stmt(Stmt::Expression{});
    }
    // `struct Name;` and `enum Name;` are not declarations. The body is the declaration.
    if (((specs.is_struct && !specs.struct_definition) || (specs.is_enum && !specs.enum_definition)) &&
        (check(TokenKind::Semicolon) || !continues_declarator())) {
        diags_.error(specs.loc, "expected '{'");
        eat(TokenKind::Semicolon);
        return make_stmt(Stmt::Expression{});
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
        reject_c_var(var.loc.valid() ? var.loc : specs.loc);
        var.type_name = specs.type_name;
        var.type_package = specs.type_package;
        var.is_struct = specs.is_struct;
        var.is_enum = specs.is_enum;
        var.indirections = declarator.indirections;
        var.is_array = declarator.is_array;
        var.array_len = declarator.array_len;
        var.is_fn_ptr = declarator.is_fn_ptr;
        var.ret_indirections = declarator.ret_indirections;
        var.fn_params = declarator.fn_params;
        var.fn_variadic = declarator.fn_variadic;
        var.exported = specs.exported;
        var.is_static = specs.is_static;
        var.is_extern = specs.is_extern;
        var.extern_c = specs.extern_c;
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

std::unique_ptr<Stmt> Parser::parse_required_block() {
    if (check(TokenKind::LBrace)) {
        return parse_block();
    }
    error_here("expected '{'");
    // Skip one statement so a following else still belongs to this if.
    if (!eof() && !check(TokenKind::RBrace) && !check(TokenKind::KwElse)) {
        const std::size_t before = pos_;
        parse_statement();
        if (pos_ == before) {
            ++pos_;
        }
    }
    return nullptr;
}

std::unique_ptr<Stmt> Parser::parse_if() {
    Stmt::If stmt;
    stmt.loc = peek().loc;
    eat(TokenKind::KwIf);
    expect(TokenKind::LParen, "expected '('");
    stmt.cond = parse_expr();
    expect(TokenKind::RParen, "expected ')'");
    stmt.then_arm = parse_required_block();
    if (eat(TokenKind::KwElse)) {
        stmt.else_arm = check(TokenKind::KwIf) ? parse_if() : parse_required_block();
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
    stmt.body = parse_required_block();
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
    } else if (check(TokenKind::KwVar)) {
        stmt.init = parse_var_or_bind();
    } else if (check(TokenKind::Identifier) &&
               (peek(1).kind == TokenKind::Comma || peek(1).kind == TokenKind::ColonEq)) {
        stmt.init = parse_bind(false);
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
    stmt.body = parse_required_block();
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

void Parser::reject_c_var(SourceLoc loc) { diags_.error(loc, "expected var"); }

bool Parser::var_is_bind() const {
    if (!check(TokenKind::KwVar) || peek(1).kind != TokenKind::Identifier) {
        return false;
    }
    const TokenKind after = peek(2).kind;
    return after == TokenKind::Comma || after == TokenKind::Eq || after == TokenKind::ColonEq;
}

bool Parser::var_is_inferred() const {
    return check(TokenKind::KwVar) && peek(1).kind == TokenKind::Identifier && peek(2).kind == TokenKind::Eq;
}

VarDecl Parser::parse_typed_var(bool exported, bool is_static, bool is_extern, bool extern_c) {
    VarDecl var;
    var.loc = peek().loc;
    var.exported = exported;
    var.is_static = is_static;
    var.is_extern = is_extern;
    var.extern_c = extern_c;
    expect(TokenKind::KwVar, "expected 'var'");
    if (check(TokenKind::Identifier)) {
        var.name = peek().text;
        var.loc = peek().loc;
        ++pos_;
    } else {
        error_here("expected an identifier");
    }
    if (check(TokenKind::Eq) || check(TokenKind::ColonEq) || check(TokenKind::Comma)) {
        error_here("expected a type");
    }
    const PlacedType type = parse_go_type();
    if (type.struct_definition || type.enum_definition) {
        diags_.error(type.loc, "not supported yet");
    }
    var.type_name = type.type_name;
    var.type_package = type.type_package;
    var.is_struct = type.is_struct;
    var.is_enum = type.is_enum;
    var.indirections = type.indirections;
    var.optional = type.optional;
    var.is_array = type.is_array;
    var.array_len = type.array_len;
    var.ptr_to_array = type.ptr_to_array;
    var.is_fn_ptr = type.is_fn_ptr;
    var.ret_indirections = type.ret_indirections;
    var.ret_optional = type.ret_optional;
    var.fn_params = type.fn_params;
    var.fn_variadic = type.fn_variadic;
    var.fn_results = type.fn_results;
    if (var.is_array && var.array_len < 0) {
        diags_.error(var.loc, "not supported yet");
    }
    if (eat(TokenKind::Eq)) {
        var.init = parse_assignment();
    }
    expect(TokenKind::Semicolon, "expected ';'");
    return var;
}

std::unique_ptr<Stmt> Parser::parse_var_or_bind() {
    if (check(TokenKind::KwVar) && !var_is_bind()) {
        Stmt::Decl decl;
        decl.loc = peek().loc;
        decl.vars.push_back(parse_typed_var(false, false, false, false));
        return make_stmt(std::move(decl));
    }
    return parse_bind(check(TokenKind::KwVar));
}

void Parser::parse_inferred_global(TranslationUnit &unit, bool is_static, bool from_var) {
    VarDecl var;
    var.loc = peek().loc;
    var.inferred = true;
    var.is_static = is_static;
    if (from_var) {
        expect(TokenKind::KwVar, "expected 'var'");
    }
    if (check(TokenKind::Identifier)) {
        var.name = peek().text;
        var.loc = peek().loc;
        ++pos_;
    } else {
        error_here("expected an identifier");
    }
    if (from_var) {
        expect(TokenKind::Eq, "expected '='");
    } else {
        expect(TokenKind::ColonEq, "expected ':='");
    }
    var.init = parse_expr();
    expect(TokenKind::Semicolon, "expected ';'");
    TopDecl top;
    top.data = std::move(var);
    unit.decls.push_back(std::move(top));
    saw_decl_ = true;
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
        if (!check(TokenKind::Identifier)) {
            error_here("expected an identifier");
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
    if (check(TokenKind::KwTest)) {
        error_here("not supported yet");
        parse_test();
        return make_stmt(Stmt::Expression{});
    }
    if (check(TokenKind::KwAssert)) {
        AssertDecl item = parse_assert();
        Stmt::Assert stmt;
        stmt.loc = item.loc;
        stmt.cond = std::move(item.cond);
        stmt.message = std::move(item.message);
        return make_stmt(std::move(stmt));
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
        return parse_var_or_bind();
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

AssertDecl Parser::parse_assert() {
    AssertDecl item;
    item.loc = peek().loc;
    ++pos_;
    expect(TokenKind::LParen, "expected '('");
    item.cond = parse_expr();
    expect(TokenKind::Comma, "expected ','");
    if (check(TokenKind::StringLiteral)) {
        item.message = peek().text;
        ++pos_;
    } else {
        error_here("type mismatch");
        if (!check(TokenKind::RParen) && !check(TokenKind::Semicolon) && !eof()) {
            parse_expr();
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Semicolon, "expected ';'");
    return item;
}

TestDecl Parser::parse_test() {
    TestDecl item;
    item.loc = peek().loc;
    ++pos_;
    if (check(TokenKind::StringLiteral)) {
        item.name = peek().text;
        item.loc = peek().loc;
        if (item.name.empty()) {
            diags_.error(item.loc, "type mismatch");
        }
        ++pos_;
    } else {
        error_here("expected a string");
    }
    item.body = parse_required_block();
    return item;
}

} // namespace tlc
