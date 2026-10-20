// Instantiating a generic function or struct, and trait default methods.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/generic.hpp"

namespace tlc {

void Checker::check_isolated(uint32_t file, const FunctionDecl &fn) {
    auto saved_scopes = std::move(scopes_);
    const uint32_t saved_file = file_id_;
    scopes_.clear();
    file_id_ = file;
    push();
    if (const PackageNode *node = find_node(file)) {
        bind_imports(*node);
    }
    if (const auto found = statics_.find(file); found != statics_.end()) {
        for (const auto &entry : found->second) {
            declare(entry.second, {});
        }
    }
    check_function(fn);
    pop();
    scopes_ = std::move(saved_scopes);
    file_id_ = saved_file;
}

void Checker::queue_spec(uint32_t file, FunctionDecl fn) {
    PendingAppend item;
    item.file_id = file;
    item.decl.data = std::move(fn);
    pending_appends_.push_back(std::move(item));
}

std::string Checker::specialize_type_app(const std::string &package, const std::string &name, const std::vector<TypeArg> &args,
                                         SourceLoc loc) {
    // `pkg.Vec[i32]` is written in the importing file. The spec is stored on the defining package,
    // and that file looks the resulting name up among its own tags.
    const uint32_t caller = file_id_;
    uint32_t owner = file_id_;
    const StructDecl *templ = nullptr;
    if (!package.empty()) {
        const Symbol *symbol = lookup(package);
        if (symbol == nullptr || !symbol->is_package) {
            error(loc, "undeclared identifier");
            return {};
        }
        owner = symbol->package_file;
        for (const uint32_t id : files_in_package(symbol->package_file)) {
            const auto file = struct_templates_.find(id);
            if (file == struct_templates_.end()) {
                continue;
            }
            const auto found = file->second.find(name);
            if (found == file->second.end()) {
                continue;
            }
            owner = id;
            templ = found->second;
            break;
        }
        if (templ == nullptr || !templ->exported) {
            if (templ == nullptr) {
                const Symbol *member = find_export(symbol->package_file, name);
                if (member != nullptr) {
                    error(loc, "type mismatch");
                    return {};
                }
            }
            error(loc, name + " is not exported");
            return {};
        }
    } else {
        templ = find_struct_template(name, owner);
        if (templ == nullptr) {
            error(loc, "type mismatch");
            return {};
        }
    }
    if (args.size() != templ->type_params.size()) {
        error(loc, "argument count");
        return {};
    }
    std::vector<SpecArg> bounds;
    bounds.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        SpecArg bound;
        if (!bind_type_arg(templ->type_params[i], args[i], bound, args[i].loc)) {
            return {};
        }
        if (templ->type_params[i].is_trait) {
            if (!templ->type_params[i].constraint_package.empty()) {
                error(loc, "not supported yet");
                return {};
            }
            const int trait_id = package_trait(owner, templ->type_params[i].constraint);
            if (trait_id < 0) {
                error(loc, "undeclared identifier");
                return {};
            }
            if (bound.is_value || bound.is_array || bound.indirections > 1) {
                error(loc, "type mismatch");
                return {};
            }
            const int struct_id = bound.struct_id >= 0 ? bound.struct_id : find_tag(file_id_, bound.type_name);
            if (!require_satisfies(struct_id, trait_id, bound.indirections == 1, loc, owner)) {
                return {};
            }
        }
        bounds.push_back(std::move(bound));
    }
    disambiguate_bounds(owner, bounds);
    const std::string spec_name = mangle_spec(templ->name, bounds);
    const std::string key = std::to_string(owner) + "\n" + spec_name;
    const auto note_caller = [&](const std::string &spec) {
        if (caller == owner) {
            return;
        }
        const int id = find_tag(owner, spec);
        if (id >= 0) {
            tags_[caller][spec] = id;
        }
    };
    if (spec_ready_.contains(key) || find_tag(owner, spec_name) >= 0) {
        // A value may have specialized the struct before anyone read `Box[i32].extra`.
        const uint32_t saved_file = file_id_;
        file_id_ = owner;
        expose_bounds(owner, bounds);
        instantiate_assoc(owner, *templ, spec_name, bounds);
        file_id_ = saved_file;
        note_caller(spec_name);
        return spec_name;
    }
    if (spec_checking_.contains(key)) {
        note_caller(spec_name);
        return spec_name;
    }
    spec_checking_.insert(key);
    const uint32_t saved_file = file_id_;
    file_id_ = owner;
    expose_bounds(owner, bounds);
    StructDecl spec = clone_struct(*templ);
    spec.name = spec_name;
    spec.type_params.clear();
    for (FieldDecl &field : spec.fields) {
        substitute_type(templ->type_params, bounds, field.type_name, field.type_package, field.is_struct, field.is_enum,
                        field.indirections, field.optional, field.is_array, field.array_len, field.array_len_name,
                        field.type_args, field.loc, &field.shells, &field.elem_const, &field.const_pointee);
        if (field.init != nullptr) {
            substitute_expr(templ->type_params, bounds, *field.init);
        }
    }
    const int id = static_cast<int>(structs_.size());
    StructInfo info;
    info.name = spec_name;
    info.file_id = owner;
    info.loc = spec.loc;
    info.exported = spec.exported;
    info.anonymous = spec.anonymous;
    info.is_extern = spec.is_extern;
    info.extern_c = spec.extern_c;
    info.align = spec.align;
    info.is_union = spec.is_union;
    info.template_name = templ->name;
    info.spec_args = bounds;
    structs_.push_back(std::move(info));
    tags_[owner][spec_name] = id;
    for (FieldDecl &field : spec.fields) {
        prepare_written(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                        field.optional, field.is_array, field.array_len, field.array_len_name, field.type_args, field.loc);
        fold_shells(field.shells, field.loc);
        Type field_type = field.is_fn_ptr
                              ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                               field.ret_indirections, field.indirections, field.fn_params, field.fn_variadic,
                                               field.fn_results, field.loc, field.ret_optional, field.ret_const_pointee)
                              : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                            false, field.type_package, field.is_struct, field.is_enum, false,
                                            field.optional, field.const_pointee, false, field.elem_const,
                                            field.elem_slice, field.shells);
        StructInfo::Field stored{field.name, std::move(field_type), field.loc, field.exported};
        if (field.init != nullptr) {
            stored.has_init = true;
            stored.init = field.init.get();
        }
        structs_[static_cast<std::size_t>(id)].fields.push_back(std::move(stored));
    }
    structs_[static_cast<std::size_t>(id)].complete = true;
    if (spec.is_union) {
        for (const FieldDecl &field : spec.fields) {
            if (field.init != nullptr) {
                error(field.loc, "not supported yet");
            }
        }
    } else {
        // The value is checked in the struct's file, not in the call that instantiates it.
        std::vector<Scope> saved_scopes = std::move(scopes_);
        scopes_.clear();
        push();
        if (const auto found = statics_.find(owner); found != statics_.end()) {
            for (const auto &[name, symbol] : found->second) {
                scopes_.back().symbols.emplace(name, symbol);
            }
        }
        for (const FieldDecl &field : spec.fields) {
            if (field.init == nullptr) {
                continue;
            }
            const StructInfo::Field *slot = nullptr;
            for (const StructInfo::Field &have : structs_[static_cast<std::size_t>(id)].fields) {
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
        pop();
        scopes_ = std::move(saved_scopes);
    }
    for (const OwnedFn &owned : owned_fns_) {
        if (owned.file_id != owner || owned.fn.method_struct != templ->name) {
            continue;
        }
        FunctionDecl method = clone_function(owned.fn);
        method.method_struct = spec_name;
        method.type_params.clear();
        if (!method.static_method && !method.params.empty()) {
            method.params.front().type_name = spec_name;
            method.params.front().is_struct = true;
        }
        substitute_function(method, templ->type_params, bounds);
        for (Param &param : method.params) {
            prepare_written(param.type_name, param.type_package, param.is_struct, param.is_enum, param.indirections,
                            param.optional, param.is_array, param.array_len, param.array_len_name, param.type_args,
                            param.loc);
        }
        for (ResultSlot &slot : method.results) {
            prepare_written(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections, slot.optional,
                            slot.is_array, slot.array_len, slot.array_len_name, slot.type_args, method.loc);
        }
        if (!method.results.empty()) {
            const ResultSlot &first = method.results.front();
            method.return_type = first.type_name;
            method.type_package = first.type_package;
            method.is_struct = first.is_struct;
            method.is_enum = first.is_enum;
            method.indirections = first.indirections;
            method.optional = first.optional;
        }
        Type signature = function_signature(method);
        const bool has_body = method.body != nullptr;
        const bool is_static = method.static_method;
        const bool unsafe = method.unsafe;
        const bool exported = method.exported;
        const std::string method_name = method.name;
        deferred_methods_.push_back(OwnedFn{owner, std::move(method)});
        FunctionDecl *stable = &deferred_methods_.back().fn;
        auto &table = methods_[owner][spec_name];
        const auto found = table.find(method_name);
        if (found == table.end()) {
            MethodInfo info;
            info.type = std::move(signature);
            info.has_body = has_body;
            info.is_static = is_static;
            info.unsafe_fn = unsafe;
            info.exported = exported;
            info.file_id = owner;
            info.decl = stable;
            table.emplace(method_name, std::move(info));
        } else {
            const bool skip = !is_static;
            MethodInfo *slot = nullptr;
            if (same_selection(found->second.type, !found->second.is_static, signature, skip)) {
                slot = &found->second;
            } else {
                for (MethodInfo &extra : found->second.more) {
                    if (same_selection(extra.type, !extra.is_static, signature, skip)) {
                        slot = &extra;
                        break;
                    }
                }
            }
            if (slot != nullptr) {
                const bool clash = !same_type(slot->type, signature) || (slot->has_body && has_body) ||
                                   slot->unsafe_fn != unsafe;
                if (!clash && has_body) {
                    slot->has_body = true;
                    slot->decl = stable;
                    slot->type = signature;
                }
            } else {
                MethodInfo extra;
                extra.type = std::move(signature);
                extra.has_body = has_body;
                extra.is_static = is_static;
                extra.unsafe_fn = unsafe;
                extra.exported = exported;
                extra.file_id = owner;
                extra.decl = stable;
                found->second.more.push_back(std::move(extra));
            }
        }
    }
    instantiate_assoc(owner, *templ, spec_name, bounds);
    PendingAppend appended;
    appended.file_id = owner;
    appended.decl.data = std::move(spec);
    pending_appends_.push_back(std::move(appended));
    file_id_ = saved_file;
    spec_checking_.erase(key);
    spec_ready_.insert(key);
    note_caller(spec_name);
    return spec_name;
}

void Checker::absorb_template(Symbol &existing, const FunctionDecl &fn, FunctionDecl *stable, bool report) {
    if (stable == nullptr || !existing.is_function || !existing.is_template) {
        if (report) {
            error(fn.loc, "redefinition");
        }
        return;
    }
    const std::string want = generic_param_key(fn, fn.type_params, false);
    const std::string full = generic_full_key(fn, fn.type_params);
    for (std::size_t i = 0; i < existing.fn_templates.size(); ++i) {
        const FunctionDecl *slot = existing.fn_templates[i];
        if (slot == nullptr || generic_param_key(*slot, slot->type_params, false) != want) {
            continue;
        }
        if (generic_full_key(*slot, slot->type_params) != full || (slot->body != nullptr && fn.body != nullptr) ||
            slot->unsafe != fn.unsafe) {
            if (report) {
                error(fn.loc, "redefinition");
            }
            return;
        }
        if (fn.body != nullptr) {
            existing.fn_templates[i] = stable;
            existing.has_body = true;
            existing.fn_template = existing.fn_templates.front();
        }
        return;
    }
    existing.fn_templates.push_back(stable);
    if (existing.fn_template == nullptr) {
        existing.fn_template = stable;
    }
}

void Checker::mirror_template(const Symbol &symbol) {
    if (!symbol.is_template) {
        return;
    }
    if (graph_ != nullptr) {
        if (const PackageNode *self = find_node(file_id_)) {
            const auto package = package_syms_.find(self->package_key);
            if (package != package_syms_.end()) {
                const auto found = package->second.find(symbol.name);
                if (found != package->second.end() && found->second.is_template) {
                    found->second.fn_template = symbol.fn_template;
                    found->second.fn_templates = symbol.fn_templates;
                    found->second.has_body = symbol.has_body;
                    found->second.unsafe_fn = symbol.unsafe_fn;
                }
            }
        }
    }
    const auto table = statics_.find(file_id_);
    if (table == statics_.end()) {
        return;
    }
    const auto prior = table->second.find(symbol.name);
    if (prior != table->second.end() && prior->second.is_template) {
        prior->second.fn_template = symbol.fn_template;
        prior->second.fn_templates = symbol.fn_templates;
        prior->second.has_body = symbol.has_body;
        prior->second.unsafe_fn = symbol.unsafe_fn;
    }
}

FunctionDecl *Checker::stable_template(uint32_t file, const FunctionDecl &fn) const {
    const auto files = fn_templates_.find(file);
    if (files == fn_templates_.end()) {
        return nullptr;
    }
    const auto found = files->second.find(fn.name);
    if (found == files->second.end()) {
        return nullptr;
    }
    for (FunctionDecl *templ : found->second) {
        if (templ != nullptr && templ->loc.offset == fn.loc.offset && templ->loc.file == fn.loc.file) {
            return templ;
        }
    }
    return nullptr;
}

void Checker::generic_method_clash(const FunctionDecl &fn) {
    uint32_t owner = file_id_;
    const StructDecl *templ = find_struct_template(fn.method_struct, owner);
    const std::vector<TypeParam> empty;
    const std::vector<TypeParam> &params = templ == nullptr ? empty : templ->type_params;
    const std::string want = generic_param_key(fn, params, !fn.static_method);
    const std::string full = generic_full_key(fn, params);
    for (const OwnedFn &owned : owned_fns_) {
        if (owned.file_id != file_id_ || owned.fn.method_struct != fn.method_struct || owned.fn.name != fn.name) {
            continue;
        }
        if (owned.fn.loc.offset == fn.loc.offset && owned.fn.loc.file == fn.loc.file) {
            continue;
        }
        if (owned.fn.loc.file == fn.loc.file && owned.fn.loc.offset > fn.loc.offset) {
            continue;
        }
        if (generic_param_key(owned.fn, params, !owned.fn.static_method) != want) {
            continue;
        }
        if (generic_full_key(owned.fn, params) != full || (owned.fn.body != nullptr && fn.body != nullptr) ||
            owned.fn.unsafe != fn.unsafe) {
            error(fn.loc, "redefinition");
            return;
        }
    }
}

namespace {

// `value *T` is the pointer form. A by-value `T` in the same list keeps the value rule.
bool constraint_through_pointer(const FunctionDecl &templ, const TypeParam &param) {
    bool value = false;
    bool pointer = false;
    for (const Param &slot : templ.params) {
        if (slot.is_receiver || slot.is_array || !slot.type_args.empty() || !slot.type_package.empty() ||
            slot.type_name != param.name) {
            continue;
        }
        if (slot.indirections == 0) {
            value = true;
        } else if (slot.indirections == 1) {
            pointer = true;
        }
    }
    return pointer && !value;
}

} // namespace

std::optional<std::vector<SpecArg>> Checker::infer_generic_bounds(const FunctionDecl &templ, const Expr::Call &call,
                                                                  const std::vector<Type> &arg_types) {
    int pack_at = -1;
    for (std::size_t i = 0; i < templ.type_params.size(); ++i) {
        if (!templ.type_params[i].is_pack) {
            continue;
        }
        if (pack_at >= 0 || i + 1 != templ.type_params.size()) {
            error(call.loc, "not supported yet");
            return std::nullopt;
        }
        pack_at = static_cast<int>(i);
    }
    const bool trailing_pack = pack_at >= 0;
    const std::size_t fixed = trailing_pack ? templ.type_params.size() - 1 : templ.type_params.size();
    if (!trailing_pack && call.type_args.size() > templ.type_params.size()) {
        error(call.loc, "argument count");
        return std::nullopt;
    }
    // A method's first parameter is the receiver. The call does not pass it.
    const std::size_t receiver = !templ.params.empty() && templ.params.front().is_receiver ? 1 : 0;
    if (call.args.size() + receiver != templ.params.size()) {
        error(call.loc, "argument count");
        return std::nullopt;
    }
    std::vector<SpecArg> bounds(templ.type_params.size());
    std::vector<char> set(templ.type_params.size(), 0);
    if (trailing_pack && call.type_args.size() > fixed) {
        for (std::size_t i = 0; i < fixed; ++i) {
            if (!bind_type_arg(templ.type_params[i], call.type_args[i], bounds[i], call.type_args[i].loc)) {
                return std::nullopt;
            }
            set[i] = 1;
        }
        std::vector<SpecArg> parts;
        parts.reserve(call.type_args.size() - fixed);
        TypeParam as_type = templ.type_params[static_cast<std::size_t>(pack_at)];
        as_type.is_pack = false;
        as_type.is_type = true;
        for (std::size_t i = fixed; i < call.type_args.size(); ++i) {
            SpecArg bound;
            if (!bind_type_arg(as_type, call.type_args[i], bound, call.type_args[i].loc)) {
                return std::nullopt;
            }
            parts.push_back(std::move(bound));
        }
        bounds[static_cast<std::size_t>(pack_at)] = pack_bound(std::move(parts));
        set[static_cast<std::size_t>(pack_at)] = 1;
    } else {
        for (std::size_t i = 0; i < call.type_args.size(); ++i) {
            if (!bind_type_arg(templ.type_params[i], call.type_args[i], bounds[i], call.type_args[i].loc)) {
                return std::nullopt;
            }
            set[i] = 1;
        }
    }
    auto constrain = [&](int index, const Type &got) {
        if (index < 0 || got.kind == TypeKind::Invalid) {
            return true;
        }
        SpecArg spelled;
        if (!spell_type(got, spelled)) {
            return false;
        }
        if (set[static_cast<std::size_t>(index)]) {
            return same_spec(bounds[static_cast<std::size_t>(index)], spelled);
        }
        bounds[static_cast<std::size_t>(index)] = std::move(spelled);
        set[static_cast<std::size_t>(index)] = 1;
        return true;
    };
    for (std::size_t param_i = 0; param_i < templ.params.size(); ++param_i) {
        const Param &param = templ.params[param_i];
        if (param.is_receiver || param_i < receiver || param_i - receiver >= arg_types.size()) {
            continue;
        }
        const Type &got = arg_types[param_i - receiver];
        if (got.kind == TypeKind::Invalid) {
            continue;
        }
        const int index = param.type_package.empty() ? param_index(templ.type_params, param.type_name) : -1;
        const bool plain = index >= 0 && param.type_args.empty();
        if (!plain) {
            if (got.kind == TypeKind::Struct && !param.type_args.empty()) {
                const StructInfo &info = structs_[static_cast<std::size_t>(got.struct_id)];
                if (info.template_name == param.type_name && info.spec_args.size() == param.type_args.size()) {
                    for (std::size_t arg_i = 0; arg_i < param.type_args.size(); ++arg_i) {
                        const int slot = param_index(templ.type_params, param.type_args[arg_i].type_name);
                        if (slot >= 0 && !templ.type_params[static_cast<std::size_t>(slot)].is_value &&
                            param.type_args[arg_i].indirections == 0 && param.type_args[arg_i].args.empty()) {
                            const SpecArg &known = info.spec_args[arg_i];
                            if (set[static_cast<std::size_t>(slot)] &&
                                !same_spec(bounds[static_cast<std::size_t>(slot)], known)) {
                                error(call.loc, "type mismatch");
                                return std::nullopt;
                            }
                            bounds[static_cast<std::size_t>(slot)] = known;
                            set[static_cast<std::size_t>(slot)] = 1;
                        }
                    }
                }
            }
            continue;
        }
        if (templ.type_params[static_cast<std::size_t>(index)].is_value) {
            continue;
        }
        if (templ.type_params[static_cast<std::size_t>(index)].is_pack && param.indirections == 0 && !param.is_array) {
            if (got.kind == TypeKind::Tuple) {
                if (got.results.empty()) {
                    error(call.loc, "type mismatch");
                    return std::nullopt;
                }
                std::vector<SpecArg> parts;
                parts.reserve(got.results.size());
                for (const Type &part : got.results) {
                    SpecArg spelled;
                    if (!spell_type(part, spelled)) {
                        error(call.loc, "type mismatch");
                        return std::nullopt;
                    }
                    parts.push_back(std::move(spelled));
                }
                const SpecArg packed = pack_bound(std::move(parts));
                if (set[static_cast<std::size_t>(index)] &&
                    !same_spec(bounds[static_cast<std::size_t>(index)], packed)) {
                    error(call.loc, "type mismatch");
                    return std::nullopt;
                }
                bounds[static_cast<std::size_t>(index)] = packed;
                set[static_cast<std::size_t>(index)] = 1;
            } else if (!constrain(index, got)) {
                error(call.loc, "type mismatch");
                return std::nullopt;
            }
            continue;
        }
        const Type *cursor = &got;
        bool peel_ok = true;
        for (int star = 0; star < param.indirections; ++star) {
            if (cursor->kind != TypeKind::Pointer || cursor->pointee == nullptr) {
                peel_ok = false;
                break;
            }
            const bool want_optional = (param.optional & (1u << star)) != 0;
            if (!want_optional && cursor->optional) {
                peel_ok = false;
                break;
            }
            cursor = cursor->pointee.get();
        }
        if (!peel_ok) {
            continue;
        }
        if (param.is_array) {
            if (cursor->kind != TypeKind::Array || cursor->element == nullptr) {
                continue;
            }
            if (!param.array_len_name.empty()) {
                const int len_index = param_index(templ.type_params, param.array_len_name);
                if (len_index >= 0 && templ.type_params[static_cast<std::size_t>(len_index)].is_value) {
                    if (set[static_cast<std::size_t>(len_index)] &&
                        bounds[static_cast<std::size_t>(len_index)].value != cursor->length) {
                        error(call.loc, "type mismatch");
                        return std::nullopt;
                    }
                    bounds[static_cast<std::size_t>(len_index)].is_value = true;
                    bounds[static_cast<std::size_t>(len_index)].value = cursor->length;
                    set[static_cast<std::size_t>(len_index)] = 1;
                }
            }
            if (!constrain(index, *cursor->element)) {
                error(call.loc, "type mismatch");
                return std::nullopt;
            }
            continue;
        }
        if (!constrain(index, *cursor)) {
            error(call.loc, "type mismatch");
            return std::nullopt;
        }
    }
    for (std::size_t i = 0; i < set.size(); ++i) {
        if (!set[i]) {
            error(call.loc, "type mismatch");
            return std::nullopt;
        }
        const TypeParam &param = templ.type_params[i];
        if (!param.is_trait) {
            continue;
        }
        if (!param.constraint_package.empty()) {
            error(call.loc, "not supported yet");
            return std::nullopt;
        }
        // The clone is stored under the file id `collect_templates` used. A lone unit
        // registers its trait at 0, while `loc.file` is the source file.
        uint32_t home = file_id_;
        for (const OwnedFn &owned : owned_fns_) {
            if (&owned.fn == &templ) {
                home = owned.file_id;
                break;
            }
        }
        const int trait_id = package_trait(home, param.constraint);
        if (trait_id < 0) {
            error(call.loc, "undeclared identifier");
            return std::nullopt;
        }
        const SpecArg &bound = bounds[i];
        if (bound.is_value || bound.is_array || bound.indirections > 1) {
            error(call.loc, "type mismatch");
            return std::nullopt;
        }
        const int struct_id = bound.struct_id >= 0 ? bound.struct_id : find_tag(file_id_, bound.type_name);
        const bool through_pointer = bound.indirections == 1 || constraint_through_pointer(templ, param);
        if (!require_satisfies(struct_id, trait_id, through_pointer, call.loc, home)) {
            return std::nullopt;
        }
    }
    return bounds;
}

bool Checker::generic_candidate_matches(const FunctionDecl &templ, const Expr::Call &call,
                                       const std::vector<Type> &arg_types, uint32_t owner) {
    (void)owner;
    if (templ.body == nullptr || templ.is_extern || templ.extern_c || templ.variadic || templ.type_params.empty()) {
        return false;
    }
    const std::optional<std::vector<SpecArg>> bounds = infer_generic_bounds(templ, call, arg_types);
    if (!bounds) {
        return false;
    }
    FunctionDecl spec = clone_function(templ);
    substitute_function(spec, templ.type_params, *bounds);
    if (spec.params.size() != arg_types.size()) {
        return false;
    }
    const auto matches = [&](const Param &param, const Type &got) {
        if (param.is_fn_ptr || param.is_tuple || got.kind == TypeKind::Invalid) {
            return false;
        }
        SpecArg spelled;
        if (!spell_type(got, spelled)) {
            return false;
        }
        if (spelled.indirections != param.indirections || spelled.optional != param.optional ||
            spelled.const_pointee != param.const_pointee || spelled.is_array != param.is_array ||
            spelled.elem_const != param.elem_const || (param.is_array && spelled.array_len != param.array_len)) {
            return false;
        }
        if (!param.type_args.empty()) {
            const Type *core = &got;
            for (int depth = 0; depth < param.indirections && core->kind == TypeKind::Pointer && core->pointee != nullptr;
                 ++depth) {
                core = core->pointee.get();
            }
            if (param.is_array && core->kind == TypeKind::Array && core->element != nullptr) {
                core = core->element.get();
            }
            if (core->kind != TypeKind::Struct || core->struct_id < 0 ||
                core->struct_id >= static_cast<int>(structs_.size())) {
                return false;
            }
            const StructInfo &info = structs_[static_cast<std::size_t>(core->struct_id)];
            if (info.template_name != param.type_name || info.spec_args.size() != param.type_args.size()) {
                return false;
            }
            for (std::size_t arg_i = 0; arg_i < param.type_args.size(); ++arg_i) {
                const TypeArg &arg = param.type_args[arg_i];
                const SpecArg &bound = info.spec_args[arg_i];
                if (arg.has_value || arg.is_value || bound.is_value) {
                    if (!(bound.is_value && (arg.has_value || arg.is_value) && bound.value == arg.value)) {
                        return false;
                    }
                    continue;
                }
                if (bound.type_name != arg.type_name || bound.type_package != arg.type_package ||
                    bound.indirections != arg.indirections || bound.optional != arg.optional ||
                    bound.const_pointee != arg.const_pointee || bound.is_array != arg.is_array ||
                    (arg.is_array && bound.array_len != arg.array_len)) {
                    return false;
                }
            }
            return true;
        }
        return spelled.type_name == param.type_name && spelled.type_package == param.type_package;
    };
    for (std::size_t i = 0; i < spec.params.size(); ++i) {
        if (!matches(spec.params[i], arg_types[i])) {
            return false;
        }
    }
    return true;
}

std::optional<Type> Checker::maybe_generic_call(Expr &expr, bool used) {
    auto *call_ptr = std::get_if<Expr::Call>(&expr.data);
    if (call_ptr == nullptr || call_ptr->callee == nullptr) {
        return std::nullopt;
    }
    Expr::Call &call = *call_ptr;
    std::vector<const FunctionDecl *> candidates;
    uint32_t owner = file_id_;
    bool selector_call = false;
    const auto take_template = [&](const Symbol *symbol) {
        if (symbol == nullptr || !symbol->is_template) {
            return false;
        }
        if (!symbol->fn_templates.empty()) {
            candidates = symbol->fn_templates;
        } else if (symbol->fn_template != nullptr) {
            candidates.push_back(symbol->fn_template);
        }
        if (candidates.empty()) {
            return false;
        }
        owner = symbol->package_file != 0 ? symbol->package_file : file_id_;
        return true;
    };
    if (const auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
        Symbol *symbol = lookup(named->text);
        const bool found_template = take_template(symbol);
        if (!found_template && !call.type_args.empty()) {
            error(call.loc, "type mismatch");
            for (const auto &arg : call.args) {
                if (arg) {
                    check_expr(*arg, true);
                }
            }
            return type_invalid();
        }
        if (!found_template) {
            return std::nullopt;
        }
    } else if (const auto *selector = std::get_if<Expr::Selector>(&call.callee->data)) {
        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const Symbol *base_symbol = base == nullptr ? nullptr : lookup(base->text);
        if (base_symbol != nullptr && base_symbol->is_package) {
            const SelectorTarget target = resolve_selector(*selector);
            if (target.kind == SelectorTarget::Kind::Failed) {
                for (const auto &arg : call.args) {
                    if (arg) {
                        check_expr(*arg, true);
                    }
                }
                return type_invalid();
            }
            if (take_template(target.symbol)) {
                selector_call = true;
            } else if (!call.type_args.empty()) {
                error(call.loc, "type mismatch");
                for (const auto &arg : call.args) {
                    if (arg) {
                        check_expr(*arg, true);
                    }
                }
                return type_invalid();
            } else {
                return std::nullopt;
            }
        } else if (!call.type_args.empty()) {
            return try_method_template(expr, used);
        } else {
            return std::nullopt;
        }
    } else if (!call.type_args.empty()) {
        error(call.loc, "type mismatch");
        return type_invalid();
    } else {
        return std::nullopt;
    }
    const bool several = candidates.size() > 1;
    const FunctionDecl *templ = candidates.size() == 1 ? candidates.front() : nullptr;
    if (several) {
        std::vector<Type> probed;
        bool arg_invalid = false;
        probed.reserve(call.args.size());
        for (const auto &arg : call.args) {
            if (arg == nullptr) {
                arg_invalid = true;
                probed.push_back(type_invalid());
                continue;
            }
            Type got = check_expr(*arg, true);
            if (got.kind == TypeKind::Invalid) {
                arg_invalid = true;
            }
            probed.push_back(std::move(got));
        }
        if (arg_invalid) {
            return type_invalid();
        }
        std::vector<const FunctionDecl *> arity;
        for (const FunctionDecl *item : candidates) {
            if (item != nullptr && item->params.size() == call.args.size()) {
                arity.push_back(item);
            }
        }
        if (arity.empty()) {
            error(call.loc, "argument count");
            return type_invalid();
        }
        if (arity.size() == 1) {
            templ = arity.front();
        } else {
            const FunctionDecl *winner = nullptr;
            int wins = 0;
            for (const FunctionDecl *item : arity) {
                const bool saved = quiet_;
                quiet_ = true;
                const bool ok = generic_candidate_matches(*item, call, probed, owner);
                quiet_ = saved;
                if (ok) {
                    winner = item;
                    ++wins;
                }
            }
            if (wins != 1 || winner == nullptr) {
                error(call.loc, "type mismatch");
                return type_invalid();
            }
            templ = winner;
        }
    }
    if (templ == nullptr || templ->type_params.empty()) {
        return std::nullopt;
    }
    if (templ->body == nullptr || templ->is_extern || templ->extern_c || templ->variadic) {
        error(call.loc, "not supported yet");
        return type_invalid();
    }
    if (templ->unsafe && unsafe_depth_ == 0) {
        error(call.loc, "unsafe call");
    }
    std::vector<Type> arg_types;
    bool arg_invalid = false;
    for (const auto &arg : call.args) {
        if (arg == nullptr) {
            arg_invalid = true;
            arg_types.push_back(type_invalid());
            continue;
        }
        Type got = check_expr(*arg, true);
        if (got.kind == TypeKind::Invalid) {
            arg_invalid = true;
        }
        arg_types.push_back(std::move(got));
    }
    if (arg_invalid) {
        return type_invalid();
    }
    const auto inferred = infer_generic_bounds(*templ, call, arg_types);
    if (!inferred) {
        return type_invalid();
    }
    std::vector<SpecArg> bounds = *inferred;
    disambiguate_bounds(owner, bounds);
    std::string spec_name = mangle_spec(templ->name, bounds);
    if (several) {
        spec_name.push_back('.');
        spec_name += generic_list_suffix(*templ);
    }
    const std::string key = std::to_string(owner) + "\n" + spec_name;
    auto rewrite = [&]() {
        if (selector_call) {
            std::get<Expr::Selector>(call.callee->data).field = spec_name;
        } else {
            std::get<Expr::Name>(call.callee->data).text = spec_name;
        }
        call.type_args.clear();
    };
    if (spec_ready_.contains(key) || spec_checking_.contains(key)) {
        rewrite();
        return check_raw(expr, used);
    }
    FunctionDecl spec = clone_function(*templ);
    spec.name = spec_name;
    spec.type_params.clear();
    spec.exported = false;
    spec.is_static = templ->is_static;
    substitute_function(spec, templ->type_params, bounds);
    const uint32_t saved_file = file_id_;
    file_id_ = owner;
    expose_bounds(owner, bounds);
    for (Param &param : spec.params) {
        prepare_written(param.type_name, param.type_package, param.is_struct, param.is_enum, param.indirections,
                        param.optional, param.is_array, param.array_len, param.array_len_name, param.type_args, param.loc);
    }
    for (ResultSlot &slot : spec.results) {
        prepare_written(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections, slot.optional,
                        slot.is_array, slot.array_len, slot.array_len_name, slot.type_args, spec.loc);
    }
    if (!spec.results.empty()) {
        const ResultSlot &first = spec.results.front();
        spec.return_type = first.type_name;
        spec.type_package = first.type_package;
        spec.is_struct = first.is_struct;
        spec.is_enum = first.is_enum;
        spec.indirections = first.indirections;
        spec.optional = first.optional;
    }
    if (!resolve_result_typeof(spec)) {
        file_id_ = saved_file;
        return type_invalid();
    }
    Type signature = function_signature(spec);
    file_id_ = saved_file;
    if (signature.kind != TypeKind::Function || signature.result == nullptr || signature.result->kind == TypeKind::Invalid) {
        return type_invalid();
    }
    Symbol symbol;
    symbol.name = spec_name;
    symbol.type = signature;
    symbol.is_function = true;
    symbol.has_body = true;
    symbol.package_file = owner;
    spec_symbols_[owner][spec_name] = symbol;
    if (templ->is_static) {
        static_spec_keys_.insert(std::to_string(owner) + "\n" + spec_name);
        statics_[owner].insert_or_assign(spec_name, symbol);
    }
    if (templ->exported) {
        exports_[owner].insert_or_assign(spec_name, symbol);
    }
    spec_checking_.insert(key);
    check_isolated(owner, spec);
    spec_checking_.erase(key);
    spec_ready_.insert(key);
    queue_spec(owner, std::move(spec));
    rewrite();
    return check_raw(expr, used);
}

namespace {

bool same_param_spelling(const Param &lhs, const Param &rhs) {
    return lhs.type_name == rhs.type_name && lhs.type_package == rhs.type_package && lhs.indirections == rhs.indirections &&
           lhs.optional == rhs.optional && lhs.const_pointee == rhs.const_pointee && lhs.is_array == rhs.is_array &&
           lhs.array_len == rhs.array_len && lhs.is_struct == rhs.is_struct && lhs.is_enum == rhs.is_enum &&
           lhs.elem_const == rhs.elem_const;
}

bool same_result_spelling(const FunctionDecl &lhs, const FunctionDecl &rhs) {
    if (lhs.results.size() != rhs.results.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.results.size(); ++i) {
        const ResultSlot &left = lhs.results[i];
        const ResultSlot &right = rhs.results[i];
        if (left.type_name != right.type_name || left.type_package != right.type_package ||
            left.indirections != right.indirections || left.optional != right.optional || left.is_array != right.is_array ||
            left.array_len != right.array_len || left.is_struct != right.is_struct || left.is_enum != right.is_enum) {
            return false;
        }
    }
    return true;
}

bool same_type_param_kinds(const FunctionDecl &lhs, const FunctionDecl &rhs) {
    if (lhs.type_params.size() != rhs.type_params.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.type_params.size(); ++i) {
        const TypeParam &left = lhs.type_params[i];
        const TypeParam &right = rhs.type_params[i];
        if (left.is_type != right.is_type || left.is_value != right.is_value || left.is_trait != right.is_trait ||
            left.is_pack != right.is_pack || left.constraint != right.constraint) {
            return false;
        }
    }
    return true;
}

// Same name, type-parameter kinds, value parameters after the receiver, and result.
// The receiver kind is not part of the selection. `unsafe` is compared by the caller.
bool same_default_shape(const FunctionDecl &lhs, const FunctionDecl &rhs) {
    if (lhs.name != rhs.name || !same_type_param_kinds(lhs, rhs)) {
        return false;
    }
    const std::size_t left_skip = !lhs.params.empty() && lhs.params.front().is_receiver ? 1 : 0;
    const std::size_t right_skip = !rhs.params.empty() && rhs.params.front().is_receiver ? 1 : 0;
    if (lhs.params.size() < left_skip || rhs.params.size() < right_skip ||
        lhs.params.size() - left_skip != rhs.params.size() - right_skip) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.params.size() - left_skip; ++i) {
        if (!same_param_spelling(lhs.params[left_skip + i], rhs.params[right_skip + i])) {
            return false;
        }
    }
    return same_result_spelling(lhs, rhs);
}

bool type_arg_count_ok(const FunctionDecl &fn, const Expr::Call &call) {
    if (fn.type_params.empty()) {
        return false;
    }
    if (fn.type_params.back().is_pack) {
        return call.type_args.size() + 1 >= fn.type_params.size();
    }
    return call.type_args.size() == fn.type_params.size();
}

} // namespace

void Checker::register_trait_default(const FunctionDecl &fn) {
    const int trait_id = package_trait(file_id_, fn.method_struct);
    if (trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        return;
    }
    if (fn.is_extern || fn.variadic) {
        error(fn.loc, "not supported yet");
        return;
    }
    if (fn.body == nullptr) {
        error(fn.loc, "not supported yet");
        return;
    }
    const bool receiver_ok = !fn.static_method && !fn.params.empty() && fn.params.front().is_receiver &&
                             fn.params.front().type_name == fn.method_struct && fn.params.front().type_package.empty() &&
                             !fn.params.front().is_array && fn.params.front().indirections <= 1;
    if (!receiver_ok) {
        error(fn.loc, "type mismatch");
        return;
    }
    // `Allocator` is built in and its file id stays 0, which is also the primary file.
    if (trait_id != allocator_trait_id_ && traits_[static_cast<std::size_t>(trait_id)].file_id != file_id_) {
        error(fn.loc, "method in another file");
        return;
    }
    TraitInfo &info = traits_[static_cast<std::size_t>(trait_id)];
    for (const FunctionDecl *existing : info.defaults) {
        if (existing != nullptr && same_default_shape(*existing, fn)) {
            error(fn.loc, "redefinition");
            return;
        }
    }
    if (fn.type_params.empty()) {
        const Type tail = method_tail(fn);
        for (const TraitInfo::Method &method : info.methods) {
            if (method.name == fn.name && method.type.kind == TypeKind::Function && same_type(tail, method.type) &&
                fn.unsafe != method.unsafe_fn) {
                error(fn.loc, "redefinition");
                return;
            }
        }
    }
    info.defaults.push_back(&fn);
}

void Checker::register_method_template(const FunctionDecl &fn) {
    if (fn.type_params.empty()) {
        return;
    }
    for (std::size_t i = 0; i < fn.type_params.size(); ++i) {
        if (fn.type_params[i].is_pack && i + 1 != fn.type_params.size()) {
            error(fn.type_params[i].loc, "not supported yet");
        }
    }
    const int struct_id = find_tag(file_id_, fn.method_struct);
    if (struct_id < 0) {
        // An alias such as `type Integer i32` is not a method template.
        error(fn.loc, "not supported yet");
        return;
    }
    if (fn.is_extern || fn.variadic || fn.exported || fn.body == nullptr || fn.static_method) {
        error(fn.loc, "not supported yet");
        return;
    }
    if (defined_in(fn.method_struct) != file_id_) {
        error(fn.loc, "method in another file");
        return;
    }
    const bool receiver_ok = !fn.params.empty() && fn.params.front().is_receiver &&
                             fn.params.front().type_name == fn.method_struct && fn.params.front().type_package.empty() &&
                             !fn.params.front().is_array && fn.params.front().indirections <= 1;
    if (!receiver_ok) {
        error(fn.loc, "type mismatch");
        return;
    }
    auto &bucket = method_templates_[file_id_][fn.method_struct][fn.name];
    for (const FunctionDecl *existing : bucket) {
        if (existing != nullptr && same_default_shape(*existing, fn)) {
            error(fn.loc, "redefinition");
            return;
        }
    }
    bucket.push_back(&fn);
}

Type Checker::method_tail(const FunctionDecl &fn) {
    std::vector<Type> params;
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i == 0 && fn.params[i].is_receiver) {
            continue;
        }
        params.push_back(type_of_param(fn.params[i]));
    }
    return type_function(function_result(fn), std::move(params), fn.variadic);
}

bool Checker::plain_default_supplies(int trait_id, const TraitInfo::Method &method, bool through_pointer) {
    if (trait_id < 0 || trait_id >= static_cast<int>(traits_.size()) || method.type.kind != TypeKind::Function) {
        return false;
    }
    for (const FunctionDecl *def : traits_[static_cast<std::size_t>(trait_id)].defaults) {
        if (def == nullptr || def->name != method.name || !def->type_params.empty() || def->unsafe != method.unsafe_fn ||
            def->body == nullptr || def->params.empty() || !def->params.front().is_receiver) {
            continue;
        }
        const bool pointer = def->params.front().indirections > 0;
        if (pointer && !through_pointer) {
            continue;
        }
        const Type tail = method_tail(*def);
        if (tail.kind == TypeKind::Function && same_type(tail, method.type)) {
            return true;
        }
    }
    return false;
}

const Checker::MethodInfo *Checker::find_method_head(uint32_t file, const std::string &owner, const std::string &name) const {
    const auto methods = methods_.find(file);
    if (methods == methods_.end()) {
        return nullptr;
    }
    const auto group = methods->second.find(owner);
    if (group == methods->second.end()) {
        return nullptr;
    }
    const auto found = group->second.find(name);
    if (found == group->second.end()) {
        return nullptr;
    }
    return &found->second;
}

const FunctionDecl *Checker::install_default(const FunctionDecl &templ, uint32_t struct_file, const std::string &struct_name,
                                             const std::string &spec_name, const std::vector<SpecArg> &bounds) {
    // `function_signature` can grow `structs_`. The caller's name must not be that storage.
    const std::string owner = struct_name;
    if (const MethodInfo *head = find_method_head(struct_file, owner, spec_name)) {
        const auto already = [&](const MethodInfo &slot) {
            const auto origin = default_origin_.find(slot.decl);
            return origin != default_origin_.end() && origin->second == &templ;
        };
        if (already(*head)) {
            return head->decl;
        }
        for (const MethodInfo &extra : head->more) {
            if (already(extra)) {
                return extra.decl;
            }
        }
    }
    FunctionDecl method = clone_function(templ);
    method.method_struct = owner;
    method.name = spec_name;
    const std::vector<TypeParam> params = templ.type_params;
    method.type_params.clear();
    if (!method.params.empty() && method.params.front().is_receiver) {
        method.params.front().type_name = owner;
        method.params.front().is_struct = true;
        method.params.front().type_package.clear();
    }
    substitute_function(method, params, bounds);
    const uint32_t saved_file = file_id_;
    file_id_ = struct_file;
    expose_bounds(struct_file, bounds);
    Type signature = function_signature(method);
    file_id_ = saved_file;
    if (signature.kind != TypeKind::Function || signature.result == nullptr || signature.result->kind == TypeKind::Invalid) {
        return nullptr;
    }
    if (const MethodInfo *head = find_method_head(struct_file, owner, spec_name)) {
        const auto same_slot = [&](const MethodInfo &slot) {
            return same_selection(slot.type, !slot.is_static, signature, true);
        };
        const MethodInfo *slot = nullptr;
        if (same_slot(*head)) {
            slot = head;
        } else {
            for (const MethodInfo &extra : head->more) {
                if (same_slot(extra)) {
                    slot = &extra;
                    break;
                }
            }
        }
        if (slot != nullptr) {
            const auto origin = default_origin_.find(slot->decl);
            if (origin != default_origin_.end() && origin->second != &templ) {
                error(templ.loc, "type mismatch");
            }
            // The struct's method wins. An earlier default with this selection stays.
            return slot->decl;
        }
    }
    deferred_methods_.push_back(OwnedFn{struct_file, std::move(method)});
    FunctionDecl *stable = &deferred_methods_.back().fn;
    default_origin_.emplace(stable, &templ);
    MethodInfo info;
    info.type = std::move(signature);
    info.has_body = stable->body != nullptr;
    info.is_static = false;
    info.unsafe_fn = templ.unsafe;
    info.exported = templ.exported;
    info.file_id = struct_file;
    info.decl = stable;
    auto &table = methods_[struct_file][owner];
    const auto found = table.find(spec_name);
    if (found == table.end()) {
        table.emplace(spec_name, std::move(info));
    } else {
        found->second.more.push_back(std::move(info));
    }
    return stable;
}

void Checker::install_trait_generics(uint32_t struct_file, const std::string &struct_name, int trait_id) {
    if (struct_name.empty() || trait_id < 0) {
        return;
    }
    const int struct_id = find_tag(struct_file, struct_name);
    const std::vector<TraitGenericUse> pending = trait_generic_uses_;
    for (const TraitGenericUse &use : pending) {
        if (use.trait_id != trait_id || use.templ == nullptr || use.spec_name.empty()) {
            continue;
        }
        const bool saved = quiet_;
        quiet_ = true;
        const bool covered = require_satisfies(struct_id, trait_id, use.through_pointer, {}, struct_file);
        quiet_ = saved;
        if (!covered) {
            continue;
        }
        const FunctionDecl *chosen = use.templ;
        std::vector<const FunctionDecl *> matches;
        if (const auto file = method_templates_.find(struct_file); file != method_templates_.end()) {
            const auto owner = file->second.find(struct_name);
            if (owner != file->second.end()) {
                const auto found = owner->second.find(use.templ->name);
                if (found != owner->second.end()) {
                    for (const FunctionDecl *item : found->second) {
                        if (item == nullptr || item->type_params.empty()) {
                            continue;
                        }
                        const bool pack = item->type_params.back().is_pack;
                        const bool count_ok = pack ? use.bounds.size() + 1 >= item->type_params.size()
                                                   : use.bounds.size() == item->type_params.size();
                        if (count_ok) {
                            matches.push_back(item);
                        }
                    }
                }
            }
        }
        if (matches.size() > 1) {
            error(use.templ->loc, "type mismatch");
            continue;
        }
        if (matches.size() == 1) {
            chosen = matches.front();
        }
        install_default(*chosen, struct_file, struct_name, use.spec_name, use.bounds);
    }
}

void Checker::grant_trait_defaults(uint32_t struct_file, const std::string &struct_name, int trait_id) {
    if (quiet_ || trait_id < 0 || trait_id >= static_cast<int>(traits_.size()) || struct_name.empty()) {
        return;
    }
    const std::string owner = struct_name;
    const std::string key = std::to_string(struct_file) + "\n" + owner + "\n" + std::to_string(trait_id);
    if (defaults_ready_.contains(key) || !granting_.insert(key).second) {
        return;
    }
    for (const FunctionDecl *templ : traits_[static_cast<std::size_t>(trait_id)].defaults) {
        if (templ == nullptr || !templ->type_params.empty() || templ->body == nullptr) {
            continue;
        }
        install_default(*templ, struct_file, owner, templ->name, {});
    }
    install_trait_generics(struct_file, owner, trait_id);
    granting_.erase(key);
    defaults_ready_.insert(key);
}

void Checker::grant_covered_defaults(int struct_id) {
    if (quiet_ || struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    const uint32_t struct_file = structs_[static_cast<std::size_t>(struct_id)].file_id;
    const std::string struct_name = structs_[static_cast<std::size_t>(struct_id)].name;
    for (std::size_t trait_i = 0; trait_i < traits_.size(); ++trait_i) {
        bool any_plain = false;
        for (const FunctionDecl *def : traits_[trait_i].defaults) {
            if (def != nullptr && def->type_params.empty()) {
                any_plain = true;
                break;
            }
        }
        if (!any_plain) {
            continue;
        }
        const bool saved = quiet_;
        quiet_ = true;
        const bool covered = require_satisfies(struct_id, static_cast<int>(trait_i), true, {}, struct_file);
        quiet_ = saved;
        if (covered) {
            grant_trait_defaults(struct_file, struct_name, static_cast<int>(trait_i));
        }
    }
}

std::optional<Type> Checker::try_method_template(Expr &expr, bool used) {
    auto *call_ptr = std::get_if<Expr::Call>(&expr.data);
    if (call_ptr == nullptr || call_ptr->callee == nullptr) {
        return std::nullopt;
    }
    Expr::Call &call = *call_ptr;
    auto *selector = std::get_if<Expr::Selector>(&call.callee->data);
    if (selector == nullptr || selector->base == nullptr || call.type_args.empty()) {
        error(call.loc, "not supported yet");
        return type_invalid();
    }
    auto check_args = [&]() {
        for (const auto &arg : call.args) {
            if (arg) {
                check_expr(*arg, true);
            }
        }
    };
    Type probe;
    {
        const bool saved = quiet_;
        quiet_ = true;
        InitQuiet init(*this);
        probe = check_expr(*selector->base, true);
        quiet_ = saved;
    }
    if (probe.kind == TypeKind::Invalid) {
        check_expr(*selector->base, true);
        check_args();
        return type_invalid();
    }
    const bool trait_base = probe.kind == TypeKind::Trait ||
                            (probe.kind == TypeKind::Pointer && probe.pointee && probe.pointee->kind == TypeKind::Trait);
    if (trait_base) {
        const bool through_pointer = probe.kind == TypeKind::Pointer;
        const Type &trait_type = through_pointer ? *probe.pointee : probe;
        if (through_pointer && probe.optional) {
            check_expr(*selector->base, true);
            error(call.loc, "type mismatch");
            check_args();
            return type_invalid();
        }
        const int trait_id = trait_type.trait_id;
        std::vector<const FunctionDecl *> matches;
        if (trait_id >= 0 && trait_id < static_cast<int>(traits_.size())) {
            for (const FunctionDecl *def : traits_[static_cast<std::size_t>(trait_id)].defaults) {
                if (def != nullptr && def->name == selector->field && type_arg_count_ok(*def, call)) {
                    matches.push_back(def);
                }
            }
        }
        if (matches.empty()) {
            check_expr(*selector->base, true);
            error(call.loc, "undeclared identifier");
            check_args();
            return type_invalid();
        }
        if (matches.size() != 1) {
            check_expr(*selector->base, true);
            error(call.loc, "type mismatch");
            check_args();
            return type_invalid();
        }
        const FunctionDecl *templ = matches.front();
        const bool receiver_pointer =
            templ->params.empty() || !templ->params.front().is_receiver ? false : templ->params.front().indirections > 0;
        if (receiver_pointer && !through_pointer) {
            check_expr(*selector->base, true);
            error(expr_loc(*selector->base), "not an lvalue");
            check_args();
            return type_invalid();
        }
        if (through_pointer && probe.pointee != nullptr && probe.pointee->is_const && receiver_pointer &&
            templ->params.front().const_pointee == 0) {
            check_expr(*selector->base, true);
            error(call.loc, "type mismatch");
            check_args();
            return type_invalid();
        }
        require_unsafe_call(templ->unsafe, call.loc);
        std::vector<Type> arg_types;
        bool arg_invalid = false;
        for (const auto &arg : call.args) {
            if (arg == nullptr) {
                arg_invalid = true;
                arg_types.push_back(type_invalid());
                continue;
            }
            Type got = check_expr(*arg, true);
            if (got.kind == TypeKind::Invalid) {
                arg_invalid = true;
            }
            arg_types.push_back(std::move(got));
        }
        if (arg_invalid) {
            check_expr(*selector->base, true);
            return type_invalid();
        }
        const auto inferred = infer_generic_bounds(*templ, call, arg_types);
        if (!inferred) {
            check_expr(*selector->base, true);
            return type_invalid();
        }
        std::vector<SpecArg> bounds = *inferred;
        disambiguate_bounds(file_id_, bounds);
        const std::string spec_name = mangle_spec(templ->name, bounds);
        bool known = false;
        for (const TraitGenericUse &use : trait_generic_uses_) {
            if (use.trait_id == trait_id && use.spec_name == spec_name && use.through_pointer == through_pointer) {
                known = true;
                break;
            }
        }
        if (!known) {
            trait_generic_uses_.push_back(TraitGenericUse{trait_id, templ, bounds, spec_name, through_pointer});
            const int count = static_cast<int>(structs_.size());
            for (int id = 0; id < count; ++id) {
                const uint32_t struct_file = structs_[static_cast<std::size_t>(id)].file_id;
                const std::string struct_name = structs_[static_cast<std::size_t>(id)].name;
                install_trait_generics(struct_file, struct_name, trait_id);
            }
        }
        call.trait_method = spec_name;
        FunctionDecl spec = clone_function(*templ);
        const std::vector<TypeParam> params = templ->type_params;
        spec.type_params.clear();
        substitute_function(spec, params, bounds);
        if (!spec.results.empty()) {
            const ResultSlot &first = spec.results.front();
            spec.return_type = first.type_name;
            spec.type_package = first.type_package;
            spec.is_struct = first.is_struct;
            spec.is_enum = first.is_enum;
            spec.indirections = first.indirections;
            spec.optional = first.optional;
            spec.const_pointee = first.const_pointee;
        }
        if (through_pointer && receiver_pointer) {
            const bool writes = templ->params.front().const_pointee == 0;
            init_address_span(*selector->base, writes, expr_loc(*selector->base), false);
        }
        check_expr(*selector->base, true);
        return function_result(spec);
    }
    Type object;
    bool through_pointer = false;
    if (!dot_object(probe, object, through_pointer) || object.struct_id < 0 ||
        object.struct_id >= static_cast<int>(structs_.size())) {
        check_expr(*selector->base, true);
        error(call.loc, "not supported yet");
        check_args();
        return type_invalid();
    }
    const uint32_t struct_file = structs_[static_cast<std::size_t>(object.struct_id)].file_id;
    const std::string struct_name = structs_[static_cast<std::size_t>(object.struct_id)].name;
    const FunctionDecl *templ = nullptr;
    bool struct_template = false;
    if (const auto file = method_templates_.find(struct_file); file != method_templates_.end()) {
        const auto owner = file->second.find(struct_name);
        if (owner != file->second.end()) {
            const auto found = owner->second.find(selector->field);
            if (found != owner->second.end()) {
                std::vector<const FunctionDecl *> matches;
                for (const FunctionDecl *item : found->second) {
                    if (item != nullptr && type_arg_count_ok(*item, call)) {
                        matches.push_back(item);
                    }
                }
                if (matches.size() > 1) {
                    check_expr(*selector->base, true);
                    error(call.loc, "type mismatch");
                    check_args();
                    return type_invalid();
                }
                if (matches.size() == 1) {
                    templ = matches.front();
                    struct_template = true;
                }
            }
        }
    }
    if (!struct_template) {
        std::vector<const FunctionDecl *> matches;
        for (std::size_t trait_i = 0; trait_i < traits_.size(); ++trait_i) {
            bool named = false;
            for (const FunctionDecl *def : traits_[trait_i].defaults) {
                if (def != nullptr && def->name == selector->field && type_arg_count_ok(*def, call)) {
                    named = true;
                    break;
                }
            }
            if (!named) {
                continue;
            }
            const bool saved = quiet_;
            quiet_ = true;
            const bool covered = require_satisfies(object.struct_id, static_cast<int>(trait_i), true, call.loc, struct_file);
            quiet_ = saved;
            if (!covered) {
                continue;
            }
            for (const FunctionDecl *def : traits_[trait_i].defaults) {
                if (def != nullptr && def->name == selector->field && type_arg_count_ok(*def, call)) {
                    matches.push_back(def);
                }
            }
        }
        if (matches.empty()) {
            check_expr(*selector->base, true);
            error(call.loc, "undeclared identifier");
            check_args();
            return type_invalid();
        }
        if (matches.size() != 1) {
            check_expr(*selector->base, true);
            error(call.loc, "type mismatch");
            check_args();
            return type_invalid();
        }
        templ = matches.front();
    }
    if (templ == nullptr || templ->body == nullptr) {
        check_expr(*selector->base, true);
        error(call.loc, "not supported yet");
        check_args();
        return type_invalid();
    }
    std::vector<Type> arg_types;
    bool arg_invalid = false;
    for (const auto &arg : call.args) {
        if (arg == nullptr) {
            arg_invalid = true;
            arg_types.push_back(type_invalid());
            continue;
        }
        Type got = check_expr(*arg, true);
        if (got.kind == TypeKind::Invalid) {
            arg_invalid = true;
        }
        arg_types.push_back(std::move(got));
    }
    if (arg_invalid) {
        check_expr(*selector->base, true);
        return type_invalid();
    }
    const auto inferred = infer_generic_bounds(*templ, call, arg_types);
    if (!inferred) {
        check_expr(*selector->base, true);
        return type_invalid();
    }
    std::vector<SpecArg> bounds = *inferred;
    disambiguate_bounds(struct_file, bounds);
    const std::string spec_name = mangle_spec(templ->name, bounds);
    const FunctionDecl *installed = install_default(*templ, struct_file, struct_name, spec_name, bounds);
    if (installed == nullptr) {
        check_expr(*selector->base, true);
        return type_invalid();
    }
    selector->field = spec_name;
    call.type_args.clear();
    return check_raw(expr, used);
}

void Checker::drain_deferred() {
    std::size_t index = 0;
    while (index < deferred_methods_.size()) {
        OwnedFn &item = deferred_methods_[index];
        ++index;
        check_isolated(item.file_id, item.fn);
    }
}

namespace {

void relocate_expr(Expr &expr, const std::unordered_map<const FunctionDecl *, const FunctionDecl *> &moved);

void relocate_stmt(Stmt &stmt, const std::unordered_map<const FunctionDecl *, const FunctionDecl *> &moved) {
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (child) {
                        relocate_stmt(*child, moved);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (node.value) {
                    relocate_expr(*node.value, moved);
                }
                for (const auto &extra : node.extra) {
                    if (extra) {
                        relocate_expr(*extra, moved);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    relocate_expr(*node.expr, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.cond) {
                    relocate_expr(*node.cond, moved);
                }
                if (node.then_arm) {
                    relocate_stmt(*node.then_arm, moved);
                }
                if (node.else_arm) {
                    relocate_stmt(*node.else_arm, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                if (node.cond) {
                    relocate_expr(*node.cond, moved);
                }
                if (node.body) {
                    relocate_stmt(*node.body, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.init) {
                    relocate_stmt(*node.init, moved);
                }
                if (node.cond) {
                    relocate_expr(*node.cond, moved);
                }
                if (node.step) {
                    relocate_expr(*node.step, moved);
                }
                if (node.body) {
                    relocate_stmt(*node.body, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                if (node.body) {
                    relocate_stmt(*node.body, moved);
                }
                if (node.cond) {
                    relocate_expr(*node.cond, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                if (node.cond) {
                    relocate_expr(*node.cond, moved);
                }
                if (node.body) {
                    relocate_stmt(*node.body, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                if (node.value) {
                    relocate_expr(*node.value, moved);
                }
                if (node.stmt) {
                    relocate_stmt(*node.stmt, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (VarDecl &var : node.vars) {
                    if (var.init) {
                        relocate_expr(*var.init, moved);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl && node.decl->body) {
                    relocate_stmt(*node.decl->body, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call) {
                    relocate_expr(*node.call, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value) {
                    relocate_expr(*node.value, moved);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                if (node.cond) {
                    relocate_expr(*node.cond, moved);
                }
            }
        },
        stmt.data);
}

void relocate_expr(Expr &expr, const std::unordered_map<const FunctionDecl *, const FunctionDecl *> &moved) {
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.expr) {
                    relocate_expr(*node.expr, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs) {
                    relocate_expr(*node.lhs, moved);
                }
                if (node.rhs) {
                    relocate_expr(*node.rhs, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                if (node.lhs) {
                    relocate_expr(*node.lhs, moved);
                }
                if (node.rhs) {
                    relocate_expr(*node.rhs, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.selected != nullptr) {
                    const auto found = moved.find(node.selected);
                    if (found != moved.end()) {
                        node.selected = found->second;
                    }
                }
                if (node.callee) {
                    relocate_expr(*node.callee, moved);
                }
                for (const auto &arg : node.args) {
                    if (arg) {
                        relocate_expr(*arg, moved);
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base) {
                    relocate_expr(*node.base, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.base) {
                    relocate_expr(*node.base, moved);
                }
                if (node.index) {
                    relocate_expr(*node.index, moved);
                }
                if (node.end) {
                    relocate_expr(*node.end, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Sizeof> || std::is_same_v<T, Expr::Typeof> ||
                                 std::is_same_v<T, Expr::Cast>) {
                if (node.expr) {
                    relocate_expr(*node.expr, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (node.decl && node.decl->body) {
                    relocate_stmt(*node.decl->body, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                for (Expr::FieldInit &field : node.fields) {
                    if (field.value) {
                        relocate_expr(*field.value, moved);
                    }
                }
                for (const auto &element : node.elements) {
                    if (element) {
                        relocate_expr(*element, moved);
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                if (node.allocator) {
                    relocate_expr(*node.allocator, moved);
                }
                if (node.count) {
                    relocate_expr(*node.count, moved);
                }
                if (node.init) {
                    relocate_expr(*node.init, moved);
                }
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                if (node.allocator) {
                    relocate_expr(*node.allocator, moved);
                }
                if (node.expr) {
                    relocate_expr(*node.expr, moved);
                }
            }
        },
        expr.data);
}

void relocate_function(FunctionDecl &fn, const std::unordered_map<const FunctionDecl *, const FunctionDecl *> &moved) {
    if (fn.body) {
        relocate_stmt(*fn.body, moved);
    }
    for (const auto &expr : fn.result_typeof) {
        if (expr) {
            relocate_expr(*expr, moved);
        }
    }
}

void relocate_unit(TranslationUnit &unit, const std::unordered_map<const FunctionDecl *, const FunctionDecl *> &moved) {
    for (TopDecl &decl : unit.decls) {
        if (auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            relocate_function(*fn, moved);
        } else if (auto *var = std::get_if<VarDecl>(&decl.data)) {
            if (var->init) {
                relocate_expr(*var->init, moved);
            }
        } else if (auto *test = std::get_if<TestDecl>(&decl.data)) {
            if (test->body) {
                relocate_stmt(*test->body, moved);
            }
        } else if (auto *item = std::get_if<AssertDecl>(&decl.data)) {
            if (item->cond) {
                relocate_expr(*item->cond, moved);
            }
        }
    }
}

} // namespace

void Checker::flush_appends() {
    // Specialized methods were addressed while they lived in `deferred_methods_`.
    // Moving them into the unit keeps one object for codegen, so calls learn the new address.
    std::unordered_map<const FunctionDecl *, const FunctionDecl *> moved;
    auto append_one = [&](uint32_t file, TopDecl decl, const FunctionDecl *from) {
        TranslationUnit *unit = unit_for(file);
        if (unit == nullptr) {
            return;
        }
        unit->decls.push_back(std::move(decl));
        if (from == nullptr) {
            return;
        }
        if (const auto *fn = std::get_if<FunctionDecl>(&unit->decls.back().data)) {
            moved.emplace(from, fn);
        }
    };
    for (OwnedFn &item : deferred_methods_) {
        const FunctionDecl *from = &item.fn;
        TopDecl decl;
        decl.data = std::move(item.fn);
        append_one(item.file_id, std::move(decl), from);
    }
    deferred_methods_.clear();
    for (PendingAppend &item : pending_appends_) {
        append_one(item.file_id, std::move(item.decl), nullptr);
    }
    pending_appends_.clear();
    if (moved.empty()) {
        return;
    }
    if (primary_unit_ != nullptr) {
        relocate_unit(*primary_unit_, moved);
    }
    if (graph_ != nullptr) {
        for (PackageNode &node : const_cast<PackageGraph *>(graph_)->nodes) {
            relocate_unit(node.unit, moved);
        }
    }
}

TranslationUnit *Checker::unit_for(uint32_t file_id) {
    if (graph_ == nullptr) {
        (void)file_id;
        return primary_unit_;
    }
    for (PackageNode &node : const_cast<PackageGraph *>(graph_)->nodes) {
        if (node.file_id == file_id) {
            return &node.unit;
        }
    }
    return primary_unit_;
}

} // namespace tlc
