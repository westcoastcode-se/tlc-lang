// Packages: structs, enums, traits, globals, and the check order.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

// `var item = call()` and `var item = unsafe call()`. A const stays a constant.
bool startup_call(const tlc::Expr &expr) {
    const tlc::Expr *cursor = &expr;
    while (const auto *unary = std::get_if<tlc::Expr::Unary>(&cursor->data)) {
        if (unary->op != tlc::TokenKind::KwUnsafe || unary->expr == nullptr) {
            return false;
        }
        cursor = unary->expr.get();
    }
    return std::holds_alternative<tlc::Expr::Call>(cursor->data);
}

} // namespace

namespace tlc {

void Checker::store_assoc_const(uint32_t file, VarDecl &var) {
    if (var.owner.empty() || var.name.empty() || assoc_defined(file, var.owner, var.name)) {
        return;
    }
    Type got = check_file_const(var);
    AssocVar entry;
    entry.exported = var.exported;
    entry.is_const = true;
    if (var.has_const) {
        entry.type = type_int();
        entry.has_const = true;
        entry.const_value = var.const_value;
    } else if (got.kind == TypeKind::Invalid) {
        entry.type = type_invalid();
    } else {
        entry.type = std::move(got);
    }
    assoc_vars_[file][var.owner].emplace(var.name, std::move(entry));
}

void Checker::declare_assoc(const VarDecl &var) {
    if (var.owner.empty() || var.name.empty()) {
        return;
    }
    const bool generic = generic_struct_name(file_id_, var.owner);
    const bool on_trait = package_trait(file_id_, var.owner) >= 0;
    // A C symbol is one library name. A generic or a trait does not name one object.
    if (var.extern_c && (generic || on_trait)) {
        error(var.loc, "extern(abi=\"c\") on a trait or a generic");
        return;
    }
    // `extern var Type::name` is not a constant. `extern const Type::name` is file scope.
    if (var.is_extern && !var.is_const_decl) {
        error(var.loc, "not supported yet");
    }
    if (on_trait) {
        error(var.loc, "type mismatch");
    }
    require_owner_params(var);
    const bool known = generic || named_type_owner(var.owner);
    if (!known) {
        error(var.loc, "type mismatch");
        return;
    }
    if (!generic && defined_in(var.owner) != file_id_) {
        error(var.loc, "method in another file");
        return;
    }
    if (generic) {
        // A static function and a variable on the same generic struct cannot share a name.
        // The earlier declaration is kept. This one is the redefinition.
        for (const OwnedFn &owned : owned_fns_) {
            if (owned.file_id == file_id_ && owned.fn.static_method && owned.fn.method_struct == var.owner &&
                owned.fn.name == var.name && owned.fn.loc.offset < var.loc.offset) {
                error(var.loc, "redefinition");
                break;
            }
        }
        return;
    }
    if (const int enum_id = find_enum(file_id_, var.owner); enum_id >= 0 && find_enum_member(enum_id, var.name) != nullptr) {
        error(var.loc, "redefinition");
        return;
    }
    if (const MethodInfo *method = find_named_method(var.owner, var.name); method != nullptr && method->is_static) {
        error(var.loc, "redefinition");
        return;
    }
    if (const auto templates = method_templates_.find(file_id_); templates != method_templates_.end()) {
        const auto owner = templates->second.find(var.owner);
        if (owner != templates->second.end()) {
            const auto found = owner->second.find(var.name);
            if (found != owner->second.end()) {
                for (const FunctionDecl *fn : found->second) {
                    if (fn != nullptr && fn->static_method) {
                        error(var.loc, "redefinition");
                        return;
                    }
                }
            }
        }
    }
    if (assoc_defined(file_id_, var.owner, var.name)) {
        error(var.loc, "redefinition");
        return;
    }
    // `check_file_const` is the only check. The variable path below would type the initializer twice.
    // A plain `const` on a type is a runtime value that cannot be assigned.
    if (var.is_const_decl && var.is_comptime) {
        store_assoc_const(file_id_, const_cast<VarDecl &>(var));
        return;
    }
    Type type = type_invalid();
    if (var.inferred) {
        if (var.init == nullptr) {
            error(var.loc, "type mismatch");
        } else if (const auto *init = std::get_if<Expr::Init>(&var.init->data);
                   init != nullptr && (init->array_literal || (var.is_array && !var.ptr_to_array))) {
            type = file_array_initializer(const_cast<VarDecl &>(var), nullptr);
        } else {
            type = check_expr(*var.init, true);
            if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Void || type.kind == TypeKind::Nullptr) {
                error(var.loc, "type mismatch");
                type = type_invalid();
            } else if (type.kind == TypeKind::Array && type.length >= 0 && !is_slice(type) &&
                       !accept_array_copy(const_cast<VarDecl &>(var))) {
                type = type_invalid();
            }
        }
    } else {
        type = type_of_var(var);
    }
    assoc_vars_[file_id_][var.owner].emplace(var.name, AssocVar{std::move(type), var.exported, var.is_const_decl});
}

FnList Checker::function_list(const FunctionDecl &fn, Type signature) const {
    FnList list;
    list.type = std::move(signature);
    list.has_body = fn.body != nullptr;
    list.unsafe_fn = fn.unsafe;
    list.extern_c = fn.extern_c;
    list.link_name = fn.link_name;
    list.decl = &fn;
    list.loc = fn.loc;
    return list;
}

void Checker::mirror_function(const Symbol &symbol) {
    if (graph_ != nullptr) {
        if (const PackageNode *self = find_node(file_id_)) {
            const auto package = package_syms_.find(self->package_key);
            if (package != package_syms_.end()) {
                const auto found = package->second.find(symbol.name);
                if (found != package->second.end() && found->second.is_function && !found->second.is_template) {
                    found->second.type = symbol.type;
                    found->second.has_body = symbol.has_body;
                    found->second.unsafe_fn = symbol.unsafe_fn;
                    found->second.link_name = symbol.link_name;
                    found->second.lists = symbol.lists;
                }
            }
        }
    }
    const auto table = statics_.find(file_id_);
    if (table == statics_.end()) {
        return;
    }
    const auto prior = table->second.find(symbol.name);
    if (prior != table->second.end() && prior->second.is_function) {
        prior->second.type = symbol.type;
        prior->second.has_body = symbol.has_body;
        prior->second.unsafe_fn = symbol.unsafe_fn;
        prior->second.link_name = symbol.link_name;
        prior->second.lists = symbol.lists;
    }
}

void Checker::absorb_function(Symbol &existing, const FunctionDecl &fn, Type signature) {
    if (!existing.is_function || existing.is_template) {
        error(fn.loc, "redefinition");
        return;
    }
    if (existing.lists.empty()) {
        FnList primary;
        primary.type = existing.type;
        primary.has_body = existing.has_body;
        primary.unsafe_fn = existing.unsafe_fn;
        primary.link_name = existing.link_name;
        primary.loc = existing.loc;
        existing.lists.push_back(std::move(primary));
    }
    FnList *slot = nullptr;
    for (FnList &list : existing.lists) {
        if (same_selection(list.type, false, signature, false)) {
            slot = &list;
            break;
        }
    }
    if (slot != nullptr) {
        const bool link_clash = !fn.link_name.empty() && !slot->link_name.empty() && slot->link_name != fn.link_name;
        // An omitted result is not known yet. Either side may be that placeholder.
        const bool new_pending = fn.infer_result && fn.results.empty();
        const bool slot_pending = slot->decl != nullptr && slot->decl->infer_result && slot->decl->results.empty();
        const bool pending = new_pending || slot_pending;
        if ((!pending && !same_type(slot->type, signature)) || (slot->has_body && fn.body != nullptr) ||
            slot->unsafe_fn != fn.unsafe || link_clash) {
            error(fn.loc, "redefinition");
            return;
        }
        // The body was registered first. The prototype names the result until the body is read.
        if (slot_pending && !new_pending) {
            slot->type = signature;
            if (slot == &existing.lists.front()) {
                existing.type = slot->type;
            }
        }
        if (slot->link_name.empty()) {
            slot->link_name = fn.link_name;
        }
        if (existing.link_name.empty()) {
            existing.link_name = fn.link_name;
        }
        if (fn.body != nullptr) {
            slot->has_body = true;
            slot->decl = &fn;
            if (slot == &existing.lists.front()) {
                existing.has_body = true;
            }
        }
        return;
    }
    for (const FnList &list : existing.lists) {
        if ((fn.extern_c && list.extern_c) ||
            (!fn.link_name.empty() && !list.link_name.empty() && list.link_name == fn.link_name)) {
            error(fn.loc, "redefinition");
            return;
        }
    }
    existing.lists.push_back(function_list(fn, std::move(signature)));
}

void Checker::declare_globals(const TranslationUnit &unit) {
    // Record every signature first. An omitted result is read in `infer_file_results`, after the
    // later functions in this file exist. The next file starts again from that same point.
    allow_lazy_infer_ = false;
    std::vector<VarDecl *> pending_consts;
    // A file-scope const is bound after the vars, so it can name a later var.
    // Bind one early when every name it uses is already declared, so
    // `var row = [_][2]i32{inner}` sees `const inner`.
    auto names_bound = [&](const auto &self, const Expr &expr) -> bool {
        return std::visit(
            [&](const auto &node) -> bool {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Expr::Name>) {
                    return lookup(node.text) != nullptr;
                } else if constexpr (std::is_same_v<T, Expr::Unary> || std::is_same_v<T, Expr::Sizeof> ||
                                     std::is_same_v<T, Expr::Typeof> || std::is_same_v<T, Expr::Cast>) {
                    return node.expr == nullptr || self(self, *node.expr);
                } else if constexpr (std::is_same_v<T, Expr::Binary> || std::is_same_v<T, Expr::Assign>) {
                    return (node.lhs == nullptr || self(self, *node.lhs)) &&
                           (node.rhs == nullptr || self(self, *node.rhs));
                } else if constexpr (std::is_same_v<T, Expr::Index>) {
                    if (node.base != nullptr && !self(self, *node.base)) {
                        return false;
                    }
                    if (node.index != nullptr && !self(self, *node.index)) {
                        return false;
                    }
                    return node.end == nullptr || self(self, *node.end);
                } else if constexpr (std::is_same_v<T, Expr::Init>) {
                    for (const Expr::FieldInit &field : node.fields) {
                        if (field.value != nullptr && !self(self, *field.value)) {
                            return false;
                        }
                    }
                    for (const auto &element : node.elements) {
                        if (element != nullptr && !self(self, *element)) {
                            return false;
                        }
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, Expr::Call> || std::is_same_v<T, Expr::Selector> ||
                                     std::is_same_v<T, Expr::Func> || std::is_same_v<T, Expr::New> ||
                                     std::is_same_v<T, Expr::Delete> || std::is_same_v<T, Expr::Unsupported>) {
                    return false;
                } else {
                    return true;
                }
            },
            expr.data);
    };
    auto bind_ready = [&]() {
        bool progress = true;
        while (progress) {
            progress = false;
            for (VarDecl *&var : pending_consts) {
                if (var == nullptr) {
                    continue;
                }
                if (var->init != nullptr && !names_bound(names_bound, *var->init)) {
                    continue;
                }
                bind_file_const(*var);
                var = nullptr;
                progress = true;
            }
        }
    };
    // A result named `T` is a type parameter. Resolving it here is `not supported yet`.
    // A result named `Shape` is a trait and has no size.
    auto trait_result_name = [&](const FunctionDecl &fn) {
        auto named = [&](const std::string &name, const std::string &package, int indirections) {
            if (!package.empty() || indirections > 1 || name.empty()) {
                return;
            }
            if (package_trait(file_id_, name) >= 0) {
                error(fn.loc, "trait has no size");
            }
            uint32_t trait_owner = file_id_;
            if (find_trait_template(name, trait_owner) != nullptr) {
                error(fn.loc, "trait has no size");
            }
        };
        if (fn.results.size() > 1) {
            for (const ResultSlot &slot : fn.results) {
                named(slot.type_name, slot.type_package, slot.indirections);
            }
            return;
        }
        if (fn.results.size() == 1) {
            const ResultSlot &slot = fn.results.front();
            named(slot.type_name, slot.type_package, slot.indirections);
            return;
        }
        named(fn.return_type, fn.type_package, fn.indirections);
    };
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            // A trait has no size, so it is not a result. Checked before the paths that
            // return early, and again below for an ordinary function.
            if (!fn->type_params.empty() && fn->method_struct.empty()) {
                // `...T` is not a type parameter. The parser already reported it.
                bool type_pack = false;
                for (const TypeParam &param : fn->type_params) {
                    if (param.is_pack) {
                        type_pack = true;
                        break;
                    }
                }
                if (type_pack) {
                    continue;
                }
                // A C symbol is one library name. A tlc `extern` is one symbol per specialization.
                // `...` is still one list, and a trait parameter is not a specialization of `T`.
                if (fn->extern_c) {
                    error(fn->loc, "extern(abi=\"c\") on a trait or a generic");
                } else if (fn->variadic || extern_generic_trait_param(file_id_, *fn) || generic_trait_parameter(*fn)) {
                    error(fn->loc, "not supported yet");
                }
                trait_result_name(*fn);
                FunctionDecl *stable = stable_template(file_id_, *fn);
                Symbol *existing = lookup_scope(fn->name);
                if (existing == nullptr && !fn->is_static) {
                    existing = lookup(fn->name);
                }
                if (existing != nullptr) {
                    if (!existing->is_template || !existing->is_function) {
                        error(fn->loc, "redefinition");
                    } else {
                        absorb_template(*existing, *fn, stable, true);
                        mirror_template(*existing);
                    }
                    continue;
                }
                if (shared_has(fn->name)) {
                    error(fn->loc, "redefinition");
                    continue;
                }
                Symbol symbol;
                symbol.name = fn->name;
                symbol.is_function = true;
                symbol.is_template = true;
                symbol.unsafe_fn = fn->unsafe;
                symbol.fn_template = stable;
                if (stable != nullptr) {
                    symbol.fn_templates.push_back(stable);
                }
                symbol.has_body = fn->body != nullptr;
                symbol.package_file = file_id_;
                symbol.type = type_function(type_invalid(), {});
                if (fn->is_static) {
                    statics_[file_id_].insert_or_assign(fn->name, symbol);
                    declare(std::move(symbol), fn->loc);
                } else if (remember_shared(symbol, fn->loc)) {
                    declare(std::move(symbol), fn->loc);
                }
                continue;
            }
            if (!fn->method_struct.empty()) {
                uint32_t trait_owner = file_id_;
                if (find_trait_template(fn->method_struct, trait_owner) != nullptr) {
                    error(fn->loc, "not supported yet");
                    continue;
                }
                require_method_params(*fn);
                if (generic_struct_name(file_id_, fn->method_struct)) {
                    // `func Vec[T]::get[U type]` is a method template on each specialization.
                    // A missing body, `...`, and a trait parameter stay rejected.
                    if (!fn->type_params.empty()) {
                        for (const TypeParam &param : fn->type_params) {
                            for (const std::string &used : fn->method_params) {
                                if (param.name == used) {
                                    error(param.loc, "redefinition");
                                }
                            }
                        }
                        if (fn->variadic || fn->body == nullptr || extern_generic_trait_param(file_id_, *fn)) {
                            error(fn->loc, "not supported yet");
                        }
                    }
                    // `export` publishes `value.name(...)` and `pkg.Vec[i32].name(...)`.
                    // `extern` specializes with the struct. A C symbol is one library name,
                    // so `extern(abi="c")` is not a method of a generic struct.
                    if (fn->extern_c) {
                        error(fn->loc, "extern(abi=\"c\") on a trait or a generic");
                    } else if (fn->is_extern && fn->variadic) {
                        error(fn->loc, "not supported yet");
                    }
                    trait_result_name(*fn);
                    if (fn->static_method) {
                        for (const OwnedVar &owned : owned_assoc_) {
                            if (owned.file_id == file_id_ && owned.var.owner == fn->method_struct &&
                                owned.var.name == fn->name && owned.var.loc.offset < fn->loc.offset) {
                                error(fn->loc, "redefinition");
                                break;
                            }
                        }
                    }
                    if (!fn->extern_c) {
                        generic_method_clash(*fn);
                    }
                    continue;
                }
                if (package_trait(file_id_, fn->method_struct) >= 0) {
                    trait_result_name(*fn);
                    register_trait_default(*fn);
                    continue;
                }
                if (!fn->type_params.empty()) {
                    trait_result_name(*fn);
                    register_method_template(*fn);
                    continue;
                }
            }
            Type signature = function_signature(*fn);
            if (signature.result != nullptr && mentions_trait(*signature.result)) {
                error(fn->loc, "trait has no size");
            }
            bool generic = false;
            for (const Type &param : signature.params) {
                if (mentions_trait(param)) {
                    generic = true;
                }
            }
            // A parameter that names a trait is specialized per struct. That is not one C symbol.
            if (generic && fn->extern_c) {
                error(fn->loc, "extern(abi=\"c\") on a trait or a generic");
            } else {
                if (fn->variadic && !fn->extern_c && (fn->is_extern || fn->body == nullptr)) {
                    error(fn->loc, "not supported yet");
                }
                if (fn->extern_c && fn->results.size() > 1) {
                    error(fn->loc, "extern function cannot return several results");
                } else if (fn->extern_c) {
                    if (signature.result != nullptr) {
                        require_extern_struct(*signature.result, fn->loc);
                    }
                    for (std::size_t index = 0; index < signature.params.size() && index < fn->params.size(); ++index) {
                        require_extern_struct(signature.params[index], fn->params[index].loc);
                    }
                    bool many = signature.result != nullptr && uses_many_results(*signature.result);
                    if (!many) {
                        for (const Type &param : signature.params) {
                            if (uses_many_results(param)) {
                                many = true;
                                break;
                            }
                        }
                    }
                    if (many) {
                        error(fn->loc, "extern function cannot use several results");
                    }
                }
            }
            if (generic && signature.result != nullptr && signature.result->kind == TypeKind::Tuple) {
                error(fn->loc, "not supported yet");
            }
            // `export` publishes the function. Each call is specialized. A tlc `extern` is one
            // symbol per struct. `...` is still one list. A variadic `extern` or a missing body
            // was already reported above.
            if (generic && !fn->extern_c && fn->variadic && !fn->is_extern && fn->body != nullptr) {
                error(fn->loc, "not supported yet");
            }
            if (!fn->method_struct.empty()) {
                const bool has_param = !fn->params.empty();
                const int receiver_refs = has_param ? fn->params.front().indirections : -1;
                const bool receiver_shape = !fn->static_method && has_param && fn->params.front().is_receiver &&
                                            (receiver_refs == 0 || receiver_refs == 1) && !fn->params.front().is_array &&
                                            fn->params.front().type_package.empty() &&
                                            fn->params.front().type_name == fn->method_struct;
                const bool known_type = fn->static_method ? static_type_owner(fn->method_struct)
                                                             : named_type_owner(fn->method_struct);
                const bool receiver_ok = receiver_shape && has_param &&
                                         (fn->params.front().is_struct || package_has_enum(file_id_, fn->method_struct));
                if (!known_type || (!fn->static_method && !receiver_ok)) {
                    error(fn->loc, "type mismatch");
                }
                // The function is written beside its struct, enum, primitive alias, or slice alias.
                // A static function may also sit beside a pointer alias, a sized-array alias, or a struct alias.
                // Other files in the package can still call it.
                if (defined_in(fn->method_struct) != file_id_) {
                    error(fn->loc, "method in another file");
                    continue;
                }
                auto &table = methods_[file_id_][fn->method_struct];
                const auto found = table.find(fn->name);
                if (found != table.end()) {
                    // The receiver is not a parameter. `self this` and `self *this` share the remaining list.
                    MethodInfo *slot = nullptr;
                    const bool skip_new = !fn->static_method;
                    if (same_selection(found->second.type, !found->second.is_static, signature, skip_new)) {
                        slot = &found->second;
                    } else {
                        for (MethodInfo &extra : found->second.more) {
                            if (same_selection(extra.type, !extra.is_static, signature, skip_new)) {
                                slot = &extra;
                                break;
                            }
                        }
                    }
                    if (slot != nullptr) {
                        // Same rule as a free function: two `name=` values on one list clash.
                        const bool link_clash =
                            !fn->link_name.empty() && !slot->link_name.empty() && slot->link_name != fn->link_name;
                        const bool new_pending = fn->infer_result && fn->results.empty();
                        const bool slot_pending =
                            slot->decl != nullptr && slot->decl->infer_result && slot->decl->results.empty();
                        const bool pending = new_pending || slot_pending;
                        if ((!pending && !same_type(slot->type, signature)) || (slot->has_body && fn->body) ||
                            slot->unsafe_fn != fn->unsafe || link_clash) {
                            error(fn->loc, "redefinition");
                        } else {
                            if (slot_pending && !new_pending) {
                                slot->type = signature;
                            }
                            if (fn->body) {
                                slot->has_body = true;
                                slot->decl = fn;
                            }
                            if (fn->exported) {
                                slot->exported = true;
                            }
                            if (slot->link_name.empty()) {
                                slot->link_name = fn->link_name;
                            }
                            slot->extern_c = slot->extern_c || fn->extern_c;
                        }
                    } else {
                        // `extern(abi="c")` keeps one C name per method, so a second list clashes.
                        const auto taken = [&](const MethodInfo &info) {
                            return (fn->extern_c && info.extern_c) ||
                                   (!fn->link_name.empty() && info.link_name == fn->link_name);
                        };
                        bool clash = taken(found->second);
                        if (!clash) {
                            for (const MethodInfo &extra : found->second.more) {
                                if (taken(extra)) {
                                    clash = true;
                                    break;
                                }
                            }
                        }
                        if (clash) {
                            error(fn->loc, "redefinition");
                        } else {
                            MethodInfo extra;
                            extra.type = std::move(signature);
                            extra.has_body = fn->body != nullptr;
                            extra.is_static = fn->static_method;
                            extra.unsafe_fn = fn->unsafe;
                            extra.exported = fn->exported;
                            extra.extern_c = fn->extern_c;
                            extra.link_name = fn->link_name;
                            extra.file_id = file_id_;
                            extra.decl = fn;
                            found->second.more.push_back(std::move(extra));
                        }
                    }
                } else {
                    const int struct_id = find_tag(file_id_, fn->method_struct);
                    if (struct_id >= 0 && find_method(struct_id, fn->name) != nullptr) {
                        error(fn->loc, "redefinition");
                    } else if (fn->static_method && assoc_defined(file_id_, fn->method_struct, fn->name)) {
                        error(fn->loc, "redefinition");
                    } else {
                        MethodInfo info;
                        info.type = std::move(signature);
                        info.has_body = fn->body != nullptr;
                        info.is_static = fn->static_method;
                        info.unsafe_fn = fn->unsafe;
                        info.exported = fn->exported;
                        info.extern_c = fn->extern_c;
                        info.link_name = fn->link_name;
                        info.file_id = file_id_;
                        info.decl = fn;
                        table.emplace(fn->name, std::move(info));
                    }
                }
                continue;
            }
            Type alias;
            if (package_typedef(file_id_, fn->name, alias) || package_trait(file_id_, fn->name) >= 0) {
                error(fn->loc, "redefinition");
                continue;
            }
            if (Symbol *existing = lookup_scope(fn->name)) {
                absorb_function(*existing, *fn, signature);
                mirror_function(*existing);
                continue;
            }
            // Another file in the package already has this name. A static stays in its file.
            if (!fn->is_static) {
                if (Symbol *shared = lookup(fn->name)) {
                    absorb_function(*shared, *fn, std::move(signature));
                    mirror_function(*shared);
                    continue;
                }
            }
            if (shared_has(fn->name)) {
                error(fn->loc, "redefinition");
                continue;
            }
            Symbol symbol;
            symbol.name = fn->name;
            symbol.type = signature;
            symbol.is_function = true;
            symbol.unsafe_fn = fn->unsafe;
            symbol.has_body = fn->body != nullptr;
            symbol.link_name = fn->link_name;
            symbol.loc = fn->loc;
            symbol.lists.push_back(function_list(*fn, std::move(signature)));
            if (fn->is_static) {
                statics_[file_id_].insert_or_assign(fn->name, symbol);
                declare(std::move(symbol), fn->loc);
            } else if (remember_shared(symbol, fn->loc)) {
                declare(std::move(symbol), fn->loc);
            }
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            if (!var->owner.empty()) {
                bind_ready();
                declare_assoc(*var);
                continue;
            }
            if (var->is_const_decl && var->is_comptime) {
                VarDecl &written = const_cast<VarDecl &>(*var);
                // Folded before typedefs, so `[N]i32` can use it. A later const keeps its own type.
                if (written.has_const) {
                    const_values_[file_id_][written.name] = ConstBinding{written.const_value, written.is_static};
                    define_constant(written, type_int(), written.const_value);
                    continue;
                }
                pending_consts.push_back(&written);
                continue;
            }
            bind_ready();
            Type alias;
            if (package_typedef(file_id_, var->name, alias) || package_trait(file_id_, var->name) >= 0) {
                error(var->loc, "redefinition");
                continue;
            }
            if (lookup_scope(var->name) != nullptr || shared_has(var->name)) {
                error(var->loc, "redefinition");
                continue;
            }
            if (var->is_const_decl && var->init == nullptr && !var->is_extern) {
                error(var->loc, "type mismatch");
            }
            Type type = type_invalid();
            if (var->inferred) {
                if (var->init == nullptr) {
                    error(var->loc, "type mismatch");
                } else if (const auto *init = std::get_if<Expr::Init>(&var->init->data);
                           init != nullptr && init->array_literal) {
                    type = file_array_initializer(const_cast<VarDecl &>(*var), nullptr);
                } else {
                    type = check_expr(*var->init, true);
                    // `null` has no pointee until a pointer type is written around it.
                    if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Void || type.kind == TypeKind::Nullptr) {
                        error(var->loc, "type mismatch");
                        type = type_invalid();
                    } else if (type.kind == TypeKind::Array && type.length >= 0 && !is_slice(type) &&
                               !accept_array_copy(const_cast<VarDecl &>(*var))) {
                        type = type_invalid();
                    }
                }
            } else {
                type = type_of_var(*var);
                // `comptime const` is handled above. A runtime tuple does not hold a function.
                if (tuple_holds_fn(type)) {
                    error(var->loc, "type mismatch");
                    type = type_invalid();
                }
            }
            Symbol symbol;
            symbol.name = var->name;
            symbol.type = std::move(type);
            // `const name = expr` is runtime storage that cannot be assigned. `var name const Type` is too.
            symbol.is_immutable = symbol.type.is_const || var->is_const_decl;
            if (var->is_const_decl && symbol.type.kind != TypeKind::Invalid) {
                symbol.type.is_const = true;
            }
            if ((var->is_const_decl || var->is_const) && !var->is_comptime) {
                note_promotable(symbol, var->init.get());
            }
            symbol.link_name = var->link_name;
            bool defined = false;
            if (var->is_static) {
                statics_[file_id_].insert_or_assign(var->name, symbol);
                declare(symbol, var->loc);
                defined = true;
            } else if (remember_shared(symbol, var->loc)) {
                declare(symbol, var->loc);
                defined = true;
            }
            // `export var name = expr` has no written type until this check. publish_exports runs earlier.
            if (var->inferred && var->exported && !var->is_static && !var->is_extern && !var->name.empty() &&
                !exports_[file_id_].contains(var->name)) {
                if (exported_elsewhere(var->name)) {
                    error(var->loc, "redefinition");
                } else if (defined) {
                    exports_[file_id_].emplace(var->name, symbol);
                }
            }
        }
    }
    for (VarDecl *var : pending_consts) {
        if (var != nullptr) {
            bind_file_const(*var);
        }
    }
    infer_file_results(unit);
}

bool Checker::integer_const_type(const Type &type) const {
    if (type.alias_id != 0) {
        return false;
    }
    return is_integer(type) || type.kind == TypeKind::Bool || is_c_enum(type);
}

bool Checker::stored_const_type(const Type &type) const {
    // `type Integer i32`, `type Point struct`, and `func (i32) i32` are file-scope constants.
    if (type.alias_id != 0) {
        return true;
    }
    if (type.kind == TypeKind::Struct) {
        return true;
    }
    return fn_pointer(type);
}

void Checker::define_constant(const VarDecl &var, Type type, int folded) {
    Symbol symbol;
    symbol.name = var.name;
    symbol.type = std::move(type);
    symbol.is_constant = true;
    symbol.const_value = folded;
    symbol.const_init = var.init.get();
    symbol.package_file = file_id_;
    Symbol exported = symbol;
    bool defined = false;
    if (var.is_static) {
        statics_[file_id_].insert_or_assign(var.name, symbol);
        declare(std::move(symbol), var.loc);
        defined = true;
    } else if (remember_shared(symbol, var.loc)) {
        declare(std::move(symbol), var.loc);
        defined = true;
    }
    if (defined && var.exported && !var.is_static && !var.name.empty()) {
        exports_[file_id_].insert_or_assign(var.name, std::move(exported));
    }
}

Type Checker::file_array_initializer(VarDecl &var, const Type *declared) {
    if (var.init == nullptr) {
        error(var.loc, "type mismatch");
        return type_invalid();
    }
    const auto *init = std::get_if<Expr::Init>(&var.init->data);
    const bool literal = init != nullptr && init->array_literal;
    const bool values = init != nullptr && !init->is_tuple && init->fields.empty() && !init->elements.empty();
    Type want = type_invalid();
    if (!var.inferred) {
        if (declared != nullptr) {
            want = *declared;
        } else {
            want = type_of_var(var);
        }
    }
    // A list stores elements. A name, an index, or a call copies that array.
    const bool copy = !var.inferred && init == nullptr && want.kind == TypeKind::Array && want.length >= 0;
    if (copy) {
        const std::size_t before = diags_.size();
        const Type got = check_against(*var.init, want);
        if (got.kind == TypeKind::Invalid || diags_.size() != before || !same_type(got, want)) {
            return type_invalid();
        }
        return want;
    }
    const bool shape = var.inferred ? literal : var.is_array && !var.ptr_to_array && values;
    if (!shape || init == nullptr) {
        error(var.loc, "not supported yet");
        if (!var.inferred && want.kind != TypeKind::Invalid) {
            check_against(*var.init, want);
        } else {
            check_expr(*var.init, true);
        }
        return type_invalid();
    }
    const std::size_t before = diags_.size();
    Type got = type_invalid();
    if (var.inferred) {
        got = check_array_literal(const_cast<Expr::Init &>(*init));
    } else {
        if (want.kind == TypeKind::Invalid) {
            return type_invalid();
        }
        got = check_against(*var.init, want);
        if (got.kind == TypeKind::Invalid || !same_type(got, want)) {
            return type_invalid();
        }
        got = want;
    }
    if (got.kind != TypeKind::Array || got.element == nullptr || diags_.size() != before) {
        return type_invalid();
    }
    std::unordered_set<std::string> seen;
    bool unfolded = false;
    bool runtime = false;
    bool startup = false;
    const auto visit = [&](const auto &self, const Expr &value, const Type &element) -> void {
        if (const auto *nested = std::get_if<Expr::Init>(&value.data);
            nested != nullptr && element.kind == TypeKind::Array && element.element != nullptr && nested->fields.empty()) {
            for (const auto &child : nested->elements) {
                if (!child) {
                    runtime = true;
                    continue;
                }
                self(self, *child, *element.element);
            }
            return;
        }
        // `[_][2]i32{inner}` copies that array. A call runs before `main`.
        if (element.kind == TypeKind::Array && element.length >= 0) {
            if (!(var.is_const_decl && var.is_comptime) && !var.is_extern && startup_call(value)) {
                startup = true;
                return;
            }
            std::unordered_set<std::string> seen_copy;
            if (constant_array_source(value, seen_copy)) {
                return;
            }
            runtime = true;
            return;
        }
        // `[_]i32{give()}` and `[_]Point{give()}` run before `main`.
        // A `const` stays a constant. `n + 1` is not this call.
        if (!(var.is_const_decl && var.is_comptime) && !var.is_extern && startup_call(value)) {
            startup = true;
            return;
        }
        const FileConst kind = file_constant(value, seen, !(var.is_const_decl && var.is_comptime));
        if (kind == FileConst::Unfolded) {
            unfolded = true;
        } else if (kind == FileConst::No) {
            runtime = true;
        }
        (void)element;
    };
    for (const auto &value : init->elements) {
        if (!value) {
            runtime = true;
            continue;
        }
        visit(visit, *value, *got.element);
    }
    if (unfolded) {
        error(var.loc, "not supported yet");
        return type_invalid();
    }
    if (runtime) {
        error(var.loc, "global initializer is not a constant");
        return type_invalid();
    }
    if (startup) {
        var.runtime_init = true;
    }
    return got;
}

bool Checker::accept_file_scalar(const VarDecl &var, const Type &type) {
    if (type.kind != TypeKind::Float || var.init == nullptr) {
        return true;
    }
    // A sign or a cast does not change whether the value is a constant.
    const Expr *cursor = var.init.get();
    while (cursor != nullptr) {
        if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data)) {
            if (unary->expr == nullptr || (unary->op != TokenKind::Plus && unary->op != TokenKind::Minus)) {
                break;
            }
            cursor = unary->expr.get();
            continue;
        }
        if (const auto *cast = std::get_if<Expr::Cast>(&cursor->data)) {
            if (cast->expr == nullptr || cast->indirections != 0 || cast->is_fn_ptr) {
                break;
            }
            cursor = cast->expr.get();
            continue;
        }
        break;
    }
    std::unordered_set<std::string> seen;
    const FileConst kind = file_constant(*cursor, seen, !(var.is_const_decl && var.is_comptime));
    if (kind == FileConst::Yes) {
        return true;
    }
    error(var.loc, kind == FileConst::Unfolded ? "not supported yet" : "global initializer is not a constant");
    return false;
}

const VarDecl *Checker::global_var(const std::string &name) {
    TranslationUnit *unit = unit_for(file_id_);
    if (unit == nullptr) {
        return nullptr;
    }
    for (const TopDecl &decl : unit->decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        if (var != nullptr && var->owner.empty() && var->name == name) {
            return var;
        }
    }
    return nullptr;
}

Checker::FileConst Checker::file_constant(const Expr &expr, std::unordered_set<std::string> &seen, bool allow_var) {
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        if (!init->fields.empty() && !init->elements.empty()) {
            return FileConst::No;
        }
        FileConst worst = FileConst::Yes;
        for (const Expr::FieldInit &field : init->fields) {
            if (field.value == nullptr) {
                return FileConst::No;
            }
            const FileConst kind = file_constant(*field.value, seen, allow_var);
            if (kind == FileConst::No) {
                return FileConst::No;
            }
            if (kind == FileConst::Unfolded) {
                worst = FileConst::Unfolded;
            }
        }
        for (const auto &element : init->elements) {
            if (!element) {
                return FileConst::No;
            }
            const FileConst kind = file_constant(*element, seen, allow_var);
            if (kind == FileConst::No) {
                return FileConst::No;
            }
            if (kind == FileConst::Unfolded) {
                worst = FileConst::Unfolded;
            }
        }
        return worst;
    }
    if (std::holds_alternative<Expr::Int>(expr.data) && constant_operand(expr) && !fold_int(expr)) {
        return FileConst::Unfolded;
    }
    if (constant_operand(expr)) {
        return FileConst::Yes;
    }
    if (!allow_var) {
        return FileConst::No;
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return FileConst::No;
    }
    if (!seen.insert(named->text).second) {
        return FileConst::No;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->is_function || symbol->is_type || symbol->is_package) {
        seen.erase(named->text);
        return FileConst::No;
    }
    if (symbol->is_constant) {
        seen.erase(named->text);
        return FileConst::Yes;
    }
    const VarDecl *var = global_var(named->text);
    if (var == nullptr) {
        seen.erase(named->text);
        return FileConst::No;
    }
    if (var->init == nullptr) {
        seen.erase(named->text);
        return FileConst::Yes;
    }
    const FileConst kind = file_constant(*var->init, seen, allow_var);
    seen.erase(named->text);
    return kind;
}

Type Checker::check_file_const(VarDecl &written) {
    if (written.init == nullptr) {
        error(written.loc, "type mismatch");
        return type_invalid();
    }
    // `comptime const folded = value` promotes a runtime `const` whose initializer is constant.
    if (const Expr *src = promotable_init(*written.init)) {
        written.init = clone_expr(*src);
    }
    // `comptime const p = &addr` points at a comptime variable.
    if (const auto *unary = std::get_if<Expr::Unary>(&written.init->data);
        unary != nullptr && unary->op == TokenKind::Amp) {
        if (!comptime_address(*written.init)) {
            error(written.loc, "global initializer is not a constant");
            return type_invalid();
        }
    }
    const auto *init = std::get_if<Expr::Init>(&written.init->data);
    const bool literal = init != nullptr && init->array_literal;
    // `const label []const u8 = "hi"` is the string slice.
    // `const v []i32 = row` is a slice of that array, not an array value list.
    const bool string_slice = written.is_array && written.array_len < 0 && written.elem_const &&
                              std::holds_alternative<Expr::String>(written.init->data);
    const bool written_slice = written.is_array && written.array_len < 0 && !written.ptr_to_array;
    if (!string_slice && !written_slice && (literal || (written.is_array && !written.ptr_to_array))) {
        Type got = file_array_initializer(written, nullptr);
        if (got.kind == TypeKind::Invalid || !accept_array_copy(written)) {
            return type_invalid();
        }
        got.is_const = true;
        return got;
    }
    Type got = type_invalid();
    if (written.inferred) {
        got = check_expr(*written.init, true);
    } else {
        const Type want = type_of_var(written);
        if (want.kind == TypeKind::Invalid) {
            return type_invalid();
        }
        const std::size_t before = diags_.size();
        got = check_against(*written.init, want);
        if (got.kind == TypeKind::Invalid || diags_.size() != before) {
            return type_invalid();
        }
        // `const wide f64 = bias` converts. A string decays to `*u8` and may be stored in `*const u8`.
        // The stored type is the one that was written.
        const bool string_pointer = want.kind == TypeKind::Pointer &&
                                    std::holds_alternative<Expr::String>(written.init->data);
        // `const text string_view = "hi"` keeps the alias. The initializer is still a plain slice.
        const bool converted_slice = is_slice(want) && accepts_slice(got, want);
        if (!same_type(got, want) && want.kind != TypeKind::Float && !string_pointer && !converted_slice) {
            return type_invalid();
        }
        got = want;
    }
    if (got.kind == TypeKind::Invalid) {
        return type_invalid();
    }
    if (got.kind == TypeKind::Tuple) {
        // `comptime const pair = (40, 2)`, `comptime const flags = (true, 1)`, and
        // `comptime const nested = ((40, 2), 1)`. A bool element is that bool.
        // `null` and a value that is not constant stay `type mismatch`.
        if (written.init == nullptr || !comptime_constant(*written.init)) {
            error(written.loc, "type mismatch");
            return type_invalid();
        }
        got.is_const = true;
        return got;
    }
    if (got.kind == TypeKind::Void || got.kind == TypeKind::Nullptr) {
        error(written.loc, "type mismatch");
        return type_invalid();
    }
    if (integer_const_type(got)) {
        const std::optional<int> value = fold_int(*written.init);
        if (!value) {
            // `const N = 4u32` is a constant tlc does not fold. A call is not a constant.
            error(written.loc, constant_operand(*written.init) ? "not supported yet"
                                                                : "global initializer is not a constant");
            return type_invalid();
        }
        written.has_const = true;
        written.const_value = *value;
        return type_int();
    }
    if (got.kind == TypeKind::Float) {
        if (!accept_file_scalar(written, got)) {
            return type_invalid();
        }
        return got;
    }
    // `const label *const u8 = "unix"` is the C pointer. `const label = "hi"` is `[]const u8`.
    if (written.init != nullptr && std::holds_alternative<Expr::String>(written.init->data)) {
        if (got.kind == TypeKind::Pointer) {
            if (written.inferred && got.pointee != nullptr) {
                got.pointee->is_const = true;
            }
            return got;
        }
        if (is_slice(got) && got.element != nullptr && got.element->is_const) {
            return got;
        }
    }
    // `const copy = row` copies that array.
    // `const p *i32 = row` stores the address of the first element.
    if (got.kind == TypeKind::Pointer && written.init != nullptr) {
        if (const auto *named = std::get_if<Expr::Name>(&written.init->data)) {
            const Symbol *source = lookup(named->text);
            if (source != nullptr && source->function_id == 0 && source->type.kind == TypeKind::Array &&
                source->type.element != nullptr && !is_slice(source->type) && source->type.length >= 0 &&
                global_var(named->text) != nullptr) {
                return got;
            }
        }
    }
    // `const copy = row` copies that array. A pointer to its element was returned above.
    if (got.kind == TypeKind::Array && got.length >= 0 && !is_slice(got)) {
        if (!accept_array_copy(written)) {
            return type_invalid();
        }
        got.is_const = true;
        return got;
    }
    // A file-scope const slice is an array, another slice, a string, or a constant range.
    if (is_slice(got)) {
        std::unordered_set<std::string> seen;
        if (written.init == nullptr || !constant_slice_expr(*written.init, seen)) {
            error(written.loc, "global initializer is not a constant");
            return type_invalid();
        }
        return got;
    }
    if (got.kind == TypeKind::Pointer && comptime_address(*written.init)) {
        return got;
    }
    if (!stored_const_type(got)) {
        error(written.loc, "type mismatch");
        return type_invalid();
    }
    if (!constant_operand(*written.init)) {
        error(written.loc, "global initializer is not a constant");
        return type_invalid();
    }
    return got;
}

void Checker::bind_file_const(VarDecl &written) {
    Type got = check_file_const(written);
    if (written.has_const) {
        const_values_[file_id_][written.name] = ConstBinding{written.const_value, written.is_static};
        define_constant(written, type_int(), written.const_value);
        return;
    }
    if (got.kind == TypeKind::Invalid) {
        return;
    }
    define_constant(written, std::move(got), 0);
}

bool Checker::constant_array_source(const Expr &expr, std::unordered_set<std::string> &seen) {
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        // `grid[1]` is that row. A range is a slice, and a runtime index is not in the binary.
        if (index->is_range || index->base == nullptr || index->index == nullptr || !fold_int(*index->index)) {
            return false;
        }
        return constant_array_source(*index->base, seen);
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return false;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->is_auto || symbol->is_function || symbol->is_type || symbol->is_package ||
        symbol->type.kind != TypeKind::Array || symbol->type.element == nullptr || is_slice(symbol->type)) {
        seen.erase(named->text);
        return false;
    }
    // A `const` array was checked when it was declared. The initializer has to be in this file.
    // A `var` follows its initializer. A name from another file is not this constant.
    const VarDecl *var = global_var(named->text);
    if (symbol->is_constant) {
        seen.erase(named->text);
        return var != nullptr;
    }
    if (var == nullptr || var->runtime_init) {
        seen.erase(named->text);
        return false;
    }
    if (var->init == nullptr) {
        seen.erase(named->text);
        return true;
    }
    bool ok = false;
    if (std::holds_alternative<Expr::Init>(var->init->data)) {
        ok = file_constant(*var->init, seen, !(var->is_const_decl && var->is_comptime)) == FileConst::Yes;
    } else {
        ok = constant_array_source(*var->init, seen);
    }
    seen.erase(named->text);
    return ok;
}

bool Checker::accept_array_copy(VarDecl &var) {
    if (var.init == nullptr || std::holds_alternative<Expr::Init>(var.init->data)) {
        return true;
    }
    // `var copy [2]i32 = give()` runs before `main`. A `const` stays a constant.
    if (!(var.is_const_decl && var.is_comptime) && !var.is_extern && startup_call(*var.init)) {
        var.runtime_init = true;
        return true;
    }
    std::unordered_set<std::string> seen;
    if (!constant_array_source(*var.init, seen)) {
        error(var.loc, "global initializer is not a constant");
        return false;
    }
    return true;
}

bool Checker::constant_slice_expr(const Expr &expr, std::unordered_set<std::string> &seen) {
    if (std::holds_alternative<Expr::String>(expr.data)) {
        return true;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        // `numbers[1:3]`, `numbers[2:]`, `numbers[:2]`, and `numbers[:]`.
        // The address is fixed. A runtime bound is not a header in the binary.
        if (!index->is_range || index->base == nullptr || !constant_slice_expr(*index->base, seen)) {
            return false;
        }
        if (index->index != nullptr && !fold_int(*index->index)) {
            return false;
        }
        return index->end == nullptr || fold_int(*index->end).has_value();
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr || !seen.insert(named->text).second) {
        return false;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || symbol->type.kind != TypeKind::Array || symbol->type.element == nullptr) {
        seen.erase(named->text);
        return false;
    }
    // A sized array lives at a fixed address. A slice follows its initializer.
    if (!is_slice(symbol->type)) {
        seen.erase(named->text);
        return true;
    }
    const VarDecl *source = global_var(named->text);
    if (source == nullptr) {
        seen.erase(named->text);
        return false;
    }
    // No initializer is the zero header. A call runs before `main` and is not this constant.
    const bool ok = source->init == nullptr || constant_slice_expr(*source->init, seen);
    seen.erase(named->text);
    return ok;
}

void Checker::check_field_default(const Expr &expr, const Type &type, SourceLoc loc) {
    // `const p *i32 = &n` is not a constant. Null is the pointer this evaluator folds.
    if (type.kind == TypeKind::Pointer && !std::holds_alternative<Expr::Nullptr>(expr.data)) {
        error(loc, "global initializer is not a constant");
        return;
    }
    // A short array list is that constant. The elements it does not name stay uninitialized.
    const bool saved_partial = init_allow_partial_;
    if (type.kind == TypeKind::Array && !is_slice(type)) {
        init_allow_partial_ = true;
    }
    const std::size_t before = diags_.size();
    const Type got = check_against(expr, type);
    init_allow_partial_ = saved_partial;
    if (diags_.size() != before || got.kind == TypeKind::Invalid) {
        return;
    }
    std::unordered_set<std::string> seen;
    const FileConst kind = file_constant(expr, seen, false);
    if (kind == FileConst::Yes) {
        return;
    }
    std::unordered_set<std::string> slice_seen;
    if (is_slice(type) && constant_slice_expr(expr, slice_seen)) {
        return;
    }
    error(loc, kind == FileConst::Unfolded ? "not supported yet" : "global initializer is not a constant");
}

void Checker::check_field_defaults(const TranslationUnit &unit) {
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<StructDecl>(&decl.data);
        if (item == nullptr || item->name.empty() || !item->type_params.empty()) {
            continue;
        }
        const int id = find_tag(file_id_, item->name);
        if (id < 0 || id >= static_cast<int>(structs_.size())) {
            continue;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(id)];
        if (info.is_union) {
            continue;
        }
        for (const FieldDecl &field : item->fields) {
            if (field.init == nullptr) {
                continue;
            }
            const StructInfo::Field *slot = nullptr;
            for (const StructInfo::Field &have : info.fields) {
                if (have.name == field.name) {
                    slot = &have;
                    break;
                }
            }
            if (slot == nullptr || !slot->has_init || slot->type.kind == TypeKind::Invalid) {
                continue;
            }
            check_field_default(*field.init, slot->type, field.loc);
        }
    }
}

bool Checker::constant_operand(const Expr &expr) {
    return std::visit(
        [&](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Bool> ||
                          std::is_same_v<T, Expr::Char> || std::is_same_v<T, Expr::Float> ||
                          std::is_same_v<T, Expr::Nullptr>) {
                return true;
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                return fold_int(expr).has_value();
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (fold_int(expr)) {
                    return true;
                }
                const std::optional<FoldedNumber> number = fold_number(expr);
                return number.has_value() && number->is_float;
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(node.text);
                return symbol != nullptr && (symbol->is_constant || symbol->is_function);
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.len_value >= 0) {
                    return true;
                }
                if (node.base == nullptr) {
                    return false;
                }
                // `Box[i32].from` after the selector has been checked. The owner is the specialization.
                if (!node.resolved_owner.empty()) {
                    const MethodInfo *method = find_named_method(node.resolved_owner, node.field);
                    if (method != nullptr && method->is_static) {
                        return true;
                    }
                }
                if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                    const Symbol *symbol = lookup(named->text);
                    if (symbol != nullptr && symbol->is_package) {
                        const Symbol *member = find_export(symbol->package_file, node.field);
                        return member != nullptr && (member->is_constant || member->is_function);
                    }
                    const bool shadowed = symbol != nullptr && !symbol->is_type;
                    if (shadowed) {
                        return false;
                    }
                    const int id = find_enum(file_id_, named->text);
                    if (id >= 0 && find_enum_member(id, node.field) != nullptr) {
                        return true;
                    }
                    const MethodInfo *method = find_named_method(named->text, node.field);
                    return method != nullptr && method->is_static;
                }
                return false;
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.is_range) {
                    if (node.base == nullptr) {
                        return false;
                    }
                    std::unordered_set<std::string> range_seen;
                    return constant_slice_expr(expr, range_seen);
                }
                // `"hi"[1]` is a compile-time byte. An index outside the length is not.
                return fold_int(expr).has_value();
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_fn_ptr || node.expr == nullptr) {
                    return false;
                }
                return constant_operand(*node.expr);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                if (!node.elements.empty()) {
                    return false;
                }
                for (const Expr::FieldInit &field : node.fields) {
                    if (field.value == nullptr || !constant_operand(*field.value)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.expr == nullptr) {
                    return false;
                }
                if (node.op == TokenKind::Plus || node.op == TokenKind::Minus || node.op == TokenKind::Tilde ||
                    node.op == TokenKind::Bang || node.op == TokenKind::KwUnsafe) {
                    return constant_operand(*node.expr);
                }
                if (node.op == TokenKind::Star && !node.postfix) {
                    std::unordered_set<std::string> pointed;
                    if (const Expr *target = comptime_pointee(*node.expr, pointed)) {
                        return constant_operand(*target);
                    }
                    return false;
                }
                if (node.op != TokenKind::Amp) {
                    return false;
                }
                const auto *named = std::get_if<Expr::Name>(&node.expr->data);
                if (named == nullptr) {
                    return false;
                }
                const Symbol *symbol = lookup(named->text);
                // A file-scope variable has an address, including a `comptime const`.
                return symbol != nullptr && !symbol->is_auto && !symbol->is_function && !symbol->is_package &&
                       !symbol->is_type;
            } else {
                return false;
            }
        },
        expr.data);
}

void Checker::check_globals(const TranslationUnit &unit) {
    loop_info_.clear();
    // A file-scope slice is an array, another slice, a string, or a constant range.
    auto require_slice_name = [&](const Expr &expr, const Type &want, SourceLoc loc, std::size_t before) {
        std::unordered_set<std::string> seen;
        if (diags_.size() == before && is_slice(want) && !constant_slice_expr(expr, seen)) {
            error(loc, "global initializer is not a constant");
        }
    };
    std::unordered_set<std::string> tests;
    // A folded `builtin.memeql` or `builtin.memcmp` is the value stored in the global.
    auto runs_at_startup = [&](const Expr &expr) {
        if (!startup_call(expr)) {
            return false;
        }
        const auto *call = std::get_if<Expr::Call>(&expr.data);
        return call == nullptr || !fold_mem_compare(*call);
    };
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            if (fn->is_comptime &&
                (!fn->method_struct.empty() || fn->is_extern || !fn->type_params.empty() || fn->exported)) {
                error(fn->loc, "not supported yet");
                continue;
            }
            // A macro may take a type parameter so a tuple argument can be `T`.
            if (fn->is_macro && (!fn->method_struct.empty() || fn->is_extern || fn->exported)) {
                error(fn->loc, "not supported yet");
                continue;
            }
            // A trait default is checked after a struct gains it. The receiver is still the trait here.
            if (!fn->type_params.empty() || (comptime_param_template(*fn) && !comptime_param_kept(*fn)) ||
                (!fn->method_struct.empty() && (generic_struct_name(file_id_, fn->method_struct) ||
                                                package_trait(file_id_, fn->method_struct) >= 0))) {
                continue;
            }
            check_function(*fn);
        } else if (const auto *test = std::get_if<TestDecl>(&decl.data)) {
            if (!test->name.empty() && !tests.insert(test->name).second) {
                error(test->loc, "redefinition");
            }
            check_test(*test);
        } else if (const auto *item = std::get_if<AssertDecl>(&decl.data)) {
            check_assert(item->loc, item->cond.get(), item->message);
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            struct PartialFlag {
                bool &slot;
                bool saved;
                ~PartialFlag() { slot = saved; }
            } partial{init_allow_partial_, init_allow_partial_};
            if (!(var->is_const_decl && var->is_comptime)) {
                init_allow_partial_ = true;
            }
            struct GlobalNote {
                Checker *self;
                const VarDecl *var;
                ~GlobalNote() {
                    if (var->is_extern || var->is_const_decl || !var->owner.empty()) {
                        return;
                    }
                    Symbol *symbol = self->lookup_scope(var->name);
                    if (symbol == nullptr) {
                        return;
                    }
                    self->init_open(*symbol);
                    self->init_cover(*symbol, var->init.get());
                }
            } note{this, var};
            if (!var->owner.empty()) {
                if (!var->owner_params.empty() || generic_struct_name(file_id_, var->owner)) {
                    continue;
                }
                // A `comptime const` was checked while it was declared.
                if (var->is_const_decl && var->is_comptime) {
                    continue;
                }
                const AssocVar *assoc = find_assoc(var->owner, var->name);
                if (!var->init && !var->is_extern && assoc != nullptr && needs_nonnull_pointer(assoc->type)) {
                    error(var->loc, "type mismatch");
                }
                if (var->is_array && !var->ptr_to_array && var->init && !var->inferred) {
                    if (var->array_len < 0) {
                        if (assoc != nullptr) {
                            const std::size_t before = diags_.size();
                            check_against(*var->init, assoc->type);
                            if (diags_.size() == before && !var->is_extern && runs_at_startup(*var->init)) {
                                const_cast<VarDecl *>(var)->runtime_init = true;
                            } else {
                                require_slice_name(*var->init, assoc->type, var->loc, before);
                            }
                        }
                    } else if (assoc != nullptr && assoc->type.kind == TypeKind::Array) {
                        const std::size_t before = diags_.size();
                        const Type got = file_array_initializer(const_cast<VarDecl &>(*var), &assoc->type);
                        if (diags_.size() == before && got.kind == TypeKind::Array) {
                            (void)accept_array_copy(const_cast<VarDecl &>(*var));
                        }
                    } else if (assoc == nullptr || assoc->type.kind != TypeKind::Invalid) {
                        error(var->loc, "not supported yet");
                    }
                } else if (var->init && std::get_if<Expr::Init>(&var->init->data) != nullptr &&
                           std::get<Expr::Init>(var->init->data).array_literal) {
                    // `var Entity::row = [_]i32{1}` was checked while declaring it.
                } else if (var->init && assoc != nullptr) {
                    if (mentions_trait(assoc->type)) {
                        error(var->loc, "not supported yet");
                        continue;
                    }
                    const std::size_t before = diags_.size();
                    check_against(*var->init, assoc->type);
                    if (diags_.size() == before) {
                        if (!var->is_extern &&
                            (runs_at_startup(*var->init) || assoc->type.kind == TypeKind::Tuple)) {
                            const_cast<VarDecl *>(var)->runtime_init = true;
                        } else if (is_slice(assoc->type)) {
                            require_slice_name(*var->init, assoc->type, var->loc, before);
                        } else {
                            (void)accept_file_scalar(*var, assoc->type);
                        }
                    }
                }
                continue;
            }
            if (!var->init && !var->is_extern) {
                const Symbol *symbol = lookup(var->name);
                if (symbol != nullptr && needs_nonnull_pointer(symbol->type)) {
                    error(var->loc, "type mismatch");
                }
            }
            if (var->is_array && !var->ptr_to_array && var->init) {
                if (!(var->is_const_decl && var->is_comptime) && var->array_len < 0) {
                    const Symbol *symbol = lookup(var->name);
                    const Type want = symbol != nullptr ? symbol->type : type_invalid();
                    const std::size_t before = diags_.size();
                    check_against(*var->init, want);
                    if (diags_.size() == before && !var->is_extern && runs_at_startup(*var->init)) {
                        const_cast<VarDecl *>(var)->runtime_init = true;
                    } else {
                        require_slice_name(*var->init, want, var->loc, before);
                    }
                } else if (!(var->is_const_decl && var->is_comptime)) {
                    const Symbol *symbol = lookup(var->name);
                    if (symbol != nullptr && symbol->type.kind == TypeKind::Array) {
                        const std::size_t before = diags_.size();
                        const Type got = file_array_initializer(const_cast<VarDecl &>(*var), &symbol->type);
                        if (diags_.size() == before && got.kind == TypeKind::Array) {
                            (void)accept_array_copy(const_cast<VarDecl &>(*var));
                        }
                    }
                }
            } else if (var->init && std::get_if<Expr::Init>(&var->init->data) != nullptr &&
                       std::get<Expr::Init>(var->init->data).array_literal) {
                // `var row = [_]i32{1, 2}` and `const row = [_]i32{1}` were checked while declaring them.
            } else if (var->init) {
                // `check_file_const` already reported an initializer that is not a constant.
                if (var->is_const_decl && var->is_comptime) {
                    const Symbol *bound = lookup(var->name);
                    if (bound == nullptr || bound->type.kind == TypeKind::Invalid) {
                        continue;
                    }
                }
                const Symbol *symbol = lookup(var->name);
                const Type want = symbol != nullptr ? symbol->type : type_invalid();
                if (mentions_trait(want)) {
                    error(var->loc, "not supported yet");
                    continue;
                }
                const std::size_t before = diags_.size();
                check_against(*var->init, want);
                if (diags_.size() == before) {
                    if (!(var->is_const_decl && var->is_comptime) && !var->is_extern &&
                        (runs_at_startup(*var->init) || want.kind == TypeKind::Tuple)) {
                        const_cast<VarDecl *>(var)->runtime_init = true;
                    } else if (is_slice(want)) {
                        require_slice_name(*var->init, want, var->loc, before);
                    } else {
                        (void)accept_file_scalar(*var, want);
                    }
                }
            }
        }
    }
    report_call_cycles(unit);
}

void Checker::analyze(const TranslationUnit &unit) {
    ensure_field_info();
    primary_unit_ = const_cast<TranslationUnit *>(&unit);
    collect_templates(const_cast<TranslationUnit &>(unit), 0);
    register_structs(unit, 0);
    register_enums(unit, 0);
    register_typedefs(unit, 0);
    register_traits(unit, 0);
    inherit_traits();
    define_struct_fields();
    define_trait_fields();
    link_parent_fields();
    check_unions();
    publish_exports(unit, 0);
    analyze_unit(unit, 0, nullptr);
    flush_appends();
    primary_unit_ = nullptr;
}

const PackageNode *Checker::find_node(uint32_t file_id) const { return package_node(graph_, file_id); }

const Symbol *Checker::find_export(uint32_t file_id, const std::string &name) const {
    for (const uint32_t id : files_in_package(file_id)) {
        const auto file = exports_.find(id);
        if (file == exports_.end()) {
            continue;
        }
        const auto symbol = file->second.find(name);
        if (symbol != file->second.end()) {
            return &symbol->second;
        }
    }
    return nullptr;
}

const PackageNode *Checker::target_of(const PackageNode &from, const ImportDecl &import) const {
    return import_target(graph_, from.file_id, import);
}

Checker::SelectorTarget Checker::resolve_selector(const Expr::Selector &selector) {
    const auto *base = std::get_if<Expr::Name>(&selector.base->data);
    if (base == nullptr) {
        return {};
    }
    const Symbol *symbol = lookup(base->text);
    if (symbol == nullptr) {
        error(base->loc, "undeclared identifier");
        return {SelectorTarget::Kind::Failed, nullptr};
    }
    if (!symbol->is_package) {
        return {};
    }
    const Symbol *member = find_export(symbol->package_file, selector.field);
    if (member == nullptr) {
        error(selector.loc, selector.field + " is not exported");
        return {SelectorTarget::Kind::Failed, nullptr};
    }
    return {SelectorTarget::Kind::Package, member};
}

void Checker::publish_exports(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            if (!fn->method_struct.empty()) {
                continue;
            }
            if (!fn->type_params.empty()) {
                if (!fn->exported || fn->is_static || fn->name.empty()) {
                    continue;
                }
                bool type_pack = false;
                for (const TypeParam &param : fn->type_params) {
                    if (param.is_pack) {
                        type_pack = true;
                        break;
                    }
                }
                if (type_pack) {
                    continue;
                }
                FunctionDecl *stable = stable_template(file_id, *fn);
                auto &table = exports_[file_id];
                const auto found = table.find(fn->name);
                if (found == table.end()) {
                    if (exported_elsewhere(fn->name)) {
                        error(fn->loc, "redefinition");
                    } else {
                        Symbol symbol;
                        symbol.name = fn->name;
                        symbol.is_function = true;
                        symbol.is_template = true;
                        symbol.unsafe_fn = fn->unsafe;
                        symbol.fn_template = stable;
                        if (stable != nullptr) {
                            symbol.fn_templates.push_back(stable);
                        }
                        symbol.has_body = fn->body != nullptr;
                        symbol.package_file = file_id;
                        symbol.type = type_function(type_invalid(), {});
                        table.emplace(fn->name, std::move(symbol));
                    }
                } else if (!found->second.is_template || !found->second.is_function) {
                    error(fn->loc, "redefinition");
                } else {
                    absorb_template(found->second, *fn, stable, false);
                }
                continue;
            }
            if (!fn->exported || fn->is_static || fn->name.empty()) {
                continue;
            }
            bool generic = false;
            for (const Param &param : fn->params) {
                if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
                    package_trait(file_id, param.type_name) >= 0) {
                    generic = true;
                }
            }
            // A trait parameter is specialized per call. `...` and a C symbol stay unpublished.
            if (generic && (fn->extern_c || fn->variadic)) {
                continue;
            }
            Symbol symbol;
            symbol.name = fn->name;
            symbol.type = function_signature(*fn);
            symbol.loc = fn->loc;
            symbol.is_function = true;
            symbol.unsafe_fn = fn->unsafe;
            symbol.has_body = fn->body != nullptr;
            symbol.link_name = fn->link_name;
            symbol.package_file = file_id;
            auto &table = exports_[file_id];
            const auto found = table.find(fn->name);
            if (found == table.end()) {
                if (exported_elsewhere(fn->name)) {
                    error(fn->loc, "redefinition");
                } else {
                    symbol.lists.push_back(function_list(*fn, symbol.type));
                    table.emplace(fn->name, std::move(symbol));
                }
            } else if (found->second.is_type || !found->second.is_function) {
                error(fn->loc, "redefinition");
            } else {
                Type signature = symbol.type;
                if (found->second.lists.empty()) {
                    FnList primary;
                    primary.type = found->second.type;
                    primary.has_body = found->second.has_body;
                    primary.unsafe_fn = found->second.unsafe_fn;
                    primary.link_name = found->second.link_name;
                    found->second.lists.push_back(std::move(primary));
                }
                FnList *slot = nullptr;
                for (FnList &list : found->second.lists) {
                    if (same_selection(list.type, false, signature, false)) {
                        slot = &list;
                        break;
                    }
                }
                if (slot == nullptr) {
                    found->second.lists.push_back(function_list(*fn, std::move(signature)));
                } else if (slot->unsafe_fn != fn->unsafe ||
                           (!fn->link_name.empty() && !slot->link_name.empty() && slot->link_name != fn->link_name)) {
                    error(fn->loc, "redefinition");
                } else if (fn->body) {
                    slot->has_body = true;
                    slot->decl = fn;
                    if (slot == &found->second.lists.front()) {
                        found->second.has_body = true;
                    }
                    if (slot->link_name.empty()) {
                        slot->link_name = fn->link_name;
                    }
                    if (found->second.link_name.empty()) {
                        found->second.link_name = fn->link_name;
                    }
                } else if (slot->link_name.empty()) {
                    slot->link_name = fn->link_name;
                    if (found->second.link_name.empty()) {
                        found->second.link_name = fn->link_name;
                    }
                }
            }
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            if (!var->owner.empty()) {
                continue;
            }
            // Inferred globals are published from declare_globals, after the initializer has a type.
            if (var->inferred && !var->is_const_decl) {
                continue;
            }
            if (var->is_const_decl && var->is_comptime) {
                if (!var->exported || var->is_static || var->name.empty() || !var->has_const) {
                    continue;
                }
                Symbol symbol;
                symbol.name = var->name;
                symbol.type = type_int();
                symbol.is_constant = true;
                symbol.const_value = var->const_value;
                symbol.package_file = file_id;
                exports_[file_id].insert_or_assign(var->name, std::move(symbol));
                continue;
            }
            if (var->inferred) {
                continue;
            }
            if (var->exported && var->is_extern) {
                error(var->loc, "export extern");
            }
            if (!var->exported || var->is_static || var->is_extern || var->name.empty()) {
                continue;
            }
            if (exports_[file_id].contains(var->name)) {
                continue;
            }
            if (exported_elsewhere(var->name)) {
                error(var->loc, "redefinition");
                continue;
            }
            Symbol symbol;
            symbol.name = var->name;
            std::vector<TypeShell> shells = var->shells;
            fold_shells(shells, var->loc);
            symbol.type = published_object(var->type_name, var->indirections, var->is_array, var->array_len, var->loc,
                                           false, var->type_package, var->is_struct, var->is_enum, var->optional,
                                           var->const_pointee, false, var->elem_slice, shells);
            if (var->is_const_decl || var->is_const) {
                symbol.is_immutable = true;
                symbol.type.is_const = true;
            }
            exports_[file_id].emplace(var->name, std::move(symbol));
        }
    }
    // `builtin.Type` is the tagged union in builtin.tlc. This export remains when that file does not declare it.
    if (graph_ != nullptr) {
        const PackageNode *node = find_node(file_id);
        if (is_compiler_builtin(node) && !exports_[file_id].contains("Type")) {
            Symbol symbol;
            symbol.name = "Type";
            symbol.is_type = true;
            symbol.type = type_meta();
            symbol.package_file = file_id;
            exports_[file_id].emplace("Type", std::move(symbol));
        }
    }
}

void Checker::bind_imports(const PackageNode &node) {
    for (const ImportDecl &import : node.unit.imports) {
        const PackageNode *target = target_of(node, import);
        if (target == nullptr) {
            continue;
        }
        if (import.form == ImportDecl::Form::Selective) {
            for (const std::string &name : import.names) {
                const Symbol *exported = find_export(target->file_id, name);
                if (exported == nullptr) {
                    error(import.loc, name + " is not exported");
                    continue;
                }
                if (exported->is_type) {
                    if (exported->is_enum && exported->type.kind == TypeKind::Enum) {
                        const int have = find_enum(file_id_, name);
                        if (have == exported->type.enum_id) {
                            continue;
                        }
                        if (name_taken(name) || have >= 0 || find_tag(file_id_, name) >= 0) {
                            error(import.loc, "redefinition");
                        } else {
                            enum_ids_[file_id_][name] = exported->type.enum_id;
                        }
                        continue;
                    }
                    if (exported->is_typedef) {
                        const auto have = typedefs_[file_id_].find(name);
                        if (have != typedefs_[file_id_].end() && same_type(have->second, exported->type)) {
                            if (exported->is_struct_tag && exported->type.kind == TypeKind::Struct &&
                                !tags_[file_id_].contains(name)) {
                                tags_[file_id_][name] = exported->type.struct_id;
                            }
                            continue;
                        }
                        if (name_taken(name) || have != typedefs_[file_id_].end()) {
                            error(import.loc, "redefinition");
                            continue;
                        }
                        typedefs_[file_id_][name] = exported->type;
                        if (exported->is_struct_tag && exported->type.kind == TypeKind::Struct &&
                            !tags_[file_id_].contains(name)) {
                            tags_[file_id_][name] = exported->type.struct_id;
                        }
                        continue;
                    }
                    if (exported->type.kind == TypeKind::Struct) {
                        const auto local = tags_[file_id_].find(name);
                        if (local != tags_[file_id_].end() && local->second == exported->type.struct_id) {
                            continue;
                        }
                        if (name_taken(name) || find_tag(file_id_, name) >= 0) {
                            error(import.loc, "redefinition");
                        } else {
                            tags_[file_id_][name] = exported->type.struct_id;
                        }
                        continue;
                    }
                    // `import { Shape }` does not publish a trait parameter. The constraint is `T pkg.Shape`.
                    if (exported->type.kind == TypeKind::Trait) {
                        error(import.loc, "not supported yet");
                        continue;
                    }
                    const auto have = typedefs_[file_id_].find(name);
                    if (have != typedefs_[file_id_].end() && same_type(have->second, exported->type)) {
                        continue;
                    }
                    if (name_taken(name) || have != typedefs_[file_id_].end()) {
                        error(import.loc, "redefinition");
                    } else {
                        typedefs_[file_id_][name] = exported->type;
                    }
                    continue;
                }
                if (name_taken(name)) {
                    error(import.loc, "redefinition");
                    continue;
                }
                Symbol local = *exported;
                local.name = name;
                local.is_package = false;
                declare(std::move(local), import.loc);
            }
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (binding.empty()) {
            continue;
        }
        if (name_taken(binding)) {
            error(import.loc, "redefinition");
            continue;
        }
        auto &table = scopes_.back().symbols;
        if (const auto found = table.find(binding); found != table.end()) {
            if (found->second.is_package) {
                error(import.loc, "duplicate package name");
            } else {
                error(import.loc, "redefinition");
            }
            continue;
        }
        Symbol symbol;
        symbol.name = binding;
        symbol.is_package = true;
        symbol.package_file = target->file_id;
        table.emplace(binding, std::move(symbol));
    }
}

void Checker::analyze_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node) {
    file_id_ = file_id;
    push();
    if (node != nullptr) {
        bind_imports(*node);
    }
    declare_globals(unit);
    check_globals(unit);
    drain_deferred();
    pop();
}

void Checker::prepare_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node) {
    file_id_ = file_id;
    push();
    if (node != nullptr) {
        bind_imports(*node);
    }
    declare_globals(unit);
    pop();
}

void Checker::check_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node) {
    file_id_ = file_id;
    push();
    if (node != nullptr) {
        bind_imports(*node);
    }
    const auto locals = statics_.find(file_id_);
    if (locals != statics_.end()) {
        for (const auto &[name, symbol] : locals->second) {
            (void)name;
            declare(symbol, {});
        }
    }
    check_field_defaults(unit);
    check_globals(unit);
    drain_deferred();
    pop();
}

namespace {

// Importers are prepared after the packages they import. A stored `export const`
// is published while its package is prepared, so `import { origin }` can see it.
std::vector<uint32_t> prepare_order(const PackageGraph &graph) {
    const auto key_for = [](const PackageNode &node) {
        if (!node.package_key.empty()) {
            return node.package_key;
        }
        if (!node.path.empty()) {
            return node.path;
        }
        return std::to_string(node.file_id);
    };
    std::unordered_map<uint32_t, std::string> key_of;
    std::unordered_map<std::string, std::vector<uint32_t>> files;
    std::vector<std::string> packages;
    for (const PackageNode &node : graph.nodes) {
        const std::string key = key_for(node);
        key_of.emplace(node.file_id, key);
        if (!files.contains(key)) {
            packages.push_back(key);
        }
        files[key].push_back(node.file_id);
    }
    std::unordered_map<std::string, std::vector<std::string>> deps;
    for (const ImportEdge &edge : graph.edges) {
        const auto from = key_of.find(edge.from_file);
        const auto to = key_of.find(edge.to_file);
        if (from == key_of.end() || to == key_of.end() || from->second == to->second) {
            continue;
        }
        std::vector<std::string> &list = deps[from->second];
        bool seen = false;
        for (const std::string &have : list) {
            if (have == to->second) {
                seen = true;
            }
        }
        if (!seen) {
            list.push_back(to->second);
        }
    }
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    std::vector<uint32_t> order;
    const auto visit = [&](auto &&self, const std::string &key) -> void {
        if (visited.contains(key) || visiting.contains(key)) {
            return;
        }
        visiting.insert(key);
        const auto dep = deps.find(key);
        if (dep != deps.end()) {
            for (const std::string &next : dep->second) {
                self(self, next);
            }
        }
        visiting.erase(key);
        visited.insert(key);
        const auto group = files.find(key);
        if (group == files.end()) {
            return;
        }
        for (const uint32_t id : group->second) {
            order.push_back(id);
        }
    };
    for (const std::string &key : packages) {
        visit(visit, key);
    }
    return order;
}

} // namespace

void Checker::analyze_graph(const PackageGraph &graph) {
    graph_ = &graph;
    ensure_field_info();
    for (const PackageNode &node : graph.nodes) {
        collect_templates(const_cast<TranslationUnit &>(node.unit), node.file_id);
    }
    if (!graph.nodes.empty()) {
        const uint32_t entry = graph.nodes.front().file_id;
        for (const PackageNode &node : graph.nodes) {
            if (node.file_id == entry) {
                continue;
            }
            for (const TopDecl &decl : node.unit.decls) {
                const auto *fn = std::get_if<FunctionDecl>(&decl.data);
                if (fn != nullptr && fn->name == "main" && fn->body != nullptr) {
                    error(fn->loc, "multiple main");
                }
            }
        }
    }
    for (const PackageNode &node : graph.nodes) {
        declare_struct_tags(node.unit, node.file_id);
    }
    for (const PackageNode &node : graph.nodes) {
        fold_early_consts(const_cast<TranslationUnit &>(node.unit), node.file_id);
    }
    for (const PackageNode &node : graph.nodes) {
        register_enums(node.unit, node.file_id);
    }
    struct PendingTypedef {
        uint32_t file_id = 0;
        const TypedefDecl *item = nullptr;
        bool done = false;
    };
    std::vector<PendingTypedef> typedefs;
    for (const PackageNode &node : graph.nodes) {
        file_id_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item == nullptr || item->name.empty()) {
                continue;
            }
            if (!item->type_params.empty()) {
                register_tuple_alias_template(*item);
                continue;
            }
            if (typedef_target_ready(*item)) {
                register_one_typedef(*item);
            } else {
                typedefs.push_back(PendingTypedef{node.file_id, item, false});
            }
        }
    }
    bool progress = true;
    while (progress) {
        progress = false;
        for (PendingTypedef &item : typedefs) {
            if (item.done || item.item == nullptr) {
                continue;
            }
            file_id_ = item.file_id;
            if (!typedef_target_ready(*item.item)) {
                continue;
            }
            register_one_typedef(*item.item);
            item.done = true;
            progress = true;
        }
    }
    for (PendingTypedef &item : typedefs) {
        if (item.done || item.item == nullptr) {
            continue;
        }
        file_id_ = item.file_id;
        register_one_typedef(*item.item);
    }
    for (const PackageNode &node : graph.nodes) {
        register_traits(node.unit, node.file_id);
    }
    inherit_traits();
    define_struct_fields();
    define_trait_fields();
    link_parent_fields();
    check_unions();
    for (const PackageNode &node : graph.nodes) {
        publish_exports(node.unit, node.file_id);
    }
    for (const uint32_t id : prepare_order(graph)) {
        const PackageNode *node = nullptr;
        for (const PackageNode &item : graph.nodes) {
            if (item.file_id == id) {
                node = &item;
                break;
            }
        }
        if (node != nullptr) {
            prepare_unit(node->unit, node->file_id, node);
        }
    }
    for (const PackageNode &node : graph.nodes) {
        check_unit(node.unit, node.file_id, &node);
    }
    flush_appends();
    publish_reflected_fields();
    graph_ = nullptr;
}

} // namespace tlc
