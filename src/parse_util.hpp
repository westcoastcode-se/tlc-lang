#pragma once

#include "parser.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace tlc {

// Small helpers shared by the parser implementation files.

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

inline std::unique_ptr<Expr> unsupported_expr(SourceLoc loc) {
    Expr::Unsupported node;
    node.loc = loc;
    return make_expr(std::move(node));
}

inline bool is_type_alias(std::string_view text) {
    return text == "i8" || text == "i16" || text == "i32" || text == "i64" || text == "u8" || text == "u16" ||
           text == "u32" || text == "u64" || text == "uint" || text == "ulong" || text == "f32" || text == "f64" ||
           text == "Type";
}

inline bool is_int_spec(TokenKind kind) {
    switch (kind) {
    case TokenKind::KwInt:
    case TokenKind::KwChar:
    case TokenKind::KwShort:
    case TokenKind::KwLong:
        return true;
    default:
        return false;
    }
}

struct IntSpecs {
    bool saw_short = false;
    bool saw_char = false;
    bool saw_int = false;
    int longs = 0;
    bool any = false;
    bool bad = false;
};

inline void take_int_spec(IntSpecs &specs, TokenKind kind) {
    specs.any = true;
    switch (kind) {
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

inline std::string canonical_int(const IntSpecs &specs) {
    if (specs.bad) {
        return "invalid";
    }
    if (specs.saw_char) {
        return "char";
    }
    if (specs.saw_short) {
        return "short";
    }
    // `long long` is written `i64` or `u64`.
    if (specs.longs >= 2) {
        return "invalid";
    }
    if (specs.longs == 1) {
        return "long";
    }
    return "int";
}

} // namespace tlc
