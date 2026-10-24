// Compile-time folding of constants.
#include "check/check.hpp"
#include "check/check_util.hpp"

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

std::optional<int> Checker::fold_switch_case(const Expr &expr) {
    if (const std::optional<int> folded = fold_int(expr)) {
        return folded;
    }
    const auto *selector = std::get_if<Expr::Selector>(&expr.data);
    if (selector == nullptr || selector->base == nullptr) {
        return std::nullopt;
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
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return std::nullopt;
    }
    const Symbol *symbol = lookup(named->text);
    const VarDecl *var = symbol != nullptr && symbol->is_constant ? global_var(named->text) : nullptr;
    std::optional<std::string> bytes;
    // `const label = "hi"` is that slice. `const p *const u8 = "hi"` is a pointer, and a `var` can be reseated.
    if (var != nullptr && var->is_const_decl && var->indirections == 0 && var->init != nullptr) {
        bytes = constant_string(*var->init, seen);
    }
    seen.erase(named->text);
    return bytes;
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
                    type.kind == TypeKind::Tuple || mentions_trait(type)) {
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
                if (const std::optional<std::string> left_type = type_constant(*node.lhs)) {
                    if (const std::optional<std::string> right_type = type_constant(*node.rhs)) {
                        const bool same = *left_type == *right_type;
                        if (node.op == TokenKind::EqEq) {
                            return same ? 1 : 0;
                        }
                        if (node.op == TokenKind::BangEq) {
                            return same ? 0 : 1;
                        }
                        return std::nullopt;
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
                return fold_comptime_call(node);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                // `"hi"[1]` is the byte. The length does not include the terminating null.
                if (node.is_range || node.base == nullptr || node.index == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int> at = fold_int(*node.index);
                if (!at || *at < 0) {
                    return std::nullopt;
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

std::optional<int> Checker::fold_comptime_call(const Expr::Call &call) {
    const FunctionDecl *fn = comptime_callee(call);
    if (fn == nullptr || !fn->params.empty() || !fn->type_params.empty() || !call.args.empty() || !call.type_args.empty() ||
        fn->results.size() != 1 || fn->return_type != "i32" || fn->body == nullptr) {
        return std::nullopt;
    }
    const ResultSlot &slot = fn->results.front();
    if (slot.type_name != "i32" || !slot.type_package.empty() || slot.is_struct || slot.is_enum || slot.indirections != 0 ||
        slot.optional != 0 || slot.is_array || slot.is_fn_ptr || slot.const_pointee != 0) {
        return std::nullopt;
    }
    const auto *body = std::get_if<Stmt::Block>(&fn->body->data);
    if (body == nullptr) {
        return std::nullopt;
    }
    if (!comptime_folding_.insert(fn).second) {
        return std::nullopt;
    }
    const Expr *value = nullptr;
    bool rejected = false;
    for (const auto &stmt : body->statements) {
        if (stmt == nullptr) {
            continue;
        }
        if (const auto *ret = std::get_if<Stmt::Return>(&stmt->data)) {
            if (value != nullptr || ret->value == nullptr || !ret->extra.empty()) {
                rejected = true;
                break;
            }
            value = ret->value.get();
            continue;
        }
        // `emit` produces statements. A value call does not run those statements.
        rejected = true;
        break;
    }
    const std::optional<int> folded = !rejected && value != nullptr ? fold_int(*value) : std::nullopt;
    comptime_folding_.erase(fn);
    return folded;
}

} // namespace tlc
