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

// A field of a struct literal, including `literal.inner.x`. A plain name is not a literal.
// `resolve` yields a kept struct only when the name's initializer is a comptime call.
template <typename Resolve>
const Expr *const_struct_field(const Expr &object, const std::string &field, Resolve &&resolve) {
    const Expr *cursor = &object;
    if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data)) {
        if (selector->base == nullptr) {
            return nullptr;
        }
        cursor = const_struct_field(*selector->base, selector->field, resolve);
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
        const auto *tuple = std::get_if<Expr::Init>(&index->base->data);
        if (tuple == nullptr || !tuple->is_tuple || at >= tuple->elements.size() || tuple->elements[at] == nullptr) {
            return nullptr;
        }
        cursor = tuple->elements[at].get();
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
    return nullptr;
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

bool plain_i32_result(const FunctionDecl &fn) {
    if (fn.results.size() != 1 || fn.return_type != "i32" || fn.body == nullptr) {
        return false;
    }
    const ResultSlot &slot = fn.results.front();
    return slot.type_name == "i32" && slot.type_package.empty() && !slot.is_struct && !slot.is_enum &&
           slot.indirections == 0 && slot.optional == 0 && !slot.is_array && !slot.is_fn_ptr && slot.const_pointee == 0;
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
                } else if (node.type_name == "f64" || node.type_name == "double") {
                    inner->is_float = true;
                    inner->wide = true;
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
                if (symbol == nullptr || !symbol->runtime_const || symbol->promote_init == nullptr ||
                    !plain_int_constant(symbol->type)) {
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
                    if (!lhs || !rhs) {
                        return std::nullopt;
                    }
                    const bool same = *lhs == *rhs;
                    return node.op == TokenKind::EqEq ? (same ? 1 : 0) : (same ? 0 : 1);
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
            const std::optional<int> at = fold_int(*index->index);
            if (!at || *at < 0 || static_cast<std::size_t>(*at) > bytes->size()) {
                return std::nullopt;
            }
            low = static_cast<std::size_t>(*at);
        }
        std::size_t high = bytes->size();
        if (index->end != nullptr) {
            const std::optional<int> at = fold_int(*index->end);
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
    const VarDecl *var = symbol != nullptr && symbol->is_constant ? global_var(named->text) : nullptr;
    std::optional<std::string> bytes;
    // `const label = "hi"` is that slice. `const p *const u8 = "hi"` is a pointer, and a `var` can be reseated.
    if (var != nullptr && var->is_comptime && var->indirections == 0 && var->init != nullptr) {
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
        if (symbol != nullptr && symbol->type.kind == TypeKind::Pointer && symbol->type.decayed_len > 0) {
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
    if (method == nullptr || method->field != "fields" || method->base == nullptr) {
        return std::nullopt;
    }
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
                    if (symbol->folded_local) {
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
                if (node.value > 255ULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(node.text);
                if (symbol != nullptr && symbol->is_constant && plain_int_constant(symbol->type)) {
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
                // `Point{.x = 1}.x` is that field. A file-scope name is still loaded. An enumerator is not a field.
                if (node.base != nullptr) {
                    if (const Expr *field = comptime_struct_field(*node.base, node.field)) {
                        return fold_int(*field);
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
                return fold_int(*node.expr);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return std::nullopt;
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
                const std::optional<int> at = node.tuple_index >= 0 ? std::optional<int>(node.tuple_index)
                                                                    : fold_int(*node.index);
                if (!at || *at < 0) {
                    return std::nullopt;
                }
                // A tuple index selects that element. An array index counts only after one,
                // so `pair[0][1][0]` folds and a lone `row[0]` still loads.
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
                    return elements_at(elements_at, base, index, depth, false);
                };
                if (const Expr *element = indexed(indexed, *node.base, *at, 0)) {
                    return fold_int(*element);
                }
                std::unordered_set<std::string> seen;
                const std::optional<std::string> bytes = constant_string(*node.base, seen);
                if (!bytes || static_cast<std::size_t>(*at) >= bytes->size()) {
                    return std::nullopt;
                }
                return static_cast<int>(static_cast<unsigned char>((*bytes)[static_cast<std::size_t>(*at)]));
            } else {
                return std::nullopt;
            }
        },
        expr.data);
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
    if (fold_int(arg)) {
        return true;
    }
    const auto *tuple = std::get_if<Expr::Init>(&arg.data);
    if (tuple == nullptr || !tuple->is_tuple || tuple->elements.size() < 2) {
        return false;
    }
    for (const auto &element : tuple->elements) {
        if (element == nullptr || !argument_is_comptime(*element)) {
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
        if (const auto *named = std::get_if<Expr::Name>(&base.data)) {
            const Symbol *symbol = lookup(named->text);
            const Expr *init_expr = symbol != nullptr && symbol->is_constant ? symbol->const_init : nullptr;
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
                if (!argument_is_comptime(*element) && !fold_switch_case(*element) && !comptime_constant(*element) &&
                    !address_element(*element) && !array_element(array_element, *element, 0)) {
                    return false;
                }
            }
            return true;
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
        if (!symbol->is_constant || symbol->is_auto) {
            return false;
        }
        const Type &type = symbol->type;
        if (type.kind == TypeKind::Float || type.kind == TypeKind::Struct || type.alias_id != 0 || fn_pointer(type)) {
            return true;
        }
        return false;
    }
    if (std::holds_alternative<Expr::Float>(expr.data)) {
        return true;
    }
    if (const auto *cast = std::get_if<Expr::Cast>(&expr.data)) {
        // `Integer(4)` and `BinOp(add)`. A cast of a `var` is not a constant.
        return cast->expr != nullptr && cast->indirections == 0 && constant_operand(*cast->expr);
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
        const auto *named = std::get_if<Expr::Name>(&cursor->data);
        if (named == nullptr || !seen.insert(named->text).second) {
            break;
        }
        const Symbol *symbol = lookup(named->text);
        if (symbol == nullptr || !symbol->is_constant) {
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

bool Checker::comptime_arguments(const FunctionDecl &fn, const Expr::Call &call) {
    const std::size_t count = fn.params.size() < call.args.size() ? fn.params.size() : call.args.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!fn.params[i].is_comptime) {
            continue;
        }
        if (call.args[i] == nullptr || !comptime_kept_argument(*call.args[i])) {
            return false;
        }
    }
    return true;
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
    if (symbol == nullptr || !symbol->is_template || !is_std_builtin_file(symbol->package_file)) {
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
        const bool accepted = fn.method_struct.empty()
                                  ? argument_is_comptime(*call.args[i]) || folded_local_int(*call.args[i]) ||
                                        comptime_constant(*call.args[i])
                                  : comptime_kept_argument(*call.args[i]) || comptime_constant(*call.args[i]);
        if (!accepted) {
            error(expr_loc(*call.args[i]), "type mismatch");
        }
    }
}

std::optional<int> Checker::fold_comptime_call(const Expr::Call &call) {
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
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime)) {
            return std::nullopt;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_int(*number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_int(*local, loc);
        } else {
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
        if (const std::optional<int> folded = fold_int(*rewritten)) {
            return folded;
        }
    }
    // A body with statements is compiled and called. The integer is the constant.
    if (!fn->is_comptime || !plain_i32_result(*fn) || graph_ == nullptr || !comptime_folding_.insert(fn).second) {
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
    const std::optional<int32_t> ran = eval_comptime_i32(*graph_, wrapper, file);
    if (!ran) {
        return std::nullopt;
    }
    call.comptime_ran = true;
    call.comptime_value = *ran;
    return call.comptime_value;
}

const Expr *Checker::comptime_struct_field(const Expr &object, const std::string &field) {
    // A handwritten `comptime const origin = Point{...}` is still loaded. Only a call that kept a literal folds.
    return const_struct_field(object, field, [&](const std::string &name) -> const Expr * {
        const Symbol *symbol = lookup(name);
        const Expr *init = symbol != nullptr && symbol->is_constant ? symbol->const_init : nullptr;
        if (init == nullptr) {
            if (const VarDecl *var = global_var(name); var != nullptr && var->is_comptime) {
                init = var->init.get();
            }
        }
        if (init == nullptr) {
            return nullptr;
        }
        const auto *call = std::get_if<Expr::Call>(&init->data);
        if (call == nullptr || call->comptime_struct == nullptr) {
            return nullptr;
        }
        return call->comptime_struct.get();
    });
}

Checker::KeptStruct Checker::fold_comptime_struct(const Expr::Call &call) {
    KeptStruct kept;
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
        if (call.args[i] == nullptr || (!fn->is_comptime && !fn->params[i].is_comptime)) {
            return kept;
        }
        const SourceLoc loc = expr_loc(*call.args[i]);
        std::unique_ptr<Expr> literal;
        if (const std::optional<int> number = fold_int(*call.args[i])) {
            literal = folded_int(*number, loc);
        } else if (const std::optional<int> local = folded_local_int(*call.args[i])) {
            literal = folded_int(*local, loc);
        } else {
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
    // remains ordinary code. The pointer must not stay in `loop_info_` after the copy dies.
    {
        const bool saved_quiet = quiet_;
        quiet_ = true;
        check_function(wrapper);
        quiet_ = saved_quiet;
        loop_info_.erase(&wrapper);
        self_loop_.erase(&wrapper);
        infer_running_.erase(&wrapper);
        infer_mismatch_.erase(&wrapper);
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
    ComptimeStructValue ran = eval_comptime_struct(*graph_, wrapper, file);
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
    if (fold_int(expr) || fold_switch_case(expr)) {
        return true;
    }
    std::unordered_set<std::string> seen;
    if (constant_string(expr, seen)) {
        return true;
    }
    return comptime_constant(expr) || comptime_address(expr);
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
    }
}

} // namespace tlc
