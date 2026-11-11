// Compile-time folding of constants.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "ast/format.hpp"
#include "gen/codegen.hpp"

#include <cstdint>
#include <cstring>

namespace tlc {

namespace {

double round_float(double value, bool wide) {
    if (wide) {
        return value;
    }
    return static_cast<double>(static_cast<float>(value));
}

// f32 arithmetic rounds each step. f64 arithmetic uses the wider value.
double float_arith(TokenKind op, double lhs, double rhs, bool wide) {
    if (wide) {
        switch (op) {
        case TokenKind::Plus: return lhs + rhs;
        case TokenKind::Minus: return lhs - rhs;
        case TokenKind::Star: return lhs * rhs;
        case TokenKind::Slash: return lhs / rhs;
        default: return 0;
        }
    }
    const float left = static_cast<float>(lhs);
    const float right = static_cast<float>(rhs);
    float value = 0;
    switch (op) {
    case TokenKind::Plus: value = left + right; break;
    case TokenKind::Minus: value = left - right; break;
    case TokenKind::Star: value = left * right; break;
    case TokenKind::Slash: value = left / right; break;
    default: break;
    }
    return static_cast<double>(value);
}

// A narrowing cast keeps the destination's low bits, then widens to `i32`.
// `bool` is one bit. Sign-extending that bit makes true into `-1`.
int cast_folded(int value, int bits, bool zero_extend, bool as_bool) {
    if (as_bool) {
        return value != 0 ? -1 : 0;
    }
    if (bits <= 0 || bits >= 32) {
        return value;
    }
    const uint32_t mask = (1u << static_cast<unsigned>(bits)) - 1u;
    const uint32_t low = static_cast<uint32_t>(value) & mask;
    if (zero_extend || (low & (1u << static_cast<unsigned>(bits - 1))) == 0) {
        return static_cast<int>(low);
    }
    return static_cast<int>(low | ~mask);
}

std::optional<int> eval_int_op(TokenKind op, int lhs, int rhs) {
    auto wrap = [](int64_t value) { return static_cast<int>(static_cast<uint32_t>(value)); };
    const int64_t a = lhs;
    const int64_t b = rhs;
    switch (op) {
    case TokenKind::Plus: return wrap(a + b);
    case TokenKind::Minus: return wrap(a - b);
    case TokenKind::Star: return wrap(a * b);
    case TokenKind::Slash: return b == 0 ? std::nullopt : std::optional<int>(wrap(a / b));
    case TokenKind::Percent: return b == 0 ? std::nullopt : std::optional<int>(wrap(a % b));
    case TokenKind::Amp: return wrap(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    case TokenKind::Pipe: return wrap(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    case TokenKind::Caret: return wrap(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
    case TokenKind::LtLt:
        if (b < 0 || b >= 32) return std::nullopt;
        return wrap(static_cast<uint32_t>(a) << static_cast<uint32_t>(b));
    case TokenKind::GtGt:
        if (b < 0 || b >= 32) return std::nullopt;
        return static_cast<int>(static_cast<uint32_t>(static_cast<int32_t>(a) >> static_cast<uint32_t>(b)));
    case TokenKind::EqEq: return a == b ? 1 : 0;
    case TokenKind::BangEq: return a != b ? 1 : 0;
    case TokenKind::Lt: return a < b ? 1 : 0;
    case TokenKind::Gt: return a > b ? 1 : 0;
    case TokenKind::Le: return a <= b ? 1 : 0;
    case TokenKind::Ge: return a >= b ? 1 : 0;
    case TokenKind::AmpAmp: return (lhs != 0 && rhs != 0) ? 1 : 0;
    case TokenKind::PipePipe: return (lhs != 0 || rhs != 0) ? 1 : 0;
    default: return std::nullopt;
    }
}

bool is_float_logic(TokenKind op) {
    switch (op) {
    case TokenKind::EqEq:
    case TokenKind::BangEq:
    case TokenKind::Lt:
    case TokenKind::Gt:
    case TokenKind::Le:
    case TokenKind::Ge:
    case TokenKind::AmpAmp:
    case TokenKind::PipePipe:
        return true;
    default:
        return false;
    }
}

std::unique_ptr<Expr> wide_int_expr(uint64_t value, int bits, bool is_unsigned, SourceLoc loc) {
    auto expr = std::make_unique<Expr>();
    Expr::Int node;
    node.loc = loc;
    node.value = value;
    node.suffix.bits = bits;
    node.suffix.is_unsigned = is_unsigned;
    expr->data = std::move(node);
    return expr;
}

std::unique_ptr<Expr> folded_int(int value, SourceLoc loc) {
    if (value < 0) {
        if (value == -2147483647 - 1) {
            return nullptr;
        }
        auto inner = folded_int(-value, loc);
        if (inner == nullptr) {
            return nullptr;
        }
        auto expr = std::make_unique<Expr>();
        Expr::Unary node;
        node.loc = loc;
        node.op = TokenKind::Minus;
        node.expr = std::move(inner);
        expr->data = std::move(node);
        return expr;
    }
    auto expr = std::make_unique<Expr>();
    Expr::Int node;
    node.loc = loc;
    node.value = static_cast<uint64_t>(value);
    expr->data = std::move(node);
    return expr;
}

// A field of a struct literal, including `literal.inner.x` and `row[0].x`.
// `resolve` yields a comptime const struct or array, or a call that kept a literal.
template <typename Resolve, typename Omitted>
const Expr *const_struct_field(const Expr &object, const std::string &field, Resolve &&resolve, Omitted &&omitted) {
    const Expr *cursor = &object;
    if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data)) {
        if (selector->base == nullptr) {
            return nullptr;
        }
        cursor = const_struct_field(*selector->base, selector->field, resolve, omitted);
        if (cursor == nullptr) {
            return nullptr;
        }
    }
    if (const auto *index = std::get_if<Expr::Index>(&cursor->data)) {
        // `(Point{.x = 1}, 2)[0].x` is that field. The index is the literal.
        if (index->is_range || index->base == nullptr || index->index == nullptr) {
            return nullptr;
        }
        const auto *number = std::get_if<Expr::Int>(&index->index->data);
        if (number == nullptr || number->value > 2147483647ULL) {
            return nullptr;
        }
        const auto at = static_cast<std::size_t>(number->value);
        const Expr *base = index->base.get();
        if (const auto *named = std::get_if<Expr::Name>(&base->data)) {
            base = resolve(named->text);
            if (base == nullptr) {
                return nullptr;
            }
        }
        const auto *list = std::get_if<Expr::Init>(&base->data);
        // A tuple literal or a comptime const array. A struct field list is not indexed.
        if (list == nullptr || (!list->is_tuple && !list->array_literal) || at >= list->elements.size() ||
            list->elements[at] == nullptr) {
            return nullptr;
        }
        cursor = list->elements[at].get();
    }
    if (const auto *call = std::get_if<Expr::Call>(&cursor->data)) {
        if (call->comptime_struct == nullptr) {
            return nullptr;
        }
        cursor = call->comptime_struct.get();
    }
    if (const auto *named = std::get_if<Expr::Name>(&cursor->data)) {
        const Expr *kept = resolve(named->text);
        if (kept == nullptr) {
            return nullptr;
        }
        cursor = kept;
    }
    const auto *init = std::get_if<Expr::Init>(&cursor->data);
    if (init == nullptr || init->is_tuple || !init->elements.empty()) {
        return nullptr;
    }
    for (const Expr::FieldInit &item : init->fields) {
        if (item.field == field && item.value != nullptr) {
            return item.value.get();
        }
    }
    return omitted(*init, field);
}

std::string spec_int_piece(int value) {
    std::string piece = std::to_string(value);
    if (!piece.empty() && piece.front() == '-') {
        piece[0] = 'n';
    }
    return piece;
}

std::string spec_hex(uint64_t bits, int digits) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string text(static_cast<std::size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        text[static_cast<std::size_t>(i)] = hex[bits & 0x0f];
        bits >>= 4;
    }
    return text;
}

// Same spelling as a comptime `[]const u8` parameter: `s`, the length, then the bytes.
std::string spec_text_piece(const std::string &text) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string piece = "s";
    piece += std::to_string(text.size());
    for (const char byte : text) {
        const auto value = static_cast<unsigned char>(byte);
        piece.push_back(digits[value >> 4]);
        piece.push_back(digits[value & 0x0f]);
    }
    return piece;
}

std::unique_ptr<Expr> folded_string(std::string text, SourceLoc loc) {
    auto expr = std::make_unique<Expr>();
    Expr::String node;
    node.loc = loc;
    node.text = std::move(text);
    expr->data = std::move(node);
    return expr;
}

bool plain_named_result(const FunctionDecl &fn, const char *name) {
    if (fn.results.size() != 1 || fn.return_type != name || fn.body == nullptr) {
        return false;
    }
    const ResultSlot &slot = fn.results.front();
    return slot.type_name == name && slot.type_package.empty() && !slot.is_struct && !slot.is_enum &&
           slot.indirections == 0 && slot.optional == 0 && !slot.is_array && !slot.is_fn_ptr && slot.const_pointee == 0;
}

bool plain_i32_result(const FunctionDecl &fn) { return plain_named_result(fn, "i32"); }

bool plain_bool_result(const FunctionDecl &fn) { return plain_named_result(fn, "bool"); }

bool plain_f64_result(const FunctionDecl &fn) { return plain_named_result(fn, "f64"); }

bool plain_f32_result(const FunctionDecl &fn) { return plain_named_result(fn, "f32"); }

// Widths the wide-integer evaluator keeps. `i32` stays on the original path.
struct IntWidth {
    const char *name;
    int bits;
    bool is_unsigned;
};

const IntWidth *wide_int_result(const FunctionDecl &fn) {
    static const IntWidth table[] = {
        {"i64", 64, false}, {"u64", 64, true}, {"u32", 32, true}, {"i16", 16, false},
        {"u16", 16, true},  {"i8", 8, false},  {"u8", 8, true},
    };
    for (const IntWidth &item : table) {
        if (plain_named_result(fn, item.name)) {
            return &item;
        }
    }
    return nullptr;
}

bool plain_enum_result(const FunctionDecl &fn) {
    if (fn.results.size() != 1 || fn.return_type.empty() || fn.body == nullptr) {
        return false;
    }
    const ResultSlot &slot = fn.results.front();
    // `Order` is a name. `prepare_written` does not set `is_enum` on that slot.
    return slot.type_name == fn.return_type && slot.type_package.empty() && slot.indirections == 0 &&
           slot.optional == 0 && !slot.is_array && !slot.is_tuple && !slot.is_struct && !slot.is_fn_ptr &&
           slot.const_pointee == 0;
}

std::unique_ptr<Expr> enum_selector(const std::string &type_name, const std::string &member, SourceLoc loc) {
    auto expr = std::make_unique<Expr>();
    Expr::Selector node;
    node.loc = loc;
    node.field = member;
    auto base = std::make_unique<Expr>();
    Expr::Name name;
    name.loc = loc;
    name.text = type_name;
    base->data = std::move(name);
    node.base = std::move(base);
    expr->data = std::move(node);
    return expr;
}

// A `bool` parameter stays a bool literal. An integer literal would be `i32` in the body.
std::unique_ptr<Expr> folded_scalar_arg(const Param &param, int number, SourceLoc loc) {
    if (param.type_name == "bool" && param.type_package.empty() && param.indirections == 0 && param.optional == 0 &&
        !param.is_array && !param.is_struct && !param.is_enum && !param.is_fn_ptr && param.const_pointee == 0 &&
        param.shells.empty() && !param.elem_slice) {
        auto expr = std::make_unique<Expr>();
        Expr::Bool node;
        node.loc = loc;
        node.value = number != 0;
        expr->data = std::move(node);
        return expr;
    }
    return folded_int(number, loc);
}

const Expr *single_return_expr(const FunctionDecl &fn) {
    if (fn.body == nullptr) {
        return nullptr;
    }
    const auto *body = std::get_if<Stmt::Block>(&fn.body->data);
    if (body == nullptr) {
        return nullptr;
    }
    const Expr *value = nullptr;
    for (const auto &stmt : body->statements) {
        if (stmt == nullptr) {
            continue;
        }
        const auto *ret = std::get_if<Stmt::Return>(&stmt->data);
        if (ret == nullptr || value != nullptr || ret->value == nullptr || !ret->extra.empty()) {
            return nullptr;
        }
        value = ret->value.get();
    }
    return value;
}

const Expr *single_i32_return(const FunctionDecl &fn) {
    if (!plain_i32_result(fn)) {
        return nullptr;
    }
    const auto *body = std::get_if<Stmt::Block>(&fn.body->data);
    if (body == nullptr) {
        return nullptr;
    }
    const Expr *value = nullptr;
    for (const auto &stmt : body->statements) {
        if (stmt == nullptr) {
            continue;
        }
        const auto *ret = std::get_if<Stmt::Return>(&stmt->data);
        if (ret == nullptr || value != nullptr || ret->value == nullptr || !ret->extra.empty()) {
            return nullptr;
        }
        value = ret->value.get();
    }
    return value;
}

} // namespace

std::optional<std::string> Checker::type_constant(const Expr &expr) const {
    const auto *kind = std::get_if<Expr::Typeof>(&expr.data);
    if (kind == nullptr || kind->canonical.empty()) {
        return std::nullopt;
    }
    return kind->canonical;
}

std::optional<Checker::FoldedNumber> Checker::fold_number(const Expr &expr) {
    std::unordered_set<std::string> seen;
    return fold_number(expr, seen);
}

std::optional<Checker::FoldedNumber> Checker::fold_number(const Expr &expr, std::unordered_set<std::string> &seen) {
    const auto from_int = [](int value) {
        return FoldedNumber{static_cast<double>(value), false, false};
    };
    return std::visit(
        [&](const auto &node) -> std::optional<FoldedNumber> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Char> || std::is_same_v<T, Expr::Bool> ||
                          std::is_same_v<T, Expr::Sizeof>) {
                const std::optional<int> value = fold_int(expr);
                if (!value) {
                    return std::nullopt;
                }
                return from_int(*value);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                const double parsed = node.text.empty() ? 0.0 : std::strtod(node.text.c_str(), nullptr);
                if (node.is_f32) {
                    return FoldedNumber{round_float(parsed, false), true, false};
                }
                return FoldedNumber{parsed, true, true};
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const std::optional<int> value = fold_int(expr)) {
                    return from_int(*value);
                }
                const Symbol *symbol = lookup(node.text);
                if (symbol == nullptr || !symbol->is_constant || symbol->type.kind != TypeKind::Float) {
                    return std::nullopt;
                }
                if (symbol->const_init != nullptr && seen.insert(node.text).second) {
                    const std::optional<FoldedNumber> local = fold_number(*symbol->const_init, seen);
                    seen.erase(node.text);
                    if (local) {
                        return local;
                    }
                }
                const VarDecl *var = global_var(node.text);
                if (var == nullptr || var->init == nullptr || !seen.insert(node.text).second) {
                    return std::nullopt;
                }
                const std::optional<FoldedNumber> inner = fold_number(*var->init, seen);
                seen.erase(node.text);
                return inner;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::KwUnsafe && node.expr != nullptr) {
                    return fold_number(*node.expr, seen);
                }
                if (node.op == TokenKind::Star && node.expr != nullptr && !node.postfix) {
                    std::unordered_set<std::string> pointed;
                    if (const Expr *target = comptime_pointee(*node.expr, pointed)) {
                        return fold_number(*target, seen);
                    }
                    return std::nullopt;
                }
                if (node.postfix || node.expr == nullptr ||
                    (node.op != TokenKind::Plus && node.op != TokenKind::Minus)) {
                    return std::nullopt;
                }
                std::optional<FoldedNumber> inner = fold_number(*node.expr, seen);
                if (!inner) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::Minus) {
                    inner->value = -inner->value;
                }
                return inner;
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_fn_ptr || node.expr == nullptr) {
                    return std::nullopt;
                }
                std::optional<FoldedNumber> inner = fold_number(*node.expr, seen);
                if (!inner) {
                    return std::nullopt;
                }
                if (node.type_name == "f32" || node.type_name == "float") {
                    inner->value = round_float(inner->value, false);
                    inner->is_float = true;
                    inner->wide = false;
                    return inner;
                }
                if (node.type_name == "f64" || node.type_name == "double") {
                    inner->is_float = true;
                    inner->wide = true;
                    return inner;
                }
                if (const std::optional<int> narrowed = fold_int(expr)) {
                    return from_int(*narrowed);
                }
                return inner;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base == nullptr) {
                    return std::nullopt;
                }
                const Expr *field = comptime_struct_field(*node.base, node.field);
                if (field == nullptr) {
                    return std::nullopt;
                }
                return fold_number(*field, seen);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int> left_int = fold_int(*node.lhs);
                const std::optional<int> right_int = fold_int(*node.rhs);
                if (left_int && right_int) {
                    const std::optional<int> value = eval_int_op(node.op, *left_int, *right_int);
                    if (!value) {
                        return std::nullopt;
                    }
                    return from_int(*value);
                }
                const std::optional<FoldedNumber> left = fold_number(*node.lhs, seen);
                const std::optional<FoldedNumber> right = fold_number(*node.rhs, seen);
                if (!left || !right || (!left->is_float && !right->is_float)) {
                    return std::nullopt;
                }
                const bool wide = left->wide || right->wide;
                const double a = round_float(left->value, wide);
                const double b = round_float(right->value, wide);
                double value = 0;
                switch (node.op) {
                case TokenKind::Plus:
                case TokenKind::Minus:
                case TokenKind::Star:
                    value = float_arith(node.op, a, b, wide);
                    break;
                case TokenKind::Slash:
                    if (b == 0.0) {
                        return std::nullopt;
                    }
                    value = float_arith(node.op, a, b, wide);
                    break;
                case TokenKind::EqEq: value = a == b ? 1 : 0; break;
                case TokenKind::BangEq: value = a != b ? 1 : 0; break;
                case TokenKind::Lt: value = a < b ? 1 : 0; break;
                case TokenKind::Gt: value = a > b ? 1 : 0; break;
                case TokenKind::Le: value = a <= b ? 1 : 0; break;
                case TokenKind::Ge: value = a >= b ? 1 : 0; break;
                case TokenKind::AmpAmp: value = (a != 0.0 && b != 0.0) ? 1 : 0; break;
                case TokenKind::PipePipe: value = (a != 0.0 || b != 0.0) ? 1 : 0; break;
                default: return std::nullopt;
                }
                const bool logic = is_float_logic(node.op);
                if (logic) {
                    return from_int(value == 0.0 ? 0 : 1);
                }
                return FoldedNumber{value, true, wide};
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (!node.comptime_float) {
                    return std::nullopt;
                }
                return FoldedNumber{node.comptime_float_value, true, node.comptime_float_wide};
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

bool Checker::switch_case_matches(const SwitchFrame &frame, const Type &got) const {
    // A TLC enum matches only that enum. It does not become an `i32`.
    if (frame.subject.kind == TypeKind::Enum && !frame.c_enum) {
        return same_type(got, frame.subject);
    }
    if (same_type(got, type_int())) {
        return true;
    }
    // `case Flag.On` and `case 1` are the same integer on an `extern(abi="c")` enum.
    return frame.c_enum && frame.subject.kind == TypeKind::Enum && same_type(got, frame.subject);
}

std::optional<int> Checker::fold_runtime_const(const Expr &expr, int depth) {
    // `const n = 1` then `comptime switch (n)` and `comptime if (n == 1)`.
    // `var n const i32` has an initializer and is still not this value.
    if (depth > 32) {
        return std::nullopt;
    }
    if (const std::optional<int> folded = fold_int(expr)) {
        return folded;
    }
    return std::visit(
        [&](const auto &item) -> std::optional<int> {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(item.text);
                if (symbol == nullptr || !symbol->runtime_const || !plain_int_constant(symbol->type)) {
                    return std::nullopt;
                }
                // `const n = a` keeps the value from that declaration.
                if (symbol->folded_init) {
                    return symbol->const_value;
                }
                if (symbol->promote_init == nullptr) {
                    return std::nullopt;
                }
                return fold_runtime_const(*symbol->promote_init, depth + 1);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (item.postfix || item.expr == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int> inner = fold_runtime_const(*item.expr, depth + 1);
                if (!inner) {
                    return std::nullopt;
                }
                switch (item.op) {
                case TokenKind::Plus: return *inner;
                case TokenKind::Minus:
                    return static_cast<int>(static_cast<uint32_t>(-static_cast<int64_t>(*inner)));
                case TokenKind::Tilde: return static_cast<int>(~static_cast<uint32_t>(*inner));
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (item.lhs == nullptr || item.rhs == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int> lhs = fold_runtime_const(*item.lhs, depth + 1);
                const std::optional<int> rhs = fold_runtime_const(*item.rhs, depth + 1);
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                return eval_int_op(item.op, *lhs, *rhs);
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

std::optional<int> Checker::fold_condition(const Expr &expr) {
    if (const std::optional<int> folded = fold_int(expr)) {
        return folded;
    }
    if (const std::optional<int> folded = fold_runtime_const(expr)) {
        return folded;
    }
    return std::visit(
        [&](const auto &node) -> std::optional<int> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op != TokenKind::Bang || node.postfix || node.expr == nullptr) {
                    return std::nullopt;
                }
                if (const std::optional<int> inner = fold_condition(*node.expr)) {
                    return *inner == 0 ? 1 : 0;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) {
                    const std::optional<int> lhs = fold_switch_case(*node.lhs);
                    const std::optional<int> rhs = fold_switch_case(*node.rhs);
                    if (lhs && rhs) {
                        const bool same = *lhs == *rhs;
                        return node.op == TokenKind::EqEq ? (same ? 1 : 0) : (same ? 0 : 1);
                    }
                }
                // `comptime const n = 4u64` then `comptime if (n > 0)`.
                // `[n]i32` and a generic value argument do not use this fold. `comptime switch` stays `i32`.
                const bool compare = node.op == TokenKind::EqEq || node.op == TokenKind::BangEq ||
                                     node.op == TokenKind::Lt || node.op == TokenKind::Gt || node.op == TokenKind::Le ||
                                     node.op == TokenKind::Ge;
                if (compare) {
                    const auto as_wide = [&](const Expr &side) -> std::optional<WideInt> {
                        if (const std::optional<WideInt> wide = fold_wide_int(side)) {
                            return wide;
                        }
                        if (const std::optional<int> narrow = fold_int(side)) {
                            WideInt wide;
                            wide.value = static_cast<uint32_t>(*narrow);
                            wide.bits = 32;
                            wide.is_unsigned = false;
                            return wide;
                        }
                        return std::nullopt;
                    };
                    const std::optional<WideInt> left = as_wide(*node.lhs);
                    const std::optional<WideInt> right = as_wide(*node.rhs);
                    if (left && right) {
                        const WideInt *kept = nullptr;
                        if (left->bits == right->bits && left->is_unsigned == right->is_unsigned) {
                            kept = &*left;
                        } else if (left->bits == 32 && !left->is_unsigned &&
                                   (right->bits > 32 || right->is_unsigned)) {
                            kept = &*right;
                        } else if (right->bits == 32 && !right->is_unsigned &&
                                   (left->bits > 32 || left->is_unsigned)) {
                            kept = &*left;
                        }
                        if (kept != nullptr) {
                            const auto as_kept = [&](const WideInt &from) -> uint64_t {
                                if (kept->bits == 64 && !kept->is_unsigned && from.bits <= 32 && !from.is_unsigned) {
                                    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(from.value)));
                                }
                                return from.value & (kept->bits >= 64 ? ~uint64_t{0}
                                                                      : (uint64_t{1} << static_cast<unsigned>(kept->bits)) -
                                                                            1u);
                            };
                            const uint64_t a = as_kept(*left);
                            const uint64_t b = as_kept(*right);
                            bool less = false;
                            bool same = false;
                            if (!kept->is_unsigned) {
                                const auto extend = [&](uint64_t value) -> int64_t {
                                    if (kept->bits >= 64) {
                                        return static_cast<int64_t>(value);
                                    }
                                    const uint64_t sign = uint64_t{1} << static_cast<unsigned>(kept->bits - 1);
                                    const uint64_t mask = (uint64_t{1} << static_cast<unsigned>(kept->bits)) - 1u;
                                    value &= mask;
                                    if ((value & sign) != 0) {
                                        value |= ~mask;
                                    }
                                    return static_cast<int64_t>(value);
                                };
                                const int64_t sa = extend(a);
                                const int64_t sb = extend(b);
                                less = sa < sb;
                                same = sa == sb;
                            } else {
                                less = a < b;
                                same = a == b;
                            }
                            switch (node.op) {
                            case TokenKind::EqEq: return same ? 1 : 0;
                            case TokenKind::BangEq: return same ? 0 : 1;
                            case TokenKind::Lt: return less ? 1 : 0;
                            case TokenKind::Gt: return (!less && !same) ? 1 : 0;
                            case TokenKind::Le: return (less || same) ? 1 : 0;
                            case TokenKind::Ge: return !less ? 1 : 0;
                            default: break;
                            }
                        }
                    }
                }
                if (node.op != TokenKind::AmpAmp && node.op != TokenKind::PipePipe) {
                    return std::nullopt;
                }
                const std::optional<int> lhs = fold_condition(*node.lhs);
                const std::optional<int> rhs = fold_condition(*node.rhs);
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::AmpAmp) {
                    return (*lhs != 0 && *rhs != 0) ? 1 : 0;
                }
                return (*lhs != 0 || *rhs != 0) ? 1 : 0;
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

bool Checker::comptime_value(const Expr &expr) {
    if (fold_int(expr) || fold_switch_case(expr)) {
        return true;
    }
    return std::visit(
        [&](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(node.text);
                return symbol != nullptr && symbol->comptime_param;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                return node.op == TokenKind::Bang && !node.postfix && node.expr != nullptr &&
                       comptime_value(*node.expr);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return false;
                }
                const bool compare = node.op == TokenKind::EqEq || node.op == TokenKind::BangEq ||
                                     node.op == TokenKind::AmpAmp || node.op == TokenKind::PipePipe;
                return compare && comptime_value(*node.lhs) && comptime_value(*node.rhs);
            } else {
                return false;
            }
        },
        expr.data);
}

std::optional<int> Checker::fold_switch_case(const Expr &expr) {
    if (const std::optional<int> folded = fold_int(expr)) {
        return folded;
    }
    // `const e = Order.equal` then `e == Order.equal`. A local `const` stays out.
    const Expr *cursor = &expr;
    std::unordered_set<std::string> seen;
    for (int depth = 0; depth < 8 && cursor != nullptr; ++depth) {
        const auto *named = std::get_if<Expr::Name>(&cursor->data);
        if (named == nullptr) {
            break;
        }
        if (!seen.insert(named->text).second) {
            return std::nullopt;
        }
        const Symbol *symbol = lookup(named->text);
        if (symbol == nullptr || symbol->is_auto || symbol->type.kind != TypeKind::Enum) {
            return std::nullopt;
        }
        const VarDecl *var = global_var(named->text);
        if (var == nullptr || !var->is_const_decl || var->init == nullptr || var->init.get() == cursor) {
            return std::nullopt;
        }
        cursor = var->init.get();
    }
    if (cursor != &expr) {
        return fold_switch_case(*cursor);
    }
    const auto *selector = std::get_if<Expr::Selector>(&expr.data);
    if (selector == nullptr || selector->base == nullptr) {
        return std::nullopt;
    }
    // `made.ink` is the enumerator that call kept. `Color.Blue` is still the selector.
    if (const Expr *field = comptime_struct_field(*selector->base, selector->field)) {
        return fold_switch_case(*field);
    }
    const int enum_id = enum_designator(*selector->base);
    const EnumInfo::Member *member = enum_id < 0 ? nullptr : find_enum_member(enum_id, selector->field);
    if (member == nullptr || member->value != static_cast<int64_t>(static_cast<int>(member->value))) {
        return std::nullopt;
    }
    return static_cast<int>(member->value);
}

std::optional<std::string> Checker::constant_string(const Expr &expr, std::unordered_set<std::string> &seen) {
    if (const auto *text = std::get_if<Expr::String>(&expr.data)) {
        return text->text;
    }
    if (const auto *call = std::get_if<Expr::Call>(&expr.data)) {
        if (call->comptime_text) {
            return call->comptime_bytes;
        }
        return format_call(*call);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base != nullptr && selector->field != "len") {
            if (const Expr *field = comptime_struct_field(*selector->base, selector->field)) {
                return constant_string(*field, seen);
            }
        }
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        // `"hello"[1:4]` is those bytes. The length does not include the terminating null.
        // A high bound below the low bound, or a bound past that length, is not this slice.
        if (!index->is_range || index->base == nullptr) {
            return std::nullopt;
        }
        const std::optional<std::string> bytes = constant_string(*index->base, seen);
        if (!bytes) {
            return std::nullopt;
        }
        std::size_t low = 0;
        if (index->index != nullptr) {
            const std::optional<int> at = string_bound(*index->index, index->base.get());
            if (!at || *at < 0 || static_cast<std::size_t>(*at) > bytes->size()) {
                return std::nullopt;
            }
            low = static_cast<std::size_t>(*at);
        }
        std::size_t high = bytes->size();
        if (index->end != nullptr) {
            const std::optional<int> at = string_bound(*index->end, index->base.get());
            if (!at || *at < 0 || static_cast<std::size_t>(*at) > bytes->size()) {
                return std::nullopt;
            }
            high = static_cast<std::size_t>(*at);
        }
        if (low > high) {
            return std::nullopt;
        }
        return bytes->substr(low, high - low);
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return std::nullopt;
    }
    const Symbol *symbol = lookup(named->text);
    // `text := "hi"` is that string until a store. A `var` and a `const` are not.
    if (symbol != nullptr && symbol->held_comptime && symbol->const_init != nullptr) {
        std::optional<std::string> bytes = constant_string(*symbol->const_init, seen);
        seen.erase(named->text);
        return bytes;
    }
    // A local `const` and a `var` are not a compile-time string. A file-scope `const` is.
    if (symbol == nullptr || symbol->is_auto || symbol->is_function || symbol->is_type || symbol->is_package) {
        seen.erase(named->text);
        return std::nullopt;
    }
    const VarDecl *var = global_var(named->text);
    std::optional<std::string> bytes;
    // `const label = "hi"` is that slice, with or without `comptime`.
    // `const p *const u8 = "hi"` is a pointer.
    if (var != nullptr && var->is_const_decl && var->indirections == 0 && var->init != nullptr) {
        bytes = constant_string(*var->init, seen);
    }
    seen.erase(named->text);
    return bytes;
}

std::optional<int> Checker::known_len(const Expr &base) {
    if (const auto *init = std::get_if<Expr::Init>(&base.data)) {
        if (init->is_tuple && init->elements.size() >= 2 && init->elements.size() <= 2147483647) {
            return static_cast<int>(init->elements.size());
        }
    }
    if (const auto *named = std::get_if<Expr::Name>(&base.data)) {
        const Symbol *symbol = lookup(named->text);
        if (symbol != nullptr && symbol->type.kind == TypeKind::Tuple && symbol->type.results.size() >= 2 &&
            symbol->type.results.size() <= 2147483647) {
            return static_cast<int>(symbol->type.results.size());
        }
        if (symbol != nullptr && symbol->type.kind == TypeKind::Array && symbol->type.element != nullptr &&
            symbol->type.length >= 0) {
            return symbol->type.length;
        }
        if (symbol != nullptr && is_decayed_array(symbol->type)) {
            return symbol->type.decayed_len;
        }
        if (symbol != nullptr && symbol->type.kind == TypeKind::Pointer && !symbol->type.optional &&
            symbol->type.pointee != nullptr && symbol->type.pointee->kind == TypeKind::Array &&
            symbol->type.pointee->length >= 0 && symbol->type.pointee->element != nullptr) {
            return symbol->type.pointee->length;
        }
    }
    std::unordered_set<std::string> seen;
    if (const std::optional<std::string> text = constant_string(base, seen)) {
        if (text->size() > 2147483647) {
            return std::nullopt;
        }
        return static_cast<int>(text->size());
    }
    const auto *call = std::get_if<Expr::Call>(&base.data);
    if (call == nullptr || call->callee == nullptr || !call->args.empty()) {
        return std::nullopt;
    }
    const auto *method = std::get_if<Expr::Selector>(&call->callee->data);
    if (method == nullptr || method->base == nullptr || (method->field != "fields" && method->field != "methods")) {
        return std::nullopt;
    }
    const bool methods = method->field == "methods";
    const auto *info = std::get_if<Expr::Typeof>(&method->base->data);
    if (info == nullptr || folding_fields_len_) {
        return std::nullopt;
    }
    folding_fields_len_ = true;
    const bool saved_quiet = quiet_;
    quiet_ = true;
    const Type type = operand_type(info->on_type, info->type_name, info->type_package, info->is_struct, info->is_enum,
                                   info->indirections, info->optional, info->expr.get(), info->loc, info->const_pointee,
                                   info->is_array, info->array_len, info->elem_const, info->elem_slice, info->shells);
    quiet_ = saved_quiet;
    folding_fields_len_ = false;
    if (type.kind == TypeKind::Invalid || mentions_trait(type)) {
        return std::nullopt;
    }
    if (methods) {
        std::string owner;
        if (type.kind == TypeKind::Struct && type.struct_id >= 0 && type.struct_id < static_cast<int>(structs_.size()) &&
            !structs_[static_cast<std::size_t>(type.struct_id)].is_union &&
            !structs_[static_cast<std::size_t>(type.struct_id)].anonymous) {
            owner = structs_[static_cast<std::size_t>(type.struct_id)].name;
        } else if (type.kind == TypeKind::Enum && type.enum_id >= 0 &&
                   type.enum_id < static_cast<int>(enum_types_.size())) {
            owner = enum_types_[static_cast<std::size_t>(type.enum_id)].name;
        } else if (type.alias_id > 0) {
            owner = type_desc(type);
        }
        if (owner.empty()) {
            return 0;
        }
        int count = 0;
        for (const auto &file : methods_) {
            const auto found = file.second.find(owner);
            if (found == file.second.end()) {
                continue;
            }
            for (const auto &named : found->second) {
                const auto one = [](const MethodInfo &info) {
                    if (info.type.kind != TypeKind::Function || info.type.result == nullptr) {
                        return false;
                    }
                    if (info.decl != nullptr && info.decl->is_macro) {
                        return false;
                    }
                    if (info.decl != nullptr && !info.decl->type_params.empty() && !info.decl->generic_instance) {
                        return false;
                    }
                    if (info.decl != nullptr && !info.decl->method_params.empty() && !info.decl->generic_instance &&
                        info.decl->method_struct.find(".spec.") == std::string::npos) {
                        return false;
                    }
                    return true;
                };
                if (one(named.second)) {
                    ++count;
                }
                for (const MethodInfo &extra : named.second.more) {
                    if (one(extra)) {
                        ++count;
                    }
                }
            }
        }
        return count;
    }
    if (type.kind == TypeKind::Tuple) {
        return static_cast<int>(type.results.size());
    }
    if (type.kind == TypeKind::Struct && type.struct_id >= 0 &&
        type.struct_id < static_cast<int>(structs_.size())) {
        const StructInfo &info_struct = structs_[static_cast<std::size_t>(type.struct_id)];
        if (info_struct.is_union || info_struct.anonymous) {
            return 0;
        }
        int count = 0;
        for (const StructInfo::Field &field : info_struct.fields) {
            if (!field.name.empty()) {
                ++count;
            }
        }
        return count;
    }
    return 0;
}

std::optional<int> Checker::constant_index(const Expr &expr) {
    // `const j = i + 1` follows `i`. A cycle, or a chain past this depth, is not an index.
    const auto walk = [&](const auto &self, const Expr &node, int depth) -> std::optional<int> {
        if (depth > 32) {
            return std::nullopt;
        }
        if (const std::optional<int> folded = fold_int(node)) {
            return folded;
        }
        return std::visit(
            [&](const auto &item) -> std::optional<int> {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Expr::Name>) {
                    const Symbol *symbol = lookup(item.text);
                    if (symbol == nullptr) {
                        return std::nullopt;
                    }
                    if (symbol->folded_local ||
                        (symbol->folded_init && symbol->runtime_const && plain_int_constant(symbol->type))) {
                        return symbol->const_value;
                    }
                    // `const i = 0` then `t[i]`. A `var` has no folded initializer.
                    if (symbol->promote_init != nullptr && plain_int_constant(symbol->type)) {
                        return self(self, *symbol->promote_init, depth + 1);
                    }
                    return std::nullopt;
                } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                    if (item.postfix || item.expr == nullptr) {
                        return std::nullopt;
                    }
                    const std::optional<int> inner = self(self, *item.expr, depth + 1);
                    if (!inner) {
                        return std::nullopt;
                    }
                    switch (item.op) {
                    case TokenKind::Plus: return *inner;
                    case TokenKind::Minus:
                        return static_cast<int>(static_cast<uint32_t>(-static_cast<int64_t>(*inner)));
                    case TokenKind::Tilde: return static_cast<int>(~static_cast<uint32_t>(*inner));
                    default: return std::nullopt;
                    }
                } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                    if (item.lhs == nullptr || item.rhs == nullptr) {
                        return std::nullopt;
                    }
                    const std::optional<int> lhs = self(self, *item.lhs, depth + 1);
                    const std::optional<int> rhs = self(self, *item.rhs, depth + 1);
                    if (!lhs || !rhs) {
                        return std::nullopt;
                    }
                    return eval_int_op(item.op, *lhs, *rhs);
                } else {
                    return std::nullopt;
                }
            },
            node.data);
    };
    return walk(walk, expr, 0);
}

namespace {

uint64_t wide_mask(int bits) {
    if (bits >= 64) {
        return ~uint64_t{0};
    }
    return (uint64_t{1} << static_cast<unsigned>(bits)) - 1u;
}

} // namespace

std::optional<Checker::WideInt> Checker::fold_wide_int(const Expr &expr) {
    // `fold_int` owns a value that fits in signed `i32`. This walk keeps a wider suffix.
    const auto walk = [&](const auto &self, const Expr &node, int depth,
                          std::unordered_set<std::string> &seen) -> std::optional<WideInt> {
        if (depth > 32) {
            return std::nullopt;
        }
        return std::visit(
            [&](const auto &item) -> std::optional<WideInt> {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Expr::Int>) {
                    // An unsuffixed number past `i32` is `i64` when it fits. Past `i64` it is not a constant.
                    int bits = item.suffix.bits;
                    if (bits == 0) {
                        if (item.value > 9223372036854775807ULL) {
                            return std::nullopt;
                        }
                        bits = item.value > 2147483647ULL ? 64 : 32;
                    }
                    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) {
                        return std::nullopt;
                    }
                    if (item.value > wide_mask(bits)) {
                        return std::nullopt;
                    }
                    WideInt wide;
                    wide.value = item.value;
                    wide.bits = bits;
                    wide.is_unsigned = item.suffix.bits != 0 && item.suffix.is_unsigned;
                    return wide;
                } else if constexpr (std::is_same_v<T, Expr::Name>) {
                    if (!seen.insert(item.text).second) {
                        return std::nullopt;
                    }
                    const Symbol *symbol = lookup(item.text);
                    std::optional<WideInt> folded;
                    if (symbol != nullptr && symbol->is_constant && symbol->const_init != nullptr) {
                        folded = self(self, *symbol->const_init, depth + 1, seen);
                    }
                    if (!folded && symbol != nullptr && plain_int_constant(symbol->type) &&
                        (symbol->is_constant || symbol->folded_local)) {
                        WideInt wide;
                        wide.value = static_cast<uint32_t>(symbol->const_value);
                        wide.bits = 32;
                        wide.is_unsigned = false;
                        folded = wide;
                    }
                    seen.erase(item.text);
                    return folded;
                } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                    if (item.postfix || item.expr == nullptr) {
                        return std::nullopt;
                    }
                    const std::optional<WideInt> inner = self(self, *item.expr, depth + 1, seen);
                    if (!inner) {
                        return std::nullopt;
                    }
                    const uint64_t mask = wide_mask(inner->bits);
                    WideInt wide = *inner;
                    switch (item.op) {
                    case TokenKind::Plus: return wide;
                    case TokenKind::Minus: wide.value = (0u - inner->value) & mask; return wide;
                    case TokenKind::Tilde: wide.value = (~inner->value) & mask; return wide;
                    default: return std::nullopt;
                    }
                } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                    if (item.lhs == nullptr || item.rhs == nullptr) {
                        return std::nullopt;
                    }
                    switch (item.op) {
                    case TokenKind::Plus:
                    case TokenKind::Minus:
                    case TokenKind::Star:
                    case TokenKind::Slash:
                    case TokenKind::Percent:
                    case TokenKind::Amp:
                    case TokenKind::Pipe:
                    case TokenKind::Caret:
                    case TokenKind::LtLt:
                    case TokenKind::GtGt:
                        break;
                    default:
                        return std::nullopt;
                    }
                    const auto piece = [&](const Expr &side) -> std::optional<WideInt> {
                        if (const std::optional<WideInt> wide = self(self, side, depth + 1, seen)) {
                            return wide;
                        }
                        if (const std::optional<int> narrow = fold_int(side)) {
                            WideInt wide;
                            wide.value = static_cast<uint32_t>(*narrow);
                            wide.bits = 32;
                            wide.is_unsigned = false;
                            return wide;
                        }
                        return std::nullopt;
                    };
                    const std::optional<WideInt> left = piece(*item.lhs);
                    const std::optional<WideInt> right = piece(*item.rhs);
                    if (!left || !right) {
                        return std::nullopt;
                    }
                    // Same suffix, or a signed `i32` converted into the wider operand.
                    const WideInt *kept = nullptr;
                    if (left->bits == right->bits && left->is_unsigned == right->is_unsigned) {
                        kept = &*left;
                    } else if (left->bits == 32 && !left->is_unsigned && (right->bits > 32 || right->is_unsigned)) {
                        kept = &*right;
                    } else if (right->bits == 32 && !right->is_unsigned && (left->bits > 32 || left->is_unsigned)) {
                        kept = &*left;
                    } else {
                        return std::nullopt;
                    }
                    const auto as_kept = [&](const WideInt &from) -> uint64_t {
                        if (kept->bits == 64 && !kept->is_unsigned && from.bits <= 32 && !from.is_unsigned) {
                            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(from.value)));
                        }
                        return from.value & wide_mask(kept->bits);
                    };
                    const uint64_t a = as_kept(*left);
                    const uint64_t b = as_kept(*right);
                    const uint64_t mask = wide_mask(kept->bits);
                    const bool signed64 = kept->bits == 64 && !kept->is_unsigned;
                    WideInt wide = *kept;
                    switch (item.op) {
                    case TokenKind::Plus: wide.value = (a + b) & mask; break;
                    case TokenKind::Minus: wide.value = (a - b) & mask; break;
                    case TokenKind::Star: wide.value = (a * b) & mask; break;
                    case TokenKind::Amp: wide.value = (a & b) & mask; break;
                    case TokenKind::Pipe: wide.value = (a | b) & mask; break;
                    case TokenKind::Caret: wide.value = (a ^ b) & mask; break;
                    case TokenKind::Slash:
                    case TokenKind::Percent: {
                        if (b == 0) {
                            return std::nullopt;
                        }
                        if (signed64) {
                            const int64_t sa = static_cast<int64_t>(a);
                            const int64_t sb = static_cast<int64_t>(b);
                            if (sa == static_cast<int64_t>(uint64_t{1} << 63) && sb == -1) {
                                wide.value = item.op == TokenKind::Slash ? static_cast<uint64_t>(sa) : 0;
                                break;
                            }
                            const int64_t result = item.op == TokenKind::Slash ? sa / sb : sa % sb;
                            wide.value = static_cast<uint64_t>(result);
                            break;
                        }
                        wide.value = (item.op == TokenKind::Slash ? a / b : a % b) & mask;
                        break;
                    }
                    case TokenKind::LtLt:
                        if (b >= static_cast<uint64_t>(kept->bits)) {
                            return std::nullopt;
                        }
                        wide.value = (a << b) & mask;
                        break;
                    case TokenKind::GtGt:
                        if (b >= static_cast<uint64_t>(kept->bits)) {
                            return std::nullopt;
                        }
                        if (signed64) {
                            wide.value = static_cast<uint64_t>(static_cast<int64_t>(a) >> static_cast<unsigned>(b));
                        } else if (!kept->is_unsigned) {
                            const uint64_t sign = uint64_t{1} << static_cast<unsigned>(kept->bits - 1);
                            const int64_t sa = (a & sign) != 0 ? static_cast<int64_t>(a | ~mask) : static_cast<int64_t>(a);
                            wide.value = static_cast<uint64_t>(sa >> static_cast<unsigned>(b)) & mask;
                        } else {
                            wide.value = (a >> static_cast<unsigned>(b)) & mask;
                        }
                        break;
                    default: return std::nullopt;
                    }
                    return wide;
                } else if constexpr (std::is_same_v<T, Expr::Call>) {
                    // `wide(0) > 0u64` reads the integer that call kept.
                    if (item.comptime_struct == nullptr) {
                        return std::nullopt;
                    }
                    return self(self, *item.comptime_struct, depth + 1, seen);
                } else {
                    return std::nullopt;
                }
            },
            node.data);
    };
    std::unordered_set<std::string> seen;
    return walk(walk, expr, 0, seen);
}

std::optional<int> Checker::folded_wide_i32(const Expr &expr) {
    const std::optional<WideInt> wide = fold_wide_int(expr);
    if (!wide) {
        return std::nullopt;
    }
    // `i8` and `i16` stay out. `i32` already folds through `fold_int`.
    const bool i64 = wide->bits == 64 && !wide->is_unsigned;
    const bool u32 = wide->bits == 32 && wide->is_unsigned;
    const bool u64 = wide->bits == 64 && wide->is_unsigned;
    if (!i64 && !u32 && !u64) {
        return std::nullopt;
    }
    if (i64) {
        const int64_t signed_value = static_cast<int64_t>(wide->value);
        if (signed_value < static_cast<int64_t>(-2147483647 - 1) || signed_value > 2147483647) {
            return std::nullopt;
        }
        return static_cast<int>(signed_value);
    }
    if (wide->value > 2147483647ULL) {
        return std::nullopt;
    }
    return static_cast<int>(wide->value);
}

bool Checker::folded_wide_negative(const Expr &expr) {
    const std::optional<WideInt> wide = fold_wide_int(expr);
    if (!wide || wide->bits != 64 || wide->is_unsigned) {
        return false;
    }
    return static_cast<int64_t>(wide->value) < 0;
}

bool Checker::rewrite_wide_int(std::unique_ptr<Expr> &expr) {
    if (expr == nullptr || fold_int(*expr)) {
        return false;
    }
    const std::optional<WideInt> wide = fold_wide_int(*expr);
    if (!wide) {
        return false;
    }
    auto replaced = std::make_unique<Expr>();
    Expr::Int number;
    number.loc = expr_loc(*expr);
    number.value = wide->value;
    number.suffix.bits = wide->bits;
    number.suffix.is_unsigned = wide->is_unsigned;
    replaced->data = std::move(number);
    expr = std::move(replaced);
    return true;
}

std::optional<int> Checker::fold_int(const Expr &expr) {
    auto wrap = [](int64_t value) { return static_cast<int>(static_cast<uint32_t>(value)); };
    return std::visit(
        [&](const auto &node) -> std::optional<int> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int>) {
                // `1u32` fits in the compile-time integer. A value past i32 does not.
                if (node.value > 2147483647ULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                // `'A'b` is a byte. `'A'` is a code point and fits in the compile-time `i32`.
                if (node.byte_form ? node.value > 255ULL : node.value > 0x10FFFFULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(node.text);
                // `name := 10` folds until a store. A `var` and a `const` do not.
                const bool held = symbol != nullptr && (symbol->is_constant || (symbol->held_comptime && symbol->folded_local));
                if (held && plain_int_constant(symbol->type)) {
                    return symbol->const_value;
                }
                // `comptime const ok = yes()` when `yes` returns `bool`. The value is 0 or 1.
                if (symbol != nullptr && (symbol->is_constant || symbol->held_comptime) && symbol->folded_local &&
                    symbol->type.alias_id == 0 && symbol->type.kind == TypeKind::Bool) {
                    return symbol->const_value;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.len_value >= 0) {
                    return node.len_value;
                }
                // A tuple's length is the element count, including before this selector is typechecked.
                if (node.field == "len" && node.base != nullptr) {
                    if (const std::optional<int> known = known_len(*node.base)) {
                        return known;
                    }
                    if (const auto *init = std::get_if<Expr::Init>(&node.base->data)) {
                        if (init->is_tuple && init->elements.size() >= 2) {
                            return static_cast<int>(init->elements.size());
                        }
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                        const Symbol *symbol = lookup(named->text);
                        if (symbol != nullptr && symbol->type.kind == TypeKind::Tuple && symbol->type.results.size() >= 2) {
                            return static_cast<int>(symbol->type.results.size());
                        }
                    }
                }
                // `Point{.x = 1}.x` is that field. `const k = 40` then `Point{.x = k}.x` is `k`.
                // A local `const` stays loaded. An enumerator is not a field.
                if (node.base != nullptr) {
                    if (const Expr *field = comptime_struct_field(*node.base, node.field)) {
                        if (const std::optional<int> folded = fold_int(*field)) {
                            return folded;
                        }
                        if (const std::optional<int> file = file_scope_const_int(*field)) {
                            return file;
                        }
                    }
                }
                const auto folded_assoc = [&](uint32_t file, const std::string &owner) -> std::optional<int> {
                    const AssocVar *var = find_assoc_in(file, owner, node.field);
                    if (var == nullptr || !var->is_const || !var->has_const) {
                        return std::nullopt;
                    }
                    return var->const_value;
                };
                const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base != nullptr) {
                    const Symbol *symbol = lookup(base->text);
                    if (symbol != nullptr && symbol->is_package) {
                        const Symbol *member = find_export(symbol->package_file, node.field);
                        if (member == nullptr || !member->is_constant || !plain_int_constant(member->type)) {
                            return std::nullopt;
                        }
                        return member->const_value;
                    }
                    if (symbol == nullptr || symbol->is_type) {
                        return folded_assoc(file_id_, base->text);
                    }
                    return std::nullopt;
                }
                const auto *inner = node.base == nullptr ? nullptr : std::get_if<Expr::Selector>(&node.base->data);
                if (inner != nullptr && inner->base != nullptr && inner->type_args.empty()) {
                    if (const auto *package_name = std::get_if<Expr::Name>(&inner->base->data)) {
                        const Symbol *package = lookup(package_name->text);
                        if (package != nullptr && package->is_package) {
                            const Symbol *type = find_export(package->package_file, inner->field);
                            if (type != nullptr && type->is_type) {
                                return folded_assoc(package->package_file, inner->field);
                            }
                        }
                    }
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                const bool saved = quiet_;
                quiet_ = true;
                const Type type = sizeof_operand(node);
                quiet_ = saved;
                if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Void || type.kind == TypeKind::Function ||
                    mentions_trait(type)) {
                    return std::nullopt;
                }
                if (node.align) {
                    return min_align_of(type);
                }
                return type_size(type);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.postfix || node.expr == nullptr) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::KwUnsafe) {
                    return fold_int(*node.expr);
                }
                // `*p` is the comptime value `p` points at.
                if (node.op == TokenKind::Star) {
                    std::unordered_set<std::string> seen;
                    if (const Expr *target = comptime_pointee(*node.expr, seen)) {
                        return fold_int(*target);
                    }
                    return std::nullopt;
                }
                if (node.op == TokenKind::Bang) {
                    if (const std::optional<int> inner = fold_int(*node.expr)) {
                        return *inner == 0 ? 1 : 0;
                    }
                    const std::optional<FoldedNumber> inner = fold_number(*node.expr);
                    if (!inner) {
                        return std::nullopt;
                    }
                    return inner->value == 0.0 ? 1 : 0;
                }
                const std::optional<int> inner = fold_int(*node.expr);
                if (!inner) {
                    return std::nullopt;
                }
                switch (node.op) {
                case TokenKind::Plus: return *inner;
                case TokenKind::Minus: return wrap(-static_cast<int64_t>(*inner));
                case TokenKind::Tilde: return wrap(~static_cast<uint32_t>(*inner));
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_struct || node.is_enum || node.expr == nullptr ||
                    !node.type_package.empty()) {
                    return std::nullopt;
                }
                Type named;
                if (!fill_builtin(node.type_name, named) && !package_typedef(file_id_, node.type_name, named)) {
                    return std::nullopt;
                }
                if (!is_integer(named) && named.kind != TypeKind::Bool && !is_c_enum(named)) {
                    return std::nullopt;
                }
                std::optional<int> inner = fold_int(*node.expr);
                // `Integer(k)` when `k` is a file-scope integer `const`. `a + b` is not this cast.
                if (!inner) {
                    inner = file_scope_const_int(*node.expr);
                }
                if (!inner || is_c_enum(named)) {
                    return inner;
                }
                if (named.kind == TypeKind::Bool) {
                    return cast_folded(*inner, 1, false, true);
                }
                // `i8` and `u8` are `Char`. Only `u8` and a plain `char` zero-extend.
                return cast_folded(*inner, width_of(named), named.is_unsigned || named.plain_char, false);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.op == TokenKind::Question) {
                    if (node.lhs == nullptr || node.mid == nullptr || node.rhs == nullptr) {
                        return std::nullopt;
                    }
                    const std::optional<int> cond = fold_condition(*node.lhs);
                    if (!cond) {
                        return std::nullopt;
                    }
                    return fold_int(*(*cond != 0 ? node.mid : node.rhs));
                }
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) {
                    const auto *left_call = std::get_if<Expr::Call>(&node.lhs->data);
                    const auto *right_call = std::get_if<Expr::Call>(&node.rhs->data);
                    const bool alias_cmp = (left_call != nullptr && left_call->type_alias) ||
                                           (right_call != nullptr && right_call->type_alias);
                    if (alias_cmp) {
                        struct TypeId {
                            bool null_type = false;
                            std::string spelling;
                            int arm = 0;
                            bool has_this = false;
                            bool generic = false;
                            bool variadic = false;
                        };
                        const auto identity = [&](const Expr &side) -> std::optional<TypeId> {
                            if (std::holds_alternative<Expr::Nullptr>(side.data)) {
                                TypeId id;
                                id.null_type = true;
                                return id;
                            }
                            if (const auto *info = std::get_if<Expr::Typeof>(&side.data)) {
                                if (info->canonical.empty()) {
                                    return std::nullopt;
                                }
                                TypeId id;
                                id.spelling = info->canonical;
                                id.arm = info->type_arm;
                                id.has_this = info->type_has_this;
                                id.generic = info->type_generic;
                                id.variadic = info->type_variadic;
                                return id;
                            }
                            if (const auto *call = std::get_if<Expr::Call>(&side.data)) {
                                if (!call->type_alias) {
                                    return std::nullopt;
                                }
                                TypeId id;
                                if (call->alias_null || call->alias_canonical.empty()) {
                                    id.null_type = true;
                                    return id;
                                }
                                id.spelling = call->alias_canonical;
                                id.arm = call->alias_arm;
                                id.has_this = call->alias_has_this;
                                id.generic = call->alias_generic;
                                id.variadic = call->alias_variadic;
                                return id;
                            }
                            return std::nullopt;
                        };
                        if (const std::optional<TypeId> left = identity(*node.lhs)) {
                            if (const std::optional<TypeId> right = identity(*node.rhs)) {
                                const bool same = left->null_type == right->null_type &&
                                                  (left->null_type ||
                                                   (left->spelling == right->spelling && left->arm == right->arm &&
                                                    left->has_this == right->has_this &&
                                                    left->generic == right->generic &&
                                                    left->variadic == right->variadic));
                                if (node.op == TokenKind::EqEq) {
                                    return same ? 1 : 0;
                                }
                                return same ? 0 : 1;
                            }
                        }
                    }
                }
                if (const auto *left_of = std::get_if<Expr::Typeof>(&node.lhs->data)) {
                    if (const auto *right_of = std::get_if<Expr::Typeof>(&node.rhs->data)) {
                        if (const std::optional<std::string> left_type = type_constant(*node.lhs)) {
                            if (const std::optional<std::string> right_type = type_constant(*node.rhs)) {
                                const bool same = *left_type == *right_type && left_of->type_arm == right_of->type_arm &&
                                                  left_of->type_has_this == right_of->type_has_this &&
                                                  left_of->type_generic == right_of->type_generic &&
                                                  left_of->type_variadic == right_of->type_variadic;
                                if (node.op == TokenKind::EqEq) {
                                    return same ? 1 : 0;
                                }
                                if (node.op == TokenKind::BangEq) {
                                    return same ? 0 : 1;
                                }
                                return std::nullopt;
                            }
                        }
                    }
                }
                const std::optional<int> lhs = fold_int(*node.lhs);
                const std::optional<int> rhs = fold_int(*node.rhs);
                if (lhs && rhs) {
                    return eval_int_op(node.op, *lhs, *rhs);
                }
                if (!is_float_logic(node.op)) {
                    return std::nullopt;
                }
                const std::optional<FoldedNumber> left = fold_number(*node.lhs);
                const std::optional<FoldedNumber> right = fold_number(*node.rhs);
                if (!left || !right || (!left->is_float && !right->is_float)) {
                    return std::nullopt;
                }
                const bool wide = left->wide || right->wide;
                const double a = round_float(left->value, wide);
                const double b = round_float(right->value, wide);
                switch (node.op) {
                case TokenKind::EqEq: return a == b ? 1 : 0;
                case TokenKind::BangEq: return a != b ? 1 : 0;
                case TokenKind::Lt: return a < b ? 1 : 0;
                case TokenKind::Gt: return a > b ? 1 : 0;
                case TokenKind::Le: return a <= b ? 1 : 0;
                case TokenKind::Ge: return a >= b ? 1 : 0;
                case TokenKind::AmpAmp: return (a != 0.0 && b != 0.0) ? 1 : 0;
                case TokenKind::PipePipe: return (a != 0.0 || b != 0.0) ? 1 : 0;
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                // `row.len()` is the field `row.len`.
                if (node.args.empty() && node.callee != nullptr) {
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                        if (selector->slice_len || selector->field == "len") {
                            if (const std::optional<int> length = fold_int(*node.callee)) {
                                return length;
                            }
                        }
                    }
                }
                if (const std::optional<int> compared = fold_mem_compare(node)) {
                    return compared;
                }
                return fold_comptime_call(node);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                // `"hi"[1]` is the byte. The length does not include the terminating null.
                // `pair[0][1]` walks each index. The base may itself be an index.
                if (node.is_range || node.base == nullptr || node.index == nullptr) {
                    return std::nullopt;
                }
                // The tuple slot is chosen while checking. `const i = 0` then `pair[i]` is that field.
                // A file-scope `const` is an index only when the base is a constant string.
                const std::optional<int> at = node.tuple_index >= 0 ? std::optional<int>(node.tuple_index)
                                                                    : string_bound(*node.index, node.base.get());
                if (!at || *at < 0) {
                    if (comptime_explain_ > 0 && at && *at < 0) {
                        note_comptime_index(node.loc);
                    }
                    return std::nullopt;
                }
                // A tuple index selects that element. An array index counts only after one,
                // so `pair[0][1][0]` folds. A lone `row[0]` still loads, unless `row` is `comptime const`.
                const auto elements_at = [&](const auto &self, const Expr &value, int index, int depth,
                                             bool array_ok) -> const Expr * {
                    if (depth > 8 || index < 0) {
                        return nullptr;
                    }
                    if (const auto *init = std::get_if<Expr::Init>(&value.data)) {
                        const bool row = array_ok && !init->is_tuple && !init->array_literal && init->fields.empty() &&
                                         !init->elements.empty();
                        if (!init->is_tuple && !(array_ok && (init->array_literal || row))) {
                            return nullptr;
                        }
                        if (static_cast<std::size_t>(index) >= init->elements.size()) {
                            return nullptr;
                        }
                        return init->elements[static_cast<std::size_t>(index)].get();
                    }
                    if (const auto *call = std::get_if<Expr::Call>(&value.data)) {
                        if (call->comptime_struct == nullptr) {
                            return nullptr;
                        }
                        return self(self, *call->comptime_struct, index, depth + 1, array_ok);
                    }
                    if (const auto *selector = std::get_if<Expr::Selector>(&value.data)) {
                        if (selector->base == nullptr) {
                            return nullptr;
                        }
                        const Expr *field = comptime_struct_field(*selector->base, selector->field);
                        if (field == nullptr) {
                            return nullptr;
                        }
                        return self(self, *field, index, depth + 1, true);
                    }
                    const auto *named = std::get_if<Expr::Name>(&value.data);
                    if (named == nullptr) {
                        return nullptr;
                    }
                    const Symbol *symbol = lookup(named->text);
                    const Expr *next = symbol != nullptr && symbol->is_constant ? symbol->const_init : nullptr;
                    if (next == nullptr) {
                        if (const VarDecl *var = global_var(named->text); var != nullptr && var->is_comptime) {
                            next = var->init.get();
                        }
                    }
                    if (next == nullptr || next == &value) {
                        return nullptr;
                    }
                    return self(self, *next, index, depth + 1, array_ok);
                };
                const auto indexed = [&](const auto &self, const Expr &base, int index, int depth) -> const Expr * {
                    if (depth > 8 || index < 0) {
                        return nullptr;
                    }
                    if (const auto *inner = std::get_if<Expr::Index>(&base.data)) {
                        if (inner->is_range || inner->base == nullptr || inner->index == nullptr) {
                            return nullptr;
                        }
                        const std::optional<int> slot = inner->tuple_index >= 0 ? std::optional<int>(inner->tuple_index)
                                                                                : fold_int(*inner->index);
                        if (!slot || *slot < 0) {
                            return nullptr;
                        }
                        const Expr *inner_value = self(self, *inner->base, *slot, depth + 1);
                        if (inner_value == nullptr) {
                            return nullptr;
                        }
                        return elements_at(elements_at, *inner_value, index, depth + 1, true);
                    }
                    return elements_at(elements_at, base, index, depth, comptime_const_array(base));
                };
                if (const Expr *element = indexed(indexed, *node.base, *at, 0)) {
                    return fold_int(*element);
                }
                if (comptime_explain_ > 0 && !comptime_fault_ && node.base != nullptr) {
                    if (const auto *init = std::get_if<Expr::Init>(&node.base->data);
                        init != nullptr && !init->elements.empty() &&
                        static_cast<std::size_t>(*at) >= init->elements.size()) {
                        note_comptime_index(node.loc);
                    } else if (const std::optional<int> length = comptime_array_len(*node.base);
                               length && *at >= *length) {
                        note_comptime_index(node.loc);
                    }
                }
                std::unordered_set<std::string> seen;
                const std::optional<std::string> bytes = constant_string(*node.base, seen);
                if (!bytes || static_cast<std::size_t>(*at) >= bytes->size()) {
                    if (comptime_explain_ > 0 && bytes && static_cast<std::size_t>(*at) >= bytes->size()) {
                        note_comptime_index(node.loc);
                    }
                    return std::nullopt;
                }
                return static_cast<int>(static_cast<unsigned char>((*bytes)[static_cast<std::size_t>(*at)]));
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

std::optional<int> Checker::file_scope_const_int(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return std::nullopt;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || !symbol->folded_init || !plain_int_constant(symbol->type)) {
        return std::nullopt;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl) {
        return std::nullopt;
    }
    return symbol->const_value;
}

std::optional<Checker::WideInt> Checker::file_scope_const_wide(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return std::nullopt;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->type.alias_id != 0 || symbol->type.kind != TypeKind::Int) {
        return std::nullopt;
    }
    const bool i64 = symbol->type.rank == 5 && !symbol->type.is_unsigned;
    const bool u32 = symbol->type.rank == 3 && symbol->type.is_unsigned;
    const bool u64 = symbol->type.rank == 5 && symbol->type.is_unsigned;
    if (!i64 && !u32 && !u64) {
        return std::nullopt;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl || var->init == nullptr) {
        return std::nullopt;
    }
    const std::optional<WideInt> wide = fold_wide_int(*var->init);
    if (!wide) {
        return std::nullopt;
    }
    if (i64 && !(wide->bits == 64 && !wide->is_unsigned)) {
        return std::nullopt;
    }
    if (u32 && !(wide->bits == 32 && wide->is_unsigned)) {
        return std::nullopt;
    }
    if (u64 && !(wide->bits == 64 && wide->is_unsigned)) {
        return std::nullopt;
    }
    return wide;
}

std::optional<int> Checker::string_bound(const Expr &bound, const Expr *base) {
    if (const std::optional<int> at = fold_int(bound)) {
        return at;
    }
    // `const i = 1` then `"hi"[i]`. The name is not a comptime argument.
    if (base == nullptr) {
        return std::nullopt;
    }
    std::unordered_set<std::string> seen;
    if (!constant_string(*base, seen)) {
        return std::nullopt;
    }
    return file_scope_const_int(bound);
}

const Type *Checker::comptime_array_type(const Expr &expr) {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        const Symbol *symbol = lookup(named->text);
        if (symbol == nullptr || !symbol->is_constant || symbol->type.kind != TypeKind::Array ||
            symbol->type.element == nullptr || is_slice(symbol->type) || symbol->type.length < 0) {
            return nullptr;
        }
        return &symbol->type;
    }
    const auto *index = std::get_if<Expr::Index>(&expr.data);
    if (index == nullptr || index->is_range || index->base == nullptr) {
        return nullptr;
    }
    const Type *base = comptime_array_type(*index->base);
    if (base == nullptr || base->element == nullptr || base->element->kind != TypeKind::Array ||
        is_slice(*base->element) || base->element->length < 0) {
        return nullptr;
    }
    return base->element.get();
}

bool Checker::comptime_const_array(const Expr &expr) { return comptime_array_type(expr) != nullptr; }

std::optional<int> Checker::comptime_array_len(const Expr &expr) {
    const Type *type = comptime_array_type(expr);
    if (type == nullptr) {
        return std::nullopt;
    }
    return type->length;
}

std::optional<bool> Checker::file_scope_const_bool(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return std::nullopt;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || !symbol->folded_init || symbol->type.alias_id != 0 ||
        symbol->type.kind != TypeKind::Bool) {
        return std::nullopt;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl) {
        return std::nullopt;
    }
    return symbol->const_value != 0;
}

const Expr *Checker::file_scope_const_float(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->type.alias_id != 0 || symbol->type.kind != TypeKind::Float) {
        return nullptr;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl || var->init == nullptr) {
        return nullptr;
    }
    std::unordered_set<std::string> seen;
    const std::optional<FoldedNumber> number = fold_number(*var->init, seen);
    if (!number || !number->is_float) {
        return nullptr;
    }
    return var->init.get();
}

const Expr *Checker::file_scope_const_struct(const Expr &expr) {
    std::unordered_set<std::string> seen;
    const Expr *cursor = &expr;
    for (int depth = 0; cursor != nullptr && depth < 8; ++depth) {
        const auto *named = std::get_if<Expr::Name>(&cursor->data);
        if (named == nullptr || !seen.insert(named->text).second) {
            return nullptr;
        }
        const Symbol *symbol = lookup(named->text);
        if (symbol == nullptr || symbol->is_auto || symbol->type.kind != TypeKind::Struct) {
            return nullptr;
        }
        if (symbol->type.struct_id >= 0 && symbol->type.struct_id < static_cast<int>(structs_.size()) &&
            structs_[static_cast<std::size_t>(symbol->type.struct_id)].is_union) {
            return nullptr;
        }
        const VarDecl *var = global_var(named->text);
        if (var == nullptr || !var->is_const_decl || var->init == nullptr || var->init.get() == cursor) {
            return nullptr;
        }
        // `const other = origin` keeps walking. `Point{0...}` is that value.
        const auto *init = std::get_if<Expr::Init>(&var->init->data);
        if (init == nullptr) {
            cursor = var->init.get();
            continue;
        }
        if (init->is_tuple || init->array_literal) {
            return nullptr;
        }
        if (init->fields.empty() && init->elements.empty() && !init->zero_rest) {
            return nullptr;
        }
        return var->init.get();
    }
    return nullptr;
}

const Expr *Checker::file_scope_const_enum(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->type.kind != TypeKind::Enum) {
        return nullptr;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl || var->init == nullptr) {
        return nullptr;
    }
    if (!fold_switch_case(*var->init)) {
        return nullptr;
    }
    return var->init.get();
}

const Expr *Checker::file_scope_const_array(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->type.kind != TypeKind::Array || symbol->type.element == nullptr ||
        is_slice(symbol->type) || symbol->type.length < 0) {
        return nullptr;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl || var->is_comptime || var->init == nullptr) {
        return nullptr;
    }
    const auto *init = std::get_if<Expr::Init>(&var->init->data);
    if (init == nullptr || !init->array_literal) {
        return nullptr;
    }
    // `{2, 0}` inside `[_][2]i32{{2, 0}, {40, 0}}` is a row. Only the outer list sets `array_literal`.
    const auto element_ok = [&](const auto &self, const Expr &item, int depth) -> bool {
        if (depth > 8 || std::holds_alternative<Expr::Nullptr>(item.data)) {
            return false;
        }
        if (const auto *inner = std::get_if<Expr::Init>(&item.data)) {
            const bool row = !inner->is_tuple && !inner->array_literal && inner->fields.empty() &&
                             !inner->elements.empty();
            if (inner->array_literal || row) {
                for (const auto &child : inner->elements) {
                    if (child == nullptr || !self(self, *child, depth + 1)) {
                        return false;
                    }
                }
                return true;
            }
            // `Point{.x = 2, .y = 0}`. A tuple and an empty brace are not this value.
            return !inner->is_tuple && !inner->fields.empty() && comptime_constant(item);
        }
        return argument_is_comptime(item) || fold_int(item).has_value() || file_scope_const_int(item).has_value() ||
               file_scope_const_bool(item).has_value() || file_scope_const_float(item) != nullptr ||
               file_scope_const_struct(item) != nullptr;
    };
    for (const auto &item : init->elements) {
        if (item == nullptr || !element_ok(element_ok, *item, 0)) {
            return nullptr;
        }
    }
    return var->init.get();
}

bool Checker::comptime_array_literal(const Expr &expr) {
    const auto *init = std::get_if<Expr::Init>(&expr.data);
    if (init == nullptr || !init->array_literal) {
        return false;
    }
    // Same elements as a file-scope const array. A `var` is rejected by the caller.
    const auto element_ok = [&](const auto &self, const Expr &item, int depth) -> bool {
        if (depth > 8 || std::holds_alternative<Expr::Nullptr>(item.data)) {
            return false;
        }
        if (const auto *inner = std::get_if<Expr::Init>(&item.data)) {
            const bool row = !inner->is_tuple && !inner->array_literal && inner->fields.empty() &&
                             !inner->elements.empty();
            if (inner->array_literal || row) {
                for (const auto &child : inner->elements) {
                    if (child == nullptr || !self(self, *child, depth + 1)) {
                        return false;
                    }
                }
                return true;
            }
            return !inner->is_tuple && !inner->fields.empty() && comptime_constant(item);
        }
        return argument_is_comptime(item) || fold_int(item).has_value() || file_scope_const_int(item).has_value() ||
               file_scope_const_bool(item).has_value() || file_scope_const_float(item) != nullptr ||
               file_scope_const_struct(item) != nullptr;
    };
    for (const auto &item : init->elements) {
        if (item == nullptr || !element_ok(element_ok, *item, 0)) {
            return false;
        }
    }
    return true;
}

const Expr *Checker::file_scope_const_alias(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->is_constant || symbol->type.alias_id == 0) {
        return nullptr;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr || !var->is_const_decl || var->init == nullptr) {
        return nullptr;
    }
    return var->init.get();
}

std::optional<int> Checker::folded_local_int(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return std::nullopt;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->folded_local) {
        return std::nullopt;
    }
    return symbol->const_value;
}

bool Checker::argument_is_comptime(const Expr &arg) {
    std::unordered_set<std::string> seen;
    if (constant_string(arg, seen)) {
        return true;
    }
    if (fold_int(arg) || fold_wide_int(arg)) {
        return true;
    }
    const auto *tuple = std::get_if<Expr::Init>(&arg.data);
    if (tuple == nullptr || !tuple->is_tuple || tuple->elements.size() < 2) {
        return false;
    }
    for (const auto &element : tuple->elements) {
        if (element == nullptr || (!argument_is_comptime(*element) && !file_scope_const_int(*element) &&
                                   !file_scope_const_wide(*element) && !file_scope_const_bool(*element) &&
                                   file_scope_const_float(*element) == nullptr &&
                                   file_scope_const_struct(*element) == nullptr &&
                                   file_scope_const_enum(*element) == nullptr &&
                                   file_scope_const_array(*element) == nullptr &&
                                   file_scope_const_alias(*element) == nullptr)) {
            return false;
        }
    }
    return true;
}

const Expr *Checker::comptime_tuple_element(const Expr &expr) {
    const auto *index = std::get_if<Expr::Index>(&expr.data);
    if (index == nullptr || index->is_range || index->base == nullptr || index->index == nullptr) {
        return nullptr;
    }
    const std::optional<int> at = fold_int(*index->index);
    if (!at || *at < 0) {
        return nullptr;
    }
    const auto walk = [&](const auto &self, const Expr &base, int slot, int depth) -> const Expr * {
        if (depth > 8 || slot < 0) {
            return nullptr;
        }
        if (const auto *init = std::get_if<Expr::Init>(&base.data)) {
            if (!init->is_tuple || static_cast<std::size_t>(slot) >= init->elements.size()) {
                return nullptr;
            }
            return init->elements[static_cast<std::size_t>(slot)].get();
        }
        if (const auto *call = std::get_if<Expr::Call>(&base.data)) {
            if (call->comptime_struct == nullptr) {
                return nullptr;
            }
            return self(self, *call->comptime_struct, slot, depth + 1);
        }
        if (const auto *named = std::get_if<Expr::Name>(&base.data)) {
            const Symbol *symbol = lookup(named->text);
            const Expr *init_expr =
                symbol != nullptr && (symbol->is_constant || symbol->held_comptime) ? symbol->const_init : nullptr;
            if (init_expr == nullptr) {
                if (const VarDecl *var = global_var(named->text); var != nullptr && var->is_comptime) {
                    init_expr = var->init.get();
                }
            }
            if (init_expr == nullptr) {
                return nullptr;
            }
            return self(self, *init_expr, slot, depth + 1);
        }
        if (const auto *inner = std::get_if<Expr::Index>(&base.data)) {
            if (inner->is_range || inner->base == nullptr || inner->index == nullptr) {
                return nullptr;
            }
            const std::optional<int> at_inner = fold_int(*inner->index);
            if (!at_inner || *at_inner < 0) {
                return nullptr;
            }
            const Expr *element = self(self, *inner->base, *at_inner, depth + 1);
            if (element == nullptr) {
                return nullptr;
            }
            return self(self, *element, slot, depth + 1);
        }
        return nullptr;
    };
    return walk(walk, *index->base, *at, 0);
}

bool Checker::comptime_constant(const Expr &expr) {
    if (const auto *call = std::get_if<Expr::Call>(&expr.data)) {
        return call->comptime_struct != nullptr;
    }
    // `pair[0]` is that element. A function does not fold to an integer.
    if (const Expr *element = comptime_tuple_element(expr)) {
        return argument_is_comptime(*element) || fold_switch_case(*element) || comptime_constant(*element);
    }
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        // An array literal is not this value.
        if (init->array_literal || (!init->is_tuple && !init->elements.empty())) {
            return false;
        }
        if (init->is_tuple) {
            if (init->elements.size() < 2) {
                return false;
            }
            // `(&addr, 2)` and `(p, 2)` when `p` is `&addr`. A lone `&addr` is not `comptime_constant`.
            const auto address_element = [&](const Expr &start) {
                const Expr *cursor = &start;
                for (int depth = 0; cursor != nullptr && depth <= 8; ++depth) {
                    if (comptime_address(*cursor)) {
                        return true;
                    }
                    const auto *named = std::get_if<Expr::Name>(&cursor->data);
                    if (named == nullptr) {
                        return false;
                    }
                    const Symbol *symbol = lookup(named->text);
                    if (symbol == nullptr || !symbol->is_constant) {
                        return false;
                    }
                    const Expr *next = symbol->const_init;
                    if (next == nullptr) {
                        if (const VarDecl *var = global_var(named->text); var != nullptr && var->is_comptime) {
                            next = var->init.get();
                        }
                    }
                    if (next == nullptr || next == cursor) {
                        return false;
                    }
                    cursor = next;
                }
                return false;
            };
            // `([_]i32{40, 2}, 0)` and `(row, 0)` when `row` is that array. A lone array is not
            // `comptime_constant`. The element stays `[N]T`.
            const auto array_element = [&](const auto &self, const Expr &start, int depth) -> bool {
                if (depth > 8) {
                    return false;
                }
                if (const auto *made = std::get_if<Expr::Init>(&start.data)) {
                    // A nested brace list is a row. Only the outer `[_]T{...}` sets `array_literal`.
                    const bool row = !made->is_tuple && !made->array_literal && made->fields.empty() &&
                                     !made->elements.empty();
                    if (!made->array_literal && !row) {
                        return false;
                    }
                    for (const auto &item : made->elements) {
                        if (item == nullptr || std::holds_alternative<Expr::Nullptr>(item->data)) {
                            return false;
                        }
                        if (const auto *inner = std::get_if<Expr::Init>(&item->data);
                            inner != nullptr &&
                            (inner->array_literal ||
                             (!inner->is_tuple && inner->fields.empty() && !inner->elements.empty()))) {
                            if (!self(self, *item, depth + 1)) {
                                return false;
                            }
                            continue;
                        }
                        if (!argument_is_comptime(*item) && !fold_switch_case(*item) && !comptime_constant(*item)) {
                            return false;
                        }
                    }
                    return true;
                }
                const auto *named = std::get_if<Expr::Name>(&start.data);
                if (named == nullptr) {
                    return false;
                }
                const Symbol *symbol = lookup(named->text);
                if (symbol == nullptr || !symbol->is_constant) {
                    return false;
                }
                if (!symbol->is_auto && symbol->type.kind == TypeKind::Array && symbol->type.element != nullptr &&
                    !is_slice(symbol->type) && symbol->type.length >= 0) {
                    return global_var(named->text) != nullptr;
                }
                const Expr *next = symbol->const_init;
                if (next == nullptr) {
                    if (const VarDecl *var = global_var(named->text); var != nullptr && var->is_comptime) {
                        next = var->init.get();
                    }
                }
                if (next == nullptr || next == &start) {
                    return false;
                }
                return self(self, *next, depth + 1);
            };
            for (const auto &element : init->elements) {
                // `null` folds to 0. It is not this value. A bool literal is, and it stays `bool`.
                if (element == nullptr || std::holds_alternative<Expr::Nullptr>(element->data)) {
                    return false;
                }
                // A nested tuple is this value only when every element is.
                if (const auto *inner = std::get_if<Expr::Init>(&element->data);
                    inner != nullptr && inner->is_tuple) {
                    if (!comptime_constant(*element)) {
                        return false;
                    }
                    continue;
                }
                if (!argument_is_comptime(*element) && !file_scope_const_int(*element) && !file_scope_const_bool(*element) &&
                    file_scope_const_float(*element) == nullptr && file_scope_const_struct(*element) == nullptr &&
                    file_scope_const_enum(*element) == nullptr && file_scope_const_array(*element) == nullptr &&
                    file_scope_const_alias(*element) == nullptr &&
                    !fold_switch_case(*element) && !comptime_constant(*element) && !address_element(*element) &&
                    !array_element(array_element, *element, 0)) {
                    return false;
                }
            }
            return true;
        }
        // `Point{.x = k}` when `k` is a file-scope integer `const`. A local `const` stays out.
        // `Point{0...}` stores 0. `comptime const c = a + b` does not use this walk.
        if (!init->array_literal) {
            for (const Expr::FieldInit &field : init->fields) {
                if (field.value == nullptr) {
                    return false;
                }
                if (constant_operand(*field.value) || file_scope_const_int(*field.value) ||
                    file_scope_const_enum(*field.value) != nullptr || comptime_constant(*field.value)) {
                    continue;
                }
                return false;
            }
            return !init->fields.empty() || init->zero_rest;
        }
        return constant_operand(expr);
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data);
        star != nullptr && !star->postfix && star->op == TokenKind::Star && star->expr != nullptr) {
        std::unordered_set<std::string> pointed;
        const Expr *target = comptime_pointee(*star->expr, pointed);
        if (target == nullptr) {
            return false;
        }
        // A local `comptime const` is `is_auto`. `*p` is still that float or struct.
        if (const auto *named = std::get_if<Expr::Name>(&target->data)) {
            const Symbol *symbol = lookup(named->text);
            if (symbol != nullptr && symbol->is_constant) {
                const Type &type = symbol->type;
                if (type.kind == TypeKind::Float || type.kind == TypeKind::Struct || type.alias_id != 0 ||
                    fn_pointer(type)) {
                    return true;
                }
            }
        }
        return comptime_constant(*target);
    }
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        const Symbol *symbol = lookup(named->text);
        if (symbol == nullptr) {
            return false;
        }
        // `name := Point{.x = 1}` is that literal until a store. The name is not `is_constant`.
        if (symbol->held_comptime && symbol->const_init != nullptr) {
            if (const auto *again = std::get_if<Expr::Name>(&symbol->const_init->data);
                again == nullptr || again->text != named->text) {
                return comptime_constant(*symbol->const_init);
            }
        }
        // `add`. A local function is not a file-scope constant.
        if (symbol->is_function && !symbol->is_local_function && symbol->function_id == 0) {
            return true;
        }
        if (symbol->is_constant && symbol->type.kind == TypeKind::Tuple) {
            const Expr *init = symbol->const_init;
            if (init == nullptr) {
                if (const VarDecl *var = global_var(named->text); var != nullptr && var->is_comptime) {
                    init = var->init.get();
                }
            }
            return init != nullptr && comptime_constant(*init);
        }
        // A local `comptime const` is `is_auto`. A runtime `const` is not `is_constant`.
        if (symbol->is_constant) {
            const Type &type = symbol->type;
            if (type.kind == TypeKind::Float || type.kind == TypeKind::Struct || type.alias_id != 0 ||
                fn_pointer(type)) {
                return true;
            }
            return false;
        }
        // `const origin = Point{.x = 2, .y = 0}` at file scope. A local const stays out.
        // `const other = origin` and `Point{0...}` are that value.
        if (!symbol->is_auto && symbol->type.kind == TypeKind::Struct) {
            if (const Expr *init = file_scope_const_struct(expr)) {
                return comptime_constant(*init);
            }
        }
        // `const other = Integer(4)` at file scope. A local const stays out.
        if (const Expr *init = file_scope_const_alias(expr)) {
            return comptime_constant(*init);
        }
        // `const e = Order.equal` at file scope. A local const stays out.
        if (file_scope_const_enum(expr) != nullptr) {
            return true;
        }
        // `const flag = true` and `const k = 0.5` at file scope. A local const stays out.
        if (file_scope_const_bool(expr).has_value() || file_scope_const_float(expr) != nullptr) {
            return true;
        }
        // `const k = 3000000000u32`, `3000000000`, and `4u64`. A local const stays out.
        if (file_scope_const_wide(expr)) {
            return true;
        }
        return false;
    }
    if (std::holds_alternative<Expr::Float>(expr.data)) {
        return true;
    }
    if (const auto *cast = std::get_if<Expr::Cast>(&expr.data)) {
        // `Integer(4)` and `Integer(k)` when `k` is a file-scope integer `const`.
        // A cast of a `var` is not a constant. `a + b` is not this cast.
        return cast->expr != nullptr && cast->indirections == 0 &&
               (constant_operand(*cast->expr) || file_scope_const_int(*cast->expr).has_value());
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data)) {
        if (unary->postfix || unary->expr == nullptr ||
            (unary->op != TokenKind::Plus && unary->op != TokenKind::Minus)) {
            return false;
        }
        return comptime_constant(*unary->expr);
    }
    if (std::holds_alternative<Expr::Binary>(expr.data)) {
        const std::optional<FoldedNumber> number = fold_number(expr);
        return number.has_value() && number->is_float;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        // An enumerator is a separate comptime value. `.len` is an integer.
        if (selector->base != nullptr) {
            const int id = enum_designator(*selector->base);
            if (id >= 0 && find_enum_member(id, selector->field) != nullptr) {
                return false;
            }
        }
        if (fold_int(expr)) {
            return false;
        }
        return constant_operand(expr);
    }
    return false;
}

std::unique_ptr<Expr> Checker::comptime_substitute(const Expr &expr) {
    std::unordered_set<std::string> seen;
    const Expr *cursor = &expr;
    while (cursor != nullptr) {
        if (const auto *call = std::get_if<Expr::Call>(&cursor->data)) {
            if (call->comptime_struct == nullptr) {
                break;
            }
            cursor = call->comptime_struct.get();
            continue;
        }
        if (const Expr *element = comptime_tuple_element(*cursor)) {
            cursor = element;
            continue;
        }
        if (const auto *star = std::get_if<Expr::Unary>(&cursor->data);
            star != nullptr && !star->postfix && star->op == TokenKind::Star && star->expr != nullptr) {
            std::unordered_set<std::string> pointed;
            const Expr *target = comptime_pointee(*star->expr, pointed);
            if (target == nullptr) {
                break;
            }
            cursor = target;
            continue;
        }
        if (const auto *init = std::get_if<Expr::Init>(&cursor->data); init != nullptr && init->is_tuple) {
            std::unique_ptr<Expr> copy = clone_expr(*cursor);
            auto *made = std::get_if<Expr::Init>(&copy->data);
            const auto rewrite = [&](const auto &self, Expr::Init &tuple) -> void {
                for (auto &element : tuple.elements) {
                    if (element == nullptr) {
                        continue;
                    }
                    // A local `comptime const` is its initializer. The specialized copy has no local.
                    if (const auto *named = std::get_if<Expr::Name>(&element->data)) {
                        const Symbol *symbol = lookup(named->text);
                        const Expr *init = symbol != nullptr && symbol->is_constant ? symbol->const_init : nullptr;
                        if (init != nullptr && init != element.get()) {
                            element = clone_expr(*init);
                            if (auto *inner = std::get_if<Expr::Init>(&element->data);
                                inner != nullptr && inner->is_tuple) {
                                self(self, *inner);
                            }
                            continue;
                        }
                    }
                    if (const std::optional<int> value = file_scope_const_int(*element)) {
                        auto literal = std::make_unique<Expr>();
                        Expr::Int number;
                        number.loc = expr_loc(*element);
                        number.value = static_cast<uint64_t>(*value);
                        literal->data = std::move(number);
                        element = std::move(literal);
                        continue;
                    }
                    if (const std::optional<WideInt> wide = file_scope_const_wide(*element)) {
                        element = wide_int_expr(wide->value, wide->bits, wide->is_unsigned, expr_loc(*element));
                        continue;
                    }
                    if (const std::optional<bool> flag = file_scope_const_bool(*element)) {
                        auto literal = std::make_unique<Expr>();
                        Expr::Bool node;
                        node.loc = expr_loc(*element);
                        node.value = *flag;
                        literal->data = std::move(node);
                        element = std::move(literal);
                        continue;
                    }
                    if (const Expr *init = file_scope_const_float(*element)) {
                        element = clone_expr(*init);
                        continue;
                    }
                    if (const Expr *init = file_scope_const_struct(*element)) {
                        element = clone_expr(*init);
                        continue;
                    }
                    if (const Expr *init = file_scope_const_enum(*element)) {
                        element = clone_expr(*init);
                        continue;
                    }
                    if (const Expr *init = file_scope_const_array(*element)) {
                        element = clone_expr(*init);
                        continue;
                    }
                    if (const Expr *init = file_scope_const_alias(*element)) {
                        element = clone_expr(*init);
                        continue;
                    }
                    if (auto *inner = std::get_if<Expr::Init>(&element->data);
                        inner != nullptr && inner->is_tuple) {
                        self(self, *inner);
                    }
                }
            };
            if (made != nullptr) {
                rewrite(rewrite, *made);
            }
            return copy;
        }
        const auto *named = std::get_if<Expr::Name>(&cursor->data);
        if (named == nullptr || !seen.insert(named->text).second) {
            break;
        }
        const Symbol *symbol = lookup(named->text);
        if (symbol == nullptr || (!symbol->is_constant && !symbol->held_comptime)) {
            // A file-scope const struct is its initializer. A local const is not.
            if (symbol != nullptr && !symbol->is_auto && symbol->type.kind == TypeKind::Struct) {
                if (const VarDecl *var = global_var(named->text);
                    var != nullptr && var->is_const_decl && var->init != nullptr) {
                    cursor = var->init.get();
                    continue;
                }
            }
            // A file-scope const alias is its initializer. A local const is not.
            if (const Expr *init = file_scope_const_alias(*cursor)) {
                cursor = init;
                continue;
            }
            // `const e = Order.equal` at file scope. A local const is not.
            if (const Expr *init = file_scope_const_enum(*cursor)) {
                cursor = init;
                continue;
            }
            // `const flag = true` at file scope. A local const is not.
            if (const std::optional<bool> flag = file_scope_const_bool(*cursor)) {
                auto literal = std::make_unique<Expr>();
                Expr::Bool node;
                node.loc = expr_loc(*cursor);
                node.value = *flag;
                literal->data = std::move(node);
                return literal;
            }
            // `const k = 0.5` and `const k = 0.5f` at file scope. A local const is not.
            if (const Expr *init = file_scope_const_float(*cursor)) {
                cursor = init;
                continue;
            }
            // `const k = 3000000000u32` at file scope. A local const is not.
            if (const std::optional<WideInt> wide = file_scope_const_wide(*cursor)) {
                return wide_int_expr(wide->value, wide->bits, wide->is_unsigned, expr_loc(*cursor));
            }
            break;
        }
        const Expr *init = symbol->const_init;
        if (init == nullptr) {
            const VarDecl *var = global_var(named->text);
            if (var != nullptr) {
                init = var->init.get();
            }
        }
        if (init == nullptr) {
            break;
        }
        cursor = init;
    }
    return clone_expr(*cursor);
}

std::optional<std::string> Checker::comptime_spec_piece(const Expr &expr) {
    std::unordered_set<std::string> seen;
    const auto piece = [&](const auto &self, const Expr &value) -> std::optional<std::string> {
        if (const auto *call = std::get_if<Expr::Call>(&value.data)) {
            if (call->comptime_struct == nullptr) {
                return std::nullopt;
            }
            return self(self, *call->comptime_struct);
        }
        // `pair[0]` spells as that element. `(add, 1)` is `t_fnadd_1`, and `pair[0]` alone is `fnadd`.
        if (const Expr *element = comptime_tuple_element(value)) {
            return self(self, *element);
        }
        // `fold_int` spells `true` as `1`. That collides with the integer, so a bool literal is `b1` or `b0`.
        if (const auto *flag = std::get_if<Expr::Bool>(&value.data)) {
            return flag->value ? std::string("b1") : std::string("b0");
        }
        const bool enumerator = [&]() {
            const auto *selector = std::get_if<Expr::Selector>(&value.data);
            if (selector == nullptr || selector->base == nullptr) {
                return false;
            }
            const int id = enum_designator(*selector->base);
            return id >= 0 && find_enum_member(id, selector->field) != nullptr;
        }();
        if (enumerator) {
            if (const std::optional<int> number = fold_switch_case(value)) {
                return spec_int_piece(*number);
            }
            return std::nullopt;
        }
        if (const std::optional<int> number = fold_int(value)) {
            return spec_int_piece(*number);
        }
        // `3000000000u32` does not fit in the compile-time `i32`. The suffix stays in the spelling.
        if (const std::optional<WideInt> wide = fold_wide_int(value)) {
            std::string piece = wide->is_unsigned ? "u" : "i";
            piece += std::to_string(wide->bits);
            piece += spec_hex(wide->value, wide->bits >= 4 ? wide->bits / 4 : 1);
            return piece;
        }
        if (const std::optional<FoldedNumber> number = fold_number(value); number && number->is_float) {
            if (number->wide) {
                uint64_t bits = 0;
                const double wide = number->value;
                static_assert(sizeof(wide) == sizeof(bits));
                std::memcpy(&bits, &wide, sizeof(bits));
                return "d" + spec_hex(bits, 16);
            }
            const float narrow = static_cast<float>(number->value);
            uint32_t bits = 0;
            static_assert(sizeof(narrow) == sizeof(bits));
            std::memcpy(&bits, &narrow, sizeof(bits));
            return "w" + spec_hex(bits, 8);
        }
        {
            std::unordered_set<std::string> text_seen;
            if (const std::optional<std::string> text = constant_string(value, text_seen)) {
                return spec_text_piece(*text);
            }
        }
        if (const auto *init = std::get_if<Expr::Init>(&value.data)) {
            if (init->array_literal) {
                return std::nullopt;
            }
            if (init->is_tuple) {
                // `(1, 2)` is `t_1_2`. Each element uses the spelling of that value.
                if (init->elements.size() < 2) {
                    return std::nullopt;
                }
                std::string text = "t";
                for (const auto &element : init->elements) {
                    if (element == nullptr) {
                        return std::nullopt;
                    }
                    const std::optional<std::string> part = self(self, *element);
                    if (!part) {
                        return std::nullopt;
                    }
                    text.push_back('_');
                    text += *part;
                }
                return text;
            }
            if (!init->elements.empty()) {
                return std::nullopt;
            }
            std::string text = "p";
            for (const Expr::FieldInit &field : init->fields) {
                if (field.value == nullptr) {
                    return std::nullopt;
                }
                const std::optional<std::string> part = self(self, *field.value);
                if (!part) {
                    return std::nullopt;
                }
                text.push_back('_');
                text += *part;
            }
            return text;
        }
        if (const auto *named = std::get_if<Expr::Name>(&value.data)) {
            const Symbol *symbol = lookup(named->text);
            if (symbol != nullptr && symbol->is_function && !symbol->is_local_function) {
                return "fn" + named->text;
            }
            if (symbol != nullptr && symbol->is_constant && seen.insert(named->text).second) {
                if (symbol->const_init != nullptr) {
                    const std::optional<std::string> inner = self(self, *symbol->const_init);
                    seen.erase(named->text);
                    if (inner) {
                        return inner;
                    }
                }
                const VarDecl *var = global_var(named->text);
                if (var != nullptr && var->init != nullptr) {
                    const std::optional<std::string> inner = self(self, *var->init);
                    seen.erase(named->text);
                    return inner;
                }
                seen.erase(named->text);
            }
            return std::nullopt;
        }
        if (const auto *cast = std::get_if<Expr::Cast>(&value.data)) {
            if (cast->expr == nullptr) {
                return std::nullopt;
            }
            return self(self, *cast->expr);
        }
        if (const auto *unary = std::get_if<Expr::Unary>(&value.data)) {
            if (unary->postfix || unary->expr == nullptr ||
                (unary->op != TokenKind::Plus && unary->op != TokenKind::Minus)) {
                return std::nullopt;
            }
            return self(self, *unary->expr);
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&value.data)) {
            if (selector->base == nullptr || enumerator) {
                return std::nullopt;
            }
            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                const Symbol *symbol = lookup(base->text);
                if (symbol != nullptr && symbol->is_package) {
                    return "fn" + base->text + "_" + selector->field;
                }
                if (symbol == nullptr || symbol->is_type) {
                    return "fn" + base->text + "_" + selector->field;
                }
            }
        }
        return std::nullopt;
    };
    return piece(piece, expr);
}

bool Checker::comptime_kept_argument(const Expr &arg) {
    if (unmarked_comptime_arg(arg)) {
        return false;
    }
    if (argument_is_comptime(arg) || folded_local_int(arg)) {
        return true;
    }
    // `Order.acquire` and `pkg.Order.acquire`. The call still passes the value.
    if (const auto *selector = std::get_if<Expr::Selector>(&arg.data)) {
        if (selector->base != nullptr) {
            const int id = enum_designator(*selector->base);
            if (id >= 0 && find_enum_member(id, selector->field) != nullptr) {
                return true;
            }
        }
    }
    // `load_at(p, order)` when `order` is a comptime parameter of the caller.
    if (const auto *named = std::get_if<Expr::Name>(&arg.data)) {
        const Symbol *symbol = lookup(named->text);
        if (symbol != nullptr && symbol->comptime_param) {
            return true;
        }
    }
    return false;
}

bool Checker::is_builtin_fmt(const Expr::Call &call) {
    if (call.callee == nullptr || call.args.size() != 2) {
        return false;
    }
    const Symbol *symbol = nullptr;
    if (const auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
        symbol = lookup(named->text);
    } else if (const auto *selector = std::get_if<Expr::Selector>(&call.callee->data)) {
        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
        if (package == nullptr || !package->is_package || selector->field != "fmt") {
            return false;
        }
        symbol = find_export(package->package_file, selector->field);
    }
    if (symbol == nullptr || !symbol->is_template || !is_std_builtin_file(symbol->package_file)) {
        return false;
    }
    const FunctionDecl *decl = symbol->fn_template;
    if (decl == nullptr && !symbol->fn_templates.empty()) {
        decl = symbol->fn_templates.front();
    }
    return decl != nullptr && decl->name == "fmt" && decl->type_params.size() == 1 && decl->params.size() == 2 &&
           decl->params[0].is_comptime && decl->params[1].is_comptime;
}

std::optional<std::string> Checker::format_call(const Expr::Call &call) {
    if (!is_builtin_fmt(call) || call.args[0] == nullptr || call.args[1] == nullptr) {
        return std::nullopt;
    }
    std::unordered_set<std::string> seen;
    const std::optional<std::string> fmt = constant_string(*call.args[0], seen);
    if (!fmt) {
        return std::nullopt;
    }
    return format_placeholders(
        *fmt, *call.args[1], [&](const Expr &element) { return fold_int(element); },
        [&](const Expr &element) {
            std::unordered_set<std::string> inner;
            return constant_string(element, inner);
        });
}

bool Checker::comptime_arguments(const FunctionDecl &fn, const Expr::Call &call, std::size_t skip) {
    if (skip > fn.params.size()) {
        return false;
    }
    for (std::size_t i = skip; i < fn.params.size(); ++i) {
        if (!fn.params[i].is_comptime) {
            continue;
        }
        const std::size_t arg = i - skip;
        if (arg >= call.args.size() || call.args[arg] == nullptr || !comptime_kept_argument(*call.args[arg])) {
            return false;
        }
    }
    return true;
}

int Checker::comptime_overload_score(const FunctionDecl *fn, const Expr::Call &call, std::size_t skip) {
    if (fn == nullptr) {
        return 0;
    }
    if (!comptime_arguments(*fn, call, skip)) {
        return -1;
    }
    int score = 0;
    const std::size_t start = skip < fn->params.size() ? skip : fn->params.size();
    for (std::size_t i = start; i < fn->params.size(); ++i) {
        if (fn->params[i].is_comptime) {
            ++score;
        }
    }
    return score;
}

const FunctionDecl *Checker::mem_compare_callee(const Expr::Call &call) {
    if (call.callee == nullptr || call.args.size() != 2 || call.type_args.size() > 1 || call.args[0] == nullptr ||
        call.args[1] == nullptr) {
        return nullptr;
    }
    if (call.type_args.size() == 1 &&
        (!plain_named_type_arg(call.type_args.front()) || call.type_args.front().type_name != "u8")) {
        return nullptr;
    }
    std::unordered_set<std::string> left_seen;
    std::unordered_set<std::string> right_seen;
    if (!constant_string(*call.args[0], left_seen) || !constant_string(*call.args[1], right_seen)) {
        return nullptr;
    }
    const Symbol *symbol = nullptr;
    if (const auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
        symbol = lookup(named->text);
    } else if (const auto *selector = std::get_if<Expr::Selector>(&call.callee->data)) {
        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
        if (package == nullptr || !package->is_package) {
            return nullptr;
        }
        symbol = find_export(package->package_file, selector->field);
    }
    if (symbol == nullptr || !symbol->is_template || !is_std_mem_package(find_node(symbol->package_file))) {
        return nullptr;
    }
    const FunctionDecl *comptime_fn = nullptr;
    const FunctionDecl *runtime_fn = nullptr;
    bool clash = false;
    const auto consider = [&](const FunctionDecl *fn) {
        if (fn == nullptr || fn->params.size() != 2) {
            return;
        }
        const MemCompare kind = mem_compare_kind(*fn);
        if (!mem_compare_slice(kind)) {
            return;
        }
        const FunctionDecl **slot = mem_compare_comptime(kind) ? &comptime_fn : &runtime_fn;
        if (*slot != nullptr) {
            clash = true;
        }
        *slot = fn;
    };
    if (symbol->fn_templates.empty()) {
        consider(symbol->fn_template);
    } else {
        for (const FunctionDecl *fn : symbol->fn_templates) {
            consider(fn);
        }
    }
    if (clash) {
        return nullptr;
    }
    // The comptime overload is the folded one. The runtime overload folds only when it is the only slice.
    if (comptime_fn != nullptr) {
        return comptime_fn;
    }
    return runtime_fn;
}

std::optional<int> Checker::fold_mem_compare(const Expr::Call &call) {
    const FunctionDecl *fn = mem_compare_callee(call);
    if (fn == nullptr || !mem_compare_slice(mem_compare_kind(*fn)) || call.args.size() != 2 || call.type_args.size() > 1 ||
        call.args[0] == nullptr || call.args[1] == nullptr) {
        return std::nullopt;
    }
    if (call.type_args.size() == 1 &&
        (!plain_named_type_arg(call.type_args.front()) || call.type_args.front().type_name != "u8")) {
        return std::nullopt;
    }
    std::unordered_set<std::string> left_seen;
    std::unordered_set<std::string> right_seen;
    const std::optional<std::string> left = constant_string(*call.args[0], left_seen);
    const std::optional<std::string> right = constant_string(*call.args[1], right_seen);
    if (!left || !right) {
        return std::nullopt;
    }
    return mem_compare_bytes(*left, *right, mem_compare_kind(*fn));
}

const FunctionDecl *Checker::baked_callee(const Expr::Call &call) {
    if (call.callee == nullptr || !call.type_args.empty() || call.args.empty()) {
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&call.callee->data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->is_function || symbol->is_template) {
        return nullptr;
    }
    const FunctionDecl *found = nullptr;
    for (const FnList &list : symbol->lists) {
        const FunctionDecl *decl = list.decl;
        if (decl == nullptr || decl->is_comptime || decl->is_macro || !decl->type_params.empty() ||
            !decl->method_struct.empty() || decl->params.size() != call.args.size() || decl->params.empty()) {
            continue;
        }
        bool all = true;
        for (const Param &param : decl->params) {
            if (!param.is_comptime) {
                all = false;
            }
        }
        if (!all) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = decl;
    }
    return found;
}

void Checker::require_comptime_params(const FunctionDecl &fn, const Expr::Call &call, std::size_t skip) {
    // `extern` / `extern(abi="tlc")` keeps the parameter in the symbol. A runtime value calls it.
    if (fn.is_comptime || fn.is_macro || !fn.type_params.empty() || (fn.is_extern && !fn.extern_c) ||
        skip > fn.params.size()) {
        return;
    }
    const std::size_t params = fn.params.size() - skip;
    const std::size_t count = params < call.args.size() ? params : call.args.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!fn.params[skip + i].is_comptime || call.args[i] == nullptr) {
            continue;
        }
        // A free function specializes through `argument_is_comptime`. A method also accepts
        // a forwarded comptime name. An enumerator then builds a copy that drops the parameter.
        // A struct, a float, an alias, a function, or a tuple is `comptime_constant`. An enumerator is not.
        const bool accepted = !unmarked_comptime_arg(*call.args[i]) &&
                              (comptime_method_base(*call.args[i]) ||
                               (fn.method_struct.empty()
                                    ? argument_is_comptime(*call.args[i]) || folded_local_int(*call.args[i]) ||
                                          comptime_constant(*call.args[i])
                                    : comptime_kept_argument(*call.args[i]) || comptime_constant(*call.args[i])));
        if (!accepted) {
            error(expr_loc(*call.args[i]), "type mismatch");
        }
    }
}

std::optional<int> Checker::fold_comptime_call(const Expr::Call &call) {
    if (call.comptime_failed || call.comptime_float || call.comptime_struct != nullptr) {
        return std::nullopt;
    }
    if (call.comptime_ran) {
        return call.comptime_value;
    }
    const FunctionDecl *fn = comptime_callee(call);
    if (fn == nullptr) {
        fn = baked_callee(call);
    }
    if (fn == nullptr || fn->is_macro || fn->variadic || !fn->type_params.empty() || !fn->method_struct.empty() ||
        !call.type_args.empty() || call.args.size() != fn->params.size() || comptime_folding_.contains(fn)) {
        return std::nullopt;
    }
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn->params.size());
    names.reserve(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime) ||
            unmarked_comptime_arg(*call.args[i])) {
            return std::nullopt;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *local, loc);
        }
        if (literal == nullptr) {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || fold_switch_case(*call.args[i])) {
                // The name of a file-scope const becomes its initializer. An enumerator stays the selector.
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return std::nullopt;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    // The call chain is the callers of this comptime func, newest first.
    ComptimeEval chain(*this, *fn, call);
    // `Buf[n]` still has a stand-in length on this body. The copy below substitutes `n`.
    if (!fn->pending_generic) {
        if (const Expr *value = single_i32_return(*fn)) {
            if (!comptime_folding_.insert(fn).second) {
                return std::nullopt;
            }
            struct Leave {
                std::unordered_set<const FunctionDecl *> &set;
                const FunctionDecl *fn;
                ~Leave() { set.erase(fn); }
            } leave{comptime_folding_, fn};
            std::unique_ptr<Expr> rewritten = clone_expr(*value);
            replace_names(rewritten, names, exprs);
            const std::size_t before_fold = diags_.size();
            if (const std::optional<int> folded = fold_int(*rewritten)) {
                call.comptime_ran = true;
                call.comptime_value = *folded;
                return folded;
            }
            if (diags_.size() != before_fold || take_comptime_fault(fn->name)) {
                return std::nullopt;
            }
        }
    }
    // `return n > 0` is 0 or 1. A body with statements is compiled below.
    if (!fn->pending_generic && plain_bool_result(*fn)) {
        if (const Expr *value = single_return_expr(*fn)) {
            if (!comptime_folding_.insert(fn).second) {
                return std::nullopt;
            }
            struct Leave {
                std::unordered_set<const FunctionDecl *> &set;
                const FunctionDecl *fn;
                ~Leave() { set.erase(fn); }
            } leave{comptime_folding_, fn};
            std::unique_ptr<Expr> rewritten = clone_expr(*value);
            replace_names(rewritten, names, exprs);
            const std::size_t before_fold = diags_.size();
            if (const std::optional<int> folded = fold_int(*rewritten)) {
                call.comptime_ran = true;
                call.comptime_value = *folded == 0 ? 0 : 1;
                return call.comptime_value;
            }
            if (diags_.size() != before_fold || take_comptime_fault(fn->name)) {
                return std::nullopt;
            }
        }
    }
    // A body with statements is compiled and called. The integer or bool is the constant.
    // An ordinary function whose parameters are all comptime is compiled the same way when a generic
    // value argument, or a local `[n]i32`, was still that parameter at the definition.
    const bool run_body = fn->is_comptime || fn->pending_generic;
    if (!run_body || (!plain_i32_result(*fn) && !plain_bool_result(*fn)) || graph_ == nullptr ||
        !comptime_folding_.insert(fn).second) {
        return std::nullopt;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept;
        ~Leave() { set.erase(kept); }
    } leave{comptime_folding_, fn};
    FunctionDecl wrapper = clone_function(*fn);
    if (wrapper.body == nullptr) {
        return std::nullopt;
    }
    replace_stmt_names(*wrapper.body, names, exprs);
    // Parameters are literals. `[n]T` becomes that length. A negative length is rejected.
    bool lengths = false;
    if (!bind_array_lengths(*wrapper.body, names, exprs, lengths)) {
        call.comptime_failed = true;
        return std::nullopt;
    }
    bool generic = false;
    if (!bind_generic_values(*wrapper.body, names, exprs, generic)) {
        call.comptime_failed = true;
        return std::nullopt;
    }
    // `comptime if`, `comptime switch`, and `comptime for` fold on this copy.
    // `repeat[n]` instantiates once `n` is the integer. A rejected call stays rejected.
    (void)generic;
    if (!recheck_comptime_copy(wrapper, lengths || wrapper.pending_generic)) {
        error(call.loc, recheck_fault_.empty() ? "not supported yet" : recheck_fault_);
        recheck_fault_.clear();
        call.comptime_failed = true;
        return std::nullopt;
    }
    wrapper.params.clear();
    wrapper.captures.clear();
    wrapper.captured_here.clear();
    wrapper.is_comptime = false;
    wrapper.is_macro = false;
    wrapper.is_extern = false;
    wrapper.extern_c = false;
    wrapper.exported = false;
    wrapper.variadic = false;
    wrapper.link_name.clear();
    uint32_t file = file_id_;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == fn) {
                file = node.file_id;
            }
        }
    }
    if (wrapper.body != nullptr && explain_comptime(*wrapper.body, fn->name)) {
        return std::nullopt;
    }
    SourceLoc fault;
    std::vector<std::pair<uint32_t, const FunctionDecl *>> specs;
    comptime_pending_specs(wrapper.body.get(), specs);
    std::vector<std::pair<uint32_t, const StructDecl *>> structs;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *st = std::get_if<StructDecl>(&item.decl.data)) {
            if (!st->name.empty() && st->type_params.empty()) {
                structs.push_back({item.file_id, st});
            }
        }
    }
    std::vector<std::pair<uint32_t, const TypedefDecl *>> aliases;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *alias = std::get_if<TypedefDecl>(&item.decl.data)) {
            if (!alias->name.empty() && alias->type_params.empty()) {
                aliases.push_back({item.file_id, alias});
            }
        }
    }
    const std::optional<int32_t> ran = eval_comptime_i32(*graph_, wrapper, file, &fault, specs, structs, aliases);
    if (fault.valid()) {
        const std::string message = comptime_assert_message(*wrapper.body, fault);
        report_comptime_fault(fault, message.empty() ? std::string("index out of range") : message, fn->name);
        return std::nullopt;
    }
    if (!ran) {
        return std::nullopt;
    }
    call.comptime_ran = true;
    call.comptime_value = *ran;
    return call.comptime_value;
}

bool Checker::fold_comptime_wide(const Expr::Call &call) {
    if (call.comptime_failed) {
        return false;
    }
    if (call.comptime_struct != nullptr) {
        return true;
    }
    const FunctionDecl *fn = comptime_callee(call);
    const IntWidth *spec = fn == nullptr ? nullptr : wide_int_result(*fn);
    if (fn == nullptr || spec == nullptr || fn->is_macro || fn->variadic || !fn->type_params.empty() ||
        !fn->method_struct.empty() || !call.type_args.empty() || call.args.size() != fn->params.size() ||
        comptime_folding_.contains(fn) || graph_ == nullptr) {
        return false;
    }
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn->params.size());
    names.reserve(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime) ||
            unmarked_comptime_arg(*call.args[i])) {
            return false;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *local, loc);
        }
        if (literal == nullptr) {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || fold_switch_case(*call.args[i])) {
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return false;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    const auto keep = [&](uint64_t value, int width, bool is_unsigned) {
        // A signed `i8` or `i16` keeps the sign in the stored integer. `200u8` stays 200.
        if (width > 0 && width < 64) {
            const uint64_t mask = (uint64_t{1} << static_cast<unsigned>(width)) - 1u;
            value &= mask;
            if (!is_unsigned && (value & (uint64_t{1} << static_cast<unsigned>(width - 1))) != 0) {
                value |= ~mask;
            }
        }
        call.comptime_struct = wide_int_expr(value, width, is_unsigned, call.loc);
        return true;
    };
    ComptimeEval chain(*this, *fn, call);
    if (!fn->pending_generic) {
        if (const Expr *value = single_return_expr(*fn)) {
            if (!comptime_folding_.insert(fn).second) {
                return false;
            }
            struct Leave {
                std::unordered_set<const FunctionDecl *> &set;
                const FunctionDecl *kept;
                ~Leave() { set.erase(kept); }
            } leave{comptime_folding_, fn};
            std::unique_ptr<Expr> rewritten = clone_expr(*value);
            replace_names(rewritten, names, exprs);
            const std::size_t before_fold = diags_.size();
            if (const std::optional<WideInt> wide = fold_wide_int(*rewritten)) {
                if (wide->bits == spec->bits && wide->is_unsigned == spec->is_unsigned) {
                    return keep(wide->value, spec->bits, spec->is_unsigned);
                }
            }
            if (diags_.size() != before_fold || take_comptime_fault(fn->name)) {
                return false;
            }
        }
    }
    if (!fn->is_comptime || !comptime_folding_.insert(fn).second) {
        return false;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept;
        ~Leave() { set.erase(kept); }
    } leave{comptime_folding_, fn};
    FunctionDecl wrapper = clone_function(*fn);
    if (wrapper.body == nullptr) {
        return false;
    }
    replace_stmt_names(*wrapper.body, names, exprs);
    bool lengths = false;
    if (!bind_array_lengths(*wrapper.body, names, exprs, lengths)) {
        call.comptime_failed = true;
        return false;
    }
    bool generic = false;
    if (!bind_generic_values(*wrapper.body, names, exprs, generic)) {
        call.comptime_failed = true;
        return false;
    }
    (void)generic;
    if (!recheck_comptime_copy(wrapper, lengths || wrapper.pending_generic)) {
        error(call.loc, recheck_fault_.empty() ? "not supported yet" : recheck_fault_);
        recheck_fault_.clear();
        call.comptime_failed = true;
        return false;
    }
    wrapper.params.clear();
    wrapper.captures.clear();
    wrapper.captured_here.clear();
    wrapper.is_comptime = false;
    wrapper.is_macro = false;
    wrapper.is_extern = false;
    wrapper.extern_c = false;
    wrapper.exported = false;
    wrapper.variadic = false;
    wrapper.link_name.clear();
    uint32_t file = file_id_;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == fn) {
                file = node.file_id;
            }
        }
    }
    if (wrapper.body != nullptr && explain_comptime(*wrapper.body, fn->name)) {
        return false;
    }
    SourceLoc fault;
    std::vector<std::pair<uint32_t, const FunctionDecl *>> specs;
    comptime_pending_specs(wrapper.body.get(), specs);
    std::vector<std::pair<uint32_t, const StructDecl *>> structs;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *st = std::get_if<StructDecl>(&item.decl.data)) {
            if (!st->name.empty() && st->type_params.empty()) {
                structs.push_back({item.file_id, st});
            }
        }
    }
    std::vector<std::pair<uint32_t, const TypedefDecl *>> aliases;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *alias = std::get_if<TypedefDecl>(&item.decl.data)) {
            if (!alias->name.empty() && alias->type_params.empty()) {
                aliases.push_back({item.file_id, alias});
            }
        }
    }
    // `i8` and `i16` are read as `i32` after the body has been checked at their own width.
    if (spec->bits < 32) {
        wrapper.return_type = "i32";
        if (!wrapper.results.empty()) {
            wrapper.results.front().type_name = "i32";
        }
    }
    const auto failed = [&]() {
        if (!fault.valid()) {
            return false;
        }
        const std::string message = comptime_assert_message(*wrapper.body, fault);
        report_comptime_fault(fault, message.empty() ? std::string("index out of range") : message, fn->name);
        return true;
    };
    uint64_t bits = 0;
    if (spec->bits >= 64) {
        const std::optional<int64_t> ran = eval_comptime_i64(*graph_, wrapper, file, &fault, specs, structs, aliases);
        if (failed() || !ran) {
            return false;
        }
        bits = static_cast<uint64_t>(*ran);
    } else if (spec->bits == 32) {
        const std::optional<uint32_t> ran = eval_comptime_u32(*graph_, wrapper, file, &fault, specs, structs, aliases);
        if (failed() || !ran) {
            return false;
        }
        bits = *ran;
    } else {
        const std::optional<int32_t> ran = eval_comptime_i32(*graph_, wrapper, file, &fault, specs, structs, aliases);
        if (failed() || !ran) {
            return false;
        }
        const uint64_t mask = (uint64_t{1} << static_cast<unsigned>(spec->bits)) - 1u;
        bits = static_cast<uint32_t>(*ran) & mask;
    }
    return keep(bits, spec->bits, spec->is_unsigned);
}

bool Checker::fold_comptime_f64(const Expr::Call &call) {
    if (call.comptime_failed) {
        return false;
    }
    if (call.comptime_float) {
        return true;
    }
    const FunctionDecl *fn = comptime_callee(call);
    const bool wide = fn != nullptr && plain_f64_result(*fn);
    const bool narrow = fn != nullptr && plain_f32_result(*fn);
    if (fn == nullptr || (!wide && !narrow) || fn->is_macro || fn->variadic || !fn->type_params.empty() ||
        !fn->method_struct.empty() || !call.type_args.empty() || call.args.size() != fn->params.size() ||
        comptime_folding_.contains(fn) || graph_ == nullptr) {
        return false;
    }
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn->params.size());
    names.reserve(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime) ||
            unmarked_comptime_arg(*call.args[i])) {
            return false;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *local, loc);
        }
        if (literal == nullptr) {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || fold_switch_case(*call.args[i])) {
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return false;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    ComptimeEval chain(*this, *fn, call);
    // `return 0.5` folds. A local in that `return` is compiled below.
    if (!fn->pending_generic) {
        if (const Expr *value = single_return_expr(*fn)) {
            if (!comptime_folding_.insert(fn).second) {
                return false;
            }
            struct Leave {
                std::unordered_set<const FunctionDecl *> &set;
                const FunctionDecl *kept;
                ~Leave() { set.erase(kept); }
            } leave{comptime_folding_, fn};
            std::unique_ptr<Expr> rewritten = clone_expr(*value);
            replace_names(rewritten, names, exprs);
            const std::size_t before_fold = diags_.size();
            if (const std::optional<FoldedNumber> folded = fold_number(*rewritten)) {
                if (folded->is_float) {
                    call.comptime_float = true;
                    call.comptime_float_wide = wide;
                    call.comptime_float_value = round_float(folded->value, wide);
                    return true;
                }
            }
            if (diags_.size() != before_fold || take_comptime_fault(fn->name)) {
                return false;
            }
        }
    }
    if (!fn->is_comptime || !comptime_folding_.insert(fn).second) {
        return false;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept;
        ~Leave() { set.erase(kept); }
    } leave{comptime_folding_, fn};
    FunctionDecl wrapper = clone_function(*fn);
    if (wrapper.body == nullptr) {
        return false;
    }
    replace_stmt_names(*wrapper.body, names, exprs);
    bool lengths = false;
    if (!bind_array_lengths(*wrapper.body, names, exprs, lengths)) {
        call.comptime_failed = true;
        return false;
    }
    bool generic = false;
    if (!bind_generic_values(*wrapper.body, names, exprs, generic)) {
        call.comptime_failed = true;
        return false;
    }
    (void)generic;
    if (!recheck_comptime_copy(wrapper, lengths || wrapper.pending_generic)) {
        error(call.loc, recheck_fault_.empty() ? "not supported yet" : recheck_fault_);
        recheck_fault_.clear();
        call.comptime_failed = true;
        return false;
    }
    wrapper.params.clear();
    wrapper.captures.clear();
    wrapper.captured_here.clear();
    wrapper.is_comptime = false;
    wrapper.is_macro = false;
    wrapper.is_extern = false;
    wrapper.extern_c = false;
    wrapper.exported = false;
    wrapper.variadic = false;
    wrapper.link_name.clear();
    uint32_t file = file_id_;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == fn) {
                file = node.file_id;
            }
        }
    }
    if (wrapper.body != nullptr && explain_comptime(*wrapper.body, fn->name)) {
        return false;
    }
    SourceLoc fault;
    std::vector<std::pair<uint32_t, const FunctionDecl *>> specs;
    comptime_pending_specs(wrapper.body.get(), specs);
    std::vector<std::pair<uint32_t, const StructDecl *>> structs;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *st = std::get_if<StructDecl>(&item.decl.data)) {
            if (!st->name.empty() && st->type_params.empty()) {
                structs.push_back({item.file_id, st});
            }
        }
    }
    std::vector<std::pair<uint32_t, const TypedefDecl *>> aliases;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *alias = std::get_if<TypedefDecl>(&item.decl.data)) {
            if (!alias->name.empty() && alias->type_params.empty()) {
                aliases.push_back({item.file_id, alias});
            }
        }
    }
    std::optional<double> ran;
    if (wide) {
        ran = eval_comptime_f64(*graph_, wrapper, file, &fault, specs, structs, aliases);
    } else if (const std::optional<float> narrow_ran =
                   eval_comptime_f32(*graph_, wrapper, file, &fault, specs, structs, aliases)) {
        ran = static_cast<double>(*narrow_ran);
    }
    if (fault.valid()) {
        const std::string message = comptime_assert_message(*wrapper.body, fault);
        report_comptime_fault(fault, message.empty() ? std::string("index out of range") : message, fn->name);
        return false;
    }
    if (!ran) {
        return false;
    }
    call.comptime_float = true;
    call.comptime_float_wide = wide;
    call.comptime_float_value = round_float(*ran, wide);
    return true;
}

bool Checker::fold_comptime_enum(const Expr::Call &call) {
    if (call.comptime_failed) {
        return false;
    }
    if (call.comptime_struct != nullptr) {
        return true;
    }
    const FunctionDecl *fn = comptime_callee(call);
    if (fn == nullptr || !plain_enum_result(*fn) || fn->is_macro || fn->variadic || !fn->type_params.empty() ||
        !fn->method_struct.empty() || !call.type_args.empty() || call.args.size() != fn->params.size() ||
        comptime_folding_.contains(fn) || graph_ == nullptr) {
        return false;
    }
    const Type result = function_result(*fn);
    if (result.kind != TypeKind::Enum || result.enum_id < 0 ||
        result.enum_id >= static_cast<int>(enum_types_.size())) {
        return false;
    }
    const EnumInfo &info = enum_types_[static_cast<std::size_t>(result.enum_id)];
    const Type &underlying = info.underlying;
    // `i8` through `u64`, plus `bool`. `i8` is a `Char` rank. A member value that does not fit stays out.
    const bool integer_under =
        underlying.alias_id == 0 &&
        ((underlying.kind == TypeKind::Int &&
          (underlying.rank == 2 || underlying.rank == 3 || underlying.rank == 5)) ||
         (underlying.kind == TypeKind::Char && underlying.rank == 1 && !underlying.plain_char));
    const bool bool_under = underlying.kind == TypeKind::Bool && underlying.alias_id == 0;
    if (!integer_under && !bool_under) {
        return false;
    }
    const auto member_at = [&](int value) -> std::string {
        for (const EnumInfo::Member &member : info.members) {
            if (member.value == value) {
                return member.name;
            }
        }
        return {};
    };
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn->params.size());
    names.reserve(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime) ||
            unmarked_comptime_arg(*call.args[i])) {
            return false;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *local, loc);
        }
        if (literal == nullptr) {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || fold_switch_case(*call.args[i])) {
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return false;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    ComptimeEval chain(*this, *fn, call);
    const auto keep = [&](int value, SourceLoc loc) -> bool {
        const std::string member = member_at(value);
        if (member.empty()) {
            return false;
        }
        call.comptime_struct = enum_selector(info.name, member, loc);
        return call.comptime_struct != nullptr;
    };
    if (!fn->pending_generic) {
        if (const Expr *value = single_return_expr(*fn)) {
            if (!comptime_folding_.insert(fn).second) {
                return false;
            }
            struct Leave {
                std::unordered_set<const FunctionDecl *> &set;
                const FunctionDecl *kept;
                ~Leave() { set.erase(kept); }
            } leave{comptime_folding_, fn};
            std::unique_ptr<Expr> rewritten = clone_expr(*value);
            replace_names(rewritten, names, exprs);
            const std::size_t before_fold = diags_.size();
            if (const std::optional<int> folded = fold_switch_case(*rewritten)) {
                return keep(*folded, expr_loc(*value));
            }
            if (diags_.size() != before_fold || take_comptime_fault(fn->name)) {
                return false;
            }
        }
    }
    if (!fn->is_comptime || !comptime_folding_.insert(fn).second) {
        return false;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept;
        ~Leave() { set.erase(kept); }
    } leave{comptime_folding_, fn};
    FunctionDecl wrapper = clone_function(*fn);
    if (wrapper.body == nullptr) {
        return false;
    }
    replace_stmt_names(*wrapper.body, names, exprs);
    bool lengths = false;
    if (!bind_array_lengths(*wrapper.body, names, exprs, lengths)) {
        call.comptime_failed = true;
        return false;
    }
    bool generic = false;
    if (!bind_generic_values(*wrapper.body, names, exprs, generic)) {
        call.comptime_failed = true;
        return false;
    }
    (void)generic;
    if (!recheck_comptime_copy(wrapper, lengths || wrapper.pending_generic)) {
        error(call.loc, recheck_fault_.empty() ? "not supported yet" : recheck_fault_);
        recheck_fault_.clear();
        call.comptime_failed = true;
        return false;
    }
    wrapper.params.clear();
    wrapper.captures.clear();
    wrapper.captured_here.clear();
    wrapper.is_comptime = false;
    wrapper.is_macro = false;
    wrapper.is_extern = false;
    wrapper.extern_c = false;
    wrapper.exported = false;
    wrapper.variadic = false;
    wrapper.link_name.clear();
    // The evaluator reads an `i32`. The enum's own type is restored on the call.
    wrapper.return_type = "i32";
    wrapper.is_enum = false;
    if (!wrapper.results.empty()) {
        wrapper.results.front().type_name = "i32";
        wrapper.results.front().is_enum = false;
    }
    uint32_t file = file_id_;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == fn) {
                file = node.file_id;
            }
        }
    }
    if (wrapper.body != nullptr && explain_comptime(*wrapper.body, fn->name)) {
        return false;
    }
    SourceLoc fault;
    std::vector<std::pair<uint32_t, const FunctionDecl *>> specs;
    comptime_pending_specs(wrapper.body.get(), specs);
    std::vector<std::pair<uint32_t, const StructDecl *>> structs;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *st = std::get_if<StructDecl>(&item.decl.data)) {
            if (!st->name.empty() && st->type_params.empty()) {
                structs.push_back({item.file_id, st});
            }
        }
    }
    std::vector<std::pair<uint32_t, const TypedefDecl *>> aliases;
    for (const PendingAppend &item : pending_appends_) {
        if (const auto *alias = std::get_if<TypedefDecl>(&item.decl.data)) {
            if (!alias->name.empty() && alias->type_params.empty()) {
                aliases.push_back({item.file_id, alias});
            }
        }
    }
    const std::optional<int32_t> ran = eval_comptime_i32(*graph_, wrapper, file, &fault, specs, structs, aliases);
    if (fault.valid()) {
        const std::string message = comptime_assert_message(*wrapper.body, fault);
        report_comptime_fault(fault, message.empty() ? std::string("index out of range") : message, fn->name);
        return false;
    }
    if (!ran) {
        return false;
    }
    return keep(*ran, call.loc);
}

bool Checker::fold_comptime_string(const Expr::Call &call) {
    if (call.comptime_failed) {
        return false;
    }
    if (call.comptime_text) {
        return true;
    }
    const FunctionDecl *fn = comptime_callee(call);
    if (fn == nullptr) {
        fn = baked_callee(call);
    }
    if (fn == nullptr || fn->is_macro || fn->variadic || !fn->type_params.empty() || !fn->method_struct.empty() ||
        !call.type_args.empty() || call.args.size() != fn->params.size() || comptime_folding_.contains(fn)) {
        return false;
    }
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn->params.size());
    names.reserve(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime) ||
            unmarked_comptime_arg(*call.args[i])) {
            return false;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *local, loc);
        }
        if (literal == nullptr) {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || fold_switch_case(*call.args[i])) {
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return false;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    if (!comptime_folding_.insert(fn).second) {
        return false;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept;
        ~Leave() { set.erase(kept); }
    } leave{comptime_folding_, fn};
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    ComptimeEval chain(*this, *fn, call);
    if (const Expr *value = single_return_expr(*fn)) {
        std::unique_ptr<Expr> rewritten = clone_expr(*value);
        replace_names(rewritten, names, exprs);
        std::unordered_set<std::string> seen;
        const std::size_t before_fold = diags_.size();
        if (const std::optional<std::string> text = constant_string(*rewritten, seen)) {
            call.comptime_text = true;
            call.comptime_bytes = *text;
            return true;
        }
        if (diags_.size() != before_fold || take_comptime_fault(fn->name)) {
            return false;
        }
    }
    if (!fn->is_comptime || graph_ == nullptr) {
        return false;
    }
    FunctionDecl wrapper = clone_function(*fn);
    if (wrapper.body == nullptr) {
        return false;
    }
    replace_stmt_names(*wrapper.body, names, exprs);
    // Parameters are literals. `[n]T` becomes that length. A negative length is rejected.
    bool lengths = false;
    if (!bind_array_lengths(*wrapper.body, names, exprs, lengths)) {
        call.comptime_failed = true;
        return false;
    }
    bool generic = false;
    if (!bind_generic_values(*wrapper.body, names, exprs, generic)) {
        call.comptime_failed = true;
        return false;
    }
    // `comptime if`, `comptime switch`, and `comptime for` fold on this copy.
    // `repeat[n]` instantiates once `n` is the integer. A rejected call stays rejected.
    (void)generic;
    if (!recheck_comptime_copy(wrapper, lengths || wrapper.pending_generic)) {
        return false;
    }
    wrapper.params.clear();
    wrapper.captures.clear();
    wrapper.captured_here.clear();
    wrapper.is_comptime = false;
    wrapper.is_macro = false;
    wrapper.is_extern = false;
    wrapper.extern_c = false;
    wrapper.exported = false;
    wrapper.variadic = false;
    wrapper.link_name.clear();
    uint32_t file = file_id_;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == fn) {
                file = node.file_id;
            }
        }
    }
    if (wrapper.body != nullptr && explain_comptime(*wrapper.body, fn->name)) {
        return false;
    }
    SourceLoc fault;
    std::vector<std::pair<uint32_t, const FunctionDecl *>> specs;
    comptime_pending_specs(wrapper.body.get(), specs);
    const std::optional<std::string> ran = eval_comptime_string(*graph_, wrapper, file, &fault, specs);
    if (fault.valid()) {
        const std::string message = comptime_assert_message(*wrapper.body, fault);
        report_comptime_fault(fault, message.empty() ? std::string("index out of range") : message, fn->name);
        return false;
    }
    if (!ran) {
        return false;
    }
    call.comptime_text = true;
    call.comptime_bytes = *ran;
    return true;
}

const Expr *Checker::comptime_struct_field(const Expr &object, const std::string &field) {
    // A `comptime const` struct or array folds. A `var` and a runtime `const` stay loaded.
    return const_struct_field(object, field, [&](const std::string &name) -> const Expr * {
        const Symbol *symbol = lookup(name);
        const Expr *init = symbol != nullptr && symbol->is_constant ? symbol->const_init : nullptr;
        const auto file_init = [&](const std::string &text, const Symbol *found) -> const Expr * {
            // A local `const` hides the file-scope name. `is_auto` is that local.
            if (found != nullptr && found->is_auto) {
                return nullptr;
            }
            if (const VarDecl *var = global_var(text);
                var != nullptr && var->init != nullptr && (var->is_comptime || var->is_const_decl)) {
                return var->init.get();
            }
            return nullptr;
        };
        if (init == nullptr) {
            init = file_init(name, symbol);
        }
        std::unordered_set<std::string> seen;
        seen.insert(name);
        while (init != nullptr) {
            const auto *next = std::get_if<Expr::Name>(&init->data);
            if (next == nullptr) {
                break;
            }
            if (!seen.insert(next->text).second) {
                return nullptr;
            }
            const Symbol *followed = lookup(next->text);
            const Expr *step = followed != nullptr && followed->is_constant ? followed->const_init : nullptr;
            if (step == nullptr) {
                step = file_init(next->text, followed);
            }
            if (step == nullptr || step == init) {
                return nullptr;
            }
            init = step;
        }
        if (init == nullptr) {
            return nullptr;
        }
        if (const auto *call = std::get_if<Expr::Call>(&init->data)) {
            if (call->comptime_struct == nullptr) {
                return nullptr;
            }
            return call->comptime_struct.get();
        }
        const auto *literal = std::get_if<Expr::Init>(&init->data);
        if (literal == nullptr || literal->is_tuple) {
            return nullptr;
        }
        if (literal->array_literal || !literal->fields.empty() || literal->struct_id >= 0 || !literal->type_name.empty()) {
            return init;
        }
        return nullptr;
    }, [&](const Expr::Init &init, const std::string &name) -> const Expr * {
        int id = init.struct_id;
        if (id < 0 && !init.type_name.empty()) {
            id = init.type_package.empty() ? find_tag(file_id_, init.type_name)
                                           : imported_tag(file_id_, init.type_package, init.type_name);
        }
        if (id < 0 || id >= static_cast<int>(structs_.size())) {
            return nullptr;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(id)];
        if (info.is_union) {
            return nullptr;
        }
        const StructInfo::Field *slot = nullptr;
        for (const StructInfo::Field &item : info.fields) {
            if (item.name == name) {
                slot = &item;
            }
        }
        if (slot == nullptr) {
            return nullptr;
        }
        // `{0...}` stores 0. It does not use the value written on the field.
        if (init.zero_rest) {
            const Type &type = slot->type;
            if (type.alias_id == 0 &&
                (type.kind == TypeKind::Int || type.kind == TypeKind::Bool || type.kind == TypeKind::Char)) {
                return fold_zero();
            }
            return nullptr;
        }
        if (slot->has_init && slot->init != nullptr) {
            return slot->init;
        }
        return nullptr;
    });
}

const Expr *Checker::fold_zero() {
    if (fold_zero_ == nullptr) {
        fold_zero_ = std::make_unique<Expr>();
        Expr::Int number;
        number.value = 0;
        fold_zero_->data = std::move(number);
    }
    return fold_zero_.get();
}

Checker::KeptStruct Checker::fold_comptime_struct(const Expr::Call &call) {
    KeptStruct kept;
    if (call.comptime_failed) {
        return kept;
    }
    if (call.comptime_struct != nullptr) {
        kept.status = KeptStruct::Status::Value;
        kept.value = clone_expr(*call.comptime_struct);
        return kept;
    }
    const FunctionDecl *fn = comptime_callee(call);
    if (fn == nullptr || fn->is_macro || fn->variadic || !fn->type_params.empty() || !fn->method_struct.empty() ||
        !call.type_args.empty() || call.args.size() != fn->params.size() || graph_ == nullptr ||
        comptime_folding_.contains(fn)) {
        return kept;
    }
    std::vector<std::unique_ptr<Expr>> owned;
    std::vector<std::string> names;
    owned.reserve(fn->params.size());
    names.reserve(fn->params.size());
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime) ||
            unmarked_comptime_arg(*call.args[i])) {
            return kept;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_scalar_arg(fn->params[i], *local, loc);
        }
        if (literal == nullptr) {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            } else if (comptime_constant(*call.args[i]) || fold_switch_case(*call.args[i])) {
                literal = comptime_substitute(*call.args[i]);
            }
        }
        if (literal == nullptr) {
            return kept;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    ComptimeEval chain(*this, *fn, call);
    if (!fn->is_comptime || !comptime_folding_.insert(fn).second) {
        return kept;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *kept_fn;
        ~Leave() { set.erase(kept_fn); }
    } leave{comptime_folding_, fn};
    FunctionDecl wrapper = clone_function(*fn);
    if (wrapper.body == nullptr) {
        return kept;
    }
    replace_stmt_names(*wrapper.body, names, exprs);
    bool lengths = false;
    if (!bind_array_lengths(*wrapper.body, names, exprs, lengths)) {
        call.comptime_failed = true;
        return kept;
    }
    bool generic = false;
    if (!bind_generic_values(*wrapper.body, names, exprs, generic)) {
        call.comptime_failed = true;
        return kept;
    }
    (void)generic;
    if (!recheck_comptime_copy(wrapper, lengths || wrapper.pending_generic)) {
        return kept;
    }
    wrapper.params.clear();
    wrapper.captures.clear();
    wrapper.captured_here.clear();
    wrapper.is_macro = false;
    wrapper.is_extern = false;
    wrapper.extern_c = false;
    wrapper.exported = false;
    wrapper.variadic = false;
    wrapper.link_name.clear();
    // A file-scope const is bound before the body is checked, so `Point{.x = 10}` has no
    // `struct_id` yet. This walk stamps the copy. `is_comptime` stays set so a nested call
    // remains ordinary code.
    if (!recheck_comptime_copy(wrapper, true)) {
        return kept;
    }
    wrapper.is_comptime = false;
    uint32_t file = file_id_;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == fn) {
                file = node.file_id;
            }
        }
    }
    if (wrapper.body != nullptr && explain_comptime(*wrapper.body, fn->name)) {
        return kept;
    }
    SourceLoc fault;
    std::vector<std::pair<uint32_t, const FunctionDecl *>> specs;
    comptime_pending_specs(wrapper.body.get(), specs);
    ComptimeStructValue ran = eval_comptime_struct(*graph_, wrapper, file, &fault, specs);
    if (fault.valid()) {
        const std::string message = comptime_assert_message(*wrapper.body, fault);
        report_comptime_fault(fault, message.empty() ? std::string("index out of range") : message, fn->name);
        return kept;
    }
    if (ran.status == ComptimeStructValue::Status::NotConstant) {
        kept.status = KeptStruct::Status::NotConstant;
        return kept;
    }
    if (ran.status != ComptimeStructValue::Status::Value || ran.value == nullptr) {
        return kept;
    }
    kept.status = KeptStruct::Status::Value;
    kept.value = std::move(ran.value);
    return kept;
}

void Checker::stamp_kept_struct(Expr &expr, const Type &type) {
    if (type.kind == TypeKind::Array && !is_slice(type) && type.element != nullptr) {
        auto *init = std::get_if<Expr::Init>(&expr.data);
        if (init == nullptr || init->is_tuple) {
            return;
        }
        init->array_literal = true;
        for (const auto &element : init->elements) {
            if (element != nullptr) {
                stamp_kept_struct(*element, *type.element);
            }
        }
        return;
    }
    if (type.kind != TypeKind::Struct || type.struct_id < 0 ||
        type.struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    const StructInfo &info = structs_[static_cast<std::size_t>(type.struct_id)];
    if (info.is_union) {
        return;
    }
    auto *init = std::get_if<Expr::Init>(&expr.data);
    if (init == nullptr || init->is_tuple) {
        return;
    }
    init->struct_id = type.struct_id;
    init->is_struct = true;
    if (!info.name.empty()) {
        init->type_name = info.name;
    }
    for (Expr::FieldInit &field : init->fields) {
        if (field.value == nullptr || field.field.empty()) {
            continue;
        }
        Type field_type;
        if (!lookup_field(type, field.field, field_type)) {
            continue;
        }
        stamp_kept_struct(*field.value, field_type);
    }
}

const Expr *Checker::promotable_init(const Expr &expr) {
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    // `promote_init` without `promotable` is only a tuple index. It is not copied here.
    if (symbol == nullptr || symbol->is_constant || !symbol->promotable || symbol->promote_init == nullptr) {
        return nullptr;
    }
    return symbol->promote_init;
}

bool Checker::comptime_address(const Expr &expr) {
    const auto *unary = std::get_if<Expr::Unary>(&expr.data);
    if (unary == nullptr || unary->postfix || unary->op != TokenKind::Amp || unary->expr == nullptr) {
        return false;
    }
    const auto *named = std::get_if<Expr::Name>(&unary->expr->data);
    if (named == nullptr) {
        return false;
    }
    const Symbol *symbol = lookup(named->text);
    return symbol != nullptr && symbol->is_constant;
}

const Expr *Checker::comptime_pointee(const Expr &pointer, std::unordered_set<std::string> &seen) {
    // `*pair[0]` is the pointee of that element. `pair[0]` is `&addr`.
    if (const Expr *element = comptime_tuple_element(pointer)) {
        return comptime_pointee(*element, seen);
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&pointer.data)) {
        if (unary->postfix || unary->expr == nullptr) {
            return nullptr;
        }
        // `&addr` points at that comptime variable.
        if (unary->op == TokenKind::Amp) {
            const auto *named = std::get_if<Expr::Name>(&unary->expr->data);
            if (named == nullptr) {
                return nullptr;
            }
            const Symbol *symbol = lookup(named->text);
            if (symbol == nullptr || !symbol->is_constant) {
                return nullptr;
            }
            return unary->expr.get();
        }
        // `**pp` points at whatever `*pp` points at.
        if (unary->op == TokenKind::Star) {
            const Expr *mid = comptime_pointee(*unary->expr, seen);
            if (mid == nullptr) {
                return nullptr;
            }
            return comptime_pointee(*mid, seen);
        }
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&pointer.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    const Expr *init = symbol != nullptr && symbol->is_constant ? symbol->const_init : nullptr;
    if (init == nullptr) {
        if (const VarDecl *var = global_var(named->text); var != nullptr && var->is_comptime) {
            init = var->init.get();
        }
    }
    const Expr *target = init == nullptr ? nullptr : comptime_pointee(*init, seen);
    seen.erase(named->text);
    return target;
}

bool Checker::strict_comptime_expr(const Expr &expr) {
    // Every type is known while compiling. `typeof` is that value.
    if (std::holds_alternative<Expr::Typeof>(expr.data)) {
        return true;
    }
    // A comptime method already checked its receiver. The result is that value.
    if (const auto *call = std::get_if<Expr::Call>(&expr.data); call != nullptr && call->comptime_call) {
        return true;
    }
    if (fold_int(expr) || fold_switch_case(expr)) {
        return true;
    }
    // `3000000000u32` fits in `u32`. The compile-time `i32` does not hold those bits.
    if (const auto *number = std::get_if<Expr::Int>(&expr.data);
        number != nullptr && number->value > 2147483647ULL) {
        return true;
    }
    std::unordered_set<std::string> seen;
    if (constant_string(expr, seen)) {
        return true;
    }
    return comptime_constant(expr) || comptime_address(expr);
}

bool Checker::mentions_var(const Expr &expr) {
    // A `var` is not promoted into a `comptime const`.
    const auto walk = [&](const auto &self, const Expr &node, int depth) -> bool {
        if (depth > 32) {
            return false;
        }
        return std::visit(
            [&](const auto &item) -> bool {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Expr::Name>) {
                    const Symbol *symbol = lookup(item.text);
                    return symbol != nullptr && symbol->is_var;
                } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                    return item.base != nullptr && self(self, *item.base, depth + 1);
                } else if constexpr (std::is_same_v<T, Expr::Unary> || std::is_same_v<T, Expr::Sizeof> ||
                                     std::is_same_v<T, Expr::Typeof> || std::is_same_v<T, Expr::Cast>) {
                    return item.expr != nullptr && self(self, *item.expr, depth + 1);
                } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                    return (item.lhs != nullptr && self(self, *item.lhs, depth + 1)) ||
                           (item.rhs != nullptr && self(self, *item.rhs, depth + 1)) ||
                           (item.mid != nullptr && self(self, *item.mid, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                    return (item.lhs != nullptr && self(self, *item.lhs, depth + 1)) ||
                           (item.rhs != nullptr && self(self, *item.rhs, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Call>) {
                    if (item.callee != nullptr && self(self, *item.callee, depth + 1)) {
                        return true;
                    }
                    for (const auto &arg : item.args) {
                        if (arg != nullptr && self(self, *arg, depth + 1)) {
                            return true;
                        }
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, Expr::Index>) {
                    return (item.base != nullptr && self(self, *item.base, depth + 1)) ||
                           (item.index != nullptr && self(self, *item.index, depth + 1)) ||
                           (item.end != nullptr && self(self, *item.end, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Init>) {
                    for (const Expr::FieldInit &field : item.fields) {
                        if (field.value != nullptr && self(self, *field.value, depth + 1)) {
                            return true;
                        }
                    }
                    for (const auto &element : item.elements) {
                        if (element != nullptr && self(self, *element, depth + 1)) {
                            return true;
                        }
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, Expr::New>) {
                    return (item.allocator != nullptr && self(self, *item.allocator, depth + 1)) ||
                           (item.count != nullptr && self(self, *item.count, depth + 1)) ||
                           (item.init != nullptr && self(self, *item.init, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                    return (item.allocator != nullptr && self(self, *item.allocator, depth + 1)) ||
                           (item.expr != nullptr && self(self, *item.expr, depth + 1));
                } else {
                    return false;
                }
            },
            node.data);
    };
    return walk(walk, expr, 0);
}

bool Checker::unmarked_comptime_arg(const Expr &expr) {
    // A `var` or a `const` is not a comptime argument. `comptime const` and a `comptime` parameter are.
    const auto walk = [&](const auto &self, const Expr &node, int depth) -> bool {
        if (depth > 32) {
            return true;
        }
        return std::visit(
            [&](const auto &item) -> bool {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Expr::Name>) {
                    // `const k = 40`, `const k = 3000000000u32`, `const origin = Point{.x = 40, .y = 2}`,
                    // `const k = 0.5`, and `const flag = true` at file scope. An enumerator and
                    // `const n = Integer(4)` are the same argument. A local `const` and a `var` stay out.
                    if (file_scope_const_int(node) || file_scope_const_wide(node) ||
                        file_scope_const_array(node) != nullptr || file_scope_const_struct(node) != nullptr ||
                        file_scope_const_enum(node) != nullptr || file_scope_const_alias(node) != nullptr ||
                        file_scope_const_bool(node).has_value() || file_scope_const_float(node) != nullptr) {
                        return false;
                    }
                    const Symbol *symbol = lookup(item.text);
                    if (symbol == nullptr || symbol->is_package || symbol->is_type || symbol->is_function ||
                        symbol->comptime_param || symbol->is_constant || symbol->held_comptime) {
                        return false;
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                    if (item.base == nullptr) {
                        return false;
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&item.base->data)) {
                        const Symbol *package = lookup(named->text);
                        if (package != nullptr && package->is_package) {
                            const Symbol *member = find_export(package->package_file, item.field);
                            if (member == nullptr || member->is_function || member->is_type || member->is_package ||
                                member->is_constant || member->comptime_param) {
                                return false;
                            }
                            return true;
                        }
                    }
                    return self(self, *item.base, depth + 1);
                } else if constexpr (std::is_same_v<T, Expr::Unary> || std::is_same_v<T, Expr::Sizeof> ||
                                     std::is_same_v<T, Expr::Typeof> || std::is_same_v<T, Expr::Cast>) {
                    return item.expr != nullptr && self(self, *item.expr, depth + 1);
                } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                    return (item.lhs != nullptr && self(self, *item.lhs, depth + 1)) ||
                           (item.rhs != nullptr && self(self, *item.rhs, depth + 1)) ||
                           (item.mid != nullptr && self(self, *item.mid, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                    return (item.lhs != nullptr && self(self, *item.lhs, depth + 1)) ||
                           (item.rhs != nullptr && self(self, *item.rhs, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Call>) {
                    if (item.callee != nullptr && self(self, *item.callee, depth + 1)) {
                        return true;
                    }
                    for (const auto &arg : item.args) {
                        if (arg != nullptr && self(self, *arg, depth + 1)) {
                            return true;
                        }
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, Expr::Index>) {
                    return (item.base != nullptr && self(self, *item.base, depth + 1)) ||
                           (item.index != nullptr && self(self, *item.index, depth + 1)) ||
                           (item.end != nullptr && self(self, *item.end, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Init>) {
                    for (const Expr::FieldInit &field : item.fields) {
                        if (field.value != nullptr && self(self, *field.value, depth + 1)) {
                            return true;
                        }
                    }
                    for (const auto &element : item.elements) {
                        if (element != nullptr && self(self, *element, depth + 1)) {
                            return true;
                        }
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, Expr::New>) {
                    return (item.allocator != nullptr && self(self, *item.allocator, depth + 1)) ||
                           (item.count != nullptr && self(self, *item.count, depth + 1)) ||
                           (item.init != nullptr && self(self, *item.init, depth + 1));
                } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                    return (item.allocator != nullptr && self(self, *item.allocator, depth + 1)) ||
                           (item.expr != nullptr && self(self, *item.expr, depth + 1));
                } else {
                    return false;
                }
            },
            node.data);
    };
    return walk(walk, expr, 0);
}

bool Checker::comptime_method_base(const Expr &expr) {
    // `typeof` is a type, and every type is known while compiling. A `comptime const` and a
    // `comptime` parameter are the variables a comptime method may use. A field or an index of
    // one stays that value. A call marked `comptime_call` is the result of such a method.
    const auto walk = [&](const auto &self, const Expr &node, int depth) -> bool {
        if (depth > 32) {
            return false;
        }
        return std::visit(
            [&](const auto &item) -> bool {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Expr::Typeof>) {
                    return true;
                } else if constexpr (std::is_same_v<T, Expr::Name>) {
                    const Symbol *symbol = lookup(item.text);
                    return symbol != nullptr && (symbol->is_constant || symbol->comptime_param || symbol->held_comptime);
                } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                    return item.base != nullptr && self(self, *item.base, depth + 1);
                } else if constexpr (std::is_same_v<T, Expr::Index>) {
                    if (item.base == nullptr || !self(self, *item.base, depth + 1)) {
                        return false;
                    }
                    const auto slot = [&](const Expr *bound) {
                        return bound == nullptr || (!unmarked_comptime_arg(*bound) &&
                                                    (fold_int(*bound) || folded_local_int(*bound) ||
                                                     argument_is_comptime(*bound)));
                    };
                    return slot(item.index.get()) && slot(item.end.get());
                } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                    return !item.postfix && item.op == TokenKind::Star && item.expr != nullptr &&
                           self(self, *item.expr, depth + 1);
                } else if constexpr (std::is_same_v<T, Expr::Call>) {
                    return item.comptime_call;
                } else {
                    return false;
                }
            },
            node.data);
    };
    return walk(walk, expr, 0);
}

void Checker::require_comptime_method_call(const Expr::Call &call, const Expr *receiver, const FunctionDecl *fn) {
    const std::size_t before = diags_.size();
    // `CompiledType.name` has a receiver and no `FunctionDecl` at this call. A static method does not.
    const bool wants_receiver = fn == nullptr || (!fn->params.empty() && fn->params.front().is_receiver);
    if (wants_receiver && (receiver == nullptr || !comptime_method_base(*receiver))) {
        error(call.loc, "not supported yet");
    }
    if (fn != nullptr && fn->is_comptime) {
        for (const auto &arg : call.args) {
            if (arg == nullptr) {
                continue;
            }
            if (unmarked_comptime_arg(*arg) ||
                (!comptime_method_base(*arg) && !argument_is_comptime(*arg) && !folded_local_int(*arg) &&
                 !comptime_constant(*arg) && !fold_switch_case(*arg))) {
                error(expr_loc(*arg), "not supported yet");
            }
        }
    }
    if (diags_.size() == before) {
        call.comptime_call = true;
    }
}

void Checker::note_promotable(Symbol &symbol, const Expr *init) {
    if (init == nullptr || symbol.is_constant) {
        return;
    }
    if (strict_comptime_expr(*init)) {
        symbol.promotable = true;
        symbol.promote_init = init;
        if (const std::optional<int> value = fold_int(*init)) {
            symbol.const_value = *value;
            symbol.folded_init = true;
        }
        return;
    }
    // `const j = i + 1` when `i` is a runtime `const`. `t[j]` chooses that field.
    // `comptime const folded = j` does not promote it.
    if (!plain_int_constant(symbol.type)) {
        return;
    }
    if (const std::optional<int> value = constant_index(*init)) {
        symbol.promote_init = init;
        symbol.const_value = *value;
        symbol.folded_init = true;
    }
}

void Checker::note_held(Symbol &symbol, const Expr &init) {
    // `var name = 10` and `const name = 10` stay runtime. Only `name := expr` calls this.
    if (!strict_comptime_expr(init) && !comptime_method_base(init)) {
        // `value := n` inside a `comptime func`. The call substitutes `n`.
        if (comptime_param_pending(&init)) {
            symbol.pending_fold = true;
        }
        return;
    }
    symbol.held_comptime = true;
    symbol.const_init = &init;
    if (plain_int_constant(symbol.type) || (symbol.type.kind == TypeKind::Bool && symbol.type.alias_id == 0)) {
        if (const std::optional<int> value = constant_index(init)) {
            symbol.folded_local = true;
            symbol.const_value = symbol.type.kind == TypeKind::Bool ? (*value != 0 ? 1 : 0) : *value;
        }
    }
}

std::optional<std::string> Checker::held_root(const Expr &expr) const {
    const Expr *cursor = &expr;
    for (int depth = 0; cursor != nullptr && depth < 32; ++depth) {
        if (const auto *named = std::get_if<Expr::Name>(&cursor->data)) {
            return named->text;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data)) {
            cursor = selector->base.get();
            continue;
        }
        if (const auto *index = std::get_if<Expr::Index>(&cursor->data)) {
            cursor = index->base.get();
            continue;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void Checker::release_held_expr(const Expr &expr) {
    const std::optional<std::string> root = held_root(expr);
    if (!root) {
        return;
    }
    Symbol *symbol = lookup(*root);
    if (symbol == nullptr) {
        return;
    }
    symbol->pending_fold = false;
    if (!symbol->held_comptime) {
        return;
    }
    symbol->held_comptime = false;
    symbol->folded_local = false;
}

bool Checker::expr_assigns(const Expr &expr, const std::string &name, bool hidden) const {
    if (hidden) {
        return false;
    }
    const auto rooted = [&](const Expr &node) {
        const std::optional<std::string> root = held_root(node);
        return root && *root == name;
    };
    return std::visit(
        [&](const auto &item) -> bool {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, Expr::Assign>) {
                if (item.lhs != nullptr && rooted(*item.lhs)) {
                    return true;
                }
                return (item.lhs != nullptr && expr_assigns(*item.lhs, name, false)) ||
                       (item.rhs != nullptr && expr_assigns(*item.rhs, name, false));
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (item.expr == nullptr) {
                    return false;
                }
                if (!item.postfix &&
                    (item.op == TokenKind::PlusPlus || item.op == TokenKind::MinusMinus || item.op == TokenKind::Amp) &&
                    rooted(*item.expr)) {
                    return true;
                }
                return expr_assigns(*item.expr, name, false);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                return (item.lhs != nullptr && expr_assigns(*item.lhs, name, false)) ||
                       (item.rhs != nullptr && expr_assigns(*item.rhs, name, false)) ||
                       (item.mid != nullptr && expr_assigns(*item.mid, name, false));
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (item.callee != nullptr && expr_assigns(*item.callee, name, false)) {
                    return true;
                }
                for (const auto &arg : item.args) {
                    if (arg != nullptr && expr_assigns(*arg, name, false)) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                return item.base != nullptr && expr_assigns(*item.base, name, false);
            } else if constexpr (std::is_same_v<T, Expr::Sizeof> || std::is_same_v<T, Expr::Typeof> ||
                                 std::is_same_v<T, Expr::Cast>) {
                return item.expr != nullptr && expr_assigns(*item.expr, name, false);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                return (item.base != nullptr && expr_assigns(*item.base, name, false)) ||
                       (item.index != nullptr && expr_assigns(*item.index, name, false)) ||
                       (item.end != nullptr && expr_assigns(*item.end, name, false));
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                for (const Expr::FieldInit &field : item.fields) {
                    if (field.value != nullptr && expr_assigns(*field.value, name, false)) {
                        return true;
                    }
                }
                for (const auto &element : item.elements) {
                    if (element != nullptr && expr_assigns(*element, name, false)) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                return (item.allocator != nullptr && expr_assigns(*item.allocator, name, false)) ||
                       (item.count != nullptr && expr_assigns(*item.count, name, false)) ||
                       (item.init != nullptr && expr_assigns(*item.init, name, false));
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                return (item.allocator != nullptr && expr_assigns(*item.allocator, name, false)) ||
                       (item.expr != nullptr && expr_assigns(*item.expr, name, false));
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (item.decl == nullptr || item.decl->body == nullptr) {
                    return false;
                }
                bool hide = false;
                for (const Param &param : item.decl->params) {
                    if (param.name == name) {
                        hide = true;
                    }
                }
                return stmt_assigns(*item.decl->body, name, hide);
            } else {
                return false;
            }
        },
        expr.data);
}

bool Checker::stmt_assigns(const Stmt &stmt, const std::string &name, bool hidden) const {
    // A parameter or an inner `name :=` hides the outer binding. Its stores are not that outer name.
    if (hidden) {
        return false;
    }
    const auto defines = [&](const Stmt &node) {
        if (const auto *bind = std::get_if<Stmt::Bind>(&node.data)) {
            if (bind->is_var || bind->is_define || bind->is_const) {
                for (const std::string &one : bind->names) {
                    if (one == name) {
                        return true;
                    }
                }
            }
        }
        if (const auto *decl = std::get_if<Stmt::Decl>(&node.data)) {
            for (const VarDecl &var : decl->vars) {
                if (var.name == name) {
                    return true;
                }
            }
        }
        return false;
    };
    return std::visit(
        [&](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                bool hide = false;
                for (const auto &child : node.statements) {
                    if (child == nullptr) {
                        continue;
                    }
                    if (stmt_assigns(*child, name, hide)) {
                        return true;
                    }
                    if (defines(*child)) {
                        hide = true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value != nullptr && expr_assigns(*node.value, name, false)) {
                    return true;
                }
                if (node.is_var || node.is_define || node.is_const) {
                    return false;
                }
                for (const std::string &one : node.names) {
                    if (one == name) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    if (var.init != nullptr && expr_assigns(*var.init, name, false)) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                return node.expr != nullptr && expr_assigns(*node.expr, name, false);
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (node.value != nullptr && expr_assigns(*node.value, name, false)) {
                    return true;
                }
                for (const auto &extra : node.extra) {
                    if (extra != nullptr && expr_assigns(*extra, name, false)) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.cond != nullptr && expr_assigns(*node.cond, name, false)) {
                    return true;
                }
                return (node.then_arm != nullptr && stmt_assigns(*node.then_arm, name, false)) ||
                       (node.else_arm != nullptr && stmt_assigns(*node.else_arm, name, false));
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                return (node.cond != nullptr && expr_assigns(*node.cond, name, false)) ||
                       (node.body != nullptr && stmt_assigns(*node.body, name, false));
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                return (node.body != nullptr && stmt_assigns(*node.body, name, false)) ||
                       (node.cond != nullptr && expr_assigns(*node.cond, name, false));
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.each) {
                    if (node.each_expr != nullptr && expr_assigns(*node.each_expr, name, false)) {
                        return true;
                    }
                    return node.body != nullptr && stmt_assigns(*node.body, name, node.each_name == name);
                }
                bool hide = false;
                if (node.init != nullptr) {
                    if (stmt_assigns(*node.init, name, false)) {
                        return true;
                    }
                    if (defines(*node.init)) {
                        hide = true;
                    }
                }
                return (node.cond != nullptr && expr_assigns(*node.cond, name, hide)) ||
                       (node.body != nullptr && stmt_assigns(*node.body, name, hide)) ||
                       (node.step != nullptr && expr_assigns(*node.step, name, hide));
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                return (node.cond != nullptr && expr_assigns(*node.cond, name, false)) ||
                       (node.body != nullptr && stmt_assigns(*node.body, name, false));
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                return (node.value != nullptr && expr_assigns(*node.value, name, false)) ||
                       (node.stmt != nullptr && stmt_assigns(*node.stmt, name, false));
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                return node.call != nullptr && expr_assigns(*node.call, name, false);
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                return node.cond != nullptr && expr_assigns(*node.cond, name, false);
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl == nullptr || node.decl->body == nullptr) {
                    return false;
                }
                bool hide = false;
                for (const Param &param : node.decl->params) {
                    if (param.name == name) {
                        hide = true;
                    }
                }
                return stmt_assigns(*node.decl->body, name, hide);
            } else {
                return false;
            }
        },
        stmt.data);
}

void Checker::release_assigned(const Stmt *stmt, const std::string &hide_name) {
    if (stmt == nullptr) {
        return;
    }
    std::vector<std::pair<std::size_t, std::string>> names;
    for (std::size_t scope = 0; scope < scopes_.size(); ++scope) {
        for (const auto &item : scopes_[scope].symbols) {
            if (item.second.held_comptime || item.second.pending_fold) {
                names.emplace_back(scope, item.first);
            }
        }
    }
    for (const auto &item : names) {
        if (!hide_name.empty() && item.second == hide_name) {
            continue;
        }
        if (!stmt_assigns(*stmt, item.second, false)) {
            continue;
        }
        if (item.first >= scopes_.size()) {
            continue;
        }
        const auto found = scopes_[item.first].symbols.find(item.second);
        if (found == scopes_[item.first].symbols.end() ||
            (!found->second.held_comptime && !found->second.pending_fold)) {
            continue;
        }
        // An inner binding of the same name is the one a store would change.
        Symbol *visible = lookup(item.second);
        if (visible != &found->second) {
            continue;
        }
        visible->held_comptime = false;
        visible->folded_local = false;
        visible->pending_fold = false;
    }
}

void Checker::release_assigned_expr(const Expr *expr) {
    if (expr == nullptr) {
        return;
    }
    std::vector<std::pair<std::size_t, std::string>> names;
    for (std::size_t scope = 0; scope < scopes_.size(); ++scope) {
        for (const auto &item : scopes_[scope].symbols) {
            if (item.second.held_comptime || item.second.pending_fold) {
                names.emplace_back(scope, item.first);
            }
        }
    }
    for (const auto &item : names) {
        if (!expr_assigns(*expr, item.second, false)) {
            continue;
        }
        if (item.first >= scopes_.size()) {
            continue;
        }
        const auto found = scopes_[item.first].symbols.find(item.second);
        if (found == scopes_[item.first].symbols.end() ||
            (!found->second.held_comptime && !found->second.pending_fold)) {
            continue;
        }
        Symbol *visible = lookup(item.second);
        if (visible != &found->second) {
            continue;
        }
        visible->held_comptime = false;
        visible->folded_local = false;
        visible->pending_fold = false;
    }
}

bool Checker::global_assigned(const TranslationUnit &unit, const std::string &name, bool file_only) const {
    const auto scan = [&](const TranslationUnit &file) {
        for (const TopDecl &decl : file.decls) {
            if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
                if (fn->body == nullptr) {
                    continue;
                }
                bool hidden = false;
                for (const Param &param : fn->params) {
                    if (param.name == name) {
                        hidden = true;
                    }
                }
                if (stmt_assigns(*fn->body, name, hidden)) {
                    return true;
                }
            } else if (const auto *test = std::get_if<TestDecl>(&decl.data)) {
                if (test->body != nullptr && stmt_assigns(*test->body, name, false)) {
                    return true;
                }
            } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                if (var->init != nullptr && expr_assigns(*var->init, name, false)) {
                    return true;
                }
            } else if (const auto *item = std::get_if<AssertDecl>(&decl.data)) {
                if (item->cond != nullptr && expr_assigns(*item->cond, name, false)) {
                    return true;
                }
            }
        }
        return false;
    };
    if (scan(unit)) {
        return true;
    }
    if (file_only || graph_ == nullptr) {
        return false;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        if (id == file_id_) {
            continue;
        }
        const PackageNode *node = find_node(id);
        if (node != nullptr && scan(node->unit)) {
            return true;
        }
    }
    return false;
}

std::vector<Checker::HeldSlot> Checker::held_snapshot() const {
    std::vector<HeldSlot> out;
    for (std::size_t scope = 0; scope < scopes_.size(); ++scope) {
        for (const auto &item : scopes_[scope].symbols) {
            if (!item.second.held_comptime && !item.second.pending_fold) {
                continue;
            }
            HeldSlot slot;
            slot.scope = scope;
            slot.name = item.first;
            slot.held = item.second.held_comptime;
            slot.pending = item.second.pending_fold;
            slot.folded = item.second.folded_local;
            slot.value = item.second.const_value;
            slot.init = item.second.const_init;
            out.push_back(std::move(slot));
        }
    }
    return out;
}

void Checker::held_restore(const std::vector<HeldSlot> &snap) {
    for (const HeldSlot &slot : snap) {
        if (slot.scope >= scopes_.size()) {
            continue;
        }
        const auto found = scopes_[slot.scope].symbols.find(slot.name);
        if (found == scopes_[slot.scope].symbols.end()) {
            continue;
        }
        found->second.held_comptime = slot.held;
        found->second.pending_fold = slot.pending;
        found->second.folded_local = slot.folded;
        found->second.const_value = slot.value;
        found->second.const_init = slot.init;
    }
}

void Checker::held_and(const std::vector<HeldSlot> &before, const std::vector<HeldSlot> &left,
                       const std::vector<HeldSlot> &right) {
    const auto kept = [](const std::vector<HeldSlot> &side, const HeldSlot &slot) {
        for (const HeldSlot &item : side) {
            if (item.scope == slot.scope && item.name == slot.name) {
                return true;
            }
        }
        return false;
    };
    for (const HeldSlot &slot : before) {
        if (slot.scope >= scopes_.size()) {
            continue;
        }
        const auto found = scopes_[slot.scope].symbols.find(slot.name);
        if (found == scopes_[slot.scope].symbols.end()) {
            continue;
        }
        if (kept(left, slot) && kept(right, slot)) {
            found->second.held_comptime = slot.held;
            found->second.pending_fold = slot.pending;
            found->second.folded_local = slot.folded;
            found->second.const_value = slot.value;
            found->second.const_init = slot.init;
        } else {
            found->second.held_comptime = false;
            found->second.pending_fold = false;
            found->second.folded_local = false;
        }
    }
}

Checker::ComptimeEval::ComptimeEval(Checker &checker, const FunctionDecl &fn, const Expr::Call &call) : self(&checker) {
    if (!fn.is_comptime) {
        return;
    }
    checker.comptime_call_nodes_.push_back(&call);
    node = true;
    std::string caller;
    if (!checker.comptime_fn_.empty()) {
        caller = checker.comptime_fn_.back();
    } else if (!checker.checking_.empty() && checker.checking_.back() != nullptr) {
        caller = checker.checking_.back()->name;
    }
    if (!caller.empty()) {
        if (checker.comptime_calls_.size() == 32) {
            checker.comptime_calls_.erase(checker.comptime_calls_.begin());
        }
        checker.comptime_calls_.push_back(ComptimeCall{call.loc, std::move(caller)});
        frame = true;
    }
    checker.comptime_fn_.push_back(fn.name);
    name = true;
    ++checker.comptime_explain_;
    explain = true;
}

Checker::ComptimeEval::~ComptimeEval() {
    if (self == nullptr) {
        return;
    }
    if (explain) {
        --self->comptime_explain_;
    }
    if (name && !self->comptime_fn_.empty()) {
        self->comptime_fn_.pop_back();
    }
    if (frame && !self->comptime_calls_.empty()) {
        self->comptime_calls_.pop_back();
    }
    if (node && !self->comptime_call_nodes_.empty()) {
        self->comptime_call_nodes_.pop_back();
    }
}

std::string Checker::trace_loc(SourceLoc loc) const {
    const SourceFile *file = diags_.source(loc.file);
    if (file == nullptr) {
        return "unknown:1:1";
    }
    const LineCol at = file->line_col(loc.offset);
    std::string path = file->path();
    std::string dir;
    if (graph_ != nullptr && !graph_->nodes.empty()) {
        const std::string &entry = graph_->nodes.front().path;
        const auto slash = entry.find_last_of("/\\");
        if (slash != std::string::npos) {
            dir = entry.substr(0, slash + 1);
        }
    }
    if (!dir.empty() && path.starts_with(dir)) {
        path = path.substr(dir.size());
    } else {
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            path = path.substr(slash + 1);
        }
    }
    return path + ":" + std::to_string(at.line) + ":" + std::to_string(at.column);
}

void Checker::note_comptime_index(SourceLoc loc) {
    if (comptime_fault_ || comptime_explain_ <= 0) {
        return;
    }
    comptime_fault_ = true;
    comptime_fault_loc_ = loc;
}

void Checker::report_comptime_fault(SourceLoc loc, const std::string &message, const std::string &here) {
    for (const Expr::Call *call : comptime_call_nodes_) {
        if (call != nullptr) {
            call->comptime_failed = true;
        }
    }
    std::string text = message;
    const auto line = [&](SourceLoc at, const std::string &name) {
        text += '\n';
        text += trace_loc(at);
        text += ": ";
        text += name;
    };
    line(loc, here);
    const std::size_t keep = comptime_calls_.size() > 31 ? 31 : comptime_calls_.size();
    const std::size_t start = comptime_calls_.size() - keep;
    for (std::size_t index = comptime_calls_.size(); index > start; --index) {
        const ComptimeCall &call = comptime_calls_[index - 1];
        line(call.loc, call.name);
    }
    error(loc, text);
}

bool Checker::take_comptime_fault(const std::string &fn_name) {
    if (!comptime_fault_) {
        return false;
    }
    const SourceLoc loc = comptime_fault_loc_;
    comptime_fault_ = false;
    report_comptime_fault(loc, "index out of range", fn_name);
    return true;
}

std::string Checker::comptime_assert_message(const Stmt &stmt, SourceLoc loc) const {
    const auto walk = [&](const auto &self, const Stmt &node) -> const Stmt::Assert * {
        return std::visit(
            [&](const auto &item) -> const Stmt::Assert * {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Stmt::Assert>) {
                    if (item.loc.file == loc.file && item.loc.offset == loc.offset) {
                        return &item;
                    }
                    return nullptr;
                } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                    for (const auto &child : item.statements) {
                        if (child == nullptr) {
                            continue;
                        }
                        if (const Stmt::Assert *found = self(self, *child)) {
                            return found;
                        }
                    }
                    return nullptr;
                } else if constexpr (std::is_same_v<T, Stmt::If>) {
                    if (item.then_arm != nullptr) {
                        if (const Stmt::Assert *found = self(self, *item.then_arm)) {
                            return found;
                        }
                    }
                    return item.else_arm == nullptr ? nullptr : self(self, *item.else_arm);
                } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                    return item.body == nullptr ? nullptr : self(self, *item.body);
                } else if constexpr (std::is_same_v<T, Stmt::For> || std::is_same_v<T, Stmt::Switch>) {
                    if constexpr (std::is_same_v<T, Stmt::For>) {
                        if (item.init != nullptr) {
                            if (const Stmt::Assert *found = self(self, *item.init)) {
                                return found;
                            }
                        }
                    }
                    if (item.body != nullptr) {
                        if (const Stmt::Assert *found = self(self, *item.body)) {
                            return found;
                        }
                    }
                    if constexpr (std::is_same_v<T, Stmt::Switch>) {
                        return nullptr;
                    }
                    return nullptr;
                } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                    return item.stmt == nullptr ? nullptr : self(self, *item.stmt);
                } else {
                    return nullptr;
                }
            },
            node.data);
    };
    const Stmt::Assert *found = walk(walk, stmt);
    if (found == nullptr) {
        return {};
    }
    return found->message.empty() ? std::string("assert") : found->message;
}

namespace {

bool deferred_comptime(const Stmt &stmt) {
    return std::visit(
        [&](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (child != nullptr && deferred_comptime(*child)) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.comptime_if && !node.folded) {
                    return true;
                }
                return (node.then_arm != nullptr && deferred_comptime(*node.then_arm)) ||
                       (node.else_arm != nullptr && deferred_comptime(*node.else_arm));
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                if (node.comptime_switch && !node.folded) {
                    return true;
                }
                return node.body != nullptr && deferred_comptime(*node.body);
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                return node.stmt != nullptr && deferred_comptime(*node.stmt);
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.comptime_for && !node.folded) {
                    return true;
                }
                if (node.body != nullptr && deferred_comptime(*node.body)) {
                    return true;
                }
                for (const auto &copy : node.unrolled) {
                    if (copy != nullptr && deferred_comptime(*copy)) {
                        return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                return node.body != nullptr && deferred_comptime(*node.body);
            } else {
                return false;
            }
        },
        stmt.data);
}

} // namespace

bool Checker::recheck_comptime_copy(FunctionDecl &wrapper, bool force) {
    if (wrapper.body == nullptr) {
        return true;
    }
    const bool deferred = deferred_comptime(*wrapper.body);
    if (!force && !deferred) {
        return true;
    }
    // The copy is checked from the call. Locals of the caller must not hide file-scope names.
    struct FileScopes {
        Checker &self;
        std::vector<Scope> saved;
        std::size_t prefix = 0;
        explicit FileScopes(Checker &checker) : self(checker), saved(std::move(checker.scopes_)) {
            while (prefix < saved.size() && saved[prefix].function_id == 0) {
                ++prefix;
            }
            self.scopes_.reserve(prefix);
            for (std::size_t i = 0; i < prefix; ++i) {
                self.scopes_.push_back(std::move(saved[i]));
            }
        }
        ~FileScopes() {
            const std::size_t have = self.scopes_.size() < prefix ? self.scopes_.size() : prefix;
            for (std::size_t i = 0; i < have; ++i) {
                saved[i] = std::move(self.scopes_[i]);
            }
            self.scopes_ = std::move(saved);
        }
    } file_scopes(*this);
    const bool saved_quiet = quiet_;
    const int saved_errors = quiet_errors_;
    const std::string saved_note = std::move(quiet_note_);
    quiet_note_.clear();
    quiet_ = true;
    check_function(wrapper);
    const std::string note = quiet_note_;
    quiet_note_ = saved_note;
    quiet_ = saved_quiet;
    loop_info_.erase(&wrapper);
    self_loop_.erase(&wrapper);
    infer_running_.erase(&wrapper);
    infer_mismatch_.erase(&wrapper);
    const bool failed = quiet_errors_ != saved_errors;
    // A negative array length is that rule. Other quiet failures stay `not supported yet`.
    recheck_fault_.clear();
    if (failed && note == kArrayLength) {
        recheck_fault_ = note;
    }
    return !failed;
}

int Checker::explain_comptime_stmt(const Stmt &stmt, const std::string &fn_name) {
    // 0 continues, 1 is a reported fault, 2 is a return.
    const auto folded_fault = [&](const Expr *expr) {
        if (expr == nullptr) {
            return 0;
        }
        const std::size_t before = diags_.size();
        fold_int(*expr);
        if (diags_.size() != before || take_comptime_fault(fn_name)) {
            return 1;
        }
        return 0;
    };
    return std::visit(
        [&](const auto &node) -> int {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                push();
                int status = 0;
                for (const auto &child : node.statements) {
                    if (child == nullptr) {
                        continue;
                    }
                    status = explain_comptime_stmt(*child, fn_name);
                    if (status != 0) {
                        break;
                    }
                }
                pop();
                return status;
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                if (node.cond == nullptr) {
                    return 0;
                }
                const std::optional<int> value = fold_condition(*node.cond);
                if (!value) {
                    if (folded_fault(node.cond.get())) {
                        return 1;
                    }
                    return 0;
                }
                if (*value == 0) {
                    report_comptime_fault(node.loc, node.message.empty() ? std::string("assert") : node.message, fn_name);
                    return 1;
                }
                return 0;
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.names.size() == 1 && node.value != nullptr &&
                    (node.is_var || node.is_define || node.is_const)) {
                    if (folded_fault(node.value.get())) {
                        return 1;
                    }
                    if (const std::optional<int> value = fold_int(*node.value)) {
                        Symbol symbol;
                        symbol.name = node.names[0];
                        symbol.type = type_int();
                        symbol.is_auto = true;
                        symbol.held_comptime = true;
                        symbol.folded_local = true;
                        symbol.const_value = *value;
                        symbol.const_init = node.value.get();
                        if (scopes_.empty()) {
                            return 0;
                        }
                        if (scopes_.back().symbols.contains(symbol.name)) {
                            shadow(std::move(symbol), node.loc);
                        } else {
                            declare(std::move(symbol), node.loc);
                        }
                    }
                }
                return 0;
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (folded_fault(node.value.get())) {
                    return 1;
                }
                for (const auto &extra : node.extra) {
                    if (folded_fault(extra.get())) {
                        return 1;
                    }
                }
                return 2;
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.cond == nullptr) {
                    return 0;
                }
                if (folded_fault(node.cond.get())) {
                    return 1;
                }
                const std::optional<int> cond = fold_condition(*node.cond);
                if (!cond) {
                    return 0;
                }
                Stmt *arm = *cond != 0 ? node.then_arm.get() : node.else_arm.get();
                if (arm == nullptr) {
                    return 0;
                }
                const int status = explain_comptime_stmt(*arm, fn_name);
                return status == 2 ? 2 : status;
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                return folded_fault(node.expr.get());
            } else {
                return 0;
            }
        },
        stmt.data);
}

bool Checker::explain_comptime(const Stmt &stmt, const std::string &fn_name) {
    push();
    const int status = explain_comptime_stmt(stmt, fn_name);
    pop();
    return status == 1;
}

} // namespace tlc
