// Expressions, casts, and anonymous functions.
#include "parser.hpp"
#include "parse_util.hpp"

#include <utility>

namespace tlc {

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
        auto rhs = parse_bit_or();
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

bool Parser::skip_type_base(std::size_t &index, int leading, bool cast) const {
    const Token &head = peek(index);
    if (is_int_spec(head.kind)) {
        while (is_int_spec(peek(index).kind)) {
            ++index;
        }
        return true;
    }
    if (head.kind == TokenKind::KwVoid || head.kind == TokenKind::KwBool || head.kind == TokenKind::KwFloat ||
        head.kind == TokenKind::KwDouble) {
        ++index;
        return true;
    }
    if (head.kind == TokenKind::KwEnum) {
        if (peek(index + 1).kind != TokenKind::Identifier) {
            return false;
        }
        index += 2;
        return true;
    }
    // `pkg.Name` is a sizeof type. `pkg.Name(x)` is a call, and `*pkg.f()` is a dereference.
    if (!cast && head.kind == TokenKind::Identifier && peek(index + 1).kind == TokenKind::Dot &&
        peek(index + 2).kind == TokenKind::Identifier) {
        index += 3;
        return true;
    }
    if (head.kind == TokenKind::Identifier && is_type_alias(head.text)) {
        ++index;
        return true;
    }
    if (head.kind != TokenKind::Identifier) {
        return false;
    }
    // `ShapeType(value)` after the type is declared in this file. `f()` stays a call.
    if (cast && type_names_.count(head.text) != 0 && peek(index + 1).kind == TokenKind::LParen) {
        ++index;
        return true;
    }
    // `*Point(p)` after Point is declared. `*f()` stays a dereference.
    // `sizeof(*Point)` is an expression; sema treats a type name as a type.
    if (cast && leading > 0 && type_names_.count(head.text) != 0) {
        ++index;
        return true;
    }
    // `Point *` is the old postfix cast. The caller reports type mismatch.
    if (leading == 0 && peek(index + 1).kind == TokenKind::Star) {
        ++index;
        return true;
    }
    return false;
}

bool Parser::starts_type_paren() const {
    if (peek().kind != TokenKind::LParen) {
        return false;
    }
    std::size_t index = 1;
    int leading = 0;
    while (peek(index).kind == TokenKind::Star || peek(index).kind == TokenKind::Question) {
        if (peek(index).kind == TokenKind::Question && peek(index + 1).kind != TokenKind::Star) {
            break;
        }
        if (peek(index).kind == TokenKind::Question) {
            ++index;
        }
        if (peek(index).kind == TokenKind::Star) {
            ++leading;
            ++index;
        }
    }
    if (!skip_type_base(index, leading, false)) {
        return false;
    }
    while (peek(index).kind == TokenKind::Star) {
        ++index;
    }
    return peek(index).kind == TokenKind::RParen;
}

bool Parser::starts_cast_type() const {
    std::size_t index = 0;
    int leading = 0;
    while (peek(index).kind == TokenKind::Star || peek(index).kind == TokenKind::Question) {
        if (peek(index).kind == TokenKind::Question && peek(index + 1).kind != TokenKind::Star) {
            break;
        }
        if (peek(index).kind == TokenKind::Question) {
            ++index;
        }
        if (peek(index).kind == TokenKind::Star) {
            ++leading;
            ++index;
        }
    }
    if (!skip_type_base(index, leading, true)) {
        return false;
    }
    while (peek(index).kind == TokenKind::Star) {
        ++index;
    }
    return peek(index).kind == TokenKind::LParen;
}

Parser::ParenType Parser::parse_cast_type() {
    ParenType type;
    type.loc = peek().loc;
    const PointerPrefix prefix = parse_pointer_prefix();
    const int leading = prefix.count;
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
            if (type.name == "invalid") {
                diags_.error(type.loc, "type mismatch");
            }
        }
    } else if (eat(TokenKind::KwVoid)) {
        type.name = "void";
    } else if (eat(TokenKind::KwBool)) {
        type.name = "bool";
    } else if (eat(TokenKind::KwFloat)) {
        type.name = "float";
    } else if (eat(TokenKind::KwDouble)) {
        type.name = "double";
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
    if (check(TokenKind::Star)) {
        // `int*(p)` and `sizeof(int *)`. The star belongs in front of the type.
        diags_.error(peek().loc, "type mismatch");
        while (eat(TokenKind::Star)) {
        }
    }
    type.indirections = leading;
    type.optional = prefix.optional;
    return type;
}

Parser::ParenType Parser::parse_paren_type() {
    eat(TokenKind::LParen);
    ParenType type = parse_cast_type();
    expect(TokenKind::RParen, "expected ')'");
    return type;
}

std::unique_ptr<Expr> Parser::parse_cast_expr() {
    Expr::Cast node;
    node.loc = peek().loc;
    const ParenType type = parse_cast_type();
    node.type_name = type.name;
    node.type_package = type.package;
    node.is_struct = type.is_struct;
    node.is_enum = type.is_enum;
    node.indirections = type.indirections;
    node.optional = type.optional;
    expect(TokenKind::LParen, "expected '('");
    if (check(TokenKind::RParen)) {
        error_here("expected an expression");
    } else {
        node.expr = parse_assignment();
        if (check(TokenKind::Comma)) {
            error_here("expected ')'");
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    return make_expr(std::move(node));
}

std::unique_ptr<Expr> Parser::parse_unary() {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwSizeof) {
        Expr::Sizeof node;
        node.loc = peek().loc;
        ++pos_;
        // `sizeof(Point)`. `struct` is not a type spelling here.
        std::size_t look = 0;
        if (peek(look).kind == TokenKind::LParen) {
            ++look;
            while (peek(look).kind == TokenKind::Star ||
                   (peek(look).kind == TokenKind::Question && peek(look + 1).kind == TokenKind::Star)) {
                if (peek(look).kind == TokenKind::Question) {
                    ++look;
                }
                ++look;
            }
        }
        if (peek().kind == TokenKind::LParen && peek(look).kind == TokenKind::KwStruct) {
            error_at(peek(look), "expected a type");
            int depth = 0;
            do {
                if (check(TokenKind::LParen)) {
                    ++depth;
                } else if (check(TokenKind::RParen)) {
                    --depth;
                }
                ++pos_;
            } while (depth > 0 && !eof());
        } else if (starts_type_paren()) {
            const ParenType type = parse_paren_type();
            node.on_type = true;
            node.type_name = type.name;
            node.type_package = type.package;
            node.is_struct = type.is_struct;
            node.is_enum = type.is_enum;
            node.indirections = type.indirections;
            node.optional = type.optional;
        } else {
            node.expr = parse_unary();
        }
        return make_expr(std::move(node));
    }
    // `*int(p)` is a cast. `*p` and `*f()` stay dereferences, so this comes before unary `*`.
    if (starts_cast_type()) {
        return parse_cast_expr();
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
    auto expr = starts_cast_type() ? parse_cast_expr() : parse_primary();
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
        if (eat(TokenKind::Bang)) {
            Expr::Unary node;
            node.loc = expr_loc(*expr);
            node.op = TokenKind::Bang;
            node.postfix = true;
            node.expr = std::move(expr);
            expr = make_expr(std::move(node));
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
    if (check(TokenKind::KwNull)) {
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
        Expr::Float literal;
        literal.loc = peek().loc;
        literal.text = peek().text;
        if (!literal.text.empty() && (literal.text.back() == 'l' || literal.text.back() == 'L')) {
            error_here("not supported yet");
        }
        literal.is_f32 = !literal.text.empty() && (literal.text.back() == 'f' || literal.text.back() == 'F');
        ++pos_;
        return make_expr(std::move(literal));
    }
    if (starts_short_function()) {
        return parse_short_function();
    }
    if (eat(TokenKind::LParen)) {
        auto expr = parse_expr();
        expect(TokenKind::RParen, "expected ')'");
        return expr;
    }
    if (check(TokenKind::KwFunc)) {
        return parse_anon_function();
    }
    if (check(TokenKind::LBrace)) {
        return parse_init();
    }
    const SourceLoc loc = peek().loc;
    error_here("expected an expression");
    if (!eof() && !check(TokenKind::Semicolon) && !check(TokenKind::RParen) && !check(TokenKind::RBrace) &&
        !check(TokenKind::Comma)) {
        ++pos_;
    }
    return unsupported_expr(loc);
}

std::unique_ptr<Expr> Parser::parse_init() {
    Expr::Init init;
    init.loc = peek().loc;
    expect(TokenKind::LBrace, "expected '{'");
    while (!check(TokenKind::RBrace) && !eof()) {
        if (!eat(TokenKind::Dot)) {
            error_here("not supported yet");
            while (!check(TokenKind::Comma) && !check(TokenKind::RBrace) && !check(TokenKind::Semicolon) && !eof()) {
                ++pos_;
            }
            if (!eat(TokenKind::Comma)) {
                break;
            }
            continue;
        }
        Expr::FieldInit field;
        field.loc = peek().loc;
        if (check(TokenKind::Identifier)) {
            field.field = peek().text;
            ++pos_;
        } else {
            error_here("expected an identifier");
        }
        expect(TokenKind::Eq, "expected '='");
        field.value = parse_assignment();
        init.fields.push_back(std::move(field));
        if (!eat(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RBrace, "expected '}'");
    return make_expr(std::move(init));
}

std::unique_ptr<Expr> Parser::parse_anon_function() {
    FunctionDecl fn;
    fn.loc = peek().loc;
    expect(TokenKind::KwFunc, "expected 'func'");
    if (!check(TokenKind::LParen)) {
        error_here("expected '('");
    }
    fn.params = parse_params(false, fn.variadic);
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

bool Parser::starts_short_function() const {
    if (peek().kind != TokenKind::LParen) {
        return false;
    }
    std::size_t index = 1;
    if (peek(index).kind == TokenKind::RParen) {
        return peek(index + 1).kind == TokenKind::LBrace;
    }
    while (peek(index).kind == TokenKind::Identifier) {
        ++index;
        if (peek(index).kind == TokenKind::Comma) {
            ++index;
            if (peek(index).kind != TokenKind::Identifier) {
                return false;
            }
            continue;
        }
        return peek(index).kind == TokenKind::RParen && peek(index + 1).kind == TokenKind::LBrace;
    }
    return false;
}

std::unique_ptr<Expr> Parser::parse_short_function() {
    FunctionDecl fn;
    fn.loc = peek().loc;
    fn.inferred = true;
    expect(TokenKind::LParen, "expected '('");
    if (!check(TokenKind::RParen)) {
        while (!check(TokenKind::RParen) && !check(TokenKind::LBrace) && !eof()) {
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
            fn.params.push_back(std::move(param));
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
    }
    expect(TokenKind::RParen, "expected ')'");
    if (check(TokenKind::LBrace)) {
        fn.body = parse_block();
    } else {
        error_here("expected '{'");
    }
    Expr::Func node;
    node.loc = fn.loc;
    node.decl = std::make_unique<FunctionDecl>(std::move(fn));
    return make_expr(std::move(node));
}

} // namespace tlc
