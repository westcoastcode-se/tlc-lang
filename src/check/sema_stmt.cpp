// Type checking statements and function bodies.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/detail.hpp"

#include <cstdint>

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

bool Checker::is_std_builtin_file(uint32_t file_id) const { return is_compiler_builtin(find_node(file_id)); }

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

void Checker::check_for_each(const Stmt::For &node) {
    if (node.each_expr == nullptr || node.each_name.empty()) {
        error(node.loc, "type mismatch");
        return;
    }
    const Expr *collection = node.each_expr.get();
    bool want_ref = false;
    if (const auto *call = std::get_if<Expr::Call>(&node.each_expr->data)) {
        const auto *named = call->callee == nullptr ? nullptr : std::get_if<Expr::Name>(&call->callee->data);
        if (named != nullptr && named->text == "ptr_of") {
            if (call->args.size() != 1 || call->args.front() == nullptr) {
                error(call->loc, "type mismatch");
                return;
            }
            want_ref = true;
            collection = call->args.front().get();
            const bool lvalue = [&]() {
                if (const auto *name = std::get_if<Expr::Name>(&collection->data)) {
                    const Symbol *symbol = lookup(name->text);
                    return symbol != nullptr && !symbol->is_function && !symbol->is_package && !symbol->is_constant;
                }
                if (const auto *index = std::get_if<Expr::Index>(&collection->data)) {
                    return index->base != nullptr && !index->is_range;
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&collection->data)) {
                    return selector->base != nullptr;
                }
                if (const auto *star = std::get_if<Expr::Unary>(&collection->data)) {
                    return star->op == TokenKind::Star && star->expr != nullptr;
                }
                return false;
            }();
            if (!lvalue) {
                error(expr_loc(*collection), "not an lvalue");
                return;
            }
        }
    }
    const Type got = check_expr(*collection, true);
    if (got.kind == TypeKind::Invalid) {
        return;
    }
    Type element;
    if (got.kind == TypeKind::Array && got.element != nullptr && got.alias_id == 0) {
        element = *got.element;
        if (got.is_const) {
            element.is_const = true;
        }
    } else if (got.kind == TypeKind::Pointer && !got.optional && got.decayed_len > 0 && got.pointee != nullptr &&
               got.alias_id == 0) {
        element = *got.pointee;
    } else if (got.kind == TypeKind::Struct && got.struct_id >= 0 && got.struct_id < static_cast<int>(structs_.size())) {
        const StructInfo &info = structs_[static_cast<std::size_t>(got.struct_id)];
        bool hidden = false;
        const MethodInfo *len = find_visible_method(info.file_id, info.name, "len", hidden);
        const char *value_name = want_ref ? "ref" : "at";
        const MethodInfo *value = find_visible_method(info.file_id, info.name, value_name, hidden);
        if (len == nullptr || value == nullptr) {
            error(expr_loc(*collection), want_ref ? "type mismatch" : (hidden ? std::string(value_name) + " is not exported" : "undeclared identifier"));
            return;
        }
        const auto receiver_ok = [&](const MethodInfo &method, bool pointer) {
            if (!method.has_body || method.is_static || method.type.kind != TypeKind::Function || method.type.result == nullptr ||
                method.type.params.empty()) {
                return false;
            }
            const Type &receiver = method.type.params[0];
            if (pointer) {
                return receiver.kind == TypeKind::Pointer && receiver.pointee != nullptr &&
                       receiver.pointee->kind == TypeKind::Struct && receiver.pointee->struct_id == got.struct_id;
            }
            return receiver.kind == TypeKind::Struct && receiver.struct_id == got.struct_id;
        };
        if (!receiver_ok(*len, false) || len->type.params.size() != 1 || !same_type(*len->type.result, type_int())) {
            error(expr_loc(*collection), "type mismatch");
            return;
        }
        if (!receiver_ok(*value, want_ref) || value->type.params.size() != 2 ||
            !same_type(value->type.params[1], type_int())) {
            error(expr_loc(*collection), "type mismatch");
            return;
        }
        if (want_ref) {
            if (value->type.result->kind != TypeKind::Pointer || value->type.result->pointee == nullptr) {
                error(expr_loc(*collection), "type mismatch");
                return;
            }
            element = *value->type.result->pointee;
        } else {
            element = *value->type.result;
        }
    } else {
        error(expr_loc(*collection), "type mismatch");
        return;
    }
    const bool element_const = element.is_const;
    Type bound = element;
    if (want_ref) {
        bound = type_pointer(element);
        if (element_const && bound.pointee != nullptr) {
            bound.pointee->is_const = true;
        }
    } else if (!node.each_const) {
        bound.is_const = false;
    }
    const AssignState before = assign_;
    push();
    FlowFrame frame;
    frame.kind = FlowFrame::Kind::Loop;
    flow_frames_.push_back(std::move(frame));
    ++loop_depth_;
    Symbol symbol;
    symbol.name = node.each_name;
    symbol.type = bound;
    symbol.is_auto = true;
    symbol.is_immutable = node.each_const;
    if (node.each_const) {
        symbol.type.is_const = true;
    }
    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
    symbol.loc = node.each_name_loc;
    if (declare(std::move(symbol), node.each_name_loc)) {
        if (Symbol *live = lookup(node.each_name); live != nullptr && !node.each_const) {
            init_open(*live);
            init_mark_all(live->init_id);
        }
    }
    if (node.body) {
        check_stmt(*node.body);
    }
    const FlowFrame done = std::move(flow_frames_.back());
    flow_frames_.pop_back();
    assign_ = init_loop_exit(done.breaks, before, true);
    --loop_depth_;
    pop();
}

const FunctionDecl *Checker::comptime_callee(const Expr::Call &call) {
    if (call.callee == nullptr || !call.type_args.empty()) {
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&call.callee->data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->is_function || symbol->lists.size() != 1) {
        return nullptr;
    }
    const FunctionDecl *decl = symbol->lists.front().decl;
    if (decl == nullptr || !decl->is_comptime) {
        return nullptr;
    }
    return decl;
}

const FunctionDecl *Checker::macro_callee(const Expr::Call &call) {
    if (call.callee == nullptr || !call.type_args.empty()) {
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&call.callee->data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->is_function || symbol->lists.size() != 1) {
        return nullptr;
    }
    const FunctionDecl *decl = symbol->lists.front().decl;
    if (decl == nullptr || !decl->is_macro) {
        return nullptr;
    }
    return decl;
}

const FunctionDecl *Checker::generic_macro(const Expr::Call &call) {
    if (call.callee == nullptr || !call.type_args.empty()) {
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&call.callee->data);
    if (named == nullptr) {
        return nullptr;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->is_template) {
        return nullptr;
    }
    const FunctionDecl *found = nullptr;
    bool clash = false;
    const auto consider = [&](const FunctionDecl *fn) {
        if (fn == nullptr || !fn->is_macro || fn->type_params.empty() || fn->params.size() != call.args.size()) {
            return;
        }
        if (found != nullptr) {
            clash = true;
        }
        found = fn;
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
    return found;
}

namespace {

// The name bound by `for (var i = ...)` or `for (var item : ...)`.
std::string loop_index_name(const Stmt::For &loop) {
    if (loop.each) {
        return loop.each_name;
    }
    if (loop.init == nullptr) {
        return {};
    }
    if (const auto *bind = std::get_if<Stmt::Bind>(&loop.init->data)) {
        if (bind->names.size() == 1) {
            return bind->names.front();
        }
    }
    if (const auto *decl = std::get_if<Stmt::Decl>(&loop.init->data)) {
        if (decl->vars.size() == 1) {
            return decl->vars.front().name;
        }
    }
    return {};
}

void apply_names(std::unique_ptr<Expr> &slot, const std::vector<std::string> &names,
                 const std::vector<const Expr *> &exprs, const std::unordered_set<std::string> &skip) {
    if (slot == nullptr) {
        return;
    }
    if (skip.empty()) {
        replace_names(slot, names, exprs);
        return;
    }
    std::vector<std::string> kept_names;
    std::vector<const Expr *> kept;
    for (std::size_t i = 0; i < names.size() && i < exprs.size(); ++i) {
        if (names[i].empty() || exprs[i] == nullptr || skip.contains(names[i])) {
            continue;
        }
        kept_names.push_back(names[i]);
        kept.push_back(exprs[i]);
    }
    replace_names(slot, kept_names, kept);
}

// `skip` names are bindings of this statement. They hide the caller's replacements.
void subst_in_stmt(Stmt &stmt, const std::vector<std::string> &names, const std::vector<const Expr *> &exprs,
                   const std::unordered_set<std::string> &skip) {
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (auto &child : node.statements) {
                    if (child != nullptr) {
                        subst_in_stmt(*child, names, exprs, skip);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                apply_names(node.value, names, exprs, skip);
                for (auto &extra : node.extra) {
                    apply_names(extra, names, exprs, skip);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                apply_names(node.expr, names, exprs, skip);
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                apply_names(node.cond, names, exprs, skip);
                if (node.then_arm != nullptr) {
                    subst_in_stmt(*node.then_arm, names, exprs, skip);
                }
                if (node.else_arm != nullptr) {
                    subst_in_stmt(*node.else_arm, names, exprs, skip);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                apply_names(node.cond, names, exprs, skip);
                if (node.body != nullptr) {
                    subst_in_stmt(*node.body, names, exprs, skip);
                }
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                if (node.body != nullptr) {
                    subst_in_stmt(*node.body, names, exprs, skip);
                }
                apply_names(node.cond, names, exprs, skip);
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                // The initializer is outside the index binding. `var i = n` reads the outer `n`.
                std::unordered_set<std::string> inner = skip;
                if (node.each) {
                    apply_names(node.each_expr, names, exprs, skip);
                    if (!node.each_name.empty()) {
                        inner.insert(node.each_name);
                    }
                } else if (node.init != nullptr) {
                    subst_in_stmt(*node.init, names, exprs, skip);
                    const std::string index = loop_index_name(node);
                    if (!index.empty()) {
                        inner.insert(index);
                    }
                }
                apply_names(node.cond, names, exprs, inner);
                apply_names(node.step, names, exprs, inner);
                if (node.body != nullptr) {
                    subst_in_stmt(*node.body, names, exprs, inner);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                apply_names(node.cond, names, exprs, skip);
                if (node.body != nullptr) {
                    subst_in_stmt(*node.body, names, exprs, skip);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                apply_names(node.value, names, exprs, skip);
                if (node.stmt != nullptr) {
                    subst_in_stmt(*node.stmt, names, exprs, skip);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (VarDecl &var : node.vars) {
                    apply_names(var.init, names, exprs, skip);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                apply_names(node.value, names, exprs, skip);
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                apply_names(node.call, names, exprs, skip);
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                apply_names(node.cond, names, exprs, skip);
            }
        },
        stmt.data);
}

const Expr *bind_value(const Stmt &stmt) {
    if (const auto *bind = std::get_if<Stmt::Bind>(&stmt.data)) {
        if (bind->names.size() == 1) {
            return bind->value.get();
        }
    }
    if (const auto *decl = std::get_if<Stmt::Decl>(&stmt.data)) {
        if (decl->vars.size() == 1) {
            return decl->vars.front().init.get();
        }
    }
    return nullptr;
}

bool index_name_expr(const Expr *expr, const std::string &index) {
    if (expr == nullptr || index.empty()) {
        return false;
    }
    const auto *named = std::get_if<Expr::Name>(&expr->data);
    return named != nullptr && named->text == index;
}

std::unique_ptr<Expr> int_literal(int value, SourceLoc loc) {
    if (value < 0) {
        if (value == -2147483647 - 1) {
            return nullptr;
        }
        auto inner = int_literal(-value, loc);
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

std::unique_ptr<Expr> element_at(const Expr &collection, int index, SourceLoc loc) {
    std::unique_ptr<Expr> at = int_literal(index, loc);
    if (at == nullptr) {
        return nullptr;
    }
    auto expr = std::make_unique<Expr>();
    Expr::Index node;
    node.loc = loc;
    node.base = clone_expr(collection);
    node.index = std::move(at);
    expr->data = std::move(node);
    return expr;
}

// `ptr_of(items)` is that call's argument. Anything else is the collection itself.
const Expr *ptr_of_collection(const Expr &each) {
    const auto *call = std::get_if<Expr::Call>(&each.data);
    if (call == nullptr || call->callee == nullptr || call->args.size() != 1 || call->args.front() == nullptr) {
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&call->callee->data);
    if (named == nullptr || named->text != "ptr_of") {
        return nullptr;
    }
    return call->args.front().get();
}

std::unique_ptr<Expr> element_ref(const Expr &collection, int index, SourceLoc loc) {
    std::unique_ptr<Expr> at = element_at(collection, index, loc);
    if (at == nullptr) {
        return nullptr;
    }
    auto expr = std::make_unique<Expr>();
    Expr::Unary node;
    node.loc = loc;
    node.op = TokenKind::Amp;
    node.expr = std::move(at);
    expr->data = std::move(node);
    return expr;
}

std::unique_ptr<Expr> string_literal(std::string text, SourceLoc loc) {
    auto expr = std::make_unique<Expr>();
    Expr::String node;
    node.loc = loc;
    node.text = std::move(text);
    expr->data = std::move(node);
    return expr;
}

} // namespace

std::unique_ptr<Expr> Checker::comptime_argument(const Expr &arg, const Param &param) {
    if (param.is_tuple) {
        if (!argument_is_comptime(arg)) {
            return nullptr;
        }
        return clone_expr(arg);
    }
    const Type type = type_of_param(param);
    const SourceLoc loc = expr_loc(arg);
    if (plain_int_constant(type)) {
        if (const std::optional<int> value = fold_int(arg)) {
            return int_literal(*value, loc);
        }
        return nullptr;
    }
    if (!slice_of_const_u8(type)) {
        return nullptr;
    }
    std::unordered_set<std::string> seen;
    const std::optional<std::string> text = constant_string(arg, seen);
    if (!text) {
        return nullptr;
    }
    return string_literal(*text, loc);
}

void Checker::fold_format_expr(std::unique_ptr<Expr> &slot) {
    if (slot == nullptr) {
        return;
    }
    if (const auto *call = std::get_if<Expr::Call>(&slot->data)) {
        if (is_builtin_fmt(*call)) {
            const std::optional<std::string> text = format_call(*call);
            if (!text) {
                error(call->loc, "not supported yet");
                return;
            }
            slot = string_literal(*text, call->loc);
            return;
        }
    }
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Unary> || std::is_same_v<T, Expr::Sizeof> ||
                          std::is_same_v<T, Expr::Typeof> || std::is_same_v<T, Expr::Cast>) {
                fold_format_expr(node.expr);
            } else if constexpr (std::is_same_v<T, Expr::Binary> || std::is_same_v<T, Expr::Assign>) {
                fold_format_expr(node.lhs);
                fold_format_expr(node.rhs);
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                fold_format_expr(node.callee);
                for (auto &arg : node.args) {
                    fold_format_expr(arg);
                }
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                fold_format_expr(node.base);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                fold_format_expr(node.base);
                fold_format_expr(node.index);
                fold_format_expr(node.end);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                for (auto &field : node.fields) {
                    fold_format_expr(field.value);
                }
                for (auto &element : node.elements) {
                    fold_format_expr(element);
                }
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                fold_format_expr(node.allocator);
                fold_format_expr(node.count);
                fold_format_expr(node.init);
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                fold_format_expr(node.allocator);
                fold_format_expr(node.expr);
            }
        },
        slot->data);
}

void Checker::expand_generic_macro(const Expr::Call &call, const FunctionDecl &fn,
                                   std::vector<std::unique_ptr<Stmt>> &out) {
    if (fn.type_params.empty() || fn.body == nullptr || call.args.size() != fn.params.size()) {
        error(call.loc, "not supported yet");
        return;
    }
    std::vector<Type> arg_types;
    arg_types.reserve(call.args.size());
    for (const auto &arg : call.args) {
        if (arg == nullptr) {
            error(call.loc, "argument count");
            return;
        }
        const Type got = check_expr(*arg, true);
        if (got.kind == TypeKind::Invalid) {
            return;
        }
        arg_types.push_back(got);
    }
    if (!comptime_arguments(fn, call)) {
        error(call.loc, "not supported yet");
        return;
    }
    const std::optional<std::vector<SpecArg>> inferred = infer_generic_bounds(fn, call, arg_types);
    if (!inferred) {
        return;
    }
    FunctionDecl spec = clone_function(fn);
    spec.type_params.clear();
    spec.is_macro = true;
    substitute_function(spec, fn.type_params, *inferred);
    expand_macro_call(call, spec, out);
}

void Checker::expand_macro_stmt(std::unique_ptr<Stmt> stmt, const FunctionDecl &fn,
                                const std::vector<std::string> &names, const std::vector<const Expr *> &exprs,
                                std::vector<std::unique_ptr<Stmt>> &out, bool substitute) {
    if (stmt == nullptr) {
        return;
    }
    if (substitute) {
        const std::unordered_set<std::string> skip;
        subst_in_stmt(*stmt, names, exprs, skip);
    }
    if (std::holds_alternative<Stmt::Return>(stmt->data)) {
        return;
    }
    if (auto *expr = std::get_if<Stmt::Expression>(&stmt->data)) {
        if (expr->emit && expr->expr != nullptr) {
            const std::size_t before = diags_.size();
            fold_format_expr(expr->expr);
            if (diags_.size() != before) {
                return;
            }
            expr->emit = false;
            append_statement(std::move(stmt), out);
            return;
        }
    }
    if (auto *block = std::get_if<Stmt::Block>(&stmt->data)) {
        std::vector<std::unique_ptr<Stmt>> children = std::move(block->statements);
        for (auto &child : children) {
            expand_macro_stmt(std::move(child), fn, names, exprs, out, false);
        }
        return;
    }
    if (auto *loop = std::get_if<Stmt::For>(&stmt->data)) {
        if (loop->emit) {
            loop->emit = false;
            append_statement(std::move(stmt), out);
            return;
        }
        unroll_inline_for(*stmt, fn, names, exprs, out);
        return;
    }
    if (auto *branch = std::get_if<Stmt::If>(&stmt->data)) {
        if (branch->emit) {
            branch->emit = false;
            append_statement(std::move(stmt), out);
            return;
        }
        if (branch->cond == nullptr) {
            error(branch->loc, "not supported yet");
            return;
        }
        const bool saved_quiet = quiet_;
        const AssignState saved_assign = assign_;
        const AssignState saved_true = cond_true_;
        const AssignState saved_false = cond_false_;
        quiet_ = true;
        const Type got = check_expr(*branch->cond, true);
        quiet_ = saved_quiet;
        assign_ = saved_assign;
        cond_true_ = saved_true;
        cond_false_ = saved_false;
        const std::optional<int> value = fold_int(*branch->cond);
        if (!value || (got.kind != TypeKind::Bool && got.kind != TypeKind::Int)) {
            error(branch->loc, "not supported yet");
            return;
        }
        std::unique_ptr<Stmt> arm = *value != 0 ? std::move(branch->then_arm) : std::move(branch->else_arm);
        if (arm != nullptr) {
            expand_macro_stmt(std::move(arm), fn, names, exprs, out, false);
        }
        return;
    }
    if (auto *loop = std::get_if<Stmt::While>(&stmt->data)) {
        if (!loop->emit) {
            error(loop->loc, "not supported yet");
            return;
        }
        loop->emit = false;
        append_statement(std::move(stmt), out);
        return;
    }
    if (auto *loop = std::get_if<Stmt::DoWhile>(&stmt->data)) {
        if (!loop->emit) {
            error(loop->loc, "not supported yet");
            return;
        }
        loop->emit = false;
        append_statement(std::move(stmt), out);
        return;
    }
    const SourceLoc loc = std::visit(
        [](const auto &node) -> SourceLoc {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, Stmt::Func>) {
                return node.decl != nullptr ? node.decl->loc : SourceLoc{};
            } else {
                return node.loc;
            }
        },
        stmt->data);
    error(loc, "not supported yet");
}

void Checker::unroll_inline_for(const Stmt &stmt, const FunctionDecl &fn, const std::vector<std::string> &names,
                                const std::vector<const Expr *> &exprs, std::vector<std::unique_ptr<Stmt>> &out) {
    const auto *loop = std::get_if<Stmt::For>(&stmt.data);
    if (loop == nullptr || loop->body == nullptr) {
        error(loop != nullptr ? loop->loc : SourceLoc{}, "not supported yet");
        return;
    }
    if (loop->each) {
        if (loop->each_expr == nullptr || loop->each_name.empty()) {
            error(loop->loc, "not supported yet");
            return;
        }
        const Expr *collection = loop->each_expr.get();
        bool want_ref = false;
        if (const Expr *inner = ptr_of_collection(*loop->each_expr)) {
            want_ref = true;
            collection = inner;
            const bool lvalue = [&]() {
                if (const auto *name = std::get_if<Expr::Name>(&collection->data)) {
                    const Symbol *symbol = lookup(name->text);
                    return symbol != nullptr && !symbol->is_function && !symbol->is_package && !symbol->is_constant;
                }
                if (const auto *index = std::get_if<Expr::Index>(&collection->data)) {
                    return index->base != nullptr && !index->is_range;
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&collection->data)) {
                    return selector->base != nullptr;
                }
                if (const auto *star = std::get_if<Expr::Unary>(&collection->data)) {
                    return star->op == TokenKind::Star && star->expr != nullptr;
                }
                return false;
            }();
            if (!lvalue) {
                error(expr_loc(*collection), "not an lvalue");
                return;
            }
        }
        const std::optional<int> count = known_len(*collection);
        // A macro unrolls a bounded compile-time loop. More than 32 trips stays out.
        constexpr int kMaxEach = 32;
        if (!count || *count < 0 || *count > kMaxEach) {
            error(loop->loc, "not supported yet");
            return;
        }
        const auto *each_body = std::get_if<Stmt::Block>(&loop->body->data);
        if (each_body == nullptr) {
            error(loop->loc, "not supported yet");
            return;
        }
        for (int index = 0; index < *count; ++index) {
            const std::unique_ptr<Expr> element =
                want_ref ? element_ref(*collection, index, loop->loc) : element_at(*collection, index, loop->loc);
            if (element == nullptr) {
                error(loop->loc, "not supported yet");
                return;
            }
            const std::vector<std::string> item_names{loop->each_name};
            const std::vector<const Expr *> item_exprs{element.get()};
            for (const auto &child : each_body->statements) {
                if (child == nullptr) {
                    continue;
                }
                std::unique_ptr<Stmt> piece = clone_stmt(*child);
                const std::unordered_set<std::string> skip;
                subst_in_stmt(*piece, item_names, item_exprs, skip);
                expand_macro_stmt(std::move(piece), fn, names, exprs, out, false);
            }
        }
        return;
    }
    // Parameters were substituted before this call. The index still hides them.
    const std::string index = loop_index_name(*loop);
    const Expr *low_expr = loop->init != nullptr ? bind_value(*loop->init) : nullptr;
    if (index.empty() || low_expr == nullptr || loop->cond == nullptr || loop->step == nullptr) {
        error(loop->loc, "not supported yet");
        return;
    }
    const std::optional<int> low = fold_int(*low_expr);
    const auto *cmp = std::get_if<Expr::Binary>(&loop->cond->data);
    const auto *step_assign = std::get_if<Expr::Assign>(&loop->step->data);
    if (!low || cmp == nullptr || cmp->op != TokenKind::Lt || !index_name_expr(cmp->lhs.get(), index) ||
        cmp->rhs == nullptr || step_assign == nullptr || !index_name_expr(step_assign->lhs.get(), index) ||
        step_assign->rhs == nullptr) {
        error(loop->loc, "not supported yet");
        return;
    }
    const std::optional<int> high = fold_int(*cmp->rhs);
    const auto *plus = std::get_if<Expr::Binary>(&step_assign->rhs->data);
    if (!high || plus == nullptr || plus->op != TokenKind::Plus || !index_name_expr(plus->lhs.get(), index) ||
        plus->rhs == nullptr) {
        error(loop->loc, "not supported yet");
        return;
    }
    const std::optional<int> step = fold_int(*plus->rhs);
    if (!step || *step <= 0) {
        error(loop->loc, "not supported yet");
        return;
    }
    // A macro unrolls a bounded compile-time loop. More than 32 trips stays out.
    constexpr int kMaxInline = 32;
    int trips = 0;
    for (int64_t cursor = *low; cursor < static_cast<int64_t>(*high); cursor += *step) {
        if (trips >= kMaxInline || cursor > static_cast<int64_t>(2147483647) - *step) {
            error(loop->loc, "not supported yet");
            return;
        }
        ++trips;
    }
    const auto *block = std::get_if<Stmt::Block>(&loop->body->data);
    if (block == nullptr) {
        error(loop->loc, "not supported yet");
        return;
    }
    for (int64_t cursor = *low; cursor < static_cast<int64_t>(*high); cursor += *step) {
        const std::unique_ptr<Expr> literal = int_literal(static_cast<int>(cursor), loop->loc);
        if (literal == nullptr) {
            error(loop->loc, "not supported yet");
            return;
        }
        const std::vector<std::string> index_names{index};
        const std::vector<const Expr *> index_exprs{literal.get()};
        for (const auto &child : block->statements) {
            if (child == nullptr) {
                continue;
            }
            std::unique_ptr<Stmt> piece = clone_stmt(*child);
            const std::unordered_set<std::string> skip;
            subst_in_stmt(*piece, index_names, index_exprs, skip);
            expand_macro_stmt(std::move(piece), fn, names, exprs, out, false);
        }
    }
}

void Checker::expand_macro_call(const Expr::Call &call, const FunctionDecl &fn,
                                std::vector<std::unique_ptr<Stmt>> &out) {
    if (comptime_expanding_.contains(&fn) || fn.variadic || !fn.type_params.empty() || fn.body == nullptr) {
        error(call.loc, "not supported yet");
        return;
    }
    if (call.args.size() != fn.params.size()) {
        error(call.loc, "argument count");
        return;
    }
    const std::size_t before = diags_.size();
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (call.args[i] == nullptr) {
            error(call.loc, "argument count");
            return;
        }
        check_call_arg(*call.args[i], type_of_param(fn.params[i]));
    }
    if (diags_.size() != before) {
        return;
    }
    std::vector<std::unique_ptr<Expr>> evaluated(fn.params.size());
    std::vector<std::string> names(fn.params.size());
    std::vector<const Expr *> replacement(fn.params.size());
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        names[i] = fn.params[i].name;
        if (!fn.params[i].is_comptime) {
            replacement[i] = call.args[i].get();
            continue;
        }
        evaluated[i] = comptime_argument(*call.args[i], fn.params[i]);
        if (evaluated[i] == nullptr) {
            error(expr_loc(*call.args[i]), "not supported yet");
            continue;
        }
        replacement[i] = evaluated[i].get();
    }
    if (diags_.size() != before) {
        return;
    }
    comptime_expanding_.insert(&fn);
    struct Leave {
        std::unordered_set<const FunctionDecl *> &set;
        const FunctionDecl *fn;
        ~Leave() { set.erase(fn); }
    } leave{comptime_expanding_, &fn};
    const auto *body = std::get_if<Stmt::Block>(&fn.body->data);
    if (body == nullptr) {
        error(call.loc, "not supported yet");
        return;
    }
    for (const auto &stmt : body->statements) {
        if (stmt == nullptr) {
            continue;
        }
        expand_macro_stmt(clone_stmt(*stmt), fn, names, replacement, out, true);
    }
}

void Checker::append_statement(std::unique_ptr<Stmt> stmt, std::vector<std::unique_ptr<Stmt>> &out) {
    if (stmt == nullptr) {
        return;
    }
    if (comptime_depth_ == 0) {
        const auto *expr_stmt = std::get_if<Stmt::Expression>(&stmt->data);
        const Expr::Call *call = expr_stmt != nullptr && !expr_stmt->emit && expr_stmt->expr != nullptr
                                     ? std::get_if<Expr::Call>(&expr_stmt->expr->data)
                                     : nullptr;
        if (call != nullptr) {
            if (const FunctionDecl *fn = macro_callee(*call)) {
                expand_macro_call(*call, *fn, out);
                return;
            }
            if (const FunctionDecl *fn = generic_macro(*call)) {
                expand_generic_macro(*call, *fn, out);
                return;
            }
        }
    }
    check_stmt(*stmt);
    out.push_back(std::move(stmt));
}

void Checker::check_statements(std::vector<std::unique_ptr<Stmt>> &statements) {
    std::vector<std::unique_ptr<Stmt>> next;
    for (auto &child : statements) {
        append_statement(std::move(child), next);
    }
    statements = std::move(next);
}

void Checker::check_stmt(const Stmt &stmt) {
    if (macro_depth_ > 0) {
        const bool allowed = std::visit(
            [](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Stmt::Block> || std::is_same_v<T, Stmt::Return>) {
                    return true;
                } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                    return node.emit && node.expr != nullptr;
                } else if constexpr (std::is_same_v<T, Stmt::For> || std::is_same_v<T, Stmt::If>) {
                    return true;
                } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                    return node.emit;
                } else {
                    return false;
                }
            },
            stmt.data);
        if (!allowed) {
            const SourceLoc loc = std::visit(
                [](const auto &node) -> SourceLoc {
                    using Node = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<Node, Stmt::Func>) {
                        return node.decl != nullptr ? node.decl->loc : SourceLoc{};
                    } else {
                        return node.loc;
                    }
                },
                stmt.data);
            error(loc, "not supported yet");
            return;
        }
    }
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                push();
                check_statements(const_cast<Stmt::Block &>(node).statements);
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                // Learning an omitted result. The real check of this body matches the type afterwards.
                if (collect_returns_) {
                    // A `return` that is not reached does not choose the type.
                    if (!assign_.unreachable) {
                        InferredReturn item;
                        item.loc = node.loc;
                        if (node.value == nullptr && node.extra.empty()) {
                            item.bare = true;
                        } else {
                            if (node.value != nullptr) {
                                item.parts.push_back(check_expr(*node.value, true));
                            }
                            for (const auto &extra : node.extra) {
                                if (extra != nullptr) {
                                    item.parts.push_back(check_expr(*extra, true));
                                }
                            }
                        }
                        if (!checking_.empty() && node.extra.empty() && node.value != nullptr) {
                            item.callee = call_target(*node.value);
                            item.self = item.callee == checking_.back();
                        }
                        inferred_returns_.push_back(std::move(item));
                    }
                    assign_.unreachable = true;
                    return;
                }
                const bool reached = !assign_.unreachable;
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
                if (reached && !checking_.empty()) {
                    ReachedReturn item;
                    item.loc = node.loc;
                    if (node.extra.empty() && node.value != nullptr) {
                        item.callee = call_target(*node.value);
                    }
                    item.self = item.callee == checking_.back();
                    reached_returns_.push_back(std::move(item));
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.emit) {
                    // The copy at the call site is typechecked. This statement is the generator.
                    // A runtime loop is not unrolled, so `emit` there has nowhere to go.
                    if (macro_depth_ == 0 || plain_loop_depth_ > 0) {
                        error(node.loc, "not supported yet");
                    }
                } else if (node.expr) {
                    check_expr(*node.expr, false);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                const int saved_macro = macro_depth_;
                if (node.emit) {
                    if (saved_macro == 0) {
                        error(node.loc, "not supported yet");
                        return;
                    }
                    // The body is pasted. Nested `for` and `if` run later.
                    macro_depth_ = 0;
                } else if (macro_depth_ > 0) {
                    // The arm is chosen while expanding. The call site checks the arm that remains.
                    return;
                }
                struct RestoreMacro {
                    int &slot;
                    int saved;
                    ~RestoreMacro() { slot = saved; }
                } restore{macro_depth_, saved_macro};
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
                const int saved_macro = macro_depth_;
                if (node.emit) {
                    if (saved_macro == 0) {
                        error(node.loc, "not supported yet");
                        return;
                    }
                    // The loop is pasted. Its body is ordinary code.
                    macro_depth_ = 0;
                }
                struct RestoreMacro {
                    int &slot;
                    int saved;
                    ~RestoreMacro() { slot = saved; }
                } restore{macro_depth_, saved_macro};
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
                ++plain_loop_depth_;
                const bool narrowed = guard && !guard->equal_null && narrow_optional(guard->name);
                if (node.body) {
                    check_stmt(*node.body);
                }
                if (narrowed) {
                    restore_optional(guard->name);
                }
                --plain_loop_depth_;
                --loop_depth_;
                const FlowFrame done = std::move(flow_frames_.back());
                flow_frames_.pop_back();
                assign_ = init_loop_exit(done.breaks, on_false, can_fail);
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                const int saved_macro = macro_depth_;
                if (node.emit) {
                    if (saved_macro == 0) {
                        error(node.loc, "not supported yet");
                        return;
                    }
                    // The loop is pasted. Its body is ordinary code.
                    macro_depth_ = 0;
                }
                struct RestoreMacro {
                    int &slot;
                    int saved;
                    ~RestoreMacro() { slot = saved; }
                } restore{macro_depth_, saved_macro};
                FlowFrame frame;
                frame.kind = FlowFrame::Kind::Loop;
                flow_frames_.push_back(std::move(frame));
                ++loop_depth_;
                ++plain_loop_depth_;
                if (node.body) {
                    check_stmt(*node.body);
                }
                --plain_loop_depth_;
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
                const int saved_macro = macro_depth_;
                if (node.emit) {
                    if (saved_macro == 0) {
                        error(node.loc, "not supported yet");
                        return;
                    }
                    // The loop is pasted. Its body is ordinary code.
                    macro_depth_ = 0;
                }
                struct RestoreMacro {
                    int &slot;
                    int saved;
                    ~RestoreMacro() { slot = saved; }
                } restore{macro_depth_, saved_macro};
                // A macro unrolls `for (var item : items)` at the call. The body is checked there.
                if (macro_depth_ > 0 && node.each) {
                    return;
                }
                if (node.each) {
                    check_for_each(node);
                    return;
                }
                push();
                FlowFrame frame;
                frame.kind = FlowFrame::Kind::Loop;
                flow_frames_.push_back(std::move(frame));
                ++loop_depth_;
                const bool plain = macro_depth_ == 0;
                if (plain) {
                    ++plain_loop_depth_;
                }
                if (node.init) {
                    // The binding is the loop header, not a statement the macro emits.
                    const int saved_macro = macro_depth_;
                    macro_depth_ = 0;
                    check_stmt(*node.init);
                    macro_depth_ = saved_macro;
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
                if (plain) {
                    --plain_loop_depth_;
                }
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
                // `var f = id` names the generic. The call specializes it. There is no slot.
                if (node.names.size() == 1 && (node.is_var || node.is_define || node.is_const)) {
                    const Symbol *source = nullptr;
                    if (const auto *named = std::get_if<Expr::Name>(&node.value->data)) {
                        source = lookup(named->text);
                    } else if (const auto *selector = std::get_if<Expr::Selector>(&node.value->data)) {
                        const auto *base =
                            selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
                        const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                        if (package != nullptr && package->is_package) {
                            source = find_export(package->package_file, selector->field);
                        }
                    }
                    if (source != nullptr && source->is_template && source->is_function &&
                        (!source->fn_templates.empty() || source->fn_template != nullptr)) {
                        const SourceLoc loc = node.name_locs.empty() ? node.loc : node.name_locs.front();
                        // A call of this alias is rewritten to a name in this package. Another package stays `pkg.id(...)`.
                        const uint32_t owner = source->package_file != 0 ? source->package_file : file_id_;
                        bool here = owner == file_id_;
                        if (!here) {
                            for (const uint32_t id : files_in_package(file_id_)) {
                                if (id == owner) {
                                    here = true;
                                    break;
                                }
                            }
                        }
                        if (!here) {
                            SourceLoc at = loc;
                            if (const auto *named = std::get_if<Expr::Name>(&node.value->data)) {
                                at = named->loc;
                            } else if (const auto *selector = std::get_if<Expr::Selector>(&node.value->data)) {
                                at = selector->loc;
                            }
                            error(at, "not supported yet");
                            return;
                        }
                        if (scopes_.empty()) {
                            error(loc, "type mismatch");
                            return;
                        }
                        const bool exists = scopes_.back().symbols.contains(node.names[0]);
                        if (node.is_const && exists) {
                            error(loc, "redefinition");
                            return;
                        }
                        Symbol symbol;
                        symbol.name = node.names[0];
                        symbol.is_function = true;
                        symbol.is_template = true;
                        symbol.fn_template = source->fn_template;
                        symbol.fn_templates = source->fn_templates;
                        symbol.package_file = source->package_file != 0 ? source->package_file : file_id_;
                        symbol.has_body = source->has_body;
                        symbol.unsafe_fn = source->unsafe_fn;
                        symbol.type = type_function(type_invalid(), {});
                        symbol.function_id = scopes_.back().function_id;
                        symbol.loc = loc;
                        symbol.is_auto = true;
                        symbol.is_immutable = node.is_const;
                        if (exists) {
                            shadow(std::move(symbol), loc);
                        } else {
                            declare(std::move(symbol), loc);
                        }
                        return;
                    }
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
                // A `new` pointee and an `&name` address keep the same field bits as a written `var p *T`.
                auto note_pointer = [&](Symbol &live) {
                    if (node.value == nullptr || live.type.kind != TypeKind::Pointer || live.type.pointee == nullptr) {
                        return;
                    }
                    if (const auto *created = std::get_if<Expr::New>(&node.value->data);
                        created != nullptr && created->count == nullptr && !created->count_inferred) {
                        init_track_pointee(live, *live.type.pointee, created->init.get(), false);
                        return;
                    }
                    const auto *unary = std::get_if<Expr::Unary>(&node.value->data);
                    if (unary == nullptr || unary->op != TokenKind::Amp || unary->expr == nullptr) {
                        return;
                    }
                    const auto *named = std::get_if<Expr::Name>(&unary->expr->data);
                    if (named == nullptr) {
                        return;
                    }
                    if (const Symbol *target = lookup(named->text); target != nullptr && target->init_id != 0) {
                        live.pointee_id = target->init_id;
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
                            note_pointer(*live);
                        }
                    }
                };
                // `var` and `:=` hide an earlier binding in this block. The value above
                // was checked first, so it still names that binding. `const` does not hide.
                auto hide_current = [&](std::size_t index) {
                    const SourceLoc loc = index < node.name_locs.size() ? node.name_locs[index] : node.loc;
                    if (parts[index].kind == TypeKind::Nullptr) {
                        error(loc, "type mismatch");
                        return;
                    }
                    if (in_current(node.names[index]) == nullptr) {
                        declare_new(index);
                        return;
                    }
                    Symbol symbol;
                    symbol.name = node.names[index];
                    symbol.type = parts[index];
                    symbol.is_auto = true;
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    symbol.loc = loc;
                    shadow(std::move(symbol), loc);
                    if (node.names.size() == 1 && node.value != nullptr) {
                        if (Symbol *live = in_current(node.names[index])) {
                            init_open(*live);
                            init_cover(*live, node.value.get());
                            note_pointer(*live);
                        }
                    }
                };
                if (node.is_const) {
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
                } else if (node.is_var || node.is_define) {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        hide_current(i);
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
                    // Same-block `var n i32 = n + 1` replaces the name. An outer `n` stays, and the
                    // initializer still reads it. A name with no earlier binding is opened first.
                    const bool same_block =
                        !var.is_const_decl && !scopes_.empty() && scopes_.back().symbols.contains(var.name);
                    const bool outer = !var.is_const_decl && !same_block && lookup(var.name) != nullptr;
                    const bool sees_previous = same_block || outer;
                    const auto *list = var.init == nullptr ? nullptr : std::get_if<Expr::Init>(&var.init->data);
                    const bool array_list = var.is_array && !var.ptr_to_array && list != nullptr && !list->elements.empty();
                    const bool slice = var.is_array && !var.ptr_to_array && var.array_len < 0;
                    const bool array_value = var.is_array && !var.ptr_to_array && var.array_len > 0 &&
                                             var.init != nullptr && list == nullptr;
                    const bool plain_init = var.init != nullptr && type.kind != TypeKind::Invalid &&
                                            !(var.is_array && !var.ptr_to_array && !array_list && !slice && !array_value);
                    // The previous binding is still visible while this initializer is checked.
                    bool init_ready = false;
                    if (sees_previous && plain_init) {
                        const std::size_t before = diags_.size();
                        const bool saved_new = init_new_partial_;
                        init_allow_partial_ = true;
                        if (std::holds_alternative<Expr::New>(var.init->data)) {
                            init_new_partial_ = true;
                        }
                        check_against(*var.init, type);
                        init_allow_partial_ = false;
                        init_new_partial_ = saved_new;
                        init_ready = diags_.size() == before;
                        if (same_block) {
                            scopes_.back().symbols.erase(var.name);
                        }
                    } else if (same_block) {
                        scopes_.back().symbols.erase(var.name);
                    }
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
                    // A name that hides an earlier binding was checked above, while that binding was visible.
                    if (live != nullptr && live->function_id != 0 && !var.is_extern && !var.is_const_decl) {
                        init_open(*live);
                        tracked = live->init_id;
                    }
                    if (var.is_const_decl && var.init == nullptr) {
                        error(var.loc, "type mismatch");
                    } else if (var.is_array && !var.ptr_to_array && var.init && !array_list && !slice && !array_value) {
                        error(var.loc, "not supported yet");
                    } else if (var.init && type.kind != TypeKind::Invalid) {
                        const std::size_t before = diags_.size();
                        // `sees_previous && plain_init` was checked while the previous binding was visible.
                        if (!(sees_previous && plain_init)) {
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
                        }
                        if ((init_ready || (!(sees_previous && plain_init) && diags_.size() == before)) && live != nullptr) {
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
    struct Depths {
        int &comptime_slot;
        int &macro_slot;
        int &loop_slot;
        int saved_comptime;
        int saved_macro;
        int saved_loop;
        ~Depths() {
            comptime_slot = saved_comptime;
            macro_slot = saved_macro;
            loop_slot = saved_loop;
        }
    } depths{comptime_depth_, macro_depth_, plain_loop_depth_, comptime_depth_, macro_depth_, plain_loop_depth_};
    comptime_depth_ = (fn.is_comptime || fn.is_macro) ? 1 : 0;
    macro_depth_ = fn.is_macro ? 1 : 0;
    plain_loop_depth_ = 0;
    auto saved_reached = std::move(reached_returns_);
    reached_returns_.clear();
    // A quiet walk may be learning the caller's result. This body is not part of that list.
    const bool saved_collect = collect_returns_;
    collect_returns_ = false;
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
    // The signature already reported a trait result. Building it again must not repeat that.
    const bool saved_quiet = quiet_;
    quiet_ = true;
    return_type_ = function_result(fn);
    quiet_ = saved_quiet;
    // The result was already rejected. Do not also report the returned expression.
    if (mentions_trait(return_type_)) {
        return_type_ = type_invalid();
    }
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
        const bool saved_param_quiet = quiet_;
        quiet_ = true;
        Type type = type_of_param(param);
        quiet_ = saved_param_quiet;
        Symbol symbol;
        symbol.name = param.name;
        symbol.type = std::move(type);
        symbol.is_auto = true;
        symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
        declare(std::move(symbol), param.loc);
    }
    // The quiet walk that learned the result recorded captures. This walk is the one that owns them.
    FunctionDecl &fresh = const_cast<FunctionDecl &>(fn);
    fresh.captures.clear();
    fresh.captured_here.clear();
    if (fn.body) {
        if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
            if (fn.is_comptime) {
                for (const auto &child : block->statements) {
                    if (child == nullptr) {
                        continue;
                    }
                    if (std::holds_alternative<Stmt::Return>(child->data)) {
                        continue;
                    }
                    if (const auto *expr = std::get_if<Stmt::Expression>(&child->data)) {
                        if (expr->emit && expr->expr != nullptr) {
                            continue;
                        }
                    }
                    const SourceLoc loc = std::visit(
                        [](const auto &node) -> SourceLoc {
                            using Node = std::decay_t<decltype(node)>;
                            if constexpr (std::is_same_v<Node, Stmt::Func>) {
                                return node.decl != nullptr ? node.decl->loc : SourceLoc{};
                            } else {
                                return node.loc;
                            }
                        },
                        child->data);
                    error(loc, "not supported yet");
                }
            }
            check_statements(const_cast<Stmt::Block &>(*block).statements);
        } else {
            check_stmt(*fn.body);
        }
    }
    adopt_captures(fn);
    // `return f()` on every reached path never finishes. A reached `return 1` is not that loop.
    report_self_loop();
    remember_loop(fn);
    reached_returns_ = std::move(saved_reached);
    variadic_function_ = saved_variadic;
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    unsafe_depth_ = saved_unsafe;
    pop();
    checking_.pop_back();
    collect_returns_ = saved_collect;
}

namespace {

ResultSlot slot_from_inferred(const SpecArg &arg) {
    ResultSlot slot;
    slot.type_name = arg.type_name;
    slot.type_package = arg.type_package;
    slot.is_struct = arg.is_struct;
    slot.is_enum = arg.is_enum;
    slot.indirections = arg.indirections;
    slot.optional = arg.optional;
    slot.const_pointee = arg.const_pointee;
    slot.is_array = arg.is_array;
    slot.elem_const = arg.elem_const;
    slot.elem_slice = arg.elem_slice;
    slot.shells = arg.shells;
    slot.array_len = arg.array_len;
    return slot;
}

// A body that never returns, a body whose returns are only `return;`, and a body whose
// returns are void values is `void`. That is the same result an anonymous function gets
// when its result is omitted.
void install_void_result(FunctionDecl &fn) {
    fn.results.clear();
    fn.return_type = "void";
    fn.type_package.clear();
    fn.is_struct = false;
    fn.is_enum = false;
    fn.indirections = 0;
    fn.optional = 0;
    fn.const_pointee = 0;
    ResultSlot slot;
    slot.type_name = "void";
    fn.results.push_back(std::move(slot));
}

} // namespace

void Checker::clear_inferred_result(FunctionDecl &fn) {
    fn.results.clear();
    fn.return_type.clear();
    fn.type_package.clear();
    fn.is_struct = false;
    fn.is_enum = false;
    fn.indirections = 0;
    fn.optional = 0;
    fn.const_pointee = 0;
}

bool Checker::spell_inferred_component(const Type &type, FnParam &out, bool allow_void) {
    out = FnParam{};
    if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Trait || type.kind == TypeKind::Tuple ||
        type.kind == TypeKind::Meta || type.kind == TypeKind::Nullptr || type.kind == TypeKind::Function) {
        return false;
    }
    if (type.alias_id > 0) {
        out.type_name = type_desc(type);
        return !out.type_name.empty() && out.type_name != "invalid";
    }
    if (fn_pointer(type)) {
        if (type.optional) {
            return false;
        }
        return spell_inferred_fn_param(type, out);
    }
    // `[N]T` as a parameter is a pointer that still records N. `same_type` does not compare that length.
    if (type.kind == TypeKind::Pointer && type.decayed_len > 0 && type.pointee != nullptr) {
        if (type.optional) {
            return false;
        }
        FnParam element;
        if (!spell_inferred_component(*type.pointee, element, false)) {
            return false;
        }
        if (element.is_fn_ptr || element.is_array || !element.shells.empty()) {
            return false;
        }
        // `[N]const T` decays to a pointer. `const` stays on the element, which `same_type` does compare.
        if (type.pointee->is_const && type.pointee->kind != TypeKind::Pointer) {
            element.elem_const = true;
        }
        out = std::move(element);
        TypeShell shell;
        shell.kind = TypeShell::Kind::Array;
        shell.length = type.decayed_len;
        out.shells.push_back(std::move(shell));
        return !out.type_name.empty() && out.type_name != "invalid";
    }
    if (type.kind == TypeKind::Void) {
        if (!allow_void) {
            return false;
        }
        out.type_name = "void";
        return true;
    }
    SpecArg spelled;
    if (!spell_type(type, spelled) || spelled.is_pack || spelled.type_name.empty() || spelled.type_name == "invalid") {
        return false;
    }
    // `is_struct` would report `struct outside definition` when this slot is read back.
    out.type_name = spelled.type_name;
    out.type_package = spelled.type_package;
    out.indirections = spelled.indirections;
    out.optional = spelled.optional;
    out.const_pointee = spelled.const_pointee;
    out.shells = std::move(spelled.shells);
    out.elem_slice = spelled.elem_slice;
    out.elem_const = spelled.elem_const;
    out.is_array = spelled.is_array && spelled.array_len < 0;
    out.array_len = out.is_array ? spelled.array_len : 0;
    return true;
}

bool Checker::spell_inferred_fn_param(const Type &type, FnParam &out) {
    if (!fn_pointer(type) || type.pointee == nullptr || type.pointee->result == nullptr) {
        return false;
    }
    const Type &fn = *type.pointee;
    FnParam spelled;
    spelled.is_fn_ptr = true;
    spelled.indirections = 1;
    spelled.variadic = fn.variadic;
    if (fn.result->kind == TypeKind::Tuple) {
        if (fn.result->results.size() < 2) {
            return false;
        }
        for (const Type &part : fn.result->results) {
            FnParam one;
            if (!spell_inferred_component(part, one, false) || one.is_fn_ptr || one.is_array || !one.shells.empty()) {
                return false;
            }
            spelled.results.push_back(std::move(one));
        }
        const FnParam &first = spelled.results.front();
        spelled.type_name = first.type_name;
        spelled.type_package = first.type_package;
        spelled.ret_indirections = first.indirections;
        spelled.ret_optional = first.optional;
        spelled.ret_const_pointee = first.const_pointee;
    } else {
        FnParam result;
        // A slice result stays rejected. A function-pointer result is the one entry in `results`.
        if (!spell_inferred_component(*fn.result, result, true) || result.is_array || !result.shells.empty()) {
            return false;
        }
        if (result.is_fn_ptr) {
            spelled.type_name = result.type_name;
            spelled.type_package = result.type_package;
            spelled.ret_indirections = result.ret_indirections;
            spelled.ret_optional = result.ret_optional;
            spelled.ret_const_pointee = result.ret_const_pointee;
            spelled.results.push_back(std::move(result));
        } else {
            spelled.type_name = result.type_name;
            spelled.type_package = result.type_package;
            spelled.ret_indirections = result.indirections;
            spelled.ret_optional = result.optional;
            spelled.ret_const_pointee = result.const_pointee;
        }
    }
    if (spelled.type_name.empty() || spelled.type_name == "invalid") {
        return false;
    }
    spelled.params.reserve(fn.params.size());
    for (const Type &param : fn.params) {
        FnParam one;
        if (!spell_inferred_component(param, one, false)) {
            return false;
        }
        spelled.params.push_back(std::move(one));
    }
    out = std::move(spelled);
    return true;
}

bool Checker::install_inferred_type(const Type &type, ResultSlot &slot) {
    if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Void || type.kind == TypeKind::Tuple ||
        type.kind == TypeKind::Function || type.kind == TypeKind::Trait) {
        return false;
    }
    // `type Integer i32` and `type Ptr *i32` are the names the program wrote. The alias sits on the
    // outer type. A pointer to an alias is peeled by `spell_type` and keeps that inner name.
    if (type.alias_id > 0) {
        const std::string name = type_desc(type);
        if (name.empty() || name == "invalid") {
            return false;
        }
        slot = ResultSlot{};
        slot.type_name = name;
        return true;
    }
    if (fn_pointer(type)) {
        if (type.optional) {
            return false;
        }
        ResultSlot filled;
        FnParam spelled;
        if (!spell_inferred_fn_param(type, spelled)) {
            return false;
        }
        filled.is_fn_ptr = true;
        filled.indirections = spelled.indirections;
        filled.type_name = spelled.type_name;
        filled.type_package = spelled.type_package;
        filled.ret_indirections = spelled.ret_indirections;
        filled.ret_optional = spelled.ret_optional;
        filled.ret_const_pointee = spelled.ret_const_pointee;
        filled.fn_params = std::move(spelled.params);
        filled.fn_variadic = spelled.variadic;
        filled.fn_results = std::move(spelled.results);
        const bool saved_quiet = quiet_;
        quiet_ = true;
        const Type rebuilt = type_of_result(filled, {});
        quiet_ = saved_quiet;
        if (rebuilt.kind == TypeKind::Invalid || !same_type(rebuilt, type)) {
            return false;
        }
        slot = std::move(filled);
        return true;
    }
    SpecArg spelled;
    if (!spell_type(type, spelled) || spelled.is_pack || spelled.type_name.empty() || spelled.type_name == "invalid") {
        return false;
    }
    slot = slot_from_inferred(spelled);
    return true;
}

bool Checker::report_inferred_mismatch() {
    if (inferred_returns_.size() < 2) {
        return false;
    }
    // A written `i32` result accepts `return flag` when `flag` is `bool`. An omitted result
    // has no written type, so the returns have to be the same type. The first one is that type.
    std::vector<Type> spelled;
    spelled.reserve(inferred_returns_.size());
    for (const InferredReturn &item : inferred_returns_) {
        if (item.bare) {
            spelled.push_back(type_void());
            continue;
        }
        if (item.parts.empty()) {
            return false;
        }
        for (const Type &part : item.parts) {
            if (part.kind == TypeKind::Invalid) {
                return false;
            }
        }
        if (item.parts.size() == 1) {
            spelled.push_back(item.parts.front());
        } else {
            spelled.push_back(type_tuple(std::vector<Type>(item.parts)));
        }
    }
    const Type &want = spelled.front();
    bool mismatch = false;
    for (std::size_t i = 1; i < spelled.size(); ++i) {
        if (same_type(want, spelled[i])) {
            continue;
        }
        mismatch = true;
        const bool saved_quiet = quiet_;
        quiet_ = false;
        error(inferred_returns_[i].loc,
              "return type: expected " + type_desc(want) + ", found " + type_desc(spelled[i]));
        quiet_ = saved_quiet;
    }
    return mismatch;
}

const FunctionDecl *Checker::call_target(const Expr &expr) {
    const auto *call = std::get_if<Expr::Call>(&expr.data);
    if (call == nullptr || call->callee == nullptr) {
        return nullptr;
    }
    const auto *named = std::get_if<Expr::Name>(&call->callee->data);
    if (named == nullptr) {
        return nullptr;
    }
    // One parameter list leaves `selected` empty. Several lists record the chosen body.
    if (call->selected != nullptr) {
        return call->selected;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->is_function || symbol->lists.size() != 1) {
        return nullptr;
    }
    return symbol->lists.front().decl;
}

bool Checker::returns_self(const FunctionDecl &fn, const Expr &expr) { return call_target(expr) == &fn; }

void Checker::report_self_loop() {
    if (reached_returns_.empty() || !assign_.unreachable) {
        return;
    }
    for (const ReachedReturn &item : reached_returns_) {
        if (!item.self) {
            return;
        }
    }
    error(reached_returns_.front().loc, "infinite loop");
}

void Checker::remember_loop(const FunctionDecl &fn) {
    if (reached_returns_.empty()) {
        return;
    }
    LoopInfo info;
    info.falls_through = !assign_.unreachable;
    info.loc = reached_returns_.front().loc;
    bool only_self = !info.falls_through;
    for (const ReachedReturn &item : reached_returns_) {
        if (item.callee == nullptr) {
            info.value = true;
            only_self = false;
        } else {
            if (item.callee != &fn) {
                only_self = false;
            }
            info.callees.push_back(item.callee);
        }
    }
    // `report_self_loop` already reported a body that only calls itself.
    info.reported = only_self;
    loop_info_[&fn] = std::move(info);
}

void Checker::report_call_cycles(const TranslationUnit &unit) {
    std::unordered_map<const FunctionDecl *, bool> terminates;
    for (const auto &[fn, info] : loop_info_) {
        terminates[fn] = info.falls_through || info.value;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &[fn, info] : loop_info_) {
            if (terminates[fn]) {
                continue;
            }
            for (const FunctionDecl *callee : info.callees) {
                const auto found = terminates.find(callee);
                // A call of a function this file did not walk can return.
                if (found == terminates.end() || found->second) {
                    terminates[fn] = true;
                    changed = true;
                    break;
                }
            }
        }
    }
    std::unordered_set<const FunctionDecl *> cyclic;
    for (const auto &[fn, info] : loop_info_) {
        if (info.reported || terminates[fn] || info.callees.empty()) {
            continue;
        }
        error(info.loc, "infinite loop");
        cyclic.insert(fn);
    }
    for (const TopDecl &decl : unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || !fn->infer_result || fn->body == nullptr || !fn->results.empty() ||
            !fn->type_params.empty()) {
            continue;
        }
        if (!fn->method_struct.empty() &&
            (generic_struct_name(file_id_, fn->method_struct) || package_trait(file_id_, fn->method_struct) >= 0)) {
            continue;
        }
        if (self_loop_.contains(const_cast<FunctionDecl *>(fn)) || infer_mismatch_.contains(const_cast<FunctionDecl *>(fn)) ||
            cyclic.contains(fn)) {
            continue;
        }
        error(fn->loc, "not supported yet");
    }
}

bool Checker::report_inferred_trait(const FunctionDecl &fn) {
    if (inferred_returns_.empty()) {
        return false;
    }
    // A part that is not known yet stays `not supported yet`. A trait that is known has no size.
    bool trait = false;
    for (const InferredReturn &item : inferred_returns_) {
        if (item.bare) {
            continue;
        }
        if (item.parts.empty()) {
            return false;
        }
        for (const Type &part : item.parts) {
            if (part.kind == TypeKind::Invalid) {
                return false;
            }
            if (mentions_trait(part)) {
                trait = true;
            }
        }
    }
    if (!trait) {
        return false;
    }
    const bool saved_quiet = quiet_;
    quiet_ = false;
    error(fn.loc, "trait has no size");
    quiet_ = saved_quiet;
    return true;
}

bool Checker::commit_inferred(FunctionDecl &fn) {
    if (inferred_returns_.empty()) {
        return false;
    }
    const InferredReturn &first = inferred_returns_.front();
    if (first.bare || first.parts.empty()) {
        return false;
    }
    for (const InferredReturn &item : inferred_returns_) {
        if (item.bare || item.parts.size() != first.parts.size()) {
            return false;
        }
        for (std::size_t i = 0; i < item.parts.size(); ++i) {
            const Type &part = item.parts[i];
            if (part.kind == TypeKind::Invalid || part.kind == TypeKind::Void || !same_type(part, first.parts[i])) {
                return false;
            }
        }
    }
    std::vector<Type> parts;
    if (first.parts.size() == 1 && first.parts.front().kind == TypeKind::Tuple) {
        const Type &tuple = first.parts.front();
        if (tuple.results.size() < 2) {
            return false;
        }
        for (const Type &part : tuple.results) {
            if (part.kind == TypeKind::Tuple) {
                return false;
            }
            parts.push_back(part);
        }
    } else {
        for (const Type &part : first.parts) {
            // One tuple among several expressions has no single spelling.
            if (part.kind == TypeKind::Tuple) {
                return false;
            }
            parts.push_back(part);
        }
    }
    std::vector<ResultSlot> slots;
    slots.reserve(parts.size());
    for (const Type &part : parts) {
        ResultSlot slot;
        if (!install_inferred_type(part, slot)) {
            return false;
        }
        slots.push_back(std::move(slot));
    }
    if (slots.empty()) {
        return false;
    }
    fn.results = std::move(slots);
    const ResultSlot &head = fn.results.front();
    fn.return_type = head.type_name;
    fn.type_package = head.type_package;
    fn.is_struct = head.is_struct;
    fn.is_enum = head.is_enum;
    fn.indirections = head.indirections;
    fn.optional = head.optional;
    fn.const_pointee = head.const_pointee;
    const bool saved_quiet = quiet_;
    quiet_ = true;
    const Type got = function_result(fn);
    quiet_ = saved_quiet;
    if (got.kind == TypeKind::Invalid || (fn.results.size() == 1 && got.kind == TypeKind::Void)) {
        clear_inferred_result(fn);
        return false;
    }
    return true;
}

bool Checker::infer_omitted_result(FunctionDecl &fn) {
    if (!fn.infer_result || fn.body == nullptr) {
        return false;
    }
    if (!fn.results.empty()) {
        return true;
    }
    if (infer_mismatch_.contains(&fn)) {
        return false;
    }
    if (!infer_running_.insert(&fn).second) {
        return false;
    }
    const bool saved_collect = collect_returns_;
    const bool saved_quiet = quiet_;
    auto saved_got = std::move(inferred_returns_);
    inferred_returns_.clear();
    collect_returns_ = true;
    quiet_ = true;
    InitSuspend suspend(*this);
    const int saved_unsafe = unsafe_depth_;
    unsafe_depth_ = fn.unsafe ? 1 : 0;
    checking_.push_back(&fn);
    push_function();
    const Type saved_ret = return_type_;
    return_type_ = type_invalid();
    const bool saved_variadic = variadic_function_;
    variadic_function_ = fn.variadic;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    loop_depth_ = 0;
    switches_.clear();
    for (const Param &param : fn.params) {
        Type type = type_of_param(param);
        Symbol symbol;
        symbol.name = param.name;
        symbol.type = std::move(type);
        symbol.is_auto = true;
        symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
        declare(std::move(symbol), param.loc);
    }
    if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
        for (const auto &child : block->statements) {
            if (child) {
                check_stmt(*child);
            }
        }
    } else {
        check_stmt(*fn.body);
    }
    bool only_self = !inferred_returns_.empty();
    for (const InferredReturn &item : inferred_returns_) {
        if (!item.self) {
            only_self = false;
            break;
        }
    }
    if (only_self) {
        self_loop_.insert(&fn);
    }
    // `return b()` while `b` is still on this walk is a cycle. That cycle is `infinite loop`.
    for (const InferredReturn &item : inferred_returns_) {
        if (item.callee == nullptr ||
            !infer_running_.contains(const_cast<FunctionDecl *>(item.callee))) {
            continue;
        }
        bool mark = false;
        for (FunctionDecl *frame : checking_) {
            if (frame == item.callee) {
                mark = true;
            }
            if (mark) {
                self_loop_.insert(frame);
            }
        }
    }
    // A call whose result is not known yet does not choose the type. The known returns still
    // have to agree. That call is checked again once the type is known.
    auto known_return = [](const InferredReturn &item) {
        if (item.bare) {
            return true;
        }
        if (item.parts.empty()) {
            return false;
        }
        for (const Type &part : item.parts) {
            if (part.kind == TypeKind::Invalid) {
                return false;
            }
        }
        return true;
    };
    bool known = false;
    bool unknown = false;
    for (const InferredReturn &item : inferred_returns_) {
        if (known_return(item)) {
            known = true;
        } else {
            unknown = true;
        }
    }
    if (known && unknown) {
        std::vector<InferredReturn> kept;
        kept.reserve(inferred_returns_.size());
        for (InferredReturn &item : inferred_returns_) {
            if (known_return(item)) {
                kept.push_back(std::move(item));
            }
        }
        inferred_returns_ = std::move(kept);
    }
    // `return touch()` when `touch` is `void` is that same result. Another type still disagrees.
    bool only_void = !inferred_returns_.empty();
    for (const InferredReturn &item : inferred_returns_) {
        const bool void_value = !item.bare && item.parts.size() == 1 && item.parts.front().kind == TypeKind::Void;
        if (!item.bare && !void_value) {
            only_void = false;
            break;
        }
    }
    bool ok = false;
    if (inferred_returns_.empty() || only_void) {
        install_void_result(fn);
        ok = true;
    } else if (report_inferred_mismatch()) {
        infer_mismatch_.insert(&fn);
    } else if (report_inferred_trait(fn)) {
        infer_mismatch_.insert(&fn);
    } else {
        ok = commit_inferred(fn);
    }
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    variadic_function_ = saved_variadic;
    return_type_ = saved_ret;
    unsafe_depth_ = saved_unsafe;
    pop();
    checking_.pop_back();
    collect_returns_ = saved_collect;
    quiet_ = saved_quiet;
    inferred_returns_ = std::move(saved_got);
    infer_running_.erase(&fn);
    if (ok) {
        publish_inferred(fn);
    }
    return ok;
}

void Checker::publish_inferred(FunctionDecl &fn) {
    if (fn.results.empty()) {
        return;
    }
    if (fn.extern_c && fn.results.size() > 1) {
        error(fn.loc, "extern function cannot return several results");
    }
    const Type signature = function_signature(fn);
    if (fn.extern_c && signature.result != nullptr) {
        require_extern_struct(*signature.result, fn.loc);
    }
    const auto pending = [](const Type &type) {
        return type.kind != TypeKind::Function || type.result == nullptr || type.result->kind == TypeKind::Invalid;
    };
    bool reported = false;
    auto install = [&](Type &slot) {
        if (pending(slot)) {
            slot = signature;
            return;
        }
        if (!same_type(slot, signature) && !reported) {
            error(fn.loc, "redefinition");
            reported = true;
        }
    };
    if (!fn.method_struct.empty()) {
        const auto file = methods_.find(file_id_);
        if (file == methods_.end()) {
            return;
        }
        const auto owner = file->second.find(fn.method_struct);
        if (owner == file->second.end()) {
            return;
        }
        const auto found = owner->second.find(fn.name);
        if (found == owner->second.end()) {
            return;
        }
        MethodInfo *slot = nullptr;
        if (found->second.decl == &fn) {
            slot = &found->second;
        } else {
            for (MethodInfo &extra : found->second.more) {
                if (extra.decl == &fn) {
                    slot = &extra;
                    break;
                }
            }
        }
        if (slot != nullptr) {
            install(slot->type);
        }
        return;
    }
    if (Symbol *symbol = lookup(fn.name); symbol != nullptr && symbol->is_function && !symbol->is_template) {
        for (std::size_t i = 0; i < symbol->lists.size(); ++i) {
            if (symbol->lists[i].decl != &fn) {
                continue;
            }
            install(symbol->lists[i].type);
            if (i == 0) {
                symbol->type = symbol->lists[0].type;
            }
            mirror_function(*symbol);
            break;
        }
    }
    const auto exported = exports_.find(file_id_);
    if (exported == exports_.end()) {
        return;
    }
    const auto found = exported->second.find(fn.name);
    if (found == exported->second.end() || !found->second.is_function || found->second.is_template) {
        return;
    }
    for (std::size_t i = 0; i < found->second.lists.size(); ++i) {
        if (found->second.lists[i].decl != &fn) {
            continue;
        }
        install(found->second.lists[i].type);
        if (i == 0) {
            found->second.type = found->second.lists[0].type;
        }
        break;
    }
}

void Checker::infer_file_results(const TranslationUnit &unit) {
    // Later checks, generic specializations, and nested functions read an omitted result on demand.
    allow_lazy_infer_ = true;
    std::vector<FunctionDecl *> pending;
    for (const TopDecl &decl : unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || !fn->infer_result || fn->body == nullptr || !fn->results.empty() ||
            !fn->type_params.empty()) {
            continue;
        }
        // A generic struct method and a trait default are specialized later, with `T` filled in.
        if (!fn->method_struct.empty() &&
            (generic_struct_name(file_id_, fn->method_struct) || package_trait(file_id_, fn->method_struct) >= 0)) {
            continue;
        }
        pending.push_back(const_cast<FunctionDecl *>(fn));
    }
    for (std::size_t pass = 0; pass < pending.size(); ++pass) {
        bool progress = false;
        for (FunctionDecl *fn : pending) {
            if (fn == nullptr || !fn->results.empty()) {
                continue;
            }
            if (infer_omitted_result(*fn)) {
                progress = true;
            }
        }
        if (!progress) {
            break;
        }
    }
}

} // namespace tlc
