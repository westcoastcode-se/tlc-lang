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
        if (peek(index).kind == TokenKind::KwConst) {
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

bool Parser::skip_balanced(std::size_t &ahead, TokenKind open, TokenKind close) const {
    if (peek(ahead).kind != open) {
        return false;
    }
    int depth = 0;
    while (true) {
        const TokenKind kind = peek(ahead).kind;
        if (kind == TokenKind::End) {
            return false;
        }
        if (kind == open) {
            ++depth;
        } else if (kind == close) {
            --depth;
        }
        ++ahead;
        if (depth == 0) {
            return true;
        }
    }
}

bool Parser::skip_placed_type(std::size_t &ahead) const {
    while (peek(ahead).kind == TokenKind::LBracket) {
        if (!skip_balanced(ahead, TokenKind::LBracket, TokenKind::RBracket)) {
            return false;
        }
    }
    while (peek(ahead).kind == TokenKind::Star || peek(ahead).kind == TokenKind::Question) {
        if (peek(ahead).kind == TokenKind::Question) {
            if (peek(ahead + 1).kind != TokenKind::Star) {
                return false;
            }
            ++ahead;
        }
        if (peek(ahead).kind != TokenKind::Star) {
            return false;
        }
        ++ahead;
        if (peek(ahead).kind == TokenKind::KwConst) {
            ++ahead;
        }
    }
    if (peek(ahead).kind == TokenKind::KwFunc && peek(ahead + 1).kind == TokenKind::LParen) {
        ++ahead;
        if (!skip_balanced(ahead, TokenKind::LParen, TokenKind::RParen)) {
            return false;
        }
        if (peek(ahead).kind == TokenKind::LParen) {
            return skip_balanced(ahead, TokenKind::LParen, TokenKind::RParen);
        }
        return skip_placed_type(ahead);
    }
    if (peek(ahead).kind == TokenKind::KwEnum || peek(ahead).kind == TokenKind::KwStruct) {
        ++ahead;
        if (peek(ahead).kind == TokenKind::Identifier) {
            ++ahead;
        }
        return true;
    }
    if (peek(ahead).kind == TokenKind::Identifier && peek(ahead + 1).kind == TokenKind::Dot &&
        peek(ahead + 2).kind == TokenKind::Identifier) {
        ahead += 3;
    } else if (peek(ahead).kind == TokenKind::Identifier || peek(ahead).kind == TokenKind::KwVoid ||
               peek(ahead).kind == TokenKind::KwBool || peek(ahead).kind == TokenKind::KwFloat ||
               peek(ahead).kind == TokenKind::KwDouble || is_int_spec(peek(ahead).kind)) {
        ++ahead;
    } else {
        return false;
    }
    if (peek(ahead).kind == TokenKind::LBracket) {
        return skip_balanced(ahead, TokenKind::LBracket, TokenKind::RBracket);
    }
    return true;
}

bool Parser::starts_func_cast() const {
    if (!at_func_type()) {
        return false;
    }
    std::size_t ahead = 1;
    if (!skip_balanced(ahead, TokenKind::LParen, TokenKind::RParen)) {
        return false;
    }
    if (peek(ahead).kind == TokenKind::LParen) {
        if (!skip_balanced(ahead, TokenKind::LParen, TokenKind::RParen)) {
            return false;
        }
    } else if (!skip_placed_type(ahead)) {
        return false;
    }
    return peek(ahead).kind == TokenKind::LParen;
}

bool Parser::starts_cast_type() const {
    if (starts_func_cast()) {
        return true;
    }
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
        if (peek(index).kind == TokenKind::KwConst) {
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
    if (at_func_type()) {
        const PlacedType fn = parse_func_type(true);
        type.name = fn.type_name;
        type.package = fn.type_package;
        type.is_struct = fn.is_struct;
        type.is_enum = fn.is_enum;
        type.indirections = fn.indirections;
        type.optional = fn.optional;
        type.const_pointee = fn.const_pointee;
        type.is_fn_ptr = fn.is_fn_ptr;
        type.ret_indirections = fn.ret_indirections;
        type.ret_optional = fn.ret_optional;
        type.ret_const_pointee = fn.ret_const_pointee;
        type.fn_params = fn.fn_params;
        type.fn_variadic = fn.fn_variadic;
        type.fn_results = fn.fn_results;
        return type;
    }
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
    type.const_pointee = prefix.const_pointee;
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
    node.const_pointee = type.const_pointee;
    node.is_fn_ptr = type.is_fn_ptr;
    node.ret_indirections = type.ret_indirections;
    node.ret_optional = type.ret_optional;
    node.ret_const_pointee = type.ret_const_pointee;
    node.fn_params = type.fn_params;
    node.fn_variadic = type.fn_variadic;
    node.fn_results = type.fn_results;
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

std::unique_ptr<Expr> Parser::parse_alloc() {
    const bool is_new = peek().kind == TokenKind::KwNew;
    const SourceLoc loc = peek().loc;
    ++pos_;
    std::unique_ptr<Expr> allocator;
    if (check(TokenKind::LParen)) {
        ++pos_;
        allocator = parse_expr();
        expect(TokenKind::RParen, "expected ')'");
    }
    if (!is_new) {
        Expr::Delete node;
        node.loc = loc;
        node.allocator = std::move(allocator);
        node.expr = parse_unary();
        return make_expr(std::move(node));
    }
    Expr::New node;
    node.loc = loc;
    node.allocator = std::move(allocator);
    // `new [n]T` reserves n elements. `new [_]T` takes the length from the element list.
    // `new Vec[i32]` is a type argument list, so those brackets follow the name.
    std::unique_ptr<Expr> count;
    bool inferred = false;
    if (check(TokenKind::LBracket)) {
        ++pos_;
        if (check(TokenKind::Identifier) && peek().text == "_" && peek(1).kind == TokenKind::RBracket) {
            inferred = true;
            ++pos_;
            ++pos_;
        } else if (check(TokenKind::RBracket)) {
            error_here("expected an expression");
            ++pos_;
        } else {
            count = parse_expr();
            expect(TokenKind::RBracket, "expected ']'");
        }
    }
    const PointerPrefix prefix = parse_pointer_prefix();
    node.indirections = prefix.count;
    node.optional = prefix.optional;
    node.const_pointee = prefix.const_pointee;
    if (!check(TokenKind::Identifier)) {
        error_here("expected a type");
        node.count = std::move(count);
        node.count_inferred = inferred;
        return make_expr(std::move(node));
    }
    node.type_name = peek().text;
    ++pos_;
    if (eat(TokenKind::Dot)) {
        node.type_package = node.type_name;
        if (!check(TokenKind::Identifier)) {
            error_here("expected a type");
            node.count = std::move(count);
            node.count_inferred = inferred;
            return make_expr(std::move(node));
        }
        node.type_name = peek().text;
        ++pos_;
    }
    if (check(TokenKind::LBracket)) {
        node.type_args = parse_type_args();
    }
    // `new Point{.x = 1}` writes the named fields after the block is reserved.
    // `new [n]Point{{.x = 1}...}` repeats one element. `new [_]Point{{.x = 1}, {.y = 2}}` lists them.
    // `{.x = 1}` on a buffer is a field list, rejected while checking.
    if (check(TokenKind::LBrace) && (count || inferred) && peek(1).kind != TokenKind::Dot &&
        peek(1).kind != TokenKind::RBrace) {
        Expr::Init list;
        list.loc = peek().loc;
        expect(TokenKind::LBrace, "expected '{'");
        bool repeat = false;
        while (!check(TokenKind::RBrace) && !eof()) {
            list.elements.push_back(parse_assignment());
            if (eat(TokenKind::Ellipsis)) {
                repeat = true;
                eat(TokenKind::Comma);
                break;
            }
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RBrace, "expected '}'");
        node.init = make_expr(std::move(list));
        node.repeat_init = repeat;
    } else if (check(TokenKind::LBrace)) {
        node.init = parse_init();
    }
    node.count = std::move(count);
    node.count_inferred = inferred;
    return make_expr(std::move(node));
}

std::unique_ptr<Expr> Parser::parse_unary() {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwUnsafe) {
        const SourceLoc loc = peek().loc;
        ++pos_;
        Expr::Unary node;
        node.loc = loc;
        node.op = TokenKind::KwUnsafe;
        node.expr = parse_unary();
        return make_expr(std::move(node));
    }
    if (kind == TokenKind::KwNew || kind == TokenKind::KwDelete) {
        return parse_alloc();
    }
    if (kind == TokenKind::KwSizeof || kind == TokenKind::KwTypeof) {
        const bool is_sizeof = kind == TokenKind::KwSizeof;
        const SourceLoc loc = peek().loc;
        ++pos_;
        // `sizeof(Point)` and `typeof(Point)`. `struct` is not a type spelling here.
        bool on_type = false;
        bool type_array = false;
        bool type_elem_const = false;
        int type_array_len = 0;
        ParenType spelled;
        std::unique_ptr<Expr> expr;
        std::size_t look = 0;
        if (peek(look).kind == TokenKind::LParen) {
            ++look;
            while (peek(look).kind == TokenKind::Star ||
                   (peek(look).kind == TokenKind::Question && peek(look + 1).kind == TokenKind::Star)) {
                if (peek(look).kind == TokenKind::Question) {
                    ++look;
                }
                ++look;
                if (peek(look).kind == TokenKind::KwConst) {
                    ++look;
                }
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
        } else if (peek().kind == TokenKind::LParen && peek(1).kind == TokenKind::LBracket) {
            // `sizeof([]i32)` and `typeof([4]i32)`. A cast does not take this form.
            ++pos_;
            const PlacedType array = parse_go_type();
            expect(TokenKind::RParen, "expected ')'");
            on_type = true;
            type_array = array.is_array;
            type_elem_const = array.elem_const;
            type_array_len = array.array_len;
            spelled.name = array.type_name;
            spelled.package = array.type_package;
            spelled.is_struct = array.is_struct;
            spelled.is_enum = array.is_enum;
            spelled.indirections = array.indirections;
            spelled.optional = array.optional;
            spelled.const_pointee = array.const_pointee;
        } else if (starts_type_paren()) {
            spelled = parse_paren_type();
            on_type = true;
        } else if (!is_sizeof && peek().kind == TokenKind::LParen) {
            // The operand stops at `)`. `(x) { ... }` after a result is the function body,
            // so this does not take the short-function parse.
            ++pos_;
            expr = parse_expr();
            expect(TokenKind::RParen, "expected ')'");
        } else {
            expr = parse_unary();
        }
        auto fill = [&](auto &node) {
            node.loc = loc;
            node.on_type = on_type;
            node.type_name = spelled.name;
            node.type_package = spelled.package;
            node.is_struct = spelled.is_struct;
            node.is_enum = spelled.is_enum;
            node.indirections = spelled.indirections;
            node.optional = spelled.optional;
            node.const_pointee = spelled.const_pointee;
            node.expr = std::move(expr);
        };
        if (is_sizeof) {
            Expr::Sizeof node;
            fill(node);
            node.is_array = type_array;
            node.elem_const = type_elem_const;
            node.array_len = type_array_len;
            return make_expr(std::move(node));
        }
        Expr::Typeof node;
        fill(node);
        node.is_array = type_array;
        node.elem_const = type_elem_const;
        node.array_len = type_array_len;
        return finish_postfix(make_expr(std::move(node)));
    }
    // `*int(p)` is a cast. `*p` and `*f()` stay dereferences, so this comes before unary `*`.
    // `Op(p)(1)` and `func (i32) i32(p)(1)` call the pointer.
    if (starts_cast_type()) {
        return finish_postfix(parse_cast_expr());
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
    return finish_postfix(starts_cast_type() ? parse_cast_expr() : parse_primary());
}

std::unique_ptr<Expr> Parser::finish_postfix(std::unique_ptr<Expr> expr) {
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
        // `Buf[4, i32].count` and `pkg.Buf[4, i32].count`. `arr[i].field` has no comma and stays an index.
        const bool package_type = std::holds_alternative<Expr::Selector>(expr->data) &&
                                  std::get<Expr::Selector>(expr->data).base != nullptr &&
                                  std::holds_alternative<Expr::Name>(std::get<Expr::Selector>(expr->data).base->data) &&
                                  std::get<Expr::Selector>(expr->data).type_args.empty();
        if (check(TokenKind::LBracket) &&
            (std::holds_alternative<Expr::Name>(expr->data) || package_type) && bracket_comma_then_dot()) {
            Expr::Selector selector;
            selector.loc = expr_loc(*expr);
            selector.base = std::move(expr);
            selector.type_args = parse_type_args();
            expect(TokenKind::Dot, "expected '.'");
            if (check(TokenKind::Identifier)) {
                selector.field = peek().text;
                ++pos_;
            } else {
                error_here("expected an identifier");
            }
            expr = make_expr(std::move(selector));
            continue;
        }
        if (check(TokenKind::LBracket) && bracket_then_paren()) {
            Expr::Call call;
            call.loc = expr_loc(*expr);
            call.callee = std::move(expr);
            call.type_args = parse_type_args();
            expect(TokenKind::LParen, "expected '('");
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
        // `Vec[i32]{}` and `pkg.Vec[i32]{}`. `a[i]` and `(f())[i]` stay indexes.
        if (check(TokenKind::LBracket) &&
            (std::holds_alternative<Expr::Name>(expr->data) || package_type) && bracket_then_brace()) {
            std::string type_name;
            std::string type_package;
            if (const auto *name = std::get_if<Expr::Name>(&expr->data)) {
                type_name = name->text;
            } else {
                const auto &selector = std::get<Expr::Selector>(expr->data);
                type_package = std::get<Expr::Name>(selector.base->data).text;
                type_name = selector.field;
            }
            const SourceLoc loc = expr_loc(*expr);
            std::vector<TypeArg> type_args = parse_type_args();
            auto init = parse_init();
            if (auto *node = std::get_if<Expr::Init>(&init->data); node != nullptr && !type_name.empty()) {
                node->type_name = std::move(type_name);
                node->type_package = std::move(type_package);
                node->type_args = std::move(type_args);
                node->loc = loc;
            }
            expr = std::move(init);
            continue;
        }
        if (eat(TokenKind::LBracket)) {
            Expr::Index index;
            index.loc = expr_loc(*expr);
            index.base = std::move(expr);
            // `a[i]` reads one element. `a[i:j]` is the slice from i up to, but not including, j.
            // A missing low bound is 0. A missing high bound is the length. `a[]` is not a range.
            if (check(TokenKind::Colon)) {
                index.is_range = true;
                ++pos_;
                if (!check(TokenKind::RBracket)) {
                    index.end = parse_expr();
                }
            } else {
                index.index = parse_expr();
                if (eat(TokenKind::Colon)) {
                    index.is_range = true;
                    if (!check(TokenKind::RBracket)) {
                        index.end = parse_expr();
                    }
                }
            }
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
        // `Point{}` and `pkg.Point{}`. A `{` after a call, an index, or `value.field` stays put.
        if (check(TokenKind::LBrace)) {
            std::string type_name;
            std::string type_package;
            if (const auto *name = std::get_if<Expr::Name>(&expr->data)) {
                type_name = name->text;
            } else if (const auto *selector = std::get_if<Expr::Selector>(&expr->data)) {
                if (selector->base != nullptr && std::holds_alternative<Expr::Name>(selector->base->data)) {
                    type_package = std::get<Expr::Name>(selector->base->data).text;
                    type_name = selector->field;
                }
            }
            if (!type_name.empty()) {
                const SourceLoc loc = expr_loc(*expr);
                auto init = parse_init();
                if (auto *node = std::get_if<Expr::Init>(&init->data)) {
                    node->type_name = std::move(type_name);
                    node->type_package = std::move(type_package);
                    node->loc = loc;
                }
                expr = std::move(init);
                continue;
            }
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
    if (check(TokenKind::LParen)) {
        const SourceLoc loc = peek().loc;
        ++pos_;
        auto expr = parse_expr();
        if (!eat(TokenKind::Comma)) {
            expect(TokenKind::RParen, "expected ')'");
            return expr;
        }
        Expr::Init tuple;
        tuple.loc = loc;
        tuple.is_tuple = true;
        tuple.elements.push_back(std::move(expr));
        while (!check(TokenKind::RParen) && !eof()) {
            tuple.elements.push_back(parse_assignment());
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RParen, "expected ')'");
        return make_expr(std::move(tuple));
    }
    if (check(TokenKind::KwFunc)) {
        return parse_anon_function();
    }
    if (check(TokenKind::LBrace)) {
        return parse_init();
    }
    // `[_]i32{1, 2, 3}`. `_` stands for the length, which is the number of values.
    // A length inside the brackets stays an ordinary expression error.
    // Empty `[]` is not this form.
    if (check(TokenKind::LBracket) &&
        ((peek(1).kind == TokenKind::Identifier && peek(1).text == "_" && peek(2).kind == TokenKind::RBracket) ||
         peek(1).kind == TokenKind::RBracket)) {
        return parse_array_literal();
    }
    const SourceLoc loc = peek().loc;
    error_here("expected an expression");
    if (!eof() && !check(TokenKind::Semicolon) && !check(TokenKind::RParen) && !check(TokenKind::RBrace) &&
        !check(TokenKind::Comma)) {
        ++pos_;
    }
    return unsupported_expr(loc);
}

std::unique_ptr<Expr> Parser::parse_array_literal() {
    const SourceLoc loc = peek().loc;
    expect(TokenKind::LBracket, "expected '['");
    if (check(TokenKind::Identifier) && peek().text == "_") {
        ++pos_;
    } else {
        error_here("expected '_'");
    }
    expect(TokenKind::RBracket, "expected ']'");
    const PlacedType elem = parse_go_type();
    if (!check(TokenKind::LBrace)) {
        error_here("expected '{'");
        return unsupported_expr(loc);
    }
    auto init = parse_init();
    if (auto *node = std::get_if<Expr::Init>(&init->data)) {
        node->loc = loc;
        node->array_literal = true;
        node->type_name = elem.type_name;
        node->type_package = elem.type_package;
        node->is_struct = elem.is_struct;
        node->is_enum = elem.is_enum;
        node->indirections = elem.indirections;
        node->optional = elem.optional;
        node->const_pointee = elem.const_pointee;
        node->type_args = elem.type_args;
        node->elem_is_array = elem.is_array;
        node->elem_ptr_to_array = elem.ptr_to_array;
        node->elem_fn = elem.is_fn_ptr;
        node->elem_array_len = elem.array_len;
        node->elem_array_len_name = elem.array_len_name;
        node->elem_ret_indirections = elem.ret_indirections;
        node->elem_ret_optional = elem.ret_optional;
        node->elem_ret_const_pointee = elem.ret_const_pointee;
        node->elem_fn_params = elem.fn_params;
        node->elem_fn_variadic = elem.fn_variadic;
        node->elem_fn_results = elem.fn_results;
    }
    return init;
}

std::unique_ptr<Expr> Parser::parse_init() {
    Expr::Init init;
    init.loc = peek().loc;
    expect(TokenKind::LBrace, "expected '{'");
    if (!check(TokenKind::Dot) && !check(TokenKind::RBrace)) {
        while (!check(TokenKind::RBrace) && !eof()) {
            if (check(TokenKind::Dot)) {
                error_here("type mismatch");
                break;
            }
            init.elements.push_back(parse_assignment());
            if (!eat(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RBrace, "expected '}'");
        return make_expr(std::move(init));
    }
    while (!check(TokenKind::RBrace) && !eof()) {
        if (!eat(TokenKind::Dot)) {
            error_here("type mismatch");
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
