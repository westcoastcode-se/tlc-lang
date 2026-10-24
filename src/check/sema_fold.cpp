// Compile-time folding of constants.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "ast/format.hpp"

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

std::unique_ptr<Expr> folded_string(std::string text, SourceLoc loc) {
    auto expr = std::make_unique<Expr>();
    Expr::String node;
    node.loc = loc;
    node.text = std::move(text);
    expr->data = std::move(node);
    return expr;
}

const Expr *single_i32_return(const FunctionDecl &fn) {
    if (fn.results.size() != 1 || fn.return_type != "i32" || fn.body == nullptr) {
        return nullptr;
    }
    const ResultSlot &slot = fn.results.front();
    if (slot.type_name != "i32" || !slot.type_package.empty() || slot.is_struct || slot.is_enum || slot.indirections != 0 ||
        slot.optional != 0 || slot.is_array || slot.is_fn_ptr || slot.const_pointee != 0) {
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
    if (const auto *call = std::get_if<Expr::Call>(&expr.data)) {
        return format_call(*call);
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
        if (symbol != nullptr && symbol->type.kind == TypeKind::Pointer && symbol->type.decayed_len > 0) {
            return symbol->type.decayed_len;
        }
        if (symbol != nullptr && symbol->type.kind == TypeKind::Pointer && !symbol->type.optional &&
            symbol->type.pointee != nullptr && symbol->type.pointee->kind == TypeKind::Array &&
            symbol->type.pointee->length > 0 && symbol->type.pointee->element != nullptr) {
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
    if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Function || mentions_trait(type)) {
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
                if (const std::optional<int> compared = fold_mem_compare(node)) {
                    return compared;
                }
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
        if (call.args[i] == nullptr || !argument_is_comptime(*call.args[i])) {
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

void Checker::require_comptime_params(const FunctionDecl &fn, const Expr::Call &call) {
    if (fn.is_comptime || fn.is_macro || !fn.type_params.empty()) {
        return;
    }
    const std::size_t count = fn.params.size() < call.args.size() ? fn.params.size() : call.args.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!fn.params[i].is_comptime || call.args[i] == nullptr) {
            continue;
        }
        if (!argument_is_comptime(*call.args[i])) {
            error(expr_loc(*call.args[i]), "type mismatch");
        }
    }
}

std::optional<int> Checker::fold_comptime_call(const Expr::Call &call) {
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
        } else {
            std::unordered_set<std::string> seen;
            if (const std::optional<std::string> text = constant_string(*call.args[i], seen)) {
                literal = folded_string(*text, loc);
            }
        }
        if (literal == nullptr) {
            return std::nullopt;
        }
        names.push_back(fn->params[i].name);
        owned.push_back(std::move(literal));
    }
    const Expr *value = single_i32_return(*fn);
    if (value == nullptr || !comptime_folding_.insert(fn).second) {
        return std::nullopt;
    }
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *fn;
        ~Leave() { set.erase(fn); }
    } leave{comptime_folding_, fn};
    std::vector<const Expr *> exprs;
    exprs.reserve(owned.size());
    for (const auto &item : owned) {
        exprs.push_back(item.get());
    }
    std::unique_ptr<Expr> rewritten = clone_expr(*value);
    replace_names(rewritten, names, exprs);
    return fold_int(*rewritten);
}

} // namespace tlc
