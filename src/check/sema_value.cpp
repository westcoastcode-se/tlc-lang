// Type checking one expression.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/detail.hpp"

namespace tlc {

Type Checker::check_raw(const Expr &expr, bool used) {
    return std::visit(
        [&](const auto &node) -> Type {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                (void)used;
                const Symbol *symbol = lookup(node.text);
                if (!symbol) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                init_touch(*symbol, node.loc);
                if (captured(*symbol)) {
                    // A function pointer is a pointer. A direct call of the inner function can pass it along.
                    if (!fn_pointer(symbol->type) || checking_.empty()) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    note_capture(*checking_.back(), node.text, symbol->function_id);
                    return symbol->type;
                }
                if (symbol->is_local_function && symbol->fn_captures && !direct_callee_) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (symbol->is_package) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (symbol->is_template) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (symbol->is_function && symbol->name == "va" && is_std_builtin_file(symbol->package_file)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                // `builtin.fence` is replaced only at a direct call. The name is not a value.
                if (symbol->is_function && !direct_callee_ && symbol->name == "fence" &&
                    is_std_builtin_file(symbol->package_file)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (symbol->is_function && !direct_callee_) {
                    for (const FnList &list : symbol->lists) {
                        if (list.decl != nullptr && (list.decl->is_comptime || list.decl->is_macro)) {
                            error(node.loc, "not supported yet");
                            return type_invalid();
                        }
                    }
                }
                if (symbol->is_function && !symbol->is_local_function && !direct_callee_) {
                    for (const FnList &list : symbol->lists) {
                        if (list.decl != nullptr && comptime_param_template(*list.decl) &&
                            !comptime_param_kept(*list.decl)) {
                            error(node.loc, "not supported yet");
                            return type_invalid();
                        }
                    }
                }
                if (symbol->is_function && symbol->lists.size() > 1) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (!allow_unsafe_fn(symbol->unsafe_fn, node.loc)) {
                    return type_invalid();
                }
                return symbol->type;
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                const int bits = node.suffix.bits;
                const bool uns = node.suffix.is_unsigned;
                const auto fits = [&](uint64_t limit) {
                    if (node.value > limit) {
                        error(node.loc, "type mismatch");
                        return false;
                    }
                    return true;
                };
                if (bits == 0) {
                    if (!fits(2147483647ULL)) {
                        return type_invalid();
                    }
                    return type_int();
                }
                if (bits == 8) {
                    if (!fits(uns ? 255ULL : 127ULL)) {
                        return type_invalid();
                    }
                    return type_rank(1, uns);
                }
                if (bits == 16) {
                    if (!fits(uns ? 65535ULL : 32767ULL)) {
                        return type_invalid();
                    }
                    return type_rank(2, uns);
                }
                if (bits == 32) {
                    if (!fits(uns ? 4294967295ULL : 2147483647ULL)) {
                        return type_invalid();
                    }
                    return uns ? type_rank(3, true) : type_int();
                }
                if (bits == 64) {
                    if (!uns && !fits(9223372036854775807ULL)) {
                        return type_invalid();
                    }
                    return type_rank(5, uns);
                }
                error(node.loc, "type mismatch");
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                (void)used;
                if (node.value > 255ULL) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                return type_rank(1, true);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                (void)used;
                return type_float(node.is_f32 ? 32 : 64);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::KwUnsafe) {
                    if (node.expr == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    // `unsafe` does not change the type. The prefix stays outside `value_type`.
                    ++unsafe_depth_;
                    const Type inner = check_raw(*node.expr, used);
                    --unsafe_depth_;
                    return inner;
                }
                if (node.op == TokenKind::Bang && node.postfix) {
                    const Type inner = check_expr(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind != TypeKind::Pointer || !inner.optional || inner.pointee == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    Type asserted = inner;
                    asserted.optional = false;
                    return asserted;
                }
                if (node.op == TokenKind::Star) {
                    InitUseGuard read(*this, InitUse::Read);
                    const Type inner = check_expr(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind != TypeKind::Pointer || !inner.pointee || inner.optional) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    if (inner.pointee->kind == TypeKind::Void) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return *inner.pointee;
                }
                if (node.op == TokenKind::Amp) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Symbol *symbol = lookup(named->text); symbol != nullptr && symbol->is_constant) {
                            error(node.loc, "not an lvalue");
                            return type_invalid();
                        }
                    }
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.expr->data)) {
                        if (selector->base != nullptr && enum_designator(*selector->base) >= 0) {
                            error(node.loc, "not an lvalue");
                            return type_invalid();
                        }
                        if (selector->base != nullptr) {
                            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                                if (const Symbol *symbol = lookup(base->text); symbol != nullptr && symbol->is_package) {
                                    const SelectorTarget target = resolve_selector(*selector);
                                    if (target.kind == SelectorTarget::Kind::Failed) {
                                        return type_invalid();
                                    }
                                    if (target.kind == SelectorTarget::Kind::Package && target.symbol != nullptr &&
                                        target.symbol->is_constant) {
                                        error(node.loc, "not an lvalue");
                                        return type_invalid();
                                    }
                                }
                            }
                            Type assoc;
                            if (assoc_from_selector(*selector, assoc)) {
                                if (assoc.kind == TypeKind::Invalid || selector_is_const(*selector)) {
                                    if (assoc.kind != TypeKind::Invalid) {
                                        error(node.loc, "not an lvalue");
                                    }
                                    return type_invalid();
                                }
                            }
                        }
                    }
                    if (!is_lvalue_form(*node.expr)) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.expr, true);
                        return type_invalid();
                    }
                    InitUseGuard address(*this, InitUse::Address);
                    Type inner = check_raw(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind == TypeKind::Function) {
                        return type_pointer(inner);
                    }
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.expr->data);
                        selector != nullptr && selector->slice_len) {
                        error(node.loc, "not an lvalue");
                        return type_invalid();
                    }
                    if (const auto *index = std::get_if<Expr::Index>(&node.expr->data);
                        index != nullptr && index->slice_element) {
                        inner.is_const = true;
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Symbol *symbol = lookup(named->text); symbol != nullptr && symbol->is_immutable) {
                            inner.is_const = true;
                        }
                    }
                    return type_pointer(inner);
                }
                const Type inner = check_expr(*node.expr, true);
                if (node.op == TokenKind::Bang) {
                    std::swap(cond_true_, cond_false_);
                    cond_split_ = true;
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    expect_condition(inner, node.loc);
                    return inner.kind == TypeKind::Bool ? type_bool() : type_invalid();
                }
                if (is_float(inner) && (node.op == TokenKind::Plus || node.op == TokenKind::Minus)) {
                    return inner;
                }
                expect_integer(inner, node.loc);
                if (inner.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (is_c_enum(inner) || is_integer(inner)) {
                    return is_c_enum(inner) ? type_int() : inner;
                }
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                Type lhs;
                Type rhs;
                if (node.op == TokenKind::AmpAmp || node.op == TokenKind::PipePipe) {
                    lhs = node.lhs ? check_expr(*node.lhs, true) : type_invalid();
                    const AssignState left_true = cond_true_;
                    const AssignState left_false = cond_false_;
                    // `&&` checks the right operand only when the left one is true. `||` is the opposite.
                    assign_ = node.op == TokenKind::AmpAmp ? left_true : left_false;
                    rhs = node.rhs ? check_expr(*node.rhs, true) : type_invalid();
                    const AssignState right_true = cond_true_;
                    const AssignState right_false = cond_false_;
                    if (node.op == TokenKind::AmpAmp) {
                        cond_true_ = right_true;
                        cond_false_ = init_merge(left_false, right_false);
                    } else {
                        cond_true_ = init_merge(left_true, right_true);
                        cond_false_ = right_false;
                    }
                    assign_ = init_merge(cond_true_, cond_false_);
                    cond_split_ = true;
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    expect_condition(lhs, expr_loc(*node.lhs));
                    expect_condition(rhs, expr_loc(*node.rhs));
                    if (lhs.kind != TypeKind::Bool || rhs.kind != TypeKind::Bool) {
                        return type_invalid();
                    }
                    return type_bool();
                } else {
                    lhs = check_expr(*node.lhs, true);
                    rhs = check_expr(*node.rhs, true);
                }
                if (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) {
                    const bool lhs_ptr = lhs.kind == TypeKind::Pointer || lhs.kind == TypeKind::Nullptr;
                    const bool rhs_ptr = rhs.kind == TypeKind::Pointer || rhs.kind == TypeKind::Nullptr;
                    if (lhs_ptr || rhs_ptr) {
                        if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                            return type_invalid();
                        }
                        if (lhs.kind == TypeKind::Pointer && rhs.kind == TypeKind::Pointer) {
                            if (lhs.alias_id != rhs.alias_id || lhs.pointee == nullptr || rhs.pointee == nullptr ||
                                !same_type(*lhs.pointee, *rhs.pointee)) {
                                error(node.loc, "type mismatch");
                                return type_invalid();
                            }
                            return type_bool();
                        }
                        if (!lhs_ptr || !rhs_ptr) {
                            error(node.loc, "type mismatch");
                            return type_invalid();
                        }
                        return type_bool();
                    }
                }
                if (lhs.kind == TypeKind::Meta || rhs.kind == TypeKind::Meta) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (lhs.kind == TypeKind::Meta && rhs.kind == TypeKind::Meta &&
                        (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq)) {
                        return type_bool();
                    }
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if ((lhs.alias_id != 0 || rhs.alias_id != 0) && !same_type(lhs, rhs)) {
                    mismatch(node.loc, lhs, rhs);
                    return type_invalid();
                }
                if (lhs.kind == TypeKind::Enum || rhs.kind == TypeKind::Enum) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (lhs.kind == TypeKind::Enum && rhs.kind == TypeKind::Enum && same_type(lhs, rhs) &&
                        (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq)) {
                        return type_bool();
                    }
                    if (is_c_enum(lhs)) {
                        lhs = type_int();
                    }
                    if (is_c_enum(rhs)) {
                        rhs = type_int();
                    }
                    if (lhs.kind == TypeKind::Enum || rhs.kind == TypeKind::Enum) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                }
                if (is_float(lhs) || is_float(rhs)) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (!is_numeric(lhs) || !is_numeric(rhs)) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    const bool wide = (is_float(lhs) && lhs.rank >= 64) || (is_float(rhs) && rhs.rank >= 64);
                    switch (node.op) {
                    case TokenKind::Percent:
                    case TokenKind::Amp:
                    case TokenKind::Pipe:
                    case TokenKind::Caret:
                    case TokenKind::LtLt:
                    case TokenKind::GtGt:
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    case TokenKind::EqEq:
                    case TokenKind::BangEq:
                    case TokenKind::Lt:
                    case TokenKind::Gt:
                    case TokenKind::Le:
                    case TokenKind::Ge:
                        return type_bool();
                    default:
                        if (lhs.alias_id != 0) {
                            return lhs;
                        }
                        return type_float(wide ? 64 : 32);
                    }
                }
                if (lhs.kind == TypeKind::Bool || rhs.kind == TypeKind::Bool) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (lhs.kind == TypeKind::Bool && rhs.kind == TypeKind::Bool &&
                        (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq)) {
                        return type_bool();
                    }
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                // `==` and `!=` of two byte slices are not integer compares.
                const auto byte_slice = [](const Type &type) {
                    if (type.alias_id != 0 || !is_slice(type) || type.element == nullptr) {
                        return false;
                    }
                    const Type &elem = *type.element;
                    return elem.kind == TypeKind::Char && elem.rank == 1 && elem.is_unsigned && !elem.plain_char;
                };
                if ((node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) && byte_slice(lhs) && byte_slice(rhs)) {
                    error(node.loc, "compare with builtin.memeql");
                    return type_invalid();
                }
                expect_integer(lhs, expr_loc(*node.lhs));
                expect_integer(rhs, expr_loc(*node.rhs));
                if (!is_integer(lhs) || !is_integer(rhs)) {
                    return type_invalid();
                }
                const Type common = usual(lhs, rhs);
                switch (node.op) {
                case TokenKind::EqEq:
                case TokenKind::BangEq:
                case TokenKind::Lt:
                case TokenKind::Gt:
                case TokenKind::Le:
                case TokenKind::Ge:
                    return type_bool();
                default:
                    return common;
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                struct CoverRhs {
                    const Expr *&slot;
                    const Expr *saved;
                    ~CoverRhs() { slot = saved; }
                } cover{init_cover_rhs_, init_cover_rhs_};
                struct PartialRestore {
                    bool &flag;
                    bool saved;
                    ~PartialRestore() { flag = saved; }
                } partial{init_allow_partial_, init_allow_partial_};
                if (node.rhs != nullptr && std::holds_alternative<Expr::Init>(node.rhs->data) &&
                    !std::get<Expr::Init>(node.rhs->data).is_tuple) {
                    init_cover_rhs_ = node.rhs.get();
                    init_allow_partial_ = true;
                }
                if (const auto *star = std::get_if<Expr::Unary>(&node.lhs->data); star != nullptr && star->op == TokenKind::Star) {
                    const Type pointer = check_expr(*star->expr, true);
                    if (pointer.kind != TypeKind::Pointer || !pointer.pointee) {
                        if (pointer.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (pointer.optional || pointer.pointee->kind == TypeKind::Void || pointer.pointee->is_const) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    const std::size_t before = diags_.size();
                    check_against(*node.rhs, *pointer.pointee);
                    init_store_if(*node.lhs, before);
                    return *pointer.pointee;
                }
                if (const auto *index = std::get_if<Expr::Index>(&node.lhs->data)) {
                    if (index->is_range) {
                        if (index->base) {
                            check_expr(*index->base, true);
                        }
                        if (index->index) {
                            check_expr(*index->index, true);
                        }
                        if (index->end) {
                            check_expr(*index->end, true);
                        }
                        check_expr(*node.rhs, true);
                        error(node.loc, "not an lvalue");
                        return type_invalid();
                    }
                    Type subscript;
                    {
                        // The subscript is its own value. It must not see this index step.
                        InitUseGuard read(*this, InitUse::Read);
                        subscript = check_expr(*index->index, true);
                    }
                    expect(subscript, type_int(), expr_loc(*index->index));
                    InitStep step;
                    step.kind = InitStep::Kind::Index;
                    if (const std::optional<int> folded = constant_index(*index->index)) {
                        step.known = true;
                        step.index = *folded;
                    }
                    Type base;
                    {
                        InitUseGuard address(*this, InitUse::Address);
                        InitPush push(*this, std::move(step));
                        // A sized array stays `[N]T`. `row[i] = n` stores one element.
                        base = check_raw(*index->base, true);
                    }
                    if (base.kind == TypeKind::Tuple) {
                        // `t[0] = 1` stores one element. The index is a constant.
                        // A temporary has no place to store into.
                        if (index->base == nullptr || !std::holds_alternative<Expr::Name>(index->base->data)) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        if (const auto *named = std::get_if<Expr::Name>(&index->base->data)) {
                            if (const Symbol *symbol = lookup(named->text);
                                symbol != nullptr && symbol->is_immutable) {
                                error(node.loc, "not an lvalue");
                                check_expr(*node.rhs, true);
                                return type_invalid();
                            }
                        }
                        const std::optional<int> folded = index->index ? constant_index(*index->index) : std::nullopt;
                        // The slot is chosen while compiling. A runtime index has no single field.
                        if (!folded) {
                            error(node.loc, "index is not a compile-time constant");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        if (*folded < 0 || static_cast<std::size_t>(*folded) >= base.results.size()) {
                            error(node.loc, "type mismatch");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        index->tuple_index = *folded;
                        Type element = base.results[static_cast<std::size_t>(*folded)];
                        if (element.is_const || base.is_const) {
                            error(node.loc, "type mismatch");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const std::size_t before = diags_.size();
                        check_against(*node.rhs, element);
                        init_store_if(*node.lhs, before);
                        return element;
                    }
                    if (base.kind == TypeKind::Pointer && base.optional) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type element;
                    bool pointee_void = false;
                    // `v[i] = n` stores one element of `[]T`. `[]const T` and a const slice do not.
                    if (is_slice(base)) {
                        if (base.element == nullptr) {
                            error(node.loc, "type mismatch");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        element = *base.element;
                        if (base.is_const) {
                            element.is_const = true;
                        }
                    } else if (base.kind == TypeKind::Array && base.length >= 0 && base.element != nullptr &&
                        !is_slice(base)) {
                        element = *base.element;
                        if (base.is_const) {
                            element.is_const = true;
                        }
                    } else if (base.kind == TypeKind::Pointer && !base.optional && base.decayed_len > 0 &&
                               base.pointee != nullptr && base.pointee->kind == TypeKind::Array &&
                               base.pointee->length >= 0) {
                        // A parameter `[N][M]T` points at the first row. `grid[i]` is that row.
                        element = *base.pointee;
                    } else if (!indexed_type(base, element, pointee_void)) {
                        if (base.kind != TypeKind::Invalid) {
                            error(node.loc, pointee_void ? "type mismatch" : "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    // `grid[i] = line` and `h.raw[i] = {7, 8}` store into that row. A const element does not.
                    // `grid[i][j] = n` still stores one element.
                    if (element.is_const || const_array_elements(element)) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    const std::size_t before = diags_.size();
                    check_against(*node.rhs, element);
                    init_store_if(*node.lhs, before);
                    return element;
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&node.lhs->data)) {
                    if (selector->base != nullptr && enum_designator(*selector->base) >= 0) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type assoc;
                    if (assoc_from_selector(*selector, assoc)) {
                        if (assoc.kind == TypeKind::Invalid) {
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        if (selector_is_const(*selector) || assoc.kind == TypeKind::Function) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const std::size_t before = diags_.size();
                        check_against(*node.rhs, assoc);
                        init_store_if(*node.lhs, before);
                        return assoc;
                    }
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (target.kind == SelectorTarget::Kind::Package) {
                        if (target.symbol->is_function || target.symbol->is_type || target.symbol->is_constant ||
                            (target.symbol->type.kind == TypeKind::Array && !is_slice(target.symbol->type))) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const std::size_t before = diags_.size();
                        check_against(*node.rhs, target.symbol->type);
                        init_store_if(*node.lhs, before);
                        return target.symbol->type;
                    }
                    Type base;
                    {
                        // `p.x = 1` names storage in `p`. The field step keeps the other fields unread.
                        InitUseGuard address(*this, InitUse::Address);
                        InitStep step;
                        step.kind = InitStep::Kind::Field;
                        step.field = selector->field;
                        InitPush push(*this, std::move(step));
                        base = check_expr(*selector->base, true);
                    }
                    Type trait_field;
                    if (as_trait_field(base, selector->field, trait_field)) {
                        if (trait_field.kind == TypeKind::Invalid) {
                            error(node.loc, "undeclared identifier");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const bool through_pointer = base.kind != TypeKind::Trait;
                        if (!through_pointer && !is_lvalue_form(*selector->base)) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        if (trait_field.is_const ||
                            (through_pointer && base.pointee != nullptr && base.pointee->is_const) ||
                            const_path(*node.lhs)) {
                            error(node.loc, "type mismatch");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const std::size_t before = diags_.size();
                        check_against(*node.rhs, trait_field);
                        init_store_if(*node.lhs, before);
                        return trait_field;
                    }
                    if (base.kind == TypeKind::Pointer && base.optional) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type object;
                    bool through_pointer = false;
                    if (!dot_object(base, object, through_pointer)) {
                        if (base.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (!through_pointer && !is_lvalue_form(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type field;
                    if (!lookup_field(object, selector->field, field)) {
                        error(node.loc, "undeclared identifier");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (hidden_field(object, selector->field, node.loc)) {
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (field.is_const || from_slice_element(selector->base.get()) ||
                        (base.kind == TypeKind::Pointer && base.pointee != nullptr && base.pointee->is_const) ||
                        const_path(*node.lhs)) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    const std::size_t before = diags_.size();
                    check_against(*node.rhs, field);
                    init_store_if(*node.lhs, before);
                    return field;
                }
                const bool name = std::holds_alternative<Expr::Name>(node.lhs->data);
                const Symbol *symbol = nullptr;
                if (name) {
                    symbol = lookup(std::get<Expr::Name>(node.lhs->data).text);
                }
                if (name && symbol != nullptr && captured(*symbol)) {
                    error(expr_loc(*node.lhs), "not supported yet");
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                // A const binding and a pointer receiver are fixed. `*self = ...` is another assignment.
                if (name && symbol != nullptr && (symbol->is_immutable || symbol->fixed_receiver)) {
                    error(node.loc, "type mismatch");
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                // `copy = row` and `row = {4, 6}` store into the array. A const element does not.
                if (name && symbol != nullptr && symbol->type.kind == TypeKind::Array && !is_slice(symbol->type) &&
                    const_array_elements(symbol->type)) {
                    error(node.loc, "type mismatch");
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                if (!name || !symbol || symbol->is_function || symbol->is_package || symbol->is_constant) {
                    if (name && !symbol) {
                        error(expr_loc(*node.lhs), "undeclared identifier");
                    } else {
                        error(node.loc, "not an lvalue");
                    }
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                const std::size_t before = diags_.size();
                check_against(*node.rhs, symbol->type);
                init_store_if(*node.lhs, before);
                return symbol->type;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (const FunctionDecl *fn = node.callee != nullptr ? mem_compare_callee(node) : nullptr) {
                    if (fold_mem_compare(node)) {
                        return mem_compare_bool(mem_compare_kind(*fn)) ? type_bool() : type_int();
                    }
                }
                if (const FunctionDecl *fn = node.callee != nullptr ? comptime_callee(node) : nullptr) {
                    const std::size_t before = diags_.size();
                    if (!node.type_args.empty() || !fn->type_params.empty()) {
                        error(node.loc, "not supported yet");
                    } else if (node.args.size() != fn->params.size()) {
                        error(node.loc, "argument count");
                    } else {
                        for (std::size_t i = 0; i < fn->params.size(); ++i) {
                            if (node.args[i] == nullptr) {
                                error(node.loc, "argument count");
                                continue;
                            }
                            check_call_arg(*node.args[i], type_of_param(fn->params[i]));
                            if (diags_.size() == before && !argument_is_comptime(*node.args[i]) &&
                                !folded_local_int(*node.args[i]) && !comptime_constant(*node.args[i]) &&
                                !fold_switch_case(*node.args[i])) {
                                error(expr_loc(*node.args[i]), "not supported yet");
                            }
                        }
                    }
                    if (diags_.size() != before) {
                        return type_invalid();
                    }
                    if (!fold_int(expr)) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    return type_int();
                }
                if (is_builtin_fmt(node)) {
                    if (!format_call(node)) {
                        error(node.loc, "not supported yet");
                        for (const auto &arg : node.args) {
                            if (arg != nullptr) {
                                check_expr(*arg, true);
                            }
                        }
                        return type_invalid();
                    }
                    return string_slice();
                }
                if (const FunctionDecl *fn = node.callee != nullptr ? macro_callee(node) : nullptr) {
                    error(node.loc, "not supported yet");
                    for (const auto &arg : node.args) {
                        if (arg != nullptr) {
                            check_expr(*arg, true);
                        }
                    }
                    (void)fn;
                    return type_invalid();
                }
                const bool deferring = checking_defer_;
                if (deferring) {
                    checking_defer_ = false;
                }
                if (node.callee != nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                    std::string trait_name;
                    std::string trait_package;
                    std::vector<TypeArg> trait_args = node.type_args;
                    bool trait_callee = false;
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        trait_name = named->text;
                        trait_callee = true;
                    } else if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
                        const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                        if (package != nullptr && package->is_package) {
                            trait_package = base->text;
                            trait_name = selector->field;
                            if (trait_args.empty()) {
                                trait_args = selector->type_args;
                            }
                            trait_callee = true;
                        }
                    }
                    if (trait_callee) {
                        Type picked;
                        if (as_trait_cast(trait_package, trait_name, trait_args, node.args.front().get(), node.loc, picked)) {
                            node.trait_cast = true;
                            return picked;
                        }
                    }
                }
                if (node.callee != nullptr) {
                    if (const std::optional<Type> generic = maybe_generic_call(const_cast<Expr &>(expr), used)) {
                        return *generic;
                    }
                    if (const std::optional<Type> specialized =
                            maybe_comptime_param_call(const_cast<Expr &>(expr), used)) {
                        return *specialized;
                    }
                }
                if (node.callee != nullptr && is_builtin_va_call(*node.callee)) {
                    if (deferring || !variadic_function_) {
                        error(node.loc, "not supported yet");
                    }
                    if (!node.args.empty()) {
                        error(node.loc, "argument count");
                        for (const auto &arg : node.args) {
                            if (arg) {
                                check_expr(*arg, true);
                            }
                        }
                    }
                    return type_pointer(type_rank(1, true));
                }
                // Another file in the package does not see the enum as a cast spelling.
                // `ShapeType(shape)` is still the tag read.
                if (node.callee != nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (lookup(named->text) == nullptr) {
                            const int enum_id = find_enum(file_id_, named->text);
                            if (enum_id >= 0) {
                                const Type source = check_expr(*node.args.front(), true);
                                if (source.kind == TypeKind::Struct && source.struct_id >= 0 &&
                                    source.struct_id < static_cast<int>(structs_.size()) &&
                                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                    return type_enum(enum_id);
                                }
                                if (source.kind != TypeKind::Invalid) {
                                    error(node.loc, "type mismatch");
                                }
                                return type_invalid();
                            }
                        }
                    }
                    // `geom.Kind(shape)` reads the tag. The enum is not a function.
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                        if (selector->base != nullptr) {
                            if (const auto *pkg_name = std::get_if<Expr::Name>(&selector->base->data)) {
                                const Symbol *package = lookup(pkg_name->text);
                                if (package != nullptr && package->is_package) {
                                    const Symbol *member = find_export(package->package_file, selector->field);
                                    if (member != nullptr && member->is_type && member->type.kind == TypeKind::Enum) {
                                        const int enum_id = member->type.enum_id;
                                        const Type source = check_expr(*node.args.front(), true);
                                        if (enum_id >= 0 && source.kind == TypeKind::Struct && source.struct_id >= 0 &&
                                            source.struct_id < static_cast<int>(structs_.size()) &&
                                            structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                            return type_enum(enum_id);
                                        }
                                        if (source.kind != TypeKind::Invalid) {
                                            error(node.loc, "type mismatch");
                                        }
                                        return type_invalid();
                                    }
                                }
                            }
                        }
                    }
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                    // `Point` is a type, not a value, so `lookup` does not see it. `Point.make(...)` is the static call.
                    // `Box[i32].from(...)` and `Buf[4, i32].width()` are the same call on a specialization.
                    if (selector->base != nullptr) {
                        auto check_args = [&]() {
                            for (const auto &arg : node.args) {
                                check_expr(*arg, true);
                            }
                        };
                        Type foreign;
                        if (package_static_call(const_cast<Expr &>(expr), *selector, deferring, used, foreign)) {
                            return foreign;
                        }
                        bool failed = false;
                        const std::string spec = same_package_spec(*selector, failed);
                        if (failed) {
                            check_args();
                            return type_invalid();
                        }
                        if (!spec.empty()) {
                            const AssocVar *var = find_assoc(spec, selector->field);
                            const MethodInfo *method = find_named_method(spec, selector->field);
                            if (var == nullptr || (method != nullptr && (method->is_static || !method->more.empty()))) {
                                if (method != nullptr && !method->more.empty()) {
                                    method = choose_method(node, *method, true, !package_owns(method->file_id),
                                                           selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function ||
                                    method->type.result == nullptr) {
                                    error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                            : "undeclared identifier");
                                    check_args();
                                    return type_invalid();
                                }
                                selector->resolved_owner = spec;
                                return finish_static_call(const_cast<Expr &>(expr), *method, deferring, used);
                            }
                        }
                        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
                            const Symbol *symbol = lookup(named->text);
                            const bool shadowed = symbol != nullptr && !symbol->is_type;
                            if (!shadowed && static_type_owner(named->text)) {
                                const MethodInfo *method = find_named_method(named->text, selector->field);
                                if (method != nullptr && !method->more.empty()) {
                                    method = choose_method(node, *method, true, !package_owns(method->file_id),
                                                           selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function ||
                                    method->type.result == nullptr) {
                                    error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                            : "undeclared identifier");
                                    check_args();
                                    return type_invalid();
                                }
                                return finish_static_call(const_cast<Expr &>(expr), *method, deferring, used);
                            }
                        }
                    }
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        for (const auto &arg : node.args) {
                            check_expr(*arg, true);
                        }
                        return type_invalid();
                    }
                    if (target.kind != SelectorTarget::Kind::Package && selector->base != nullptr) {
                        // `self *this` does not read the fields. `self this` copies them below.
                        Type base;
                        {
                            InitUseGuard method(*this, InitUse::Method);
                            base = check_expr(*selector->base, true);
                        }
                        if (base.kind == TypeKind::Invalid) {
                            for (const auto &arg : node.args) {
                                check_expr(*arg, true);
                            }
                            return type_invalid();
                        }
                        // `p.make(...)` on a static function is `type mismatch`. The call is `Type.make(...)`.
                        const Type *alias_type = &base;
                        if (base.alias_id == 0 && base.kind == TypeKind::Pointer && base.pointee != nullptr) {
                            alias_type = base.pointee.get();
                        }
                        if (alias_type->alias_id > 0 &&
                            static_cast<std::size_t>(alias_type->alias_id) < alias_file_.size()) {
                            bool hidden = false;
                            const MethodInfo *owned = find_visible_method(
                                alias_file_[static_cast<std::size_t>(alias_type->alias_id)], type_desc(*alias_type),
                                selector->field, hidden);
                            bool only_static = owned != nullptr && owned->is_static;
                            if (only_static) {
                                for (const MethodInfo &extra : owned->more) {
                                    if (!extra.is_static) {
                                        only_static = false;
                                        break;
                                    }
                                }
                            }
                            if (only_static) {
                                error(node.loc, "type mismatch");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                        }
                        const bool type_value = base.kind == TypeKind::Meta;
                        const bool through_type = base.kind == TypeKind::Pointer && base.pointee &&
                                                  base.pointee->kind == TypeKind::Meta;
                        if (type_value || through_type) {
                            if (through_type && base.optional) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (selector->field != "name" && selector->field != "fields") {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (!node.args.empty()) {
                                error(node.loc, "argument count");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (selector->field == "fields") {
                                const Type slice = type_field_slice();
                                if (slice.kind == TypeKind::Invalid) {
                                    error(node.loc, "type mismatch");
                                    return type_invalid();
                                }
                                node.type_fields = true;
                                return slice;
                            }
                            node.type_name = true;
                            // The spelling lives in a constant global. `*const u8` rejects a store.
                            Type bytes = type_rank(1, true);
                            bytes.is_const = true;
                            return type_pointer(std::move(bytes));
                        }
                        if (base.kind == TypeKind::Trait ||
                            (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                            const bool through_pointer = base.kind != TypeKind::Trait;
                            const int trait_id = through_pointer ? base.pointee->trait_id : base.trait_id;
                            const TraitInfo::Method *method = find_trait_method(trait_id, selector->field);
                            if (method != nullptr &&
                                (method->type.kind != TypeKind::Function || method->type.result == nullptr)) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (method == nullptr) {
                                std::vector<const FunctionDecl *> plain;
                                bool saw_generic = false;
                                if (trait_id >= 0 && trait_id < static_cast<int>(traits_.size())) {
                                    for (const FunctionDecl *def : traits_[static_cast<std::size_t>(trait_id)].defaults) {
                                        if (def == nullptr || def->name != selector->field) {
                                            continue;
                                        }
                                        if (def->type_params.empty()) {
                                            plain.push_back(def);
                                        } else {
                                            saw_generic = true;
                                        }
                                    }
                                }
                                auto check_args = [&]() {
                                    for (const auto &arg : node.args) {
                                        check_expr(*arg, true);
                                    }
                                };
                                if (plain.empty()) {
                                    error(node.loc, saw_generic ? "not supported yet" : "undeclared identifier");
                                    check_args();
                                    return type_invalid();
                                }
                                const FunctionDecl *chosen = nullptr;
                                Type chosen_tail;
                                int hits = 0;
                                for (const FunctionDecl *def : plain) {
                                    const Type tail = method_tail(*def);
                                    if (tail.kind != TypeKind::Function || tail.result == nullptr ||
                                        tail.params.size() != node.args.size()) {
                                        continue;
                                    }
                                    ++hits;
                                    chosen = def;
                                    chosen_tail = tail;
                                }
                                if (hits == 0) {
                                    error(node.loc, "argument count");
                                    check_args();
                                    return type_invalid();
                                }
                                if (hits != 1 || chosen == nullptr || chosen_tail.result == nullptr) {
                                    error(node.loc, "type mismatch");
                                    check_args();
                                    return type_invalid();
                                }
                                require_unsafe_call(chosen->unsafe, node.loc);
                                if (deferring) {
                                    error(node.loc, "not supported yet");
                                }
                                for (std::size_t i = 0; i < node.args.size(); ++i) {
                                    check_call_arg(*node.args[i], chosen_tail.params[i]);
                                }
                                return *chosen_tail.result;
                            }
                            require_unsafe_call(method->unsafe_fn, node.loc);
                            if (deferring) {
                                error(node.loc, "not supported yet");
                            }
                            const std::size_t arity = method->type.params.size();
                            const bool count_ok = node.args.size() == arity;
                            if (!count_ok) {
                                error(node.loc, "argument count");
                            }
                            for (std::size_t i = 0; i < node.args.size(); ++i) {
                                if (count_ok && i < arity) {
                                    check_call_arg(*node.args[i], method->type.params[i]);
                                } else {
                                    check_expr(*node.args[i], true);
                                }
                            }
                            return *method->type.result;
                        }
                        if (base.kind == TypeKind::Enum ||
                            (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Enum)) {
                            const bool through_pointer = base.kind != TypeKind::Enum;
                            const int enum_id = through_pointer ? base.pointee->enum_id : base.enum_id;
                            const std::string enum_name =
                                enum_id >= 0 && enum_id < static_cast<int>(enum_types_.size())
                                    ? enum_types_[static_cast<std::size_t>(enum_id)].name
                                    : std::string{};
                            bool hidden = false;
                            uint32_t home = 0;
                            if (enum_id >= 0 && enum_id < static_cast<int>(enum_types_.size())) {
                                home = enum_types_[static_cast<std::size_t>(enum_id)].file_id;
                            }
                            if (const MethodInfo *method = find_visible_method(home, enum_name, selector->field, hidden)) {
                                if (!method->more.empty()) {
                                    method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                selector->resolved_file = method->file_id;
                                return finish_value_method(const_cast<Expr &>(expr), *selector, *method, base, through_pointer,
                                                            used);
                            }
                            if (hidden) {
                                error(node.loc, selector->field + " is not exported");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                        }
                        bool alias_pointer = false;
                        if (const Type *alias = scalar_alias(base, alias_pointer)) {
                            bool hidden = false;
                            uint32_t home = 0;
                            if (alias->alias_id > 0 && static_cast<std::size_t>(alias->alias_id) < alias_file_.size()) {
                                home = alias_file_[static_cast<std::size_t>(alias->alias_id)];
                            }
                            const MethodInfo *method = find_visible_method(home, type_desc(*alias), selector->field, hidden);
                            if (method != nullptr && !method->more.empty()) {
                                method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                if (method == nullptr) {
                                    return type_invalid();
                                }
                            }
                            if (method == nullptr || method->type.kind != TypeKind::Function ||
                                method->type.result == nullptr) {
                                error(node.loc, hidden ? selector->field + " is not exported" : "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (!method->is_static) {
                                selector->resolved_owner = type_desc(*alias);
                                selector->resolved_file = method->file_id;
                            }
                            return finish_value_method(const_cast<Expr &>(expr), *selector, *method, base, alias_pointer, used);
                        }
                        bool slice_pointer = false;
                        if (const Type *alias = slice_alias(base, slice_pointer)) {
                            bool hidden = false;
                            uint32_t home = 0;
                            if (alias->alias_id > 0 && static_cast<std::size_t>(alias->alias_id) < alias_file_.size()) {
                                home = alias_file_[static_cast<std::size_t>(alias->alias_id)];
                            }
                            const MethodInfo *method = find_visible_method(home, type_desc(*alias), selector->field, hidden);
                            if (method != nullptr && !method->more.empty()) {
                                method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                if (method == nullptr) {
                                    return type_invalid();
                                }
                            }
                            if (method == nullptr || method->type.kind != TypeKind::Function ||
                                method->type.result == nullptr) {
                                error(node.loc, hidden ? selector->field + " is not exported" : "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (!method->is_static) {
                                selector->resolved_owner = type_desc(*alias);
                                selector->resolved_file = method->file_id;
                            }
                            return finish_value_method(const_cast<Expr &>(expr), *selector, *method, base, slice_pointer, used);
                        }
                        Type object;
                        bool through_pointer = false;
                        if (dot_object(base, object, through_pointer)) {
                            bool hidden = false;
                            uint32_t home = 0;
                            std::string owner;
                            if (object.struct_id >= 0 && object.struct_id < static_cast<int>(structs_.size())) {
                                grant_covered_defaults(object.struct_id);
                                home = structs_[static_cast<std::size_t>(object.struct_id)].file_id;
                                owner = structs_[static_cast<std::size_t>(object.struct_id)].name;
                            }
                            if (const MethodInfo *method = find_visible_method(home, owner, selector->field, hidden)) {
                                if (!method->more.empty()) {
                                    method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                if (method->is_static) {
                                    error(node.loc, "type mismatch");
                                    for (const auto &arg : node.args) {
                                        check_expr(*arg, true);
                                    }
                                    return type_invalid();
                                }
                                selector->resolved_file = method->file_id;
                                const std::size_t call_mark = diags_.size();
                                require_unsafe_call(method->unsafe_fn, node.loc);
                                if (deferring && method->type.variadic) {
                                    error(node.loc, "not supported yet");
                                }
                                const std::size_t arity = method->type.params.size();
                                const bool count_ok = method->type.variadic ? node.args.size() + 1 >= arity
                                                                            : node.args.size() + 1 == arity;
                                if (!count_ok) {
                                    error(node.loc, "argument count");
                                }
                                if (!method->type.params.empty()) {
                                    const Type &receiver = method->type.params[0];
                                    if (receiver.kind == TypeKind::Pointer) {
                                        if (!through_pointer && !is_lvalue_form(*selector->base)) {
                                            error(node.loc, "not an lvalue");
                                        } else {
                                            const Type pointer =
                                                method_pointer(base, object, through_pointer, *selector->base);
                                            if (!accepts_pointer(pointer, receiver)) {
                                                mismatch(expr_loc(*selector->base), receiver, pointer);
                                            } else if (selector->base != nullptr) {
                                                const bool writes = receiver.pointee != nullptr && !receiver.pointee->is_const;
                                                init_address_span(*selector->base, writes, expr_loc(*selector->base), false);
                                            }
                                        }
                                    } else {
                                        init_require_receiver(*selector->base);
                                        expect(object, receiver, expr_loc(*selector->base));
                                    }
                                }
                                uint32_t trait_home = 0;
                                for (const Type &param : method->type.params) {
                                    if (mentions_trait(param)) {
                                        trait_home = method->file_id;
                                        break;
                                    }
                                }
                                const TraitHome home(*this, trait_home);
                                reject_trait_prototype(method->type, method->has_body, node.loc);
                                for (std::size_t i = 0; i < node.args.size(); ++i) {
                                    if (count_ok && i + 1 < arity) {
                                        check_call_arg(*node.args[i], method->type.params[i + 1]);
                                    } else {
                                        check_expr(*node.args[i], true);
                                    }
                                }
                                if (method->decl != nullptr) {
                                    const std::size_t skip =
                                        !method->decl->params.empty() && method->decl->params.front().is_receiver ? 1
                                                                                                                 : 0;
                                    require_comptime_params(*method->decl, node, skip);
                                    if (diags_.size() == call_mark) {
                                        if (const std::optional<Type> specialized = call_comptime_spec(
                                                const_cast<Expr &>(expr), method->decl, skip, method->file_id, used)) {
                                            return *specialized;
                                        }
                                    }
                                }
                                if (method->type.result == nullptr) {
                                    return type_invalid();
                                }
                                return *method->type.result;
                            }
                            if (hidden) {
                                error(node.loc, selector->field + " is not exported");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (base.kind == TypeKind::Pointer && base.optional) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                        }
                    }
                }
                if (node.callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (const Symbol *symbol = lookup(named->text);
                            symbol != nullptr && symbol->is_function && !symbol->is_template && symbol->lists.size() > 1) {
                            return finish_overloaded(const_cast<Expr &>(expr), *symbol, deferring, used);
                        }
                    } else if (const auto *called = std::get_if<Expr::Selector>(&node.callee->data)) {
                        if (const auto *base = called->base == nullptr ? nullptr : std::get_if<Expr::Name>(&called->base->data)) {
                            const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                            if (package != nullptr && package->is_package) {
                                const Symbol *member = find_export(package->package_file, called->field);
                                if (member != nullptr && member->is_function && !member->is_template &&
                                    member->lists.size() > 1) {
                                    return finish_overloaded(const_cast<Expr &>(expr), *member, deferring, used);
                                }
                            }
                        }
                    }
                }
                const bool saved_direct = direct_callee_;
                direct_callee_ = std::holds_alternative<Expr::Name>(node.callee->data) ||
                                 std::holds_alternative<Expr::Func>(node.callee->data) ||
                                 std::holds_alternative<Expr::Selector>(node.callee->data);
                const Type callee = check_expr(*node.callee, true);
                direct_callee_ = saved_direct;
                if (callee.kind == TypeKind::Invalid) {
                    for (const auto &arg : node.args) {
                        check_expr(*arg, true);
                    }
                    return type_invalid();
                }
                const Type *signature = &callee;
                if (callee.kind == TypeKind::Pointer && callee.pointee && callee.pointee->kind == TypeKind::Function) {
                    signature = callee.pointee.get();
                } else if (callee.kind != TypeKind::Function || !callee.result) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (signature->result == nullptr) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (deferring) {
                    bool generic = signature->variadic;
                    for (const Type &param : signature->params) {
                        if (mentions_trait(param)) {
                            generic = true;
                        }
                    }
                    if (generic) {
                        error(node.loc, "not supported yet");
                    }
                }
                const TraitHome home(*this, trait_signature_home(*signature, node.callee.get()));
                reject_trait_prototype(*signature, callee_has_body(*node.callee), node.loc);
                const bool count_ok = signature->variadic ? node.args.size() >= signature->params.size()
                                                         : node.args.size() == signature->params.size();
                if (!count_ok) {
                    error(node.loc, "argument count");
                }
                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    if (count_ok && i < signature->params.size()) {
                        check_call_arg(*node.args[i], signature->params[i]);
                    } else {
                        const Type arg = check_expr(*node.args[i], true);
                        // A variadic C argument has no written type. A constant string passes its address.
                        if (slice_of_const_u8(arg)) {
                            continue;
                        }
                        if (arg.alias_id != 0) {
                            error(expr_loc(*node.args[i]), "type mismatch");
                        } else if (arg.kind != TypeKind::Invalid && !is_numeric(arg) && arg.kind != TypeKind::Pointer &&
                                   arg.kind != TypeKind::Enum && arg.kind != TypeKind::Nullptr) {
                            error(expr_loc(*node.args[i]), "not supported yet");
                        }
                    }
                }
                if (node.callee != nullptr) {
                    const FunctionDecl *ordinary = nullptr;
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (const Symbol *symbol = lookup(named->text);
                            symbol != nullptr && symbol->is_function && !symbol->is_template && symbol->lists.size() == 1) {
                            ordinary = symbol->lists.front().decl;
                        }
                    } else if (const auto *called = std::get_if<Expr::Selector>(&node.callee->data)) {
                        const auto *base = called->base == nullptr ? nullptr : std::get_if<Expr::Name>(&called->base->data);
                        const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                        if (package != nullptr && package->is_package) {
                            const Symbol *member = find_export(package->package_file, called->field);
                            if (member != nullptr && member->is_function && !member->is_template && member->lists.size() == 1) {
                                ordinary = member->lists.front().decl;
                            }
                        }
                    }
                    if (ordinary != nullptr) {
                        const std::size_t before = diags_.size();
                        require_comptime_params(*ordinary, node);
                        // `builtin.fence` is one function. A generic atomic is checked on the template.
                        if (diags_.size() == before && atomic_builtin_rejected(*ordinary, node, nullptr)) {
                            return type_invalid();
                        }
                    }
                }
                return *signature->result;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base != nullptr) {
                    const int enum_id = enum_designator(*node.base);
                    if (enum_id >= 0 && find_enum_member(enum_id, node.field) != nullptr) {
                        return type_enum(enum_id);
                    }
                    Type assoc;
                    if (assoc_from_selector(node, assoc)) {
                        return assoc;
                    }
                    // `Point.make` names the static function. An enumerator such as `Color.Red` was returned above.
                    if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                        const Symbol *symbol = lookup(named->text);
                        const bool shadowed = symbol != nullptr && !symbol->is_type;
                        if (!shadowed && static_type_owner(named->text)) {
                            const MethodInfo *method = find_named_method(named->text, node.field);
                            if (method != nullptr && !method->more.empty()) {
                                error(node.loc, "not supported yet");
                                return type_invalid();
                            }
                            if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function) {
                                error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                        : "undeclared identifier");
                                return type_invalid();
                            }
                            if (!allow_unsafe_fn(method->unsafe_fn, node.loc)) {
                                return type_invalid();
                            }
                            return method->type;
                        }
                    }
                }
                const SelectorTarget target = resolve_selector(node);
                if (target.kind == SelectorTarget::Kind::Failed) {
                    return type_invalid();
                }
                if (target.kind == SelectorTarget::Kind::Package) {
                    const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                    const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                    if (node.field == "va" && package != nullptr && package->is_package &&
                        is_std_builtin_file(package->package_file)) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    // A call sets `direct_callee_`. `var f = builtin.fence` does not.
                    if (node.field == "fence" && !direct_callee_ && package != nullptr && package->is_package &&
                        is_std_builtin_file(package->package_file)) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    if (target.symbol->is_type) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    if (target.symbol->is_function && target.symbol->lists.size() > 1) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    if (!allow_unsafe_fn(target.symbol->unsafe_fn, node.loc)) {
                        return type_invalid();
                    }
                    return target.symbol->type;
                }
                Type raw;
                {
                    InitStep step;
                    step.kind = InitStep::Kind::Field;
                    step.field = node.field;
                    InitPush push(*this, std::move(step));
                    raw = check_raw(*node.base, true);
                }
                // `name.len` is the number of elements, for a slice, a sized array, and a tuple.
                // The array decays to a pointer only when the field is not `len`.
                // A tuple's length is the element count. It does not depend on the values.
                if (raw.kind == TypeKind::Tuple && node.field == "len") {
                    node.slice_len = true;
                    node.len_value = static_cast<int>(raw.results.size());
                    return type_int();
                }
                if (raw.kind == TypeKind::Array && raw.element != nullptr && node.field == "len") {
                    node.slice_len = true;
                    if (raw.length >= 0) {
                        node.len_value = raw.length;
                    } else if (node.base != nullptr) {
                        if (const std::optional<int> known = known_len(*node.base)) {
                            node.len_value = *known;
                        }
                    }
                    return type_int();
                }
                // `a.len` on a parameter `[N]T`. The value is a pointer. The length is the one that was written.
                // This stays ahead of the pointer-to-array check: `[N][M]T` decays to a pointer whose
                // pointee is the row, and `.len` is the outer `N`, not that row.
                if (raw.kind == TypeKind::Pointer && raw.decayed_len > 0 && node.field == "len") {
                    node.slice_len = true;
                    node.len_value = raw.decayed_len;
                    return type_int();
                }
                // `p.len` on `*[N]T` is the written length. `*i32`, `*[]T`, `**[N]T`, and `?*[N]T` have none.
                if (raw.kind == TypeKind::Pointer && !raw.optional && raw.pointee != nullptr &&
                    raw.pointee->kind == TypeKind::Array && raw.pointee->length > 0 &&
                    raw.pointee->element != nullptr && node.field == "len") {
                    node.slice_len = true;
                    node.len_value = raw.pointee->length;
                    return type_int();
                }
                const Type base = value_type(raw);
                if (is_slice(base)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                Type trait_field;
                if (as_trait_field(base, node.field, trait_field)) {
                    if (trait_field.kind == TypeKind::Invalid) {
                        error(node.loc, "undeclared identifier");
                        return type_invalid();
                    }
                    return trait_field;
                }
                if (base.kind == TypeKind::Pointer && base.optional) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (base.kind == TypeKind::Meta ||
                    (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Meta)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                Type object;
                bool through_pointer = false;
                if (!dot_object(base, object, through_pointer)) {
                    if (base.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    return type_invalid();
                }
                Type field;
                if (!lookup_field(object, node.field, field)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                if (hidden_field(object, node.field, node.loc)) {
                    return type_invalid();
                }
                if (from_slice_element(node.base.get())) {
                    field.is_const = true;
                }
                return field;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                (void)used;
                // `"Hello"` and a raw string are `[]const u8`. The bytes still end with a null.
                return string_slice();
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                (void)used;
                // `a[i:j]` is a slice of the elements from i up to, but not including, j.
                // A missing low bound is 0. A missing high bound is the length.
                // The base is an array, a slice, a parameter `[N]T`, or a pointer `*[N]T`.
                // `*i32` and `?*[N]T` have no array length.
                if (node.is_range) {
                    if (node.index) {
                        InitUseGuard read(*this, InitUse::Read);
                        const Type low = check_expr(*node.index, true);
                        expect(low, type_int(), expr_loc(*node.index));
                    }
                    if (node.end) {
                        InitUseGuard read(*this, InitUse::Read);
                        const Type high = check_expr(*node.end, true);
                        expect(high, type_int(), expr_loc(*node.end));
                    }
                    // A range of a sized array is an address, not a read of the elements.
                    InitStep step;
                    step.kind = InitStep::Kind::Range;
                    InitPush push(*this, std::move(step));
                    const Type base = node.base ? check_raw(*node.base, true) : type_invalid();
                    if (base.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    // A tuple is not a slice element. `[N]Duo` and `[N][M]Duo` stay arrays.
                    auto tuple_element = [&](const Type &element) -> bool {
                        if (!array_of_tuple(element)) {
                            return false;
                        }
                        error(node.loc, "type mismatch");
                        return true;
                    };
                    // `row[0:2]` on `func open(row [4]i32)`. The pointer keeps the written length.
                    if (base.kind == TypeKind::Pointer && base.decayed_len > 0 && base.pointee != nullptr &&
                        !base.optional) {
                        if (tuple_element(*base.pointee)) {
                            return type_invalid();
                        }
                        return type_array(*base.pointee, -1);
                    }
                    // `p[0:2]` on `*[4]i32` is `(*p)[0:2]`. The pointee is the array.
                    if (base.kind == TypeKind::Pointer && !base.optional && base.pointee != nullptr &&
                        base.pointee->kind == TypeKind::Array && base.pointee->length > 0 &&
                        base.pointee->element != nullptr) {
                        Type element = *base.pointee->element;
                        if (base.pointee->is_const) {
                            element.is_const = true;
                        }
                        if (tuple_element(element)) {
                            return type_invalid();
                        }
                        return type_array(std::move(element), -1);
                    }
                    if (base.kind != TypeKind::Array || base.element == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    Type element = *base.element;
                    // A const array or a const slice yields `[]const T`, so the range cannot store.
                    if (base.is_const) {
                        element.is_const = true;
                    }
                    if (tuple_element(element)) {
                        return type_invalid();
                    }
                    Type slice = type_array(std::move(element), -1);
                    // `type Row []i32` stays `Row`. A sized array becomes the plain slice `[]T`.
                    if (is_slice(base)) {
                        slice.alias_id = base.alias_id;
                    }
                    return slice;
                }
                // An array keeps its element, including when that element is an array.
                // A pointer to an array still means `(*p)[i]`.
                Type subscript;
                {
                    InitUseGuard read(*this, InitUse::Read);
                    subscript = check_expr(*node.index, true);
                }
                expect(subscript, type_int(), expr_loc(*node.index));
                InitStep step;
                step.kind = InitStep::Kind::Index;
                if (node.index != nullptr) {
                    if (const std::optional<int> folded = constant_index(*node.index)) {
                        step.known = true;
                        step.index = *folded;
                    }
                }
                Type base;
                {
                    InitPush push(*this, std::move(step));
                    base = check_raw(*node.base, true);
                }
                if (base.kind == TypeKind::Array && base.element != nullptr) {
                    Type element = *base.element;
                    if (base.is_const) {
                        element.is_const = true;
                    }
                    // `[]const T` and a const slice name a const element. `[]T` does not.
                    if (is_slice(base) && element.is_const) {
                        node.slice_element = true;
                    }
                    return element;
                }
                if (base.kind == TypeKind::Tuple) {
                    const std::optional<int> folded = node.index ? constant_index(*node.index) : std::nullopt;
                    if (!folded) {
                        // A tuple slot is chosen while compiling. `i` does not name a field.
                        error(node.loc, "index is not a compile-time constant");
                        return type_invalid();
                    }
                    if (*folded < 0 || static_cast<std::size_t>(*folded) >= base.results.size()) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    node.tuple_index = *folded;
                    return base.results[static_cast<std::size_t>(*folded)];
                }
                if (base.kind == TypeKind::Pointer && base.optional) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                // A parameter `[N][M]T` points at the first row. `grid[i]` is that row.
                // `*[M]T` has no decayed length, so `p[i]` is still `(*p)[i]`.
                if (base.kind == TypeKind::Pointer && base.decayed_len > 0 && base.pointee != nullptr &&
                    base.pointee->kind == TypeKind::Array && base.pointee->length >= 0) {
                    return *base.pointee;
                }
                Type element;
                bool pointee_void = false;
                if (!indexed_type(base, element, pointee_void)) {
                    if (base.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    return type_invalid();
                }
                return element;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                (void)used;
                return type_bool();
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                (void)used;
                return type_nullptr();
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                (void)used;
                Type type;
                {
                    InitQuiet quiet(*this);
                    type = sizeof_operand(node);
                }
                if (type.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (type.kind == TypeKind::Void || type.kind == TypeKind::Function || mentions_trait(type)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (type.kind == TypeKind::Tuple) {
                    const std::optional<int> measured = node.align ? std::optional<int>(min_align_of(type)) : type_size(type);
                    if (!measured) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                }
                return type_int();
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                (void)used;
                InitQuiet quiet(*this);
                const Type type = operand_type(node.on_type, node.type_name, node.type_package, node.is_struct, node.is_enum,
                                               node.indirections, node.optional, node.expr.get(), node.loc,
                                               node.const_pointee, node.is_array, node.array_len, node.elem_const,
                                               node.elem_slice, node.shells);
                if (type.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (type.kind == TypeKind::Function || mentions_trait(type)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (type.kind == TypeKind::Tuple) {
                    note_tuple_fields(type);
                }
                node.canonical = type_desc(type);
                return type_meta();
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                (void)used;
                if (!node.is_fn_ptr && node.indirections == 0) {
                    Type picked;
                    if (as_trait_cast(node.type_package, node.type_name, node.type_args, node.expr.get(), node.loc, picked)) {
                        node.trait_value = true;
                        return picked;
                    }
                }
                // `Something[i32, i32]((40, 2))`. The name becomes that specialization.
                if (!node.is_fn_ptr && !node.type_args.empty()) {
                    Expr::Cast &written = const_cast<Expr::Cast &>(node);
                    bool is_array = false;
                    int array_len = 0;
                    std::string array_len_name;
                    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum,
                                    written.indirections, written.optional, is_array, array_len, array_len_name,
                                    written.type_args, written.loc);
                }
                if (node.is_fn_ptr && node.type_name == "invalid") {
                    if (node.expr) {
                        check_expr(*node.expr, true);
                    }
                    return type_invalid();
                }
                const Type target =
                    node.is_fn_ptr
                        ? type_of_fn_ptr(node.type_name, node.type_package, node.is_struct, node.is_enum,
                                         node.ret_indirections, node.indirections < 1 ? 1 : node.indirections,
                                         node.fn_params, node.fn_variadic, node.fn_results, node.loc, node.ret_optional,
                                         node.ret_const_pointee)
                        : spelled_type(node.type_name, node.type_package, node.is_struct, node.is_enum, node.indirections,
                                       node.loc, node.optional, node.const_pointee);
                if (node.expr != nullptr && target.kind == TypeKind::Tuple && target.results.size() == 1) {
                    Type bare = target;
                    bare.alias_id = 0;
                    adopt_one_tuple(const_cast<Expr &>(*node.expr), bare);
                }
                Type source = node.expr ? check_raw(*node.expr, true) : type_invalid();
                // `*i32(row)` is a pointer to the element. Dropping `const` is `unsafe cast`.
                if (target.kind == TypeKind::Pointer && source.kind == TypeKind::Array && source.length >= 0 &&
                    source.element != nullptr && target.pointee != nullptr && !is_slice(source)) {
                    Type element = *source.element;
                    if (source.is_const) {
                        element.is_const = true;
                    }
                    Type element_bare = element;
                    Type pointee_bare = *target.pointee;
                    element_bare.is_const = false;
                    pointee_bare.is_const = false;
                    if (same_type(element_bare, pointee_bare)) {
                        if (element.is_const && !target.pointee->is_const && unsafe_depth_ == 0) {
                            error(node.loc, "unsafe cast");
                        }
                        return target;
                    }
                }
                source = value_type(std::move(source));
                if (target.kind == TypeKind::Invalid || source.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (target.kind == TypeKind::Pointer && source.kind == TypeKind::Nullptr && !target.optional) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (target.kind == TypeKind::Void) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if ((is_c_enum(source) && is_integer(target)) || (is_c_enum(target) && is_integer(source))) {
                    return target;
                }
                // `ShapeType(shape)` reads the tag of a tlc union.
                if (source.kind == TypeKind::Struct && target.kind == TypeKind::Enum && source.struct_id >= 0 &&
                    source.struct_id < static_cast<int>(structs_.size()) &&
                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == target.enum_id &&
                    target.enum_id >= 0) {
                    return target;
                }
                Type source_rep = source;
                Type target_rep = target;
                source_rep.alias_id = 0;
                target_rep.alias_id = 0;
                const bool source_scalar = is_numeric(source_rep);
                const bool target_scalar = is_numeric(target_rep);
                const bool source_int = same_type(source_rep, type_int());
                const bool target_int = same_type(target_rep, type_int());
                if ((target.kind == TypeKind::Pointer && source.kind == TypeKind::Pointer) ||
                    (target.kind == TypeKind::Pointer && source_int) ||
                    (target.kind == TypeKind::Pointer && source.kind == TypeKind::Nullptr) ||
                    (target_int && source.kind == TypeKind::Pointer) ||
                    (target_scalar && source_scalar) || same_type(source_rep, target_rep)) {
                    if (target.kind == TypeKind::Pointer && source.kind == TypeKind::Pointer &&
                        unsafe_pointer_cast(source, target) && unsafe_depth_ == 0) {
                        error(node.loc, "unsafe cast");
                    }
                    return target;
                }
                error(node.loc, "type mismatch");
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                (void)used;
                if (node.array_literal || !node.type_name.empty()) {
                    return check_init(const_cast<Expr::Init &>(node), type_invalid());
                }
                if (!node.elements.empty() && node.fields.empty()) {
                    if (!node.is_tuple) {
                        if (node.elements.size() == 1 && node.elements.front()) {
                            return check_raw(*node.elements.front(), true);
                        }
                        error(node.loc, "type mismatch");
                        for (const auto &element : node.elements) {
                            if (element) {
                                check_expr(*element, true);
                            }
                        }
                        return type_invalid();
                    }
                    if (node.elements.size() < 2 && !node.explicit_one) {
                        error(node.loc, "type mismatch");
                        for (const auto &element : node.elements) {
                            if (element) {
                                check_expr(*element, true);
                            }
                        }
                        return type_invalid();
                    }
                    std::vector<Type> parts;
                    parts.reserve(node.elements.size());
                    bool invalid = false;
                    for (const auto &element : node.elements) {
                        if (!element) {
                            invalid = true;
                            parts.push_back(type_invalid());
                            continue;
                        }
                        Type part = check_raw(*element, true);
                        if (part.kind == TypeKind::Invalid) {
                            invalid = true;
                        }
                        parts.push_back(std::move(part));
                    }
                    if (invalid) {
                        return type_invalid();
                    }
                    return type_tuple(std::move(parts));
                }
                // `struct{.value = 10}` names a struct. The field types come from the values.
                if (node.inferred && !node.fields.empty() && node.elements.empty() && node.type_name.empty()) {
                    return check_init(const_cast<Expr::Init &>(node), type_invalid());
                }
                error(node.loc, "type mismatch");
                for (const Expr::FieldInit &field : node.fields) {
                    if (field.value) {
                        check_expr(*field.value, true);
                    }
                }
                for (const auto &element : node.elements) {
                    if (element) {
                        check_expr(*element, true);
                    }
                }
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                (void)used;
                const bool as_callee = direct_callee_;
                direct_callee_ = false;
                if (node.decl == nullptr) {
                    direct_callee_ = as_callee;
                    return type_invalid();
                }
                const FunctionDecl &fn = *node.decl;
                if (fn.inferred && !fn.elaborated) {
                    direct_callee_ = as_callee;
                    error(fn.loc, "type mismatch");
                    return type_invalid();
                }
                if (fn.inferred && fn.elaborated) {
                    direct_callee_ = as_callee;
                    return function_signature(fn);
                }
                if (!fn.method_struct.empty() || fn.exported || fn.is_static || fn.is_extern || fn.variadic) {
                    error(fn.loc, "not supported yet");
                }
                bool generic = false;
                for (const Param &param : fn.params) {
                    if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
                        package_trait(file_id_, param.type_name) >= 0) {
                        generic = true;
                    }
                }
                Type signature = function_signature(fn);
                if (signature.result != nullptr && mentions_trait(*signature.result)) {
                    error(fn.loc, "trait has no size");
                }
                if (generic) {
                    error(fn.loc, "not supported yet");
                }
                if (fn.body != nullptr) {
                    check_function(fn);
                }
                // Hidden arguments are passed at a direct call. The function cannot escape.
                if (!fn.captures.empty() && !as_callee) {
                    error(fn.loc, "not supported yet");
                }
                direct_callee_ = as_callee;
                return signature;
            } else if constexpr (std::is_same_v<T, Expr::New> || std::is_same_v<T, Expr::Delete>) {
                (void)used;
                if (scopes_.size() <= 1) {
                    error(node.loc, "global initializer is not a constant");
                    return type_invalid();
                }
                const auto resolve_allocator = [&](Expr *allocator, bool &builtin_alloc, uint32_t &alloc_file,
                                                   std::string &alloc_name, bool &alloc_is_pointer) {
                    builtin_alloc = allocator == nullptr;
                    alloc_file = 0;
                    alloc_name.clear();
                    alloc_is_pointer = false;
                    if (allocator == nullptr) {
                        return;
                    }
                    const Type got = check_expr(*allocator, true);
                    int struct_id = -1;
                    // The argument is `*T` where `T` has `Allocator`'s methods. `T` is the pointee.
                    if (got.kind == TypeKind::Pointer && got.pointee != nullptr &&
                        got.pointee->kind == TypeKind::Struct) {
                        struct_id = got.pointee->struct_id;
                        alloc_is_pointer = true;
                    } else if (got.kind == TypeKind::Struct) {
                        mismatch(expr_loc(*allocator), type_pointer(got), got);
                        return;
                    } else if (got.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
                        return;
                    }
                    const StructInfo &info = structs_[static_cast<std::size_t>(struct_id)];
                    alloc_file = info.file_id;
                    alloc_name = info.name;
                    if (info.name == "Malloc" && is_std_builtin_file(info.file_id)) {
                        builtin_alloc = true;
                        return;
                    }
                    ensure_allocator_trait();
                    require_satisfies(struct_id, allocator_trait_id_, true, node.loc);
                    auto trait_form = [&](const char *name) -> const MethodInfo * {
                        const MethodInfo *method = find_method(struct_id, name);
                        if (method == nullptr || method->more.empty()) {
                            return method;
                        }
                        const TraitInfo::Method *wanted = find_trait_method(allocator_trait_id_, name);
                        if (wanted == nullptr) {
                            return method;
                        }
                        const MethodInfo *matched = nullptr;
                        auto consider = [&](const MethodInfo &info) {
                            if (trait_method_matches(wanted->type, info.type)) {
                                matched = &info;
                            }
                        };
                        consider(*method);
                        for (const MethodInfo &extra : method->more) {
                            consider(extra);
                        }
                        return matched != nullptr ? matched : method;
                    };
                    for (const char *name : {"malloc", "free", "realloc"}) {
                        const MethodInfo *method = trait_form(name);
                        const bool pointer = method != nullptr && method->type.kind == TypeKind::Function &&
                                             !method->type.params.empty() &&
                                             method->type.params[0].kind == TypeKind::Pointer;
                        if (method != nullptr && !pointer) {
                            error(node.loc, "type mismatch");
                        }
                    }
                    // `malloc` takes `*this`. A const allocator is rejected the same way as `arena.malloc(...)`.
                    const MethodInfo *malloc_method = trait_form("malloc");
                    if (malloc_method != nullptr && malloc_method->type.kind == TypeKind::Function &&
                        !malloc_method->type.params.empty() &&
                        malloc_method->type.params[0].kind == TypeKind::Pointer) {
                        const Type object =
                            got.kind == TypeKind::Pointer && got.pointee != nullptr ? *got.pointee : got;
                        const Type passed = method_pointer(got, object, alloc_is_pointer, *allocator);
                        if (!accepts_pointer(passed, malloc_method->type.params[0])) {
                            mismatch(expr_loc(*allocator), malloc_method->type.params[0], passed);
                        }
                    }
                };
                if constexpr (std::is_same_v<T, Expr::Delete>) {
                    resolve_allocator(node.allocator.get(), node.builtin_alloc, node.alloc_file, node.alloc_name,
                                      node.alloc_is_pointer);
                    if (node.expr == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    const Type got = check_expr(*node.expr, true);
                    if (got.kind != TypeKind::Invalid && got.kind != TypeKind::Pointer) {
                        error(node.loc, "type mismatch");
                    }
                    return type_void();
                } else {
                    resolve_allocator(node.allocator.get(), node.builtin_alloc, node.alloc_file, node.alloc_name,
                                      node.alloc_is_pointer);
                    if (node.type_name.empty()) {
                        error(node.loc, "expected a type");
                        return type_invalid();
                    }
                    bool is_enum = false;
                    bool is_array = false;
                    int array_len = 0;
                    std::string array_len_name;
                    // Specializing `Box[i32]` rewrites the name on the expression.
                    Expr::New &written = const_cast<Expr::New &>(node);
                    prepare_written(written.type_name, written.type_package, written.is_struct, is_enum,
                                    written.indirections, written.optional, is_array, array_len, array_len_name,
                                    written.type_args, written.loc);
                    if (node.count) {
                        const Type got = check_expr(*node.count, true);
                        if (got.kind != TypeKind::Invalid) {
                            expect(got, type_int(), expr_loc(*node.count));
                        }
                        if (const std::optional<int> folded = fold_int(*node.count); folded && *folded <= 0) {
                            error(expr_loc(*node.count), "type mismatch");
                        }
                    }
                    const Type object = spelled_type(node.type_name, node.type_package, node.is_struct, false,
                                                     node.indirections, node.loc, node.optional, node.const_pointee);
                    if (object.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (object.kind == TypeKind::Void || object.kind == TypeKind::Trait ||
                        object.kind == TypeKind::Function) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    // A non-nullable pointer has to be written. A buffer has no place to name each field.
                    auto loose_init = [&]() {
                        const auto *init = node.init ? std::get_if<Expr::Init>(&node.init->data) : nullptr;
                        if (init == nullptr) {
                            return;
                        }
                        for (const Expr::FieldInit &field : init->fields) {
                            if (field.value) {
                                check_expr(*field.value, true);
                            }
                        }
                        for (const auto &element : init->elements) {
                            if (element) {
                                check_expr(*element, true);
                            }
                        }
                    };
                    if (node.count || node.count_inferred) {
                        const auto *list = node.init == nullptr ? nullptr : std::get_if<Expr::Init>(&node.init->data);
                        if (node.init != nullptr && (list == nullptr || !list->fields.empty())) {
                            error(expr_loc(*node.init), "type mismatch");
                            loose_init();
                            return type_pointer(object);
                        }
                        auto check_element = [&](const Expr &element) {
                            const bool saved = init_allow_partial_;
                            init_allow_partial_ = true;
                            check_against(element, object);
                            init_allow_partial_ = saved;
                        };
                        if (node.count_inferred) {
                            // `_` takes its length from the list. `...` has no length to repeat.
                            if (list == nullptr || node.repeat_init || list->elements.empty()) {
                                error(node.loc, "type mismatch");
                                return type_invalid();
                            }
                            for (const auto &element : list->elements) {
                                if (element) {
                                    check_element(*element);
                                }
                            }
                            return type_pointer(type_array(object, static_cast<int>(list->elements.size())));
                        }
                        if (list != nullptr) {
                            if (node.repeat_init) {
                                if (list->elements.size() != 1 || list->elements.front() == nullptr) {
                                    error(expr_loc(*node.init), "type mismatch");
                                } else {
                                    check_element(*list->elements.front());
                                }
                            } else if (list->elements.empty()) {
                                error(expr_loc(*node.init), "type mismatch");
                            } else {
                                const std::optional<int> folded = node.count ? fold_int(*node.count) : std::nullopt;
                                const int listed = static_cast<int>(list->elements.size());
                                if (list->zero_rest) {
                                    if (!folded || *folded < listed ||
                                        (*folded > listed && needs_nonnull_pointer(object))) {
                                        error(expr_loc(*node.init), "type mismatch");
                                    }
                                } else if (!folded || *folded != listed) {
                                    error(expr_loc(*node.init), "type mismatch");
                                }
                                for (const auto &element : list->elements) {
                                    if (element) {
                                        check_element(*element);
                                    }
                                }
                            }
                        } else if (needs_nonnull_pointer(object)) {
                            error(node.loc, "type mismatch");
                        }
                        return type_pointer(object);
                    }
                    // The fields are checked against the reserved object. A non-struct has no fields.
                    if (node.init) {
                        auto *init = std::get_if<Expr::Init>(&node.init->data);
                        if (init == nullptr || object.kind != TypeKind::Struct) {
                            error(expr_loc(*node.init), "type mismatch");
                            loose_init();
                        } else {
                            // The allocated object may leave a field unset.
                            const bool saved = init_allow_partial_;
                            init_allow_partial_ = true;
                            check_init(*init, object);
                            init_allow_partial_ = saved;
                        }
                    } else if (needs_nonnull_pointer(object)) {
                        error(node.loc, "type mismatch");
                    }
                    return type_pointer(object);
                }
            } else {
                (void)used;
                return type_invalid();
            }
        },
        expr.data);
}

Type Checker::operand_type(bool on_type, const std::string &type_name, const std::string &type_package, bool is_struct,
                           bool is_enum, int indirections, uint32_t optional, const Expr *expr, SourceLoc loc,
                           uint32_t const_pointee, bool is_array, int array_len, bool elem_const, bool elem_slice,
                           const std::vector<TypeShell> &shells) {
    if (on_type) {
        if (is_array || elem_slice || !shells.empty()) {
            std::vector<TypeShell> folded = shells;
            fold_shells(folded, loc);
            return object_type(type_name, indirections, is_array, array_len, loc, false, type_package, is_struct, is_enum,
                               false, optional, const_pointee, false, elem_const, elem_slice, folded);
        }
        return spelled_type(type_name, type_package, is_struct, is_enum, indirections, loc, optional, const_pointee);
    }
    if (expr == nullptr) {
        return type_invalid();
    }
    int stars = 0;
    const Expr *cursor = expr;
    while (cursor != nullptr) {
        const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
        if (unary == nullptr || unary->op != TokenKind::Star || unary->postfix || unary->expr == nullptr) {
            break;
        }
        ++stars;
        cursor = unary->expr.get();
    }
    const auto *named = cursor == nullptr ? nullptr : std::get_if<Expr::Name>(&cursor->data);
    Type ignored;
    const bool alias = named != nullptr && package_typedef(file_id_, named->text, ignored);
    const bool tag = named != nullptr && find_tag(file_id_, named->text) >= 0;
    const bool enumeration = named != nullptr && find_enum(file_id_, named->text) >= 0;
    if (named != nullptr && lookup(named->text) == nullptr && (alias || tag || enumeration)) {
        // `sizeof(Point)` and `typeof(*Point)` when the name is a type.
        return with_indirections(type_of_name(named->text, loc, stars > 0), stars, loc);
    }
    return check_raw(*expr, true);
}

Type Checker::sizeof_operand(const Expr::Sizeof &node) {
    return operand_type(node.on_type, node.type_name, node.type_package, node.is_struct, node.is_enum, node.indirections,
                        node.optional, node.expr.get(), node.loc, node.const_pointee, node.is_array, node.array_len,
                        node.elem_const, node.elem_slice, node.shells);
}

} // namespace tlc
