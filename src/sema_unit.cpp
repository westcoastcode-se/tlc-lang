// Packages: structs, enums, traits, globals, and the check order.
#include "check.hpp"
#include "check_util.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tlc {

void Checker::declare_globals(const TranslationUnit &unit) {
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            std::vector<Type> params;
            for (const Param &param : fn->params) {
                params.push_back(type_of_param(param));
            }
            Type result = function_result(*fn);
            Type signature = type_function(std::move(result), std::move(params), fn->variadic);
            if (signature.result != nullptr && mentions_trait(*signature.result)) {
                error(fn->loc, "not supported yet");
            }
            bool generic = false;
            for (const Type &param : signature.params) {
                if (mentions_trait(param)) {
                    generic = true;
                }
            }
            if (fn->variadic && !fn->extern_c) {
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
            if (generic && signature.result != nullptr && signature.result->kind == TypeKind::Tuple) {
                error(fn->loc, "not supported yet");
            }
            if (generic && (!fn->method_struct.empty() || fn->exported || fn->is_extern || fn->variadic)) {
                error(fn->loc, "not supported yet");
            }
            if (!fn->method_struct.empty()) {
                if (fn->exported || fn->is_extern) {
                    error(fn->loc, "not supported yet");
                }
                if (package_trait(file_id_, fn->method_struct) >= 0) {
                    error(fn->loc, "type mismatch");
                }
                const int receiver_refs = fn->params.empty() ? -1 : fn->params.front().indirections;
                const bool receiver_shape = !fn->params.empty() && fn->params.front().is_receiver &&
                                            (receiver_refs == 0 || receiver_refs == 1) && !fn->params.front().is_array &&
                                            fn->params.front().type_package.empty() &&
                                            fn->params.front().type_name == fn->method_struct;
                const bool receiver_ok =
                    receiver_shape && (fn->params.front().is_struct || package_has_enum(file_id_, fn->method_struct));
                if (!receiver_ok) {
                    error(fn->loc, "type mismatch");
                }
                // The method is written beside its struct or enum. Other files in the package can still call it.
                if (defined_in(fn->method_struct) != file_id_) {
                    error(fn->loc, "method in another file");
                    continue;
                }
                auto &table = methods_[file_id_][fn->method_struct];
                const auto found = table.find(fn->name);
                if (found != table.end()) {
                    if (!same_type(found->second.type, signature) || (found->second.has_body && fn->body)) {
                        error(fn->loc, "redefinition");
                    } else if (fn->body) {
                        found->second.has_body = true;
                    }
                } else {
                    const int struct_id = find_tag(file_id_, fn->method_struct);
                    if (struct_id >= 0 && find_method(struct_id, fn->name) != nullptr) {
                        error(fn->loc, "redefinition");
                    } else {
                        table.emplace(fn->name, MethodInfo{std::move(signature), fn->body != nullptr});
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
                if (!existing->is_function || !same_type(existing->type, signature) ||
                    (existing->has_body && fn->body)) {
                    error(fn->loc, "redefinition");
                } else if (fn->body) {
                    existing->has_body = true;
                    note_body(fn->name);
                    if (fn->is_static) {
                        const auto table = statics_.find(file_id_);
                        if (table != statics_.end()) {
                            const auto prior = table->second.find(fn->name);
                            if (prior != table->second.end()) {
                                prior->second.has_body = true;
                            }
                        }
                    }
                }
                continue;
            }
            if (shared_has(fn->name)) {
                error(fn->loc, "redefinition");
                continue;
            }
            Symbol symbol;
            symbol.name = fn->name;
            symbol.type = std::move(signature);
            symbol.is_function = true;
            symbol.has_body = fn->body != nullptr;
            if (fn->is_static) {
                statics_[file_id_].insert_or_assign(fn->name, symbol);
                declare(std::move(symbol), fn->loc);
            } else if (remember_shared(symbol, fn->loc)) {
                declare(std::move(symbol), fn->loc);
            }
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            Type alias;
            if (package_typedef(file_id_, var->name, alias) || package_trait(file_id_, var->name) >= 0) {
                error(var->loc, "redefinition");
                continue;
            }
            if (lookup_scope(var->name) != nullptr || shared_has(var->name)) {
                error(var->loc, "redefinition");
                continue;
            }
            Type type = type_invalid();
            if (var->inferred) {
                if (var->init == nullptr) {
                    error(var->loc, "type mismatch");
                } else {
                    type = check_expr(*var->init, true);
                    if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Void) {
                        error(var->loc, "type mismatch");
                        type = type_invalid();
                    }
                }
            } else {
                type = type_of_var(*var);
            }
            Symbol symbol;
            symbol.name = var->name;
            symbol.type = std::move(type);
            if (var->is_static) {
                statics_[file_id_].insert_or_assign(var->name, symbol);
                declare(std::move(symbol), var->loc);
            } else if (remember_shared(symbol, var->loc)) {
                declare(std::move(symbol), var->loc);
            }
        }
    }
}

void Checker::check_globals(const TranslationUnit &unit) {
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            check_function(*fn);
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            if (var->is_array && !var->ptr_to_array && var->init) {
                error(var->loc, "not supported yet");
            } else if (var->init) {
                const Symbol *symbol = lookup(var->name);
                const Type want = symbol != nullptr ? symbol->type : type_invalid();
                if (mentions_trait(want)) {
                    error(var->loc, "not supported yet");
                    continue;
                }
                check_against(*var->init, want);
            }
        }
    }
}

void Checker::analyze(const TranslationUnit &unit) {
    register_structs(unit, 0);
    register_enums(unit, 0);
    register_traits(unit, 0);
    register_typedefs(unit, 0);
    publish_exports(unit, 0);
    analyze_unit(unit, 0, nullptr);
}

const PackageNode *Checker::find_node(uint32_t file_id) const {
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.file_id == file_id) {
            return &node;
        }
    }
    return nullptr;
}

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
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const ImportEdge &edge : graph_->edges) {
        if (edge.from_file == from.file_id && edge.spec == import.path && edge.loc.file == import.loc.file &&
            edge.loc.offset == import.loc.offset) {
            return find_node(edge.to_file);
        }
    }
    return nullptr;
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
            if (generic) {
                continue;
            }
            std::vector<Type> params;
            for (const Param &param : fn->params) {
                params.push_back(type_of_param(param));
            }
            Type result = function_result(*fn);
            Symbol symbol;
            symbol.name = fn->name;
            symbol.type = type_function(std::move(result), std::move(params), fn->variadic);
            symbol.is_function = true;
            symbol.has_body = fn->body != nullptr;
            auto &table = exports_[file_id];
            const auto found = table.find(fn->name);
            if (found == table.end()) {
                if (exported_elsewhere(fn->name)) {
                    error(fn->loc, "redefinition");
                } else {
                    table.emplace(fn->name, std::move(symbol));
                }
            } else if (found->second.is_type || !found->second.is_function) {
                error(fn->loc, "redefinition");
            } else if (fn->body) {
                found->second.has_body = true;
            }
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
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
            symbol.type = published_object(var->type_name, var->indirections, var->is_array, var->array_len, var->loc,
                                           false, var->type_package, var->is_struct, var->is_enum);
            exports_[file_id].emplace(var->name, std::move(symbol));
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
    check_globals(unit);
    pop();
}

void Checker::analyze_graph(const PackageGraph &graph) {
    graph_ = &graph;
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
    define_struct_fields();
    for (const PackageNode &node : graph.nodes) {
        register_enums(node.unit, node.file_id);
    }
    for (const PackageNode &node : graph.nodes) {
        register_traits(node.unit, node.file_id);
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
        publish_exports(node.unit, node.file_id);
    }
    for (const PackageNode &node : graph.nodes) {
        prepare_unit(node.unit, node.file_id, &node);
    }
    for (const PackageNode &node : graph.nodes) {
        check_unit(node.unit, node.file_id, &node);
    }
    graph_ = nullptr;
}

} // namespace tlc
