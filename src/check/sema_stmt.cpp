// Type checking statements and function bodies.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/detail.hpp"

namespace tlc {

void Checker::check_assert(SourceLoc loc, const Expr *cond, const std::string &message) {
    if (cond == nullptr) {
        error(loc, "type mismatch");
        return;
    }
    const Type got = check_expr(*cond, true);
    if (got.kind == TypeKind::Invalid) {
        return;
    }
    if (got.kind != TypeKind::Bool) {
        if (got.kind == TypeKind::Pointer) {
            error(loc, "compare with null");
        } else {
            error(loc, "type mismatch: expected bool, found " + type_desc(got));
        }
        return;
    }
    const std::optional<int> value = fold_int(*cond);
    if (!value) {
        error(loc, "not supported yet");
        return;
    }
    if (*value == 0) {
        error(loc, message.empty() ? "assert" : message);
    }
}

bool Checker::is_std_builtin_file(uint32_t file_id) const {
    const PackageNode *node = find_node(file_id);
    return node != nullptr && is_std_builtin_path(node->path);
}

bool Checker::is_builtin_va_call(const Expr &callee) {
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->field != "va" || selector->base == nullptr) {
            return false;
        }
        const auto *base = std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return false;
        }
        const Symbol *symbol = lookup(base->text);
        return symbol != nullptr && symbol->is_package && is_std_builtin_file(symbol->package_file);
    }
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        const Symbol *symbol = lookup(named->text);
        return symbol != nullptr && symbol->is_function && symbol->name == "va" &&
               is_std_builtin_file(symbol->package_file);
    }
    return false;
}

void Checker::check_test(const TestDecl &test) {
    InitSuspend suspend(*this);
    push_function();
    const Type saved = return_type_;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    loop_depth_ = 0;
    switches_.clear();
    return_type_ = type_int();
    if (test.body) {
        if (const auto *block = std::get_if<Stmt::Block>(&test.body->data)) {
            for (const auto &child : block->statements) {
                if (child) {
                    check_stmt(*child);
                }
            }
        } else {
            check_stmt(*test.body);
        }
    }
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    pop();
}
namespace {

struct NullGuard {
    std::string name;
    bool equal_null = false;
};

bool always_exits(const Stmt *stmt) {
    if (stmt == nullptr) {
        return false;
    }
    return std::visit(
        [](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Return> || std::is_same_v<T, Stmt::Break> ||
                          std::is_same_v<T, Stmt::Continue>) {
                return true;
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                if (node.statements.empty() || node.statements.back() == nullptr) {
                    return false;
                }
                return always_exits(node.statements.back().get());
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                return node.then_arm != nullptr && node.else_arm != nullptr && always_exits(node.then_arm.get()) &&
                       always_exits(node.else_arm.get());
            } else {
                return false;
            }
        },
        stmt->data);
}

const Expr::Name *null_side(const Expr *expr, bool &is_null) {
    is_null = false;
    if (expr == nullptr) {
        return nullptr;
    }
    if (std::holds_alternative<Expr::Nullptr>(expr->data)) {
        is_null = true;
        return nullptr;
    }
    return std::get_if<Expr::Name>(&expr->data);
}

std::optional<NullGuard> null_guard(const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }
    const auto *binary = std::get_if<Expr::Binary>(&expr->data);
    if (binary == nullptr || (binary->op != TokenKind::EqEq && binary->op != TokenKind::BangEq)) {
        return std::nullopt;
    }
    bool lhs_null = false;
    bool rhs_null = false;
    const Expr::Name *lhs = null_side(binary->lhs.get(), lhs_null);
    const Expr::Name *rhs = null_side(binary->rhs.get(), rhs_null);
    const Expr::Name *name = nullptr;
    if (lhs != nullptr && rhs_null) {
        name = lhs;
    } else if (rhs != nullptr && lhs_null) {
        name = rhs;
    } else {
        return std::nullopt;
    }
    return NullGuard{name->text, binary->op == TokenKind::EqEq};
}

} // namespace

bool Checker::narrow_optional(const std::string &name) {
    Symbol *symbol = lookup_scope(name);
    if (symbol == nullptr || scopes_.empty() || symbol->function_id != scopes_.back().function_id) {
        return false;
    }
    if (symbol->type.kind != TypeKind::Pointer || !symbol->type.optional) {
        return false;
    }
    symbol->type.optional = false;
    return true;
}

void Checker::restore_optional(const std::string &name) {
    Symbol *symbol = lookup_scope(name);
    if (symbol == nullptr) {
        return;
    }
    symbol->type.optional = true;
}

bool Checker::const_path(const Expr &expr) {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Symbol *symbol = lookup(name->text);
        return symbol != nullptr && (symbol->is_immutable || symbol->is_constant) &&
               symbol->type.kind != TypeKind::Pointer;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector_is_const(*selector)) {
            return true;
        }
        if (selector->base != nullptr) {
            if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
                const Symbol *symbol = lookup(named->text);
                if (symbol != nullptr && symbol->is_package) {
                    const Symbol *member = find_export(symbol->package_file, selector->field);
                    if (member == nullptr) {
                        return false;
                    }
                    if (member->type.kind == TypeKind::Pointer) {
                        return member->type.pointee != nullptr && member->type.pointee->is_const;
                    }
                    return member->is_constant;
                }
            }
        }
    }
    const Expr *base = nullptr;
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        base = selector->base.get();
    } else if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        base = index->base.get();
    } else if (const auto *star = std::get_if<Expr::Unary>(&expr.data);
               star != nullptr && star->op == TokenKind::Star) {
        base = star->expr.get();
    }
    if (base == nullptr) {
        return false;
    }
    if (const auto *name = std::get_if<Expr::Name>(&base->data)) {
        const Symbol *symbol = lookup(name->text);
        if (symbol != nullptr && symbol->type.kind == TypeKind::Pointer && symbol->type.pointee != nullptr &&
            symbol->type.pointee->is_const) {
            return true;
        }
    }
    return const_path(*base);
}

Type Checker::method_pointer(const Type &base, const Type &object, bool through_pointer, const Expr &source) {
    if (through_pointer) {
        return base;
    }
    Type value = object;
    // `const origin` and `*const Point` both pass `*const Point` into the method.
    if (const_path(source)) {
        value.is_const = true;
    }
    return type_pointer(std::move(value));
}

void Checker::check_stmt(const Stmt &stmt) {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                push();
                for (const auto &child : node.statements) {
                    if (child) {
                        check_stmt(*child);
                    }
                }
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                UnreachableAfter exit(*this);
                auto match_return = [&](const Type &got, const Type &want) {
                    const bool null_pointer =
                        want.kind == TypeKind::Pointer && got.kind == TypeKind::Nullptr && want.optional;
                    const bool widened = accepts_pointer(got, want);
                    const bool scalar =
                        is_scalar(got) && is_scalar(want) && got.alias_id == 0 && want.alias_id == 0;
                    if (!null_pointer && !widened && !scalar && !accepts_slice(got, want) &&
                        (want.kind == TypeKind::Void ||
                         (got.kind != TypeKind::Invalid && want.kind != TypeKind::Invalid && !same_type(got, want)))) {
                        error(node.loc, "return type: expected " + type_desc(want) + ", found " + type_desc(got));
                    }
                };
                std::vector<const Expr *> exprs;
                if (node.value) {
                    exprs.push_back(node.value.get());
                }
                for (const auto &extra : node.extra) {
                    if (extra) {
                        exprs.push_back(extra.get());
                    }
                }
                // A short anonymous function, `(n) { ... }`, takes its types from the result.
                auto supplied = [&](const Expr &value, const Type &want) {
                    if (inferred_function(value) != nullptr) {
                        return check_against(value, want);
                    }
                    if (const auto *init = std::get_if<Expr::Init>(&value.data)) {
                        Type got = check_init(const_cast<Expr::Init &>(*init), want);
                        // One array result stores `{4, 6}` in a temporary. A tuple piece has no slot.
                        if (return_type_.kind == TypeKind::Tuple && want.kind == TypeKind::Array && want.length >= 0 &&
                            !init->array_literal && got.kind != TypeKind::Invalid) {
                            error(expr_loc(value), "not supported yet");
                            return type_invalid();
                        }
                        return got;
                    }
                    if (want.kind == TypeKind::Array && want.length > 0) {
                        // `return row` on a parameter `[N]T` copies the elements. `return row`
                        // where `[]T` is expected builds the slice header.
                        InitKeep keep(*this);
                        const Type got = check_raw(value, true);
                        if (got.kind != TypeKind::Invalid && decayed_array_value(got, want)) {
                            return want;
                        }
                        return got;
                    }
                    if (want.kind == TypeKind::Array) {
                        return check_raw(value, true);
                    }
                    return check_expr(value, true);
                };
                if (return_type_.kind == TypeKind::Tuple && exprs.size() == 1 && exprs.front() != nullptr) {
                    Type got = type_invalid();
                    if (const auto *init = std::get_if<Expr::Init>(&exprs.front()->data)) {
                        got = check_init(const_cast<Expr::Init &>(*init), return_type_);
                    } else {
                        got = check_raw(*exprs.front(), true);
                    }
                    if (got.kind == TypeKind::Tuple && same_type(got, return_type_)) {
                        return;
                    }
                    if (got.kind == TypeKind::Tuple) {
                        error(node.loc, "type mismatch");
                        return;
                    }
                }
                if (return_type_.kind == TypeKind::Tuple) {
                    if (exprs.size() != return_type_.results.size()) {
                        error(node.loc, "return type: expected " + std::to_string(return_type_.results.size()) +
                                            " results, found " + std::to_string(exprs.size()));
                    }
                    for (std::size_t i = 0; i < exprs.size(); ++i) {
                        const Type got = i < return_type_.results.size() ? supplied(*exprs[i], return_type_.results[i])
                                                                          : check_expr(*exprs[i], true);
                        if (i < return_type_.results.size()) {
                            match_return(got, return_type_.results[i]);
                        }
                    }
                } else if (!node.extra.empty()) {
                    error(node.loc, "return type: expected " + type_desc(return_type_) + ", found " +
                                        std::to_string(exprs.size()) + " results");
                    for (const Expr *expr : exprs) {
                        check_expr(*expr, true);
                    }
                } else if (!node.value) {
                    if (return_type_.kind != TypeKind::Void && return_type_.kind != TypeKind::Invalid) {
                        error(node.loc, "return type: expected " + type_desc(return_type_) + ", found void");
                    }
                } else {
                    match_return(supplied(*node.value, return_type_), return_type_);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    check_expr(*node.expr, false);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                AssignState on_true = assign_;
                AssignState on_false = assign_;
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                    on_true = cond_true_;
                    on_false = cond_false_;
                }
                const std::optional<NullGuard> guard = null_guard(node.cond.get());
                if (!guard) {
                    assign_ = on_true;
                    if (node.then_arm) {
                        check_stmt(*node.then_arm);
                    }
                    const AssignState then_out = assign_;
                    assign_ = on_false;
                    if (node.else_arm) {
                        check_stmt(*node.else_arm);
                    }
                    const AssignState else_out = node.else_arm ? assign_ : on_false;
                    assign_ = init_merge(then_out, else_out);
                } else if (guard->equal_null) {
                    assign_ = on_true;
                    if (node.then_arm) {
                        check_stmt(*node.then_arm);
                    }
                    const AssignState then_out = assign_;
                    assign_ = on_false;
                    if (node.else_arm) {
                        const bool narrowed = narrow_optional(guard->name);
                        check_stmt(*node.else_arm);
                        if (narrowed) {
                            restore_optional(guard->name);
                        }
                    }
                    const AssignState else_out = node.else_arm ? assign_ : on_false;
                    assign_ = init_merge(then_out, else_out);
                    if (node.then_arm && always_exits(node.then_arm.get())) {
                        narrow_optional(guard->name);
                    }
                } else {
                    assign_ = on_true;
                    const bool narrowed = narrow_optional(guard->name);
                    if (node.then_arm) {
                        check_stmt(*node.then_arm);
                    }
                    if (narrowed) {
                        restore_optional(guard->name);
                    }
                    const AssignState then_out = assign_;
                    assign_ = on_false;
                    if (node.else_arm) {
                        check_stmt(*node.else_arm);
                    }
                    const AssignState else_out = node.else_arm ? assign_ : on_false;
                    assign_ = init_merge(then_out, else_out);
                    if (node.else_arm && always_exits(node.else_arm.get())) {
                        narrow_optional(guard->name);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                AssignState on_true = assign_;
                AssignState on_false = assign_;
                bool can_fail = false;
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                    on_true = cond_true_;
                    on_false = cond_false_;
                    can_fail = !on_false.unreachable;
                }
                // The body sees this condition only. A store in the body is not carried to the next test.
                FlowFrame frame;
                frame.kind = FlowFrame::Kind::Loop;
                frame.entry = on_true;
                flow_frames_.push_back(std::move(frame));
                assign_ = on_true;
                const std::optional<NullGuard> guard = null_guard(node.cond.get());
                ++loop_depth_;
                const bool narrowed = guard && !guard->equal_null && narrow_optional(guard->name);
                if (node.body) {
                    check_stmt(*node.body);
                }
                if (narrowed) {
                    restore_optional(guard->name);
                }
                --loop_depth_;
                const FlowFrame done = std::move(flow_frames_.back());
                flow_frames_.pop_back();
                assign_ = init_loop_exit(done.breaks, on_false, can_fail);
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                FlowFrame frame;
                frame.kind = FlowFrame::Kind::Loop;
                flow_frames_.push_back(std::move(frame));
                ++loop_depth_;
                if (node.body) {
                    check_stmt(*node.body);
                }
                --loop_depth_;
                const FlowFrame done = std::move(flow_frames_.back());
                flow_frames_.pop_back();
                AssignState at_cond;
                at_cond.unreachable = true;
                if (!assign_.unreachable) {
                    at_cond = assign_;
                }
                for (const AssignState &continued : done.continues) {
                    at_cond = init_merge(at_cond, continued);
                }
                const bool reach = !at_cond.unreachable;
                assign_ = at_cond;
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                const bool can_fail = reach && node.cond && !cond_false_.unreachable;
                assign_ = init_loop_exit(done.breaks, node.cond ? cond_false_ : at_cond, node.cond ? can_fail : reach);
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                SwitchFrame sw;
                if (node.cond) {
                    const Type cond = check_expr(*node.cond, true);
                    if (cond.kind == TypeKind::Enum) {
                        // A TLC enum stays that enum. A C enum is accepted here and matches `i32` cases below.
                        sw.subject = cond;
                        sw.c_enum = is_c_enum(cond);
                    } else if (cond.kind != TypeKind::Invalid) {
                        expect(cond, type_int(), expr_loc(*node.cond));
                    }
                }
                const bool has_default = init_has_default(node.body.get());
                FlowFrame frame;
                frame.kind = FlowFrame::Kind::Switch;
                frame.entry = assign_;
                flow_frames_.push_back(std::move(frame));
                switches_.push_back(std::move(sw));
                // Statements before the first case are not reached. A case loads the entry state.
                assign_.unreachable = true;
                if (node.body) {
                    check_stmt(*node.body);
                }
                switches_.pop_back();
                const FlowFrame done = std::move(flow_frames_.back());
                flow_frames_.pop_back();
                AssignState out;
                out.unreachable = true;
                for (const AssignState &stopped : done.breaks) {
                    out = init_merge(out, stopped);
                }
                if (!assign_.unreachable) {
                    out = init_merge(out, assign_);
                }
                // Without `default` the switch can be skipped, so the entry state still counts.
                if (!has_default) {
                    out = init_merge(out, done.entry);
                }
                assign_ = out;
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                if (FlowFrame *sw = init_switch_frame()) {
                    const AssignState entry = sw->entry;
                    if (!assign_.unreachable) {
                        assign_ = init_merge(assign_, entry);
                    } else {
                        assign_ = entry;
                    }
                    assign_.unreachable = false;
                    for (const FlowFrame::Local &local : sw->locals) {
                        assign_.open[local.id] =
                            std::vector<uint8_t>(static_cast<std::size_t>(local.leaves), 0);
                    }
                }
                if (switches_.empty()) {
                    error(node.loc, node.is_default ? "default outside switch" : "case outside switch");
                } else if (node.is_default) {
                    if (switches_.back().has_default) {
                        error(node.loc, "redefinition");
                    } else {
                        switches_.back().has_default = true;
                    }
                } else if (node.value) {
                    const Type got = check_expr(*node.value, true);
                    if (got.kind != TypeKind::Invalid && !switch_case_matches(switches_.back(), got)) {
                        error(expr_loc(*node.value), "type mismatch");
                    } else if (switch_case_matches(switches_.back(), got)) {
                        const std::optional<int> folded = fold_switch_case(*node.value);
                        if (!folded) {
                            error(node.loc, "not supported yet");
                        } else if (!switches_.back().values.insert(*folded).second) {
                            error(expr_loc(*node.value), "redefinition");
                        }
                    }
                }
                if (node.stmt) {
                    check_stmt(*node.stmt);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                push();
                FlowFrame frame;
                frame.kind = FlowFrame::Kind::Loop;
                flow_frames_.push_back(std::move(frame));
                ++loop_depth_;
                if (node.init) {
                    check_stmt(*node.init);
                }
                AssignState on_true = assign_;
                AssignState on_false = assign_;
                bool can_fail = false;
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                    on_true = cond_true_;
                    on_false = cond_false_;
                    can_fail = !on_false.unreachable;
                }
                assign_ = on_true;
                const std::optional<NullGuard> guard = null_guard(node.cond.get());
                const bool narrowed = guard && !guard->equal_null && narrow_optional(guard->name);
                if (node.body) {
                    check_stmt(*node.body);
                }
                if (narrowed) {
                    restore_optional(guard->name);
                }
                AssignState step_state = assign_;
                for (const AssignState &continued : flow_frames_.back().continues) {
                    step_state = init_merge(step_state, continued);
                }
                assign_ = step_state;
                if (node.step) {
                    check_expr(*node.step, false);
                }
                const FlowFrame done = std::move(flow_frames_.back());
                flow_frames_.pop_back();
                assign_ = init_loop_exit(done.breaks, on_false, can_fail);
                --loop_depth_;
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                if (loop_depth_ == 0 && switches_.empty()) {
                    error(node.loc, "break outside loop");
                } else {
                    init_break();
                }
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                if (loop_depth_ == 0) {
                    error(node.loc, "continue outside loop");
                } else {
                    init_continue();
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    check_local_function(*node.decl);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                const Expr *call = node.call.get();
                int extra = 0;
                while (call != nullptr) {
                    const auto *unary = std::get_if<Expr::Unary>(&call->data);
                    if (unary == nullptr || unary->op != TokenKind::KwUnsafe || unary->expr == nullptr) {
                        break;
                    }
                    call = unary->expr.get();
                    ++extra;
                }
                if (call == nullptr || !std::holds_alternative<Expr::Call>(call->data)) {
                    if (node.call) {
                        check_expr(*node.call, false);
                    }
                    error(node.loc, "type mismatch");
                } else {
                    unsafe_depth_ += extra;
                    checking_defer_ = true;
                    check_expr(*call, false);
                    checking_defer_ = false;
                    unsafe_depth_ -= extra;
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value == nullptr || node.names.empty()) {
                    error(node.loc, "type mismatch");
                    return;
                }
                Type rhs = type_invalid();
                const bool brace = std::holds_alternative<Expr::Init>(node.value->data) &&
                                   !std::get<Expr::Init>(node.value->data).is_tuple;
                struct PartialRestore {
                    bool &flag;
                    bool saved;
                    ~PartialRestore() { flag = saved; }
                } partial{init_allow_partial_, init_allow_partial_};
                // `var p = Point{.x = 1}` may leave a field unset. A const may not.
                if (!node.is_const && brace && node.names.size() == 1) {
                    init_allow_partial_ = true;
                }
                // `var row = [_]i32{1, 2}` keeps `[2]i32`. A read of the name is that array.
                if (node.names.size() == 1) {
                    if (auto *init = std::get_if<Expr::Init>(&node.value->data); init != nullptr && init->array_literal) {
                        rhs = check_array_literal(*init);
                    } else {
                        rhs = check_expr(*node.value, true);
                    }
                } else {
                    rhs = check_expr(*node.value, true);
                }
                if (rhs.kind == TypeKind::Invalid) {
                    return;
                }
                std::vector<Type> parts;
                if (node.names.size() == 1) {
                    parts.push_back(rhs);
                } else if (rhs.kind != TypeKind::Tuple || rhs.results.size() != node.names.size()) {
                    const std::string found = rhs.kind == TypeKind::Tuple
                                                  ? std::to_string(rhs.results.size()) + " results"
                                                  : type_desc(rhs);
                    error(node.loc, "type mismatch: expected " + std::to_string(node.names.size()) + " results, found " +
                                        found);
                    return;
                } else {
                    parts = rhs.results;
                }
                bool duplicate = false;
                for (std::size_t i = 0; i < node.names.size(); ++i) {
                    for (std::size_t j = i + 1; j < node.names.size(); ++j) {
                        if (node.names[i] == node.names[j]) {
                            error(node.name_locs[i], "redefinition");
                            duplicate = true;
                        }
                    }
                }
                if (duplicate) {
                    return;
                }
                auto in_current = [&](const std::string &name) -> Symbol * {
                    if (scopes_.empty()) {
                        return nullptr;
                    }
                    const auto found = scopes_.back().symbols.find(name);
                    if (found == scopes_.back().symbols.end()) {
                        return nullptr;
                    }
                    return &found->second;
                };
                auto assignable = [&](const Symbol &symbol, SourceLoc loc, const Type &got) {
                    if (symbol.is_function || symbol.is_package || symbol.is_constant ||
                        (symbol.type.kind == TypeKind::Array && !is_slice(symbol.type))) {
                        error(loc, "not an lvalue");
                        return;
                    }
                    if (captured(symbol)) {
                        error(loc, "not supported yet");
                        return;
                    }
                    const int tracked = symbol.init_id;
                    const std::size_t before = diags_.size();
                    expect_value(got, symbol.type, *node.value, loc);
                    if (diags_.size() == before) {
                        init_mark_all(tracked);
                    }
                };
                auto declare_new = [&](std::size_t index) {
                    const SourceLoc loc = index < node.name_locs.size() ? node.name_locs[index] : node.loc;
                    Symbol symbol;
                    symbol.name = node.names[index];
                    symbol.type = parts[index];
                    symbol.is_auto = true;
                    symbol.is_immutable = node.is_const;
                    if (node.is_const) {
                        symbol.type.is_const = true;
                    }
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    symbol.loc = loc;
                    declare(std::move(symbol), loc);
                    if (!node.is_const && node.names.size() == 1 && node.value != nullptr) {
                        if (Symbol *live = in_current(node.names[index])) {
                            init_open(*live);
                            init_cover(*live, node.value.get());
                            if (const auto *created = std::get_if<Expr::New>(&node.value->data);
                                created != nullptr && created->count == nullptr && !created->count_inferred &&
                                live->type.kind == TypeKind::Pointer && live->type.pointee != nullptr) {
                                init_track_pointee(*live, *live->type.pointee, created->init.get(), false);
                            }
                        }
                    }
                };
                if (node.is_var || node.is_const) {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        if (parts[i].kind == TypeKind::Nullptr) {
                            error(loc, "type mismatch");
                        } else if (in_current(node.names[i]) != nullptr) {
                            error(loc, "redefinition");
                        } else {
                            declare_new(i);
                        }
                    }
                } else if (node.is_define) {
                    bool any_new = false;
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        if (const Symbol *existing = in_current(node.names[i])) {
                            assignable(*existing, loc, parts[i]);
                        } else {
                            any_new = true;
                        }
                    }
                    if (!any_new) {
                        error(node.loc, "type mismatch");
                    } else {
                        for (std::size_t i = 0; i < node.names.size(); ++i) {
                            if (in_current(node.names[i]) != nullptr) {
                                continue;
                            }
                            const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                            if (parts[i].kind == TypeKind::Nullptr) {
                                error(loc, "type mismatch");
                            } else {
                                declare_new(i);
                            }
                        }
                    }
                } else {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        const Symbol *existing = lookup(node.names[i]);
                        if (existing == nullptr) {
                            error(loc, "undeclared identifier");
                        } else {
                            assignable(*existing, loc, parts[i]);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                check_assert(node.loc, node.cond.get(), node.message);
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    if (!var.owner.empty()) {
                        error(var.loc, "not supported yet");
                        continue;
                    }
                    Type type = type_of_var(var);
                    Symbol symbol;
                    symbol.name = var.name;
                    symbol.type = type;
                    symbol.is_auto = true;
                    symbol.is_immutable = var.is_const_decl;
                    if (var.is_const_decl) {
                        symbol.type.is_const = true;
                    }
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    Symbol *live = nullptr;
                    if (declare(std::move(symbol), var.loc)) {
                        live = lookup(var.name);
                    }
                    int tracked = 0;
                    // Open before the initializer so `var n i32 = n` reads an uninitialized local.
                    if (live != nullptr && live->function_id != 0 && !var.is_extern && !var.is_const_decl) {
                        init_open(*live);
                        tracked = live->init_id;
                    }
                    const auto *list = var.init == nullptr ? nullptr : std::get_if<Expr::Init>(&var.init->data);
                    const bool array_list = var.is_array && !var.ptr_to_array && list != nullptr && !list->elements.empty();
                    // `var v []i32 = row` converts. `var copy [2]i32 = row` copies that array.
                    // A brace list is the other initializer. A file-scope copy is checked with the globals.
                    const bool slice = var.is_array && !var.ptr_to_array && var.array_len < 0;
                    const bool array_value = var.is_array && !var.ptr_to_array && var.array_len > 0 &&
                                             var.init != nullptr && list == nullptr;
                    if (var.is_const_decl && var.init == nullptr) {
                        error(var.loc, "type mismatch");
                    } else if (var.is_array && !var.ptr_to_array && var.init && !array_list && !slice && !array_value) {
                        error(var.loc, "not supported yet");
                    } else if (var.init) {
                        const std::size_t before = diags_.size();
                        const bool saved_new = init_new_partial_;
                        if (!var.is_const_decl) {
                            init_allow_partial_ = true;
                            if (std::holds_alternative<Expr::New>(var.init->data)) {
                                init_new_partial_ = true;
                            }
                        }
                        check_against(*var.init, type);
                        init_allow_partial_ = false;
                        init_new_partial_ = saved_new;
                        if (diags_.size() == before && live != nullptr) {
                            init_cover(*live, var.init.get());
                            if (const auto *created = std::get_if<Expr::New>(&var.init->data);
                                created != nullptr && created->count == nullptr && !created->count_inferred &&
                                live->type.kind == TypeKind::Pointer && live->type.pointee != nullptr) {
                                const Expr *brace = created->init.get();
                                init_track_pointee(*live, *live->type.pointee, brace, false);
                            } else if (const auto *unary = std::get_if<Expr::Unary>(&var.init->data);
                                       unary != nullptr && unary->op == TokenKind::Amp && unary->expr != nullptr) {
                                if (const auto *named = std::get_if<Expr::Name>(&unary->expr->data)) {
                                    if (const Symbol *target = lookup(named->text);
                                        target != nullptr && target->init_id != 0) {
                                        live->pointee_id = target->init_id;
                                    }
                                }
                            }
                        }
                    } else if (!var.is_extern && needs_nonnull_pointer(type)) {
                        error(var.loc, "type mismatch");
                    } else if (!var.is_const_decl && live != nullptr) {
                        init_cover(*live, nullptr);
                    }
                }
            }
        },
        stmt.data);
}

void Checker::check_function(const FunctionDecl &fn) {
    InitSuspend suspend(*this);
    const int saved_unsafe = unsafe_depth_;
    unsafe_depth_ = fn.unsafe ? 1 : 0;
    if (fn.results.size() > 1) {
        for (const ResultSlot &slot : fn.results) {
            if (slot.is_fn_ptr) {
                error(fn.loc, "not supported yet");
                break;
            }
        }
    }
    checking_.push_back(const_cast<FunctionDecl *>(&fn));
    push_function();
    const_cast<FunctionDecl &>(fn).function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
    const Type saved = return_type_;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    const bool saved_variadic = variadic_function_;
    loop_depth_ = 0;
    switches_.clear();
    variadic_function_ = fn.variadic;
    resolve_result_typeof(const_cast<FunctionDecl &>(fn));
    return_type_ = function_result(fn);
    // Codegen reads `return_type`. Preparing the result slot rewrites `Vec[i32]` to the specialization.
    if (fn.results.size() == 1 && !fn.results.front().is_fn_ptr) {
        FunctionDecl &written = const_cast<FunctionDecl &>(fn);
        const ResultSlot &first = written.results.front();
        written.return_type = first.type_name;
        written.type_package = first.type_package;
        written.is_struct = first.is_struct;
        written.is_enum = first.is_enum;
        written.indirections = first.indirections;
        written.optional = first.optional;
        written.const_pointee = first.const_pointee;
    }
    for (const Param &param : fn.params) {
        Type type = type_of_param(param);
        Symbol symbol;
        symbol.name = param.name;
        symbol.type = std::move(type);
        symbol.is_auto = true;
        symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
        declare(std::move(symbol), param.loc);
    }
    if (fn.body) {
        if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
            for (const auto &child : block->statements) {
                if (child) {
                    check_stmt(*child);
                }
            }
        } else {
            check_stmt(*fn.body);
        }
    }
    adopt_captures(fn);
    variadic_function_ = saved_variadic;
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    unsafe_depth_ = saved_unsafe;
    pop();
    checking_.pop_back();
}

} // namespace tlc
