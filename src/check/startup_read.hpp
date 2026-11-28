// A comptime call that reads a global set at start, `var h = make()`. The run would read 0, since
// `make` runs only when the program starts. The read is `set at start, not at compile time`.
//
// The body is scanned before it runs, with each comptime func it calls and each ordinary function that
// call runs. A local of the same name hides the global for the whole function. `&h` takes the address,
// which is fixed, so it is not a read.
//
// Included once, from sema_fold.cpp.
#pragma once

#include "check/check.hpp"

#include <string>
#include <unordered_set>

namespace tlc {

namespace startup_detail {

// Every name a function declares: its parameters and each local, loop name, and bound name.
inline void collect_locals(const Stmt &stmt, std::unordered_set<std::string> &out) {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &item : node.statements) {
                    if (item != nullptr) {
                        collect_locals(*item, out);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.then_arm != nullptr) {
                    collect_locals(*node.then_arm, out);
                }
                if (node.else_arm != nullptr) {
                    collect_locals(*node.else_arm, out);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile> ||
                                 std::is_same_v<T, Stmt::Switch>) {
                if (node.body != nullptr) {
                    collect_locals(*node.body, out);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (!node.each_name.empty()) {
                    out.insert(node.each_name);
                }
                if (node.init != nullptr) {
                    collect_locals(*node.init, out);
                }
                if (node.body != nullptr) {
                    collect_locals(*node.body, out);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                if (node.stmt != nullptr) {
                    collect_locals(*node.stmt, out);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    out.insert(var.name);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                for (const std::string &name : node.names) {
                    out.insert(name);
                }
            }
        },
        stmt.data);
}

} // namespace startup_detail

bool Checker::startup_read_fault(const FunctionDecl &fn) {
    std::unordered_set<const FunctionDecl *> visiting;
    // True once a read was reported. `here` names the function that holds the read.
    const auto scan_fn = [&](auto &&scan_self, const FunctionDecl &current) -> bool {
        if (current.body == nullptr || !visiting.insert(&current).second) {
            return false;
        }
        const TranslationUnit *unit = unit_for(comptime_decl_file(current));
        if (unit == nullptr) {
            return false;
        }
        std::unordered_set<std::string> locals;
        for (const Param &param : current.params) {
            locals.insert(param.name);
        }
        startup_detail::collect_locals(*current.body, locals);
        const uint32_t file = comptime_decl_file(current);
        // `h`, a package sibling's `h`, and `st.h` of an imported package.
        const auto global_set_at_start = [&](const Expr &root) {
            const auto *named = std::get_if<Expr::Name>(&root.data);
            const auto *selector = std::get_if<Expr::Selector>(&root.data);
            const auto *base = selector == nullptr || selector->base == nullptr
                                   ? nullptr
                                   : std::get_if<Expr::Name>(&selector->base->data);
            if ((named != nullptr && locals.contains(named->text)) || (base != nullptr && locals.contains(base->text))) {
                return false;
            }
            uint32_t owner = 0;
            const VarDecl *var = file_scope_var(root, file, owner);
            return var != nullptr && set_at_start(*var, owner);
        };
        // True when a callee's body reads a start-time global. A comptime func in this file is the old
        // path. An ordinary function with a body is run in the same compiled call, including one in
        // another file of this package, so its reads are the same fault.
        const auto scan_call = [&](const Expr::Call &call) -> bool {
            const auto *named = call.callee == nullptr ? nullptr : std::get_if<Expr::Name>(&call.callee->data);
            if (named == nullptr || locals.contains(named->text)) {
                return false;
            }
            const auto follow = [&](const FunctionDecl &callee) {
                comptime_calls_.push_back(ComptimeCall{call.loc, current.name});
                const bool found = scan_self(scan_self, callee);
                comptime_calls_.pop_back();
                return found;
            };
            for (const TopDecl &decl : unit->decls) {
                if (const auto *item = std::get_if<FunctionDecl>(&decl.data);
                    item != nullptr && item->name == named->text && item->method_struct.empty() && item->is_comptime &&
                    item->body != nullptr) {
                    return follow(*item);
                }
            }
            const auto ordinary = [&](const FunctionDecl &item, uint32_t id) {
                if (item.name != named->text || !item.method_struct.empty() || item.body == nullptr || item.is_comptime ||
                    item.extern_c || item.is_extern || item.extern_compiler || item.is_macro || !item.type_params.empty() ||
                    comptime_param_template(item) || (id != file && item.is_static)) {
                    return false;
                }
                if (item.variadic) {
                    return call.args.size() >= item.params.size();
                }
                return call.args.size() == item.params.size();
            };
            const auto scan_unit = [&](const TranslationUnit &other, uint32_t id) {
                for (const TopDecl &decl : other.decls) {
                    const auto *item = std::get_if<FunctionDecl>(&decl.data);
                    if (item != nullptr && ordinary(*item, id) && follow(*item)) {
                        return true;
                    }
                }
                return false;
            };
            if (scan_unit(*unit, file)) {
                return true;
            }
            for (const uint32_t id : files_in_package(file)) {
                if (id == file) {
                    continue;
                }
                const PackageNode *node = find_node(id);
                if (node != nullptr && scan_unit(node->unit, id)) {
                    return true;
                }
            }
            return false;
        };
        const auto walk_expr = [&](auto &&self, const Expr &expr) -> bool {
            return std::visit(
                [&](const auto &node) -> bool {
                    using T = std::decay_t<decltype(node)>;
                    const auto sub = [&](const std::unique_ptr<Expr> &item) {
                        return item != nullptr && self(self, *item);
                    };
                    if constexpr (std::is_same_v<T, Expr::Name>) {
                        if (global_set_at_start(expr)) {
                            report_comptime_fault(node.loc, "set at start, not at compile time", current.name);
                            return true;
                        }
                        return false;
                    } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                        // `&h` is the address. It does not read the value.
                        if (node.op == TokenKind::Amp) {
                            return false;
                        }
                        return sub(node.expr);
                    } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                        return sub(node.lhs) || sub(node.rhs) || sub(node.mid);
                    } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                        return sub(node.lhs) || sub(node.rhs);
                    } else if constexpr (std::is_same_v<T, Expr::Call>) {
                        for (const auto &arg : node.args) {
                            if (sub(arg)) {
                                return true;
                            }
                        }
                        // The callee's read is reported with this call on the chain.
                        return scan_call(node);
                    } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                        if (global_set_at_start(expr)) {
                            report_comptime_fault(node.loc, "set at start, not at compile time", current.name);
                            return true;
                        }
                        return sub(node.base);
                    } else if constexpr (std::is_same_v<T, Expr::Index>) {
                        return sub(node.base) || sub(node.index) || sub(node.end);
                    } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                        return sub(node.expr);
                    } else if constexpr (std::is_same_v<T, Expr::Init>) {
                        for (const Expr::FieldInit &field : node.fields) {
                            if (sub(field.value)) {
                                return true;
                            }
                        }
                        for (const auto &element : node.elements) {
                            if (sub(element)) {
                                return true;
                            }
                        }
                        return false;
                    } else if constexpr (std::is_same_v<T, Expr::New>) {
                        return sub(node.count) || sub(node.init);
                    } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                        return sub(node.expr);
                    } else {
                        // `sizeof` and `typeof` read no value. A literal reads no global.
                        return false;
                    }
                },
                expr.data);
        };
        const auto walk_stmt = [&](auto &&self, const Stmt &stmt) -> bool {
            return std::visit(
                [&](const auto &node) -> bool {
                    using T = std::decay_t<decltype(node)>;
                    const auto expr = [&](const std::unique_ptr<Expr> &item) {
                        return item != nullptr && walk_expr(walk_expr, *item);
                    };
                    const auto inner = [&](const std::unique_ptr<Stmt> &item) {
                        return item != nullptr && self(self, *item);
                    };
                    if constexpr (std::is_same_v<T, Stmt::Block>) {
                        for (const auto &item : node.statements) {
                            if (inner(item)) {
                                return true;
                            }
                        }
                        return false;
                    } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                        if (expr(node.value)) {
                            return true;
                        }
                        for (const auto &item : node.extra) {
                            if (expr(item)) {
                                return true;
                            }
                        }
                        return false;
                    } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                        return expr(node.expr);
                    } else if constexpr (std::is_same_v<T, Stmt::If>) {
                        return expr(node.cond) || inner(node.then_arm) || inner(node.else_arm);
                    } else if constexpr (std::is_same_v<T, Stmt::While>) {
                        return expr(node.cond) || inner(node.body);
                    } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                        return inner(node.body) || expr(node.cond);
                    } else if constexpr (std::is_same_v<T, Stmt::For>) {
                        return inner(node.init) || expr(node.cond) || expr(node.step) || expr(node.each_expr) ||
                               inner(node.body);
                    } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                        return expr(node.cond) || inner(node.body);
                    } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                        return inner(node.stmt);
                    } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                        for (const VarDecl &var : node.vars) {
                            if (expr(var.init)) {
                                return true;
                            }
                        }
                        return false;
                    } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                        return expr(node.value);
                    } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                        return expr(node.call);
                    } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                        return expr(node.cond);
                    } else {
                        return false;
                    }
                },
                stmt.data);
        };
        return walk_stmt(walk_stmt, *current.body);
    };
    return scan_fn(scan_fn, fn);
}

} // namespace tlc
