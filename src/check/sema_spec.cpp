// Instantiating a generic function or struct, and trait default methods.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/generic.hpp"

namespace tlc {

namespace {

bool same_default_shape(const FunctionDecl &lhs, const FunctionDecl &rhs);

} // namespace

void Checker::check_isolated(uint32_t file, const FunctionDecl &fn) {
    auto saved_scopes = std::move(scopes_);
    const uint32_t saved_file = file_id_;
    // The caller may be learning a result type. This body is the real check of the specialization.
    const bool saved_quiet = quiet_;
    quiet_ = false;
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
    quiet_ = saved_quiet;
}

void Checker::queue_spec(uint32_t file, FunctionDecl fn) {
    PendingAppend item;
    item.file_id = file;
    item.decl.data = std::move(fn);
    pending_appends_.push_back(std::move(item));
}

std::string Checker::specialize_trait(const TraitDecl &templ, uint32_t owner, const std::vector<TypeArg> &args,
                                       SourceLoc loc) {
    // The use is written in the importing file. The spec is stored on the defining package.
    const uint32_t caller = file_id_;
    if (args.size() != templ.type_params.size()) {
        error(loc, "argument count");
        return {};
    }
    std::vector<SpecArg> bounds;
    bounds.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        SpecArg bound;
        if (!bind_type_arg(templ.type_params[i], args[i], bound, args[i].loc.valid() ? args[i].loc : loc)) {
            return {};
        }
        bounds.push_back(std::move(bound));
    }
    disambiguate_bounds(owner, bounds);
    const std::string spec_name = mangle_spec(templ.name, bounds);
    const std::string key = std::to_string(owner) + "\n" + spec_name;
    // The importer looks the mangled name up in its own file. The trait info stays one entry.
    const auto note_caller = [&](int trait_id) {
        if (caller == owner || trait_id < 0) {
            return;
        }
        trait_names_[caller][spec_name] = trait_id;
    };
    const int known = package_trait(owner, spec_name);
    if (spec_ready_.contains(key) || known >= 0) {
        note_caller(known);
        return spec_name;
    }
    if (spec_checking_.contains(key)) {
        note_caller(package_trait(owner, spec_name));
        return spec_name;
    }
    spec_checking_.insert(key);
    const uint32_t saved_file = file_id_;
    file_id_ = owner;
    expose_bounds(owner, bounds);
    // Registered before the methods are typed, so a result that names this same specialization can find it.
    const int id = static_cast<int>(traits_.size());
    TraitInfo info;
    info.name = spec_name;
    info.file_id = owner;
    traits_.push_back(std::move(info));
    trait_names_[owner][spec_name] = id;
    // Codegen reads the trait from the unit. The specialized declaration is that trait with `T` replaced.
    TraitDecl spec_decl;
    spec_decl.loc = templ.loc;
    spec_decl.name = spec_name;
    const bool iteration = templ.name == "Seq" && package_is_iteration(owner);
    const std::string type_param = templ.type_params.empty() ? std::string{} : templ.type_params.front().name;
    bool seen_length = false;
    bool seen_element = false;
    bool seen_ref = false;
    for (const FunctionDecl &src : templ.methods) {
        if (src.name.empty()) {
            continue;
        }
        bool duplicate = false;
        for (const TraitInfo::Method &existing : traits_[static_cast<std::size_t>(id)].methods) {
            if (existing.name == src.name) {
                error(src.loc, "redefinition");
                duplicate = true;
            }
        }
        if (duplicate) {
            continue;
        }
        bool wrote_receiver = false;
        for (const Param &param : src.params) {
            if (param.is_receiver) {
                wrote_receiver = true;
            }
        }
        if (wrote_receiver || src.variadic) {
            error(src.loc, src.variadic && !wrote_receiver ? "not supported yet" : "type mismatch");
            continue;
        }
        IterationMethod kind = IterationMethod::Other;
        if (iteration) {
            kind = iteration_method(src, type_param);
            const bool repeat = (kind == IterationMethod::Length && seen_length) ||
                                (kind == IterationMethod::Element && seen_element) ||
                                (kind == IterationMethod::Ref && seen_ref);
            // A field, a further method, or a repeated shape was reported when the template was registered.
            if (kind == IterationMethod::Other || repeat) {
                continue;
            }
            if (kind == IterationMethod::Length) {
                seen_length = true;
            } else if (kind == IterationMethod::Element) {
                seen_element = true;
            } else {
                seen_ref = true;
            }
        }
        FunctionDecl method = clone_function(src);
        substitute_function(method, templ.type_params, bounds);
        Type signature = function_signature(method);
        if (signature.result != nullptr && mentions_trait(*signature.result)) {
            error(method.loc, "trait has no size");
            continue;
        }
        TraitInfo::Method saved;
        saved.name = method.name;
        saved.type = std::move(signature);
        saved.unsafe_fn = method.unsafe;
        saved.loc = method.loc;
        saved.home = id;
        saved.iteration_ref = kind == IterationMethod::Ref;
        saved.iteration_element = kind == IterationMethod::Element;
        if (kind == IterationMethod::Length) {
            method.iteration_kind = 1;
        } else if (kind == IterationMethod::Element) {
            method.iteration_kind = 2;
        } else if (kind == IterationMethod::Ref) {
            method.iteration_kind = 3;
        }
        traits_[static_cast<std::size_t>(id)].methods.push_back(std::move(saved));
        spec_decl.methods.push_back(std::move(method));
    }
    if (iteration) {
        TraitInfo &info = traits_[static_cast<std::size_t>(id)];
        info.iteration = true;
        for (const TraitInfo::Method &method : info.methods) {
            if (method.iteration_element && method.type.result != nullptr) {
                info.iteration_elem = method.type.result;
            }
        }
    }
    for (const FieldDecl &source : templ.fields) {
        // The iteration trait's fields were reported when the template was registered.
        if (iteration || source.name.empty()) {
            continue;
        }
        bool duplicate = false;
        for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(id)].methods) {
            if (method.name == source.name) {
                error(source.loc, "redefinition");
                duplicate = true;
            }
        }
        for (const TraitInfo::Field &existing : traits_[static_cast<std::size_t>(id)].fields) {
            if (existing.name == source.name) {
                error(source.loc, "redefinition");
                duplicate = true;
            }
        }
        if (duplicate) {
            continue;
        }
        if (source.init != nullptr) {
            error(source.loc, "not supported yet");
        }
        FieldDecl field = source;
        substitute_type(templ.type_params, bounds, field.type_name, field.type_package, field.is_struct, field.is_enum,
                        field.indirections, field.optional, field.is_array, field.array_len, field.array_len_name,
                        field.type_args, field.loc, &field.shells, &field.elem_const, &field.const_pointee);
        for (ResultSlot &part : field.tuple_parts) {
            substitute_slot(templ.type_params, bounds, part, field.loc);
        }
        for (FnParam &param : field.fn_params) {
            substitute_fn_param(templ.type_params, bounds, param, field.loc);
        }
        for (FnParam &param : field.fn_results) {
            substitute_fn_param(templ.type_params, bounds, param, field.loc);
        }
        prepare_written(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                        field.optional, field.is_array, field.array_len, field.array_len_name, field.type_args, field.loc);
        fold_shells(field.shells, field.loc);
        Type field_type = field.is_tuple
                              ? type_of_written_tuple(field.tuple_parts, field.indirections, field.is_array, field.shells,
                                                      field.loc)
                              : field.is_fn_ptr
                              ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                               field.ret_indirections, field.indirections, field.fn_params, field.fn_variadic,
                                               field.fn_results, field.loc, field.ret_optional, field.ret_const_pointee)
                              : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                            false, field.type_package, field.is_struct, field.is_enum, false, field.optional,
                                            field.const_pointee, false, field.elem_const, field.elem_slice, field.shells,
                                            true);
        if (field.is_const) {
            field_type.is_const = true;
        }
        if (field_type.kind == TypeKind::Invalid) {
            continue;
        }
        if (mentions_trait(field_type)) {
            error(field.loc, "not supported yet");
            continue;
        }
        TraitInfo::Field saved;
        saved.name = field.name;
        saved.type = std::move(field_type);
        saved.loc = field.loc;
        saved.home = id;
        traits_[static_cast<std::size_t>(id)].fields.push_back(std::move(saved));
        spec_decl.fields.push_back(std::move(field));
    }
    PendingAppend appended;
    appended.file_id = owner;
    appended.decl.data = std::move(spec_decl);
    pending_appends_.push_back(std::move(appended));
    file_id_ = saved_file;
    note_caller(id);
    spec_checking_.erase(key);
    spec_ready_.insert(key);
    return spec_name;
}

std::string Checker::specialize_tuple_alias(const TypedefDecl &templ, uint32_t owner, const std::vector<TypeArg> &args,
                                            SourceLoc loc) {
    if (args.size() != templ.type_params.size()) {
        error(loc, "argument count");
        return {};
    }
    std::vector<SpecArg> bounds;
    bounds.reserve(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        SpecArg bound;
        if (!bind_type_arg(templ.type_params[i], args[i], bound, args[i].loc)) {
            return {};
        }
        bounds.push_back(std::move(bound));
    }
    // `type Name[T type] T`. A tuple alias stays that name. Unpacking it would make
    // `Name[Duo]` and `Name[Pair]` the same type when both tuples are `(i32, i32)`.
    const bool identity = !templ.is_tuple && templ.type_args.empty() && templ.type_package.empty() &&
                          !templ.type_name.empty() && templ.indirections == 0 && !templ.is_array && templ.shells.empty() &&
                          !templ.is_fn_ptr && param_index(templ.type_params, templ.type_name) >= 0;
    if (identity) {
        const int slot = param_index(templ.type_params, templ.type_name);
        if (slot >= 0 && static_cast<std::size_t>(slot) < bounds.size() &&
            static_cast<std::size_t>(slot) < args.size() && bounds[static_cast<std::size_t>(slot)].is_pack) {
            TypeArg concrete = args[static_cast<std::size_t>(slot)];
            if (!concrete.args.empty()) {
                const std::string spec =
                    specialize_type_app(concrete.type_package, concrete.type_name, concrete.args, concrete.loc);
                if (!spec.empty()) {
                    concrete.type_name = spec;
                    concrete.type_package.clear();
                    concrete.args.clear();
                }
            }
            if (concrete.args.empty() && !concrete.type_name.empty()) {
                const bool saved_quiet = quiet_;
                quiet_ = true;
                const Type type =
                    object_type(concrete.type_name, concrete.indirections, concrete.is_array, concrete.array_len,
                                concrete.loc, false, concrete.type_package, false, concrete.is_enum, false,
                                concrete.optional, concrete.const_pointee, false, concrete.elem_const, false,
                                concrete.shells);
                quiet_ = saved_quiet;
                if (type.alias_id > 0) {
                    SpecArg named;
                    named.type_name = type_desc(type);
                    if (named.type_name == concrete.type_name) {
                        named.type_package = concrete.type_package;
                    }
                    bounds[static_cast<std::size_t>(slot)] = std::move(named);
                }
            }
        }
    }
    disambiguate_bounds(owner, bounds);
    const std::string spec_name = mangle_spec(templ.name, bounds);
    const std::string key = std::to_string(owner) + "\n" + spec_name;
    const uint32_t caller = file_id_;
    auto visible = [&]() {
        if (caller == owner) {
            return;
        }
        Type alias;
        if (!package_typedef(owner, spec_name, alias)) {
            return;
        }
        Type have;
        if (package_typedef(caller, spec_name, have)) {
            return;
        }
        typedefs_[caller].emplace(spec_name, std::move(alias));
    };
    if (spec_ready_.contains(key)) {
        visible();
        return spec_name;
    }
    if (spec_checking_.contains(key)) {
        // A part that names this specialization is typed against the alias being built.
        return spec_name;
    }
    spec_checking_.insert(key);
    const uint32_t saved_file = file_id_;
    file_id_ = owner;
    expose_bounds(owner, bounds);
    TypedefDecl spec = templ;
    spec.name = spec_name;
    spec.type_params.clear();
    auto subst_slot = [&](auto &&self, ResultSlot &slot) -> void {
        if (slot.is_tuple) {
            for (ResultSlot &part : slot.tuple_parts) {
                self(self, part);
            }
            return;
        }
        substitute_type(templ.type_params, bounds, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum,
                        slot.indirections, slot.optional, slot.is_array, slot.array_len, slot.array_len_name,
                        slot.type_args, loc, &slot.shells, &slot.elem_const, &slot.const_pointee);
    };
    if (spec.is_tuple) {
        for (ResultSlot &part : spec.tuple_parts) {
            subst_slot(subst_slot, part);
        }
    } else {
        // `type Name[T type] T` when `T` is a tuple is that tuple alias. Substitution rejects the pack.
        const bool identity = spec.type_args.empty() && spec.type_package.empty() && !spec.type_name.empty() &&
                              spec.indirections == 0 && !spec.is_array && spec.shells.empty() && !spec.is_fn_ptr;
        const int slot = identity ? param_index(templ.type_params, spec.type_name) : -1;
        const SpecArg *bound =
            slot >= 0 && static_cast<std::size_t>(slot) < bounds.size() ? &bounds[static_cast<std::size_t>(slot)] : nullptr;
        if (bound != nullptr && bound->is_pack) {
            spec.is_tuple = true;
            spec.type_name.clear();
            spec.tuple_parts.reserve(bound->parts.size());
            for (const SpecArg &part : bound->parts) {
                ResultSlot written;
                written.type_name = part.type_name;
                written.type_package = part.type_package;
                written.is_struct = part.is_struct;
                written.is_enum = part.is_enum;
                written.indirections = part.indirections;
                written.optional = part.optional;
                written.const_pointee = part.const_pointee;
                written.is_array = part.is_array;
                written.elem_const = part.elem_const;
                written.elem_slice = part.elem_slice;
                written.shells = part.shells;
                written.array_len = part.array_len;
                spec.tuple_parts.push_back(std::move(written));
            }
        } else {
            // `type MyAlias[T type] Vec[T]` and `type Name[T type] T` become a concrete alias.
            substitute_type(templ.type_params, bounds, spec.type_name, spec.type_package, spec.is_struct, spec.is_enum,
                            spec.indirections, spec.optional, spec.is_array, spec.array_len, spec.array_len_name,
                            spec.type_args, loc, &spec.shells, &spec.elem_const, &spec.const_pointee);
        }
    }
    register_one_typedef(spec);
    Type made;
    const bool ok = package_typedef(owner, spec_name, made);
    if (ok) {
        // Codegen lays this out as a concrete tuple alias. The template itself is not one.
        PendingAppend appended;
        appended.file_id = owner;
        appended.decl.data = std::move(spec);
        pending_appends_.push_back(std::move(appended));
    }
    file_id_ = saved_file;
    spec_checking_.erase(key);
    if (!ok) {
        return {};
    }
    spec_ready_.insert(key);
    visible();
    return spec_name;
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
        if (templ == nullptr) {
            // `pkg.Seq[i32]`. Export publishes the template. The spec stays on that package.
            const TraitDecl *trait = nullptr;
            uint32_t trait_owner = owner;
            for (const uint32_t id : files_in_package(symbol->package_file)) {
                const auto file = trait_templates_.find(id);
                if (file == trait_templates_.end()) {
                    continue;
                }
                const auto found = file->second.find(name);
                if (found == file->second.end()) {
                    continue;
                }
                trait_owner = id;
                trait = found->second;
                break;
            }
            if (trait != nullptr) {
                if (!trait->exported) {
                    error(loc, name + " is not exported");
                    return {};
                }
                return specialize_trait(*trait, trait_owner, args, loc);
            }
            const TypedefDecl *alias = nullptr;
            uint32_t alias_owner = owner;
            for (const uint32_t id : files_in_package(symbol->package_file)) {
                const auto file = typedef_templates_.find(id);
                if (file == typedef_templates_.end()) {
                    continue;
                }
                const auto found = file->second.find(name);
                if (found == file->second.end()) {
                    continue;
                }
                alias_owner = id;
                alias = found->second;
                break;
            }
            if (alias != nullptr) {
                if (!alias->exported) {
                    error(loc, name + " is not exported");
                    return {};
                }
                return specialize_tuple_alias(*alias, alias_owner, args, loc);
            }
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
            uint32_t trait_owner = file_id_;
            const TraitDecl *trait = find_trait_template(name, trait_owner);
            if (trait != nullptr) {
                return specialize_trait(*trait, trait_owner, args, loc);
            }
            uint32_t alias_owner = file_id_;
            const TypedefDecl *alias = find_tuple_alias_template(name, alias_owner);
            if (alias != nullptr) {
                return specialize_tuple_alias(*alias, alias_owner, args, loc);
            }
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
            const TypeParam &param = templ->type_params[i];
            int trait_id = -1;
            if (param.constraint_package.empty()) {
                trait_id = package_trait(owner, param.constraint);
                if (trait_id < 0) {
                    error(loc, "undeclared identifier");
                    return {};
                }
            } else {
                trait_id = imported_trait(owner, param.constraint_package, param.constraint, loc);
                if (trait_id < 0) {
                    return {};
                }
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
    if (generic_spec_depth_ >= 32) {
        error(loc, "not supported yet");
        return {};
    }
    ++generic_spec_depth_;
    struct DepthLeave {
        int &depth;
        ~DepthLeave() { --depth; }
    } leave{generic_spec_depth_};
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
        for (ResultSlot &part : field.tuple_parts) {
            substitute_slot(templ->type_params, bounds, part, field.loc);
        }
        for (FnParam &param : field.fn_params) {
            substitute_fn_param(templ->type_params, bounds, param, field.loc);
        }
        for (FnParam &param : field.fn_results) {
            substitute_fn_param(templ->type_params, bounds, param, field.loc);
        }
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
    info.tag_name = spec.tag_name;
    info.tag_package = spec.tag_package;
    info.template_name = templ->name;
    info.spec_args = bounds;
    structs_.push_back(std::move(info));
    tags_[owner][spec_name] = id;
    // Recorded before the fields are typed. A field type may specialize another struct.
    if (graph_ != nullptr) {
        const_cast<PackageGraph *>(graph_)->struct_specs.push_back(StructSpecNote{owner, spec_name});
    }
    // `One struct { value T; }` is not a concrete struct until `T` is known.
    // `data union { n T; m T; }` keeps the union from `Box.data`. The bare name still says `T`.
    const auto source_decl = [&](const std::string &source) -> const StructDecl * {
        if (source.empty()) {
            return nullptr;
        }
        const PackageNode *node = find_node(owner);
        if (node == nullptr) {
            return nullptr;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *item = std::get_if<StructDecl>(&decl.data);
            if (item != nullptr && item->name == source) {
                return item;
            }
        }
        return nullptr;
    };
    const auto materialize_inline = [&](auto &&self, const std::string &name, std::vector<FieldDecl> fields,
                                        SourceLoc loc, const std::string &source) -> int {
        if (const int existing = find_tag(file_id_, name); existing >= 0) {
            return existing;
        }
        const StructDecl *origin = source_decl(source);
        for (FieldDecl &field : fields) {
            if (!field.inline_fields.empty()) {
                const int child = self(self, name + "." + field.name, field.inline_fields, field.loc, field.type_name);
                field.type_name = child < 0 ? "invalid" : structs_[static_cast<std::size_t>(child)].name;
                field.type_package.clear();
                field.type_args.clear();
                field.inline_fields.clear();
                field.is_struct = false;
                field.indirections = 0;
            }
            substitute_type(templ->type_params, bounds, field.type_name, field.type_package, field.is_struct,
                            field.is_enum, field.indirections, field.optional, field.is_array, field.array_len,
                            field.array_len_name, field.type_args, field.loc, &field.shells, &field.elem_const,
                            &field.const_pointee);
            for (ResultSlot &part : field.tuple_parts) {
                substitute_slot(templ->type_params, bounds, part, field.loc);
            }
            for (FnParam &param : field.fn_params) {
                substitute_fn_param(templ->type_params, bounds, param, field.loc);
            }
            for (FnParam &param : field.fn_results) {
                substitute_fn_param(templ->type_params, bounds, param, field.loc);
            }
            if (field.init != nullptr) {
                substitute_expr(templ->type_params, bounds, *field.init);
            }
        }
        const int inline_id = static_cast<int>(structs_.size());
        StructInfo inline_info;
        inline_info.name = name;
        inline_info.file_id = owner;
        inline_info.loc = loc;
        inline_info.anonymous = true;
        if (origin != nullptr) {
            inline_info.is_union = origin->is_union;
            inline_info.tag_name = origin->tag_name;
            inline_info.tag_package = origin->tag_package;
            inline_info.extern_c = origin->extern_c;
            inline_info.is_extern = origin->is_extern;
            inline_info.exported = origin->exported;
            inline_info.align = origin->align;
        }
        structs_.push_back(std::move(inline_info));
        tags_[owner][name] = inline_id;
        if (graph_ != nullptr) {
            const_cast<PackageGraph *>(graph_)->struct_specs.push_back(StructSpecNote{owner, name});
        }
        for (FieldDecl &field : fields) {
            prepare_written(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                            field.optional, field.is_array, field.array_len, field.array_len_name, field.type_args,
                            field.loc);
            fold_shells(field.shells, field.loc);
            Type field_type = field.is_tuple
                                  ? type_of_written_tuple(field.tuple_parts, field.indirections, field.is_array,
                                                          field.shells, field.loc)
                                  : field.is_fn_ptr
                                  ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                   field.ret_indirections, field.indirections, field.fn_params,
                                                   field.fn_variadic, field.fn_results, field.loc, field.ret_optional,
                                                   field.ret_const_pointee)
                                  : object_type(field.type_name, field.indirections, field.is_array, field.array_len,
                                                field.loc, false, field.type_package, field.is_struct, field.is_enum,
                                                false, field.optional, field.const_pointee, false, field.elem_const,
                                                field.elem_slice, field.shells);
            if (field.is_const) {
                field_type.is_const = true;
            }
            StructInfo::Field stored{field.name, std::move(field_type), field.loc, field.exported};
            const bool as_union = origin != nullptr && origin->is_union;
            const bool tagged = as_union && !origin->tag_name.empty();
            if (field.init != nullptr) {
                // A tagged arm is not a field value. An untagged union keeps one value.
                // A struct field keeps its own value, including `x T = 40` inside `point struct`.
                bool prior = false;
                if (as_union) {
                    for (const StructInfo::Field &have : structs_[static_cast<std::size_t>(inline_id)].fields) {
                        if (have.has_init) {
                            prior = true;
                            break;
                        }
                    }
                }
                if (as_union && (tagged || prior)) {
                    error(field.loc, "not supported yet");
                } else {
                    stored.has_init = true;
                    stored.init = field.init.get();
                }
            }
            structs_[static_cast<std::size_t>(inline_id)].fields.push_back(std::move(stored));
        }
        structs_[static_cast<std::size_t>(inline_id)].complete = true;
        if (origin != nullptr && origin->is_union) {
            bind_union_tag(structs_[static_cast<std::size_t>(inline_id)]);
        }
        if (origin == nullptr || !origin->is_union || origin->tag_name.empty()) {
            std::vector<Scope> saved_scopes = std::move(scopes_);
            scopes_.clear();
            push();
            if (const auto found = statics_.find(owner); found != statics_.end()) {
                for (const auto &[static_name, symbol] : found->second) {
                    scopes_.back().symbols.emplace(static_name, symbol);
                }
            }
            for (const FieldDecl &field : fields) {
                if (field.init == nullptr) {
                    continue;
                }
                const StructInfo::Field *slot = nullptr;
                for (const StructInfo::Field &have : structs_[static_cast<std::size_t>(inline_id)].fields) {
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
        StructDecl arm;
        arm.loc = loc;
        arm.name = name;
        arm.anonymous = true;
        arm.union_payload = origin == nullptr || origin->union_payload;
        if (origin != nullptr) {
            arm.is_union = origin->is_union;
            arm.tag_name = origin->tag_name;
            arm.tag_package = origin->tag_package;
            arm.extern_c = origin->extern_c;
            arm.is_extern = origin->is_extern;
            arm.exported = origin->exported;
            arm.align = origin->align;
        }
        arm.fields = std::move(fields);
        PendingAppend appended;
        appended.file_id = owner;
        appended.decl.data = std::move(arm);
        pending_appends_.push_back(std::move(appended));
        return inline_id;
    };
    for (FieldDecl &field : spec.fields) {
        if (field.inline_fields.empty()) {
            continue;
        }
        const int child = materialize_inline(materialize_inline, spec_name + "." + field.name, field.inline_fields,
                                             field.loc, field.type_name);
        field.type_name = child < 0 ? "invalid" : structs_[static_cast<std::size_t>(child)].name;
        field.type_package.clear();
        field.type_args.clear();
        field.inline_fields.clear();
        field.is_struct = false;
        field.indirections = 0;
    }
    for (FieldDecl &field : spec.fields) {
        prepare_written(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                        field.optional, field.is_array, field.array_len, field.array_len_name, field.type_args, field.loc);
        fold_shells(field.shells, field.loc);
        Type field_type = field.is_tuple
                              ? type_of_written_tuple(field.tuple_parts, field.indirections, field.is_array, field.shells,
                                                      field.loc)
                              : field.is_fn_ptr
                              ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                               field.ret_indirections, field.indirections, field.fn_params, field.fn_variadic,
                                               field.fn_results, field.loc, field.ret_optional, field.ret_const_pointee)
                              : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                            false, field.type_package, field.is_struct, field.is_enum, false,
                                            field.optional, field.const_pointee, false, field.elem_const,
                                            field.elem_slice, field.shells);
        if (field.is_const) {
            field_type.is_const = true;
        }
        StructInfo::Field stored{field.name, std::move(field_type), field.loc, field.exported};
        // A tagged arm is not a field value. An untagged union keeps one value.
        if (field.init != nullptr && !(spec.is_union && !spec.tag_name.empty())) {
            bool prior = false;
            if (spec.is_union) {
                for (const StructInfo::Field &have : structs_[static_cast<std::size_t>(id)].fields) {
                    if (have.has_init) {
                        prior = true;
                        break;
                    }
                }
            }
            if (!prior) {
                stored.has_init = true;
                stored.init = field.init.get();
            }
        }
        structs_[static_cast<std::size_t>(id)].fields.push_back(std::move(stored));
    }
    structs_[static_cast<std::size_t>(id)].complete = true;
    if (spec.is_union) {
        bind_union_tag(structs_[static_cast<std::size_t>(id)]);
    }
    if (!spec.is_union || spec.tag_name.empty()) {
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
        // The declaration is already rejected. Do not specialize a C symbol.
        if (owned.fn.extern_c) {
            continue;
        }
        FunctionDecl method = clone_function(owned.fn);
        method.method_struct = spec_name;
        if (!method.static_method && !method.params.empty()) {
            method.params.front().type_name = spec_name;
            method.params.front().is_struct = true;
        }
        // Substitute the struct's parameters only. `[U type]` stays open until the call.
        substitute_function(method, templ->type_params, bounds);
        if (!method.type_params.empty()) {
            // `T` is filled in. `U` is chosen at the call `value.get[i32](...)`.
            bool shadow = false;
            for (const TypeParam &param : method.type_params) {
                for (const std::string &used : method.method_params) {
                    if (param.name == used) {
                        shadow = true;
                    }
                }
            }
            // `export` and a static method are registered. `...` and a trait parameter are
            // not one concrete method. An `extern` prototype names the specialization.
            // A body in this compilation defines that symbol and keeps `extern`.
            const bool missing_body = method.body == nullptr && !extern_generic_prototype(method);
            const bool rejected = method.variadic || method.extern_c || shadow || missing_body;
            if (rejected) {
                continue;
            }
            auto &bucket = method_templates_[owner][spec_name][method.name];
            std::size_t same = bucket.size();
            for (std::size_t i = 0; i < bucket.size(); ++i) {
                const FunctionDecl *existing = bucket[i];
                if (existing != nullptr && existing->static_method == method.static_method &&
                    same_default_shape(*existing, method)) {
                    same = i;
                    break;
                }
            }
            if (same < bucket.size()) {
                const FunctionDecl *existing = bucket[same];
                if ((existing->body != nullptr && method.body != nullptr) || existing->unsafe != method.unsafe) {
                    error(method.loc, "redefinition");
                    continue;
                }
                const bool keep_extern = existing->is_extern || method.is_extern;
                const bool keep_export = existing->exported || method.exported;
                if (method.body != nullptr) {
                    method.is_extern = keep_extern;
                    method.exported = keep_export;
                    held_method_templates_.push_back(OwnedFn{owner, std::move(method)});
                    bucket[same] = &held_method_templates_.back().fn;
                } else {
                    FunctionDecl *slot = const_cast<FunctionDecl *>(existing);
                    slot->is_extern = keep_extern;
                    slot->exported = keep_export;
                }
                continue;
            }
            held_method_templates_.push_back(OwnedFn{owner, std::move(method)});
            FunctionDecl *stable = &held_method_templates_.back().fn;
            bucket.push_back(stable);
            continue;
        }
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
        if (method.infer_result && method.results.empty()) {
            error(method.loc, "not supported yet");
        }
        const bool has_body = method.body != nullptr;
        const bool is_static = method.static_method;
        const bool unsafe = method.unsafe;
        const bool exported = method.exported;
        const bool extern_c = method.extern_c;
        const std::string link_name = method.link_name;
        const std::string method_name = method.name;
        auto &table = methods_[owner][spec_name];
        const auto found = table.find(method_name);
        MethodInfo *slot = nullptr;
        if (found != table.end()) {
            const bool skip = !is_static;
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
        }
        // One `name=` is one object symbol. The next specialization that would use it clashes.
        // The copy is still recorded so the call is not a second error.
        const bool introducing = !link_name.empty() && (slot == nullptr || slot->link_name.empty());
        if (introducing && link_name_taken(link_name)) {
            error(method.loc, "redefinition");
        }
        deferred_methods_.push_back(OwnedFn{owner, std::move(method), generic_spec_depth_});
        FunctionDecl *stable = &deferred_methods_.back().fn;
        if (found == table.end()) {
            MethodInfo info;
            info.type = std::move(signature);
            info.has_body = has_body;
            info.is_static = is_static;
            info.unsafe_fn = unsafe;
            info.exported = exported;
            info.extern_c = extern_c;
            info.link_name = link_name;
            info.file_id = owner;
            info.decl = stable;
            table.emplace(method_name, std::move(info));
        } else if (slot != nullptr) {
            const bool link_clash = !link_name.empty() && !slot->link_name.empty() && slot->link_name != link_name;
            const bool clash = !same_type(slot->type, signature) || (slot->has_body && has_body) ||
                               slot->unsafe_fn != unsafe || link_clash;
            if (!clash && has_body) {
                slot->has_body = true;
                slot->decl = stable;
                slot->type = signature;
            }
            if (!clash) {
                if (slot->link_name.empty()) {
                    slot->link_name = link_name;
                }
                slot->extern_c = slot->extern_c || extern_c;
            }
        } else {
            MethodInfo extra;
            extra.type = std::move(signature);
            extra.has_body = has_body;
            extra.is_static = is_static;
            extra.unsafe_fn = unsafe;
            extra.exported = exported;
            extra.extern_c = extern_c;
            extra.link_name = link_name;
            extra.file_id = owner;
            extra.decl = stable;
            found->second.more.push_back(std::move(extra));
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
    // The prototype may be the declaration that says `extern`. The body that is specialized
    // has to keep that flag, or the symbol stays internal.
    auto note_extern = [&](const FunctionDecl *decl) {
        if (decl == nullptr || decl->is_extern) {
            return;
        }
        for (OwnedFn &owned : owned_fns_) {
            if (&owned.fn == decl) {
                owned.fn.is_extern = true;
                return;
            }
        }
    };
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
        if (fn.is_extern || slot->is_extern) {
            note_extern(fn.body != nullptr ? stable : slot);
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
        const bool same_list = generic_param_key(owned.fn, params, !owned.fn.static_method) == want;
        if (same_list && owned.fn.type_params.size() != fn.type_params.size()) {
            continue;
        }
        if (!same_list) {
            // `name=` is one object symbol. `extern(abi="c")` on this method is rejected earlier.
            if (!fn.link_name.empty() && owned.fn.link_name == fn.link_name) {
                error(fn.loc, "redefinition");
                return;
            }
            continue;
        }
        const bool link_clash =
            !fn.link_name.empty() && !owned.fn.link_name.empty() && owned.fn.link_name != fn.link_name;
        if (generic_full_key(owned.fn, params) != full || (owned.fn.body != nullptr && fn.body != nullptr) ||
            owned.fn.unsafe != fn.unsafe || link_clash) {
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

// A bare `[...U]` parameter, not `*U` or `[N]U`. The index is the type parameter.
int bare_pack_slot(const FunctionDecl &fn, const Param &param) {
    if (param.is_receiver || param.indirections != 0 || param.is_array || !param.type_args.empty() ||
        !param.type_package.empty()) {
        return -1;
    }
    const int index = param_index(fn.type_params, param.type_name);
    if (index < 0 || !fn.type_params[static_cast<std::size_t>(index)].is_pack) {
        return -1;
    }
    return index;
}

// Arguments line up from the left. A missing suffix is an empty pack: `keep()` and `head(40)`.
bool pack_suffix_fits(const FunctionDecl &fn, std::size_t arg_count) {
    const std::size_t receiver = !fn.params.empty() && fn.params.front().is_receiver ? 1 : 0;
    if (receiver > fn.params.size() || arg_count + receiver > fn.params.size()) {
        return false;
    }
    const std::size_t missing = fn.params.size() - receiver - arg_count;
    for (std::size_t k = 0; k < missing; ++k) {
        if (bare_pack_slot(fn, fn.params[fn.params.size() - 1 - k]) < 0) {
            return false;
        }
    }
    return true;
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
    // A missing suffix of bare pack parameters is an empty pack. `keep()` and `head(40)`.
    const std::size_t receiver = !templ.params.empty() && templ.params.front().is_receiver ? 1 : 0;
    if (!pack_suffix_fits(templ, arg_types.size())) {
        error(call.loc, "argument count");
        return std::nullopt;
    }
    std::vector<int> source(templ.params.size(), -1);
    for (std::size_t i = 0; i < arg_types.size(); ++i) {
        source[receiver + i] = static_cast<int>(i);
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
        if (param.is_receiver || param_i < receiver) {
            continue;
        }
        if (source[param_i] < 0) {
            const int index = bare_pack_slot(templ, param);
            if (index < 0) {
                error(call.loc, "argument count");
                return std::nullopt;
            }
            if (set[static_cast<std::size_t>(index)]) {
                const SpecArg &bound = bounds[static_cast<std::size_t>(index)];
                if (!bound.is_pack || !bound.parts.empty()) {
                    error(call.loc, "argument count");
                    return std::nullopt;
                }
                continue;
            }
            bounds[static_cast<std::size_t>(index)] = pack_bound({});
            set[static_cast<std::size_t>(index)] = 1;
            continue;
        }
        const Type &got = arg_types[static_cast<std::size_t>(source[param_i])];
        if (got.kind == TypeKind::Invalid) {
            continue;
        }
        // `op func (T, T) T` binds `T` from that function. The pointer itself is not `T`.
        if (param.is_fn_ptr) {
            const Type *sig = nullptr;
            if (got.kind == TypeKind::Pointer && got.pointee != nullptr && got.pointee->kind == TypeKind::Function) {
                sig = got.pointee.get();
            } else if (got.kind == TypeKind::Function) {
                sig = &got;
            }
            const auto bind_name = [&](const std::string &type_name, const std::string &type_package, int indirections,
                                       const Type &have) -> bool {
                if (!type_package.empty()) {
                    return true;
                }
                const int slot = param_index(templ.type_params, type_name);
                if (slot < 0 || templ.type_params[static_cast<std::size_t>(slot)].is_value ||
                    templ.type_params[static_cast<std::size_t>(slot)].is_pack) {
                    return true;
                }
                const Type *cursor = &have;
                for (int star = 0; star < indirections; ++star) {
                    if (cursor->kind != TypeKind::Pointer || cursor->pointee == nullptr) {
                        return true;
                    }
                    cursor = cursor->pointee.get();
                }
                if (!constrain(slot, *cursor)) {
                    error(call.loc, "type mismatch");
                    return false;
                }
                return true;
            };
            if (sig != nullptr && sig->result != nullptr && sig->variadic == param.fn_variadic &&
                sig->params.size() == param.fn_params.size()) {
                for (std::size_t part_i = 0; part_i < param.fn_params.size(); ++part_i) {
                    const FnParam &slot = param.fn_params[part_i];
                    if (slot.is_fn_ptr || slot.is_tuple || slot.is_array) {
                        continue;
                    }
                    if (!bind_name(slot.type_name, slot.type_package, slot.indirections, sig->params[part_i])) {
                        return std::nullopt;
                    }
                }
                if (param.fn_results.empty() &&
                    !bind_name(param.type_name, param.type_package, param.ret_indirections, *sig->result)) {
                    return std::nullopt;
                }
            }
            continue;
        }
        // `(P1, P2)` binds each element. A concrete element is checked after substitution.
        if (param.is_tuple) {
            if (got.kind != TypeKind::Tuple || got.alias_id != 0 || got.results.size() != param.tuple_parts.size()) {
                error(call.loc, "type mismatch");
                return std::nullopt;
            }
            for (std::size_t part_i = 0; part_i < param.tuple_parts.size(); ++part_i) {
                const ResultSlot &slot = param.tuple_parts[part_i];
                if (slot.is_tuple || slot.is_fn_ptr || !slot.type_args.empty()) {
                    continue;
                }
                const int part_index = slot.type_package.empty() ? param_index(templ.type_params, slot.type_name) : -1;
                if (part_index < 0 || templ.type_params[static_cast<std::size_t>(part_index)].is_value ||
                    templ.type_params[static_cast<std::size_t>(part_index)].is_pack) {
                    continue;
                }
                const Type *cursor = &got.results[part_i];
                bool peel_ok = true;
                for (int star = 0; star < slot.indirections; ++star) {
                    if (cursor->kind != TypeKind::Pointer || cursor->pointee == nullptr) {
                        peel_ok = false;
                        break;
                    }
                    const bool want_optional = (slot.optional & (1u << star)) != 0;
                    if (!want_optional && cursor->optional) {
                        peel_ok = false;
                        break;
                    }
                    cursor = cursor->pointee.get();
                }
                if (!peel_ok) {
                    continue;
                }
                if (slot.is_array) {
                    if (cursor->kind != TypeKind::Array || cursor->element == nullptr) {
                        continue;
                    }
                    if (!slot.array_len_name.empty()) {
                        const int len_index = param_index(templ.type_params, slot.array_len_name);
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
                    if (!constrain(part_index, *cursor->element)) {
                        error(call.loc, "type mismatch");
                        return std::nullopt;
                    }
                    continue;
                }
                if (!constrain(part_index, *cursor)) {
                    error(call.loc, "type mismatch");
                    return std::nullopt;
                }
            }
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
        // The clone is stored under the file id `collect_templates` used. A lone unit
        // registers its trait at 0, while `loc.file` is the source file.
        // `T pkg.Shape` uses that file's imports. The caller does not have to import `pkg`.
        uint32_t home = file_id_;
        for (const OwnedFn &owned : owned_fns_) {
            if (&owned.fn == &templ) {
                home = owned.file_id;
                break;
            }
        }
        int trait_id = -1;
        if (param.constraint_package.empty()) {
            trait_id = package_trait(home, param.constraint);
            if (trait_id < 0) {
                error(call.loc, "undeclared identifier");
                return std::nullopt;
            }
        } else {
            trait_id = imported_trait(home, param.constraint_package, param.constraint, call.loc);
            if (trait_id < 0) {
                return std::nullopt;
            }
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

bool Checker::generic_trait_parameter(const FunctionDecl &fn) const {
    for (const Param &param : fn.params) {
        if (param.is_receiver || param.is_struct || param.is_enum || param.is_array || param.is_fn_ptr ||
            param.is_tuple || !param.type_package.empty()) {
            continue;
        }
        bool type_param = false;
        for (const TypeParam &item : fn.type_params) {
            if (item.name == param.type_name) {
                type_param = true;
                break;
            }
        }
        if (type_param) {
            continue;
        }
        if (package_trait(file_id_, param.type_name) >= 0) {
            return true;
        }
        uint32_t owner = file_id_;
        if (find_trait_template(param.type_name, owner) != nullptr) {
            return true;
        }
    }
    return false;
}

bool Checker::extern_generic_trait_param(uint32_t file, const FunctionDecl &fn) const {
    if (!fn.is_extern || fn.extern_c) {
        return false;
    }
    for (const Param &param : fn.params) {
        if (param.is_receiver || param.is_struct || param.is_enum || param.is_array || param.is_fn_ptr ||
            param.is_tuple || !param.type_package.empty()) {
            continue;
        }
        bool type_param = false;
        for (const TypeParam &item : fn.type_params) {
            if (item.name == param.type_name) {
                type_param = true;
                break;
            }
        }
        if (type_param) {
            continue;
        }
        if (package_trait(file, param.type_name) >= 0) {
            return true;
        }
    }
    return false;
}

bool Checker::extern_generic_prototype(const FunctionDecl &fn) const {
    // A comptime parameter has no external symbol. The copy that would hold the constant
    // is local to the compilation that has the body.
    if (fn.body != nullptr || !fn.is_extern || fn.extern_c) {
        return false;
    }
    for (const Param &param : fn.params) {
        if (param.is_comptime) {
            return false;
        }
    }
    return true;
}

bool Checker::generic_candidate_matches(const FunctionDecl &templ, const Expr::Call &call,
                                       const std::vector<Type> &arg_types, uint32_t owner) {
    if (templ.extern_c || templ.variadic || templ.type_params.empty() ||
        extern_generic_trait_param(owner, templ) || (templ.body == nullptr && !extern_generic_prototype(templ))) {
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
        if (param.is_tuple) {
            if (got.kind != TypeKind::Tuple || got.alias_id != 0 || got.results.size() != param.tuple_parts.size()) {
                return false;
            }
            for (std::size_t part_i = 0; part_i < param.tuple_parts.size(); ++part_i) {
                const ResultSlot &slot = param.tuple_parts[part_i];
                if (slot.is_tuple || slot.is_fn_ptr) {
                    return false;
                }
                SpecArg spelled;
                if (!spell_type(got.results[part_i], spelled)) {
                    return false;
                }
                const bool added_elem_const = slot.elem_const && !spelled.elem_const;
                if (spelled.indirections != slot.indirections || spelled.optional != slot.optional ||
                    spelled.const_pointee != slot.const_pointee || spelled.is_array != slot.is_array ||
                    (spelled.elem_const != slot.elem_const && !added_elem_const) ||
                    (slot.is_array && spelled.array_len != slot.array_len) || spelled.type_name != slot.type_name ||
                    spelled.type_package != slot.type_package) {
                    return false;
                }
            }
            return true;
        }
        if (got.kind == TypeKind::Invalid) {
            return false;
        }
        // `op func (T, T) T` is that pointer after `T` is substituted.
        if (param.is_fn_ptr) {
            const bool saved = quiet_;
            quiet_ = true;
            const Type want =
                type_of_fn_ptr(param.type_name, param.type_package, param.is_struct, param.is_enum, param.ret_indirections,
                               param.indirections, param.fn_params, param.fn_variadic, param.fn_results, param.loc,
                               param.ret_optional, param.ret_const_pointee);
            quiet_ = saved;
            return want.kind != TypeKind::Invalid && same_type(want, got);
        }
        SpecArg spelled;
        if (!spell_type(got, spelled)) {
            return false;
        }
        // A slice of `T` may be passed where `[]const T` is expected. Dropping const may not.
        const bool added_elem_const = param.elem_const && !spelled.elem_const;
        if (spelled.indirections != param.indirections || spelled.optional != param.optional ||
            spelled.const_pointee != param.const_pointee || spelled.is_array != param.is_array ||
            (spelled.elem_const != param.elem_const && !added_elem_const) ||
            (param.is_array && spelled.array_len != param.array_len)) {
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
                    int written = arg.value;
                    bool known = arg.has_value || (arg.is_value && arg.value_expr == nullptr);
                    if (arg.value_expr != nullptr) {
                        const bool saved = quiet_;
                        quiet_ = true;
                        if (const std::optional<int> folded = fold_int(*arg.value_expr)) {
                            written = *folded;
                            known = true;
                        }
                        quiet_ = saved;
                    }
                    if (!(bound.is_value && known && bound.value == written)) {
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

namespace {

enum class AtomicBuiltin { None, Load, Store, Rmw, Cmpxchg, Fence };

AtomicBuiltin atomic_builtin(const std::string &name) {
    const auto is = [&](const char *stem) {
        const std::string base(stem);
        return name == base || name.starts_with(base + ".spec");
    };
    if (is("atomic_load")) {
        return AtomicBuiltin::Load;
    }
    if (is("atomic_store")) {
        return AtomicBuiltin::Store;
    }
    if (is("atomic_rmw")) {
        return AtomicBuiltin::Rmw;
    }
    if (is("atomic_cmpxchg")) {
        return AtomicBuiltin::Cmpxchg;
    }
    if (is("fence")) {
        return AtomicBuiltin::Fence;
    }
    return AtomicBuiltin::None;
}

bool atomic_integer(const std::string &name) {
    return name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "u8" || name == "u16" ||
           name == "u32" || name == "u64";
}

bool atomic_type_ok(const SpecArg &bound) {
    if (bound.is_value || bound.is_text || bound.is_struct || bound.is_enum || bound.is_pack || bound.is_array ||
        bound.elem_slice || bound.array_len != 0) {
        return false;
    }
    for (const TypeShell &shell : bound.shells) {
        if (shell.kind != TypeShell::Kind::Pointer) {
            return false;
        }
    }
    if (bound.indirections > 0 || !bound.shells.empty()) {
        return true;
    }
    return bound.type_package.empty() && atomic_integer(bound.type_name);
}

bool atomic_pointer(const SpecArg &bound) {
    if (bound.indirections > 0) {
        return true;
    }
    for (const TypeShell &shell : bound.shells) {
        if (shell.kind == TypeShell::Kind::Pointer) {
            return true;
        }
    }
    return false;
}

// Load: relaxed, acquire, seq_cst. Store: relaxed, release, seq_cst. The rest take every order.
bool atomic_order_ok(int order, AtomicBuiltin kind) {
    if (order < 0 || order > 4) {
        return false;
    }
    if (kind == AtomicBuiltin::Load) {
        return order != 2 && order != 3;
    }
    if (kind == AtomicBuiltin::Store) {
        return order != 1 && order != 3;
    }
    return true;
}

// Failure is not release or acq_rel, and success is at least as strong.
bool cmpxchg_orders(int success, int failure) {
    if (failure == 2 || failure == 3) {
        return false;
    }
    if (success == failure || success == 4) {
        return true;
    }
    if (success == 3) {
        return failure != 4;
    }
    if (success == 1) {
        return failure == 0 || failure == 1;
    }
    if (success == 2) {
        return failure == 0 || failure == 2;
    }
    return failure == 0;
}

} // namespace

bool Checker::atomic_builtin_rejected(const FunctionDecl &fn, const Expr::Call &call, const SpecArg *bound) {
    const AtomicBuiltin kind = atomic_builtin(fn.name);
    if (kind == AtomicBuiltin::None || !is_std_builtin_file(comptime_decl_file(fn))) {
        return false;
    }
    if (bound != nullptr && !atomic_type_ok(*bound)) {
        error(call.loc, "not supported yet");
        return true;
    }
    const auto folded = [&](std::size_t index, int &out) {
        if (index >= call.args.size() || call.args[index] == nullptr) {
            error(call.loc, "type mismatch");
            return false;
        }
        const std::optional<int> number = fold_int(*call.args[index]);
        if (!number) {
            error(expr_loc(*call.args[index]), "type mismatch");
            return false;
        }
        out = *number;
        return true;
    };
    const auto want = [&](std::size_t index, AtomicBuiltin mode) {
        int order = 0;
        if (!folded(index, order)) {
            return false;
        }
        if (!atomic_order_ok(order, mode)) {
            error(expr_loc(*call.args[index]), "type mismatch");
            return false;
        }
        return true;
    };
    if (kind == AtomicBuiltin::Load) {
        return !want(1, AtomicBuiltin::Load);
    }
    if (kind == AtomicBuiltin::Store) {
        return !want(2, AtomicBuiltin::Store);
    }
    if (kind == AtomicBuiltin::Fence) {
        return !want(0, AtomicBuiltin::Fence);
    }
    if (kind == AtomicBuiltin::Rmw) {
        int op = 0;
        if (!folded(1, op)) {
            return true;
        }
        if (op < 0 || op > 5) {
            error(expr_loc(*call.args[1]), "type mismatch");
            return true;
        }
        const bool pointer = bound != nullptr ? atomic_pointer(*bound)
                                              : (!fn.params.empty() && (fn.params.front().indirections >= 2 ||
                                                                        !fn.params.front().shells.empty()));
        if (pointer && op != 0) {
            error(call.loc, "not supported yet");
            return true;
        }
        return !want(3, AtomicBuiltin::Rmw);
    }
    int success = 0;
    int failure = 0;
    if (!folded(3, success) || !folded(4, failure)) {
        return true;
    }
    if (!atomic_order_ok(success, AtomicBuiltin::Rmw) || !atomic_order_ok(failure, AtomicBuiltin::Rmw) ||
        !cmpxchg_orders(success, failure)) {
        error(call.loc, "type mismatch");
        return true;
    }
    return false;
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
            if (item == nullptr) {
                continue;
            }
            // The same count as before, plus a call that omits a suffix of bare pack parameters.
            bool empty_suffix = false;
            if (call.args.size() < item->params.size()) {
                empty_suffix = true;
                const std::size_t missing = item->params.size() - call.args.size();
                for (std::size_t k = 0; k < missing; ++k) {
                    if (bare_pack_slot(*item, item->params[item->params.size() - 1 - k]) < 0) {
                        empty_suffix = false;
                        break;
                    }
                }
            }
            if (item->params.size() == call.args.size() || empty_suffix) {
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
            // A comptime parameter is viable only for a comptime argument, and it wins over the
            // same parameter list without `comptime`.
            const FunctionDecl *winner = nullptr;
            int best = -1;
            int wins = 0;
            for (const FunctionDecl *item : arity) {
                const bool saved = quiet_;
                quiet_ = true;
                const bool ok = generic_candidate_matches(*item, call, probed, owner) && comptime_arguments(*item, call);
                quiet_ = saved;
                if (!ok) {
                    continue;
                }
                int score = 0;
                for (const Param &param : item->params) {
                    if (param.is_comptime) {
                        ++score;
                    }
                }
                if (score > best) {
                    best = score;
                    winner = item;
                    wins = 1;
                } else if (score == best) {
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
    if (templ != nullptr && !comptime_arguments(*templ, call)) {
        error(call.loc, "type mismatch");
        return type_invalid();
    }
    if (templ == nullptr || templ->type_params.empty()) {
        return std::nullopt;
    }
    // A macro pastes statements. It has no runtime specialization.
    if (templ->is_macro) {
        error(call.loc, "not supported yet");
        return type_invalid();
    }
    // The comptime slice overload of memeql or memcmp is the value. It has no runtime specialization.
    if (is_std_builtin_file(owner) && mem_compare_comptime(mem_compare_kind(*templ)) && fold_mem_compare(call)) {
        return mem_compare_bool(mem_compare_kind(*templ)) ? type_bool() : type_int();
    }
    // A body defines the symbol. An `extern` prototype names that same specialization.
    // A trait parameter is not specialized away with `T`.
    if (templ->extern_c || templ->variadic || extern_generic_trait_param(owner, *templ) ||
        (templ->body == nullptr && !extern_generic_prototype(*templ))) {
        error(call.loc, templ->extern_c ? "extern(abi=\"c\") on a trait or a generic" : "not supported yet");
        return type_invalid();
    }
    if (templ->unsafe && unsafe_depth_ == 0) {
        error(call.loc, "unsafe call");
    }
    // `i8` and `u16` widen to `i32` as values. An explicit `[i8]` still names that narrow
    // type on the parameter that is that type parameter. Another parameter keeps the `i32`.
    auto narrow_integer = [](const std::string &name) {
        return name == "i8" || name == "u8" || name == "i16" || name == "u16";
    };
    auto explicit_narrow_param = [&](const std::string &name) {
        if (name.empty()) {
            return false;
        }
        const std::size_t count = call.type_args.size() < templ->type_params.size() ? call.type_args.size()
                                                                                     : templ->type_params.size();
        for (std::size_t i = 0; i < count; ++i) {
            const TypeParam &slot = templ->type_params[i];
            if (slot.name != name || !slot.is_type || slot.is_value || slot.is_pack) {
                continue;
            }
            const TypeArg &item = call.type_args[i];
            if (item.type_package.empty() && item.indirections == 0 && !item.is_array && item.shells.empty() &&
                item.args.empty() && !item.is_value && narrow_integer(item.type_name)) {
                return true;
            }
        }
        return false;
    };
    // A pointer argument is not widened, so only the value parameter is put back.
    auto wants_narrow = [&](std::size_t index) {
        if (index >= templ->params.size()) {
            return false;
        }
        const Param &param = templ->params[index];
        if (param.indirections != 0 || param.optional != 0 || param.is_array || param.is_tuple || param.is_fn_ptr ||
            param.is_struct || param.is_enum || param.elem_slice || !param.type_package.empty() ||
            !param.type_args.empty() || !param.shells.empty()) {
            return false;
        }
        return explicit_narrow_param(param.type_name);
    };
    auto unpromoted = [&](bool narrow, Expr &arg, Type got) {
        if (!narrow || got.kind != TypeKind::Int || got.rank != 3 || got.is_unsigned || got.alias_id != 0) {
            return got;
        }
        Type raw = check_raw(arg, true);
        raw.is_const = false;
        raw.decayed_len = 0;
        SpecArg spelled;
        if (!spell_type(raw, spelled) || !spelled.type_package.empty() || spelled.indirections != 0 ||
            !spelled.shells.empty() || spelled.is_array || spelled.is_struct || spelled.is_enum || spelled.is_pack ||
            !narrow_integer(spelled.type_name)) {
            return got;
        }
        return raw;
    };
    std::vector<Type> arg_types;
    bool arg_invalid = false;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
        const auto &arg = call.args[i];
        if (arg == nullptr) {
            arg_invalid = true;
            arg_types.push_back(type_invalid());
            continue;
        }
        Type got = check_expr(*arg, true);
        if (got.kind != TypeKind::Invalid) {
            got = unpromoted(wants_narrow(i), *arg, std::move(got));
        }
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
    if (!bounds.empty() && atomic_builtin_rejected(*templ, call, &bounds.front())) {
        return type_invalid();
    }
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
        const bool keep_atomic = is_std_builtin_file(owner) && atomic_builtin(templ->name) != AtomicBuiltin::None;
        if (!keep_atomic) {
            const std::size_t mark = diags_.size();
            if (expand_generic_comptime_value(call, *templ, bounds, spec_name, owner)) {
                return check_raw(expr, used);
            }
            if (diags_.size() != mark) {
                return type_invalid();
            }
        }
        rewrite();
        return check_raw(expr, used);
    }
    if (generic_spec_depth_ >= 32) {
        error(call.loc, "not supported yet");
        return type_invalid();
    }
    FunctionDecl spec = clone_function(*templ);
    spec.name = spec_name;
    spec.type_params.clear();
    spec.generic_instance = true;
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
    // The body may call this function. Mark it before the result is read so that call does not
    // start a second specialization of the same copy.
    spec_checking_.insert(key);
    Type signature = function_signature(spec);
    file_id_ = saved_file;
    if (signature.kind != TypeKind::Function || signature.result == nullptr || signature.result->kind == TypeKind::Invalid) {
        if (spec.infer_result && spec.results.empty()) {
            error(spec.loc, "not supported yet");
        }
        spec_checking_.erase(key);
        return type_invalid();
    }
    // `func use[T type](items Seq[T])` is a trait parameter beside `[T type]`.
    for (const Type &param : signature.params) {
        if (mentions_trait(param)) {
            error(call.loc, "not supported yet");
            spec_checking_.erase(key);
            return type_invalid();
        }
    }
    // A constant comptime argument drops that parameter. A forwarded name keeps this copy.
    // Clear the in-progress mark first. A nested call of this same generic builds its own copy.
    // `builtin.atomic_load` and the other atomic calls keep the integer. Codegen reads it.
    if (!(is_std_builtin_file(owner) && atomic_builtin(spec.name) != AtomicBuiltin::None)) {
        spec_checking_.erase(key);
        const std::size_t spec_mark = diags_.size();
        if (expand_generic_comptime_value(call, *templ, bounds, spec_name, owner)) {
            return check_raw(expr, used);
        }
        if (diags_.size() != spec_mark) {
            return type_invalid();
        }
        if (apply_comptime_template(call, spec, false, 0, owner)) {
            return check_raw(expr, used);
        }
        if (diags_.size() != spec_mark) {
            return type_invalid();
        }
        spec_checking_.insert(key);
    }
    Symbol symbol;
    symbol.name = spec_name;
    symbol.type = signature;
    symbol.is_function = true;
    symbol.has_body = templ->body != nullptr;
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
    ++generic_spec_depth_;
    check_isolated(owner, spec);
    --generic_spec_depth_;
    spec_checking_.erase(key);
    spec_ready_.insert(key);
    queue_spec(owner, std::move(spec));
    rewrite();
    return check_raw(expr, used);
}

namespace {

bool comptime_i32_param(const Param &param) {
    return !param.is_array && param.indirections == 0 && param.optional == 0 && param.const_pointee == 0 &&
           !param.elem_const && !param.elem_slice && param.shells.empty() && !param.is_struct && !param.is_enum &&
           !param.is_fn_ptr && !param.is_tuple && param.type_name == "i32" && param.type_package.empty() &&
           param.array_len == 0 && param.array_len_name.empty() && param.type_args.empty();
}

// `order comptime Order` is not marked `is_enum` by the parser. The checked type is the enum.
bool comptime_enum_arg(const Param &param) {
    return !param.is_array && param.indirections == 0 && param.optional == 0 && param.const_pointee == 0 &&
           !param.elem_const && !param.elem_slice && param.shells.empty() && !param.is_fn_ptr && !param.is_tuple &&
           param.array_len == 0 && param.array_len_name.empty() && param.type_args.empty();
}

bool comptime_text_param(const Param &param) {
    return param.is_array && param.array_len < 0 && param.elem_const && !param.elem_slice && param.indirections == 0 &&
           param.optional == 0 && param.type_name == "u8" && param.type_package.empty() && !param.is_struct &&
           !param.is_enum && !param.is_fn_ptr && !param.is_tuple && param.array_len_name.empty();
}

std::string comptime_text_piece(const std::string &text) {
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

} // namespace

uint32_t Checker::comptime_decl_file(const FunctionDecl &fn) const {
    if (graph_ != nullptr) {
        for (const PackageNode &node : graph_->nodes) {
            for (const TopDecl &decl : node.unit.decls) {
                if (const auto *item = std::get_if<FunctionDecl>(&decl.data); item == &fn) {
                    return node.file_id;
                }
            }
        }
    }
    // A generic template is the clone in `owned_fns_`. The unit still has the source.
    for (const OwnedFn &owned : owned_fns_) {
        if (&owned.fn == &fn) {
            return owned.file_id;
        }
    }
    return file_id_;
}

bool Checker::apply_comptime_template(Expr::Call &call, const FunctionDecl &fn, bool several, std::size_t skip,
                                      std::optional<uint32_t> owner_file) {
    if (comptime_spec_depth_ >= 32) {
        error(call.loc, "not supported yet");
        return false;
    }
    // The receiver is not an argument. A method's comptime parameter is the next one.
    if (call.callee == nullptr || skip > fn.params.size() || call.args.size() + skip != fn.params.size()) {
        return false;
    }
    const uint32_t owner = owner_file.has_value() ? *owner_file : comptime_decl_file(fn);
    std::vector<TypeParam> names;
    std::vector<SpecArg> bounds;
    std::string pieces;
    for (std::size_t i = skip; i < fn.params.size(); ++i) {
        const Param &param = fn.params[i];
        if (!param.is_comptime) {
            continue;
        }
        if (call.args[i - skip] == nullptr) {
            return false;
        }
        const std::size_t mark = diags_.size();
        const uint32_t saved_file = file_id_;
        file_id_ = owner;
        const Type want = type_of_param(param);
        file_id_ = saved_file;
        if (want.kind == TypeKind::Invalid || diags_.size() != mark) {
            return false;
        }
        const Type got = check_expr(*call.args[i - skip], true);
        if (got.kind == TypeKind::Invalid || diags_.size() != mark) {
            return false;
        }
        // `BinOp(add)` is that function. The alias sits on the pointer. The pointee is the parameter.
        if (!same_type(got, want) && !accepts_pointer(got, want)) {
            return false;
        }
        TypeParam slot;
        slot.name = param.name;
        slot.is_value = true;
        SpecArg bound;
        std::string piece;
        if (comptime_i32_param(param)) {
            std::optional<int> number = fold_int(*call.args[i - skip]);
            if (!number) {
                number = folded_local_int(*call.args[i - skip]);
            }
            if (!number) {
                return false;
            }
            bound.is_value = true;
            bound.value = *number;
            piece = std::to_string(*number);
            if (!piece.empty() && piece.front() == '-') {
                piece[0] = 'n';
            }
        } else if (comptime_text_param(param)) {
            std::unordered_set<std::string> seen;
            const std::optional<std::string> text = constant_string(*call.args[i - skip], seen);
            if (!text) {
                return false;
            }
            bound.is_text = true;
            bound.text = *text;
            piece = comptime_text_piece(*text);
        } else if (want.kind == TypeKind::Enum && comptime_enum_arg(param)) {
            const auto *selector = std::get_if<Expr::Selector>(&call.args[i - skip]->data);
            const EnumInfo::Member *member = nullptr;
            if (selector != nullptr && selector->base != nullptr && enum_designator(*selector->base) == want.enum_id) {
                member = find_enum_member(want.enum_id, selector->field);
            }
            if (member == nullptr || member->value != static_cast<int64_t>(static_cast<int>(member->value))) {
                return false;
            }
            bound.is_enum_value = true;
            bound.value = static_cast<int>(member->value);
            bound.enum_member = member->name;
            bound.type_name = param.type_name;
            bound.type_package = param.type_package;
            bound.is_enum = true;
            bound.enum_id = want.enum_id;
            piece = std::to_string(bound.value);
            if (!piece.empty() && piece.front() == '-') {
                piece[0] = 'n';
            }
        } else if (comptime_constant(*call.args[i - skip])) {
            // The use stays the expression. An integer would change an alias into `i32`.
            const std::unique_ptr<Expr> subst = comptime_substitute(*call.args[i - skip]);
            const std::optional<std::string> encoded = subst != nullptr ? comptime_spec_piece(*subst) : std::nullopt;
            if (!encoded) {
                if (!comptime_param_kept(fn)) {
                    error(expr_loc(*call.args[i - skip]), "not supported yet");
                }
                return false;
            }
            bound.is_constant_expr = true;
            bound.constant_expr = clone_expr(*subst);
            piece = *encoded;
        } else if (argument_is_comptime(*call.args[i - skip])) {
            // The unspecialized extern function accepts this argument.
            // A forwarded comptime name is not this case. The caller keeps the parameter.
            if (!comptime_param_kept(fn)) {
                error(expr_loc(*call.args[i - skip]), "not supported yet");
            }
            return false;
        } else {
            return false;
        }
        names.push_back(std::move(slot));
        bounds.push_back(std::move(bound));
        pieces.push_back('.');
        pieces += piece;
    }
    if (names.empty()) {
        return false;
    }
    std::string spec_name = fn.name + ".spec" + pieces;
    if (several) {
        spec_name.push_back('.');
        spec_name += generic_list_suffix(fn);
    }
    const std::string key = std::to_string(owner) + "\n" + spec_name;
    auto rewrite = [&]() {
        std::vector<std::unique_ptr<Expr>> kept;
        for (std::size_t i = skip; i < fn.params.size(); ++i) {
            if (!fn.params[i].is_comptime && call.args[i - skip] != nullptr) {
                kept.push_back(std::move(call.args[i - skip]));
            }
        }
        call.args = std::move(kept);
        call.selected = nullptr;
        call.type_args.clear();
        if (auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
            named->text = spec_name;
        } else if (auto *selector = std::get_if<Expr::Selector>(&call.callee->data)) {
            selector->field = spec_name;
        }
    };
    if (spec_ready_.contains(key) || spec_checking_.contains(key)) {
        rewrite();
        return true;
    }
    const std::size_t before = diags_.size();
    FunctionDecl spec = clone_function(fn);
    spec.name = spec_name;
    spec.type_params.clear();
    // The generic instance kept the parameter. This further copy does not.
    spec.generic_instance = false;
    spec.exported = false;
    // The dynamic symbol is the unspecialized function. This copy is local.
    spec.is_extern = false;
    const uint32_t saved_file = file_id_;
    file_id_ = owner;
    substitute_function(spec, names, bounds);
    std::vector<Param> runtime;
    runtime.reserve(spec.params.size());
    for (Param &param : spec.params) {
        if (!param.is_comptime) {
            runtime.push_back(std::move(param));
        }
    }
    spec.params = std::move(runtime);
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
    if (diags_.size() != before || !resolve_result_typeof(spec)) {
        file_id_ = saved_file;
        return false;
    }
    spec_checking_.insert(key);
    Type signature = function_signature(spec);
    file_id_ = saved_file;
    if (diags_.size() != before || signature.kind != TypeKind::Function || signature.result == nullptr ||
        signature.result->kind == TypeKind::Invalid) {
        spec_checking_.erase(key);
        return false;
    }
    Symbol symbol;
    symbol.name = spec_name;
    symbol.type = std::move(signature);
    symbol.is_function = true;
    symbol.has_body = true;
    symbol.unsafe_fn = fn.unsafe;
    symbol.package_file = owner;
    spec_symbols_[owner][spec_name] = symbol;
    if (fn.is_static) {
        static_spec_keys_.insert(key);
        statics_[owner].insert_or_assign(spec_name, symbol);
    }
    // A generic instance is published under its mangled name even when `exported` is clear.
    // The further copy has to be found the same way. A method is found on its type.
    bool publish = fn.exported && fn.method_struct.empty();
    if (!publish && fn.method_struct.empty()) {
        const auto file = exports_.find(owner);
        publish = file != exports_.end() && file->second.contains(fn.name);
    }
    if (publish) {
        exports_[owner].insert_or_assign(spec_name, symbol);
    }
    if (!spec.method_struct.empty()) {
        // `decl` stays empty. Codegen finds `Type.name.spec.1` by that key.
        // The queued copy is a different object, so a pointer here would not match it.
        MethodInfo info;
        info.type = symbol.type;
        info.has_body = true;
        info.is_static = spec.static_method;
        info.unsafe_fn = spec.unsafe;
        info.exported = fn.exported;
        info.file_id = owner;
        methods_[owner][spec.method_struct].insert_or_assign(spec_name, std::move(info));
    }
    ++comptime_spec_depth_;
    check_isolated(owner, spec);
    --comptime_spec_depth_;
    spec_checking_.erase(key);
    spec_ready_.insert(key);
    queue_spec(owner, std::move(spec));
    rewrite();
    return true;
}

bool Checker::comptime_arg_specializes(const Param &param, const Expr &arg) {
    if (comptime_i32_param(param)) {
        return fold_int(arg).has_value() || folded_local_int(arg).has_value();
    }
    if (comptime_text_param(param)) {
        std::unordered_set<std::string> seen;
        return constant_string(arg, seen).has_value();
    }
    const bool saved_quiet = quiet_;
    quiet_ = true;
    const std::size_t before = diags_.size();
    const Type want = type_of_param(param);
    quiet_ = saved_quiet;
    if (diags_.size() != before || want.kind == TypeKind::Invalid) {
        return false;
    }
    if (want.kind == TypeKind::Enum && comptime_enum_arg(param)) {
        const auto *selector = std::get_if<Expr::Selector>(&arg.data);
        if (selector == nullptr || selector->base == nullptr || enum_designator(*selector->base) != want.enum_id) {
            return false;
        }
        const EnumInfo::Member *member = find_enum_member(want.enum_id, selector->field);
        return member != nullptr && member->value == static_cast<int64_t>(static_cast<int>(member->value));
    }
    if (!comptime_constant(arg)) {
        return false;
    }
    const std::unique_ptr<Expr> subst = comptime_substitute(arg);
    if (subst == nullptr) {
        return false;
    }
    return comptime_spec_piece(*subst).has_value();
}

bool Checker::expand_generic_comptime_value(Expr::Call &call, const FunctionDecl &templ,
                                            const std::vector<SpecArg> &bounds, const std::string &spec_name,
                                            uint32_t owner) {
    const std::size_t skip = !templ.params.empty() && templ.params.front().is_receiver ? 1 : 0;
    if (call.args.size() + skip != templ.params.size()) {
        return false;
    }
    std::vector<std::size_t> slots;
    for (std::size_t i = skip; i < templ.params.size(); ++i) {
        const Param &orig = templ.params[i];
        if (orig.is_comptime || orig.is_receiver || orig.indirections != 0 || orig.optional != 0 || orig.is_array ||
            orig.is_tuple || orig.is_fn_ptr || orig.is_struct || orig.is_enum || orig.const_pointee != 0 ||
            orig.elem_const || orig.elem_slice || !orig.type_args.empty() || !orig.shells.empty() ||
            !orig.type_package.empty() || call.args[i - skip] == nullptr) {
            continue;
        }
        bool type_param = false;
        for (const TypeParam &item : templ.type_params) {
            if (item.is_type && !item.is_value && !item.is_pack && item.name == orig.type_name) {
                type_param = true;
                break;
            }
        }
        if (type_param) {
            slots.push_back(i);
        }
    }
    if (slots.empty()) {
        return false;
    }
    FunctionDecl spec = clone_function(templ);
    spec.name = spec_name;
    spec.type_params.clear();
    spec.generic_instance = true;
    spec.exported = false;
    const uint32_t saved_file = file_id_;
    file_id_ = owner;
    substitute_function(spec, templ.type_params, bounds);
    expose_bounds(owner, bounds);
    for (Param &param : spec.params) {
        prepare_written(param.type_name, param.type_package, param.is_struct, param.is_enum, param.indirections,
                        param.optional, param.is_array, param.array_len, param.array_len_name, param.type_args, param.loc);
    }
    bool marked = false;
    for (const std::size_t index : slots) {
        if (index >= spec.params.size()) {
            continue;
        }
        Expr &arg = *call.args[index - skip];
        if (!comptime_arg_specializes(spec.params[index], arg)) {
            continue;
        }
        spec.params[index].is_comptime = true;
        marked = true;
    }
    file_id_ = saved_file;
    if (!marked) {
        return false;
    }
    return apply_comptime_template(call, spec, false, skip, owner);
}

std::optional<Type> Checker::call_comptime_spec(Expr &expr, const FunctionDecl *fn, std::size_t skip, uint32_t owner,
                                                bool used) {
    if (fn == nullptr || !comptime_param_template(*fn)) {
        return std::nullopt;
    }
    auto *call = std::get_if<Expr::Call>(&expr.data);
    if (call == nullptr) {
        return std::nullopt;
    }
    const std::size_t before = diags_.size();
    if (!apply_comptime_template(*call, *fn, false, skip, owner)) {
        if (diags_.size() != before) {
            return type_invalid();
        }
        return std::nullopt;
    }
    return check_raw(expr, used);
}

std::optional<Type> Checker::maybe_comptime_param_call(Expr &expr, bool used) {
    auto *call_ptr = std::get_if<Expr::Call>(&expr.data);
    if (call_ptr == nullptr || call_ptr->callee == nullptr || !call_ptr->type_args.empty()) {
        return std::nullopt;
    }
    Expr::Call &call = *call_ptr;
    const Symbol *symbol = nullptr;
    if (const auto *named = std::get_if<Expr::Name>(&call.callee->data)) {
        symbol = lookup(named->text);
    } else if (const auto *selector = std::get_if<Expr::Selector>(&call.callee->data)) {
        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
        if (package == nullptr || !package->is_package) {
            return std::nullopt;
        }
        symbol = find_export(package->package_file, selector->field);
    } else {
        return std::nullopt;
    }
    if (symbol == nullptr || !symbol->is_function || symbol->is_template || symbol->is_local_function ||
        symbol->lists.size() != 1 || symbol->lists.front().decl == nullptr) {
        return std::nullopt;
    }
    const FunctionDecl &fn = *symbol->lists.front().decl;
    if (!comptime_param_template(fn) || call.args.size() != fn.params.size()) {
        return std::nullopt;
    }
    const std::size_t before = diags_.size();
    if (apply_comptime_template(call, fn, false)) {
        return check_raw(expr, used);
    }
    if (diags_.size() != before) {
        return type_invalid();
    }
    return std::nullopt;
}

namespace {

bool same_param_spelling(const Param &lhs, const Param &rhs) {
    if (lhs.is_tuple != rhs.is_tuple || lhs.tuple_parts.size() != rhs.tuple_parts.size()) {
        return false;
    }
    if (lhs.is_tuple) {
        for (std::size_t part = 0; part < lhs.tuple_parts.size(); ++part) {
            const ResultSlot &one = lhs.tuple_parts[part];
            const ResultSlot &other = rhs.tuple_parts[part];
            if (one.type_name != other.type_name || one.type_package != other.type_package ||
                one.indirections != other.indirections || one.is_array != other.is_array ||
                one.array_len != other.array_len || one.is_struct != other.is_struct || one.is_enum != other.is_enum) {
                return false;
            }
        }
        return true;
    }
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
        if (left.is_tuple != right.is_tuple || left.tuple_parts.size() != right.tuple_parts.size()) {
            return false;
        }
        if (left.is_tuple) {
            for (std::size_t part = 0; part < left.tuple_parts.size(); ++part) {
                const ResultSlot &one = left.tuple_parts[part];
                const ResultSlot &other = right.tuple_parts[part];
                if (one.type_name != other.type_name || one.type_package != other.type_package ||
                    one.indirections != other.indirections || one.is_array != other.is_array ||
                    one.array_len != other.array_len || one.is_struct != other.is_struct || one.is_enum != other.is_enum) {
                    return false;
                }
            }
            continue;
        }
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
            left.is_pack != right.is_pack || left.constraint != right.constraint ||
            left.constraint_package != right.constraint_package) {
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
    // A C symbol is one library name. A trait default is copied onto each struct.
    if (fn.extern_c) {
        error(fn.loc, "extern(abi=\"c\") on a trait or a generic");
        return;
    }
    // A variadic default has no body convention here.
    // `name=` on a generic default cannot name every specialization.
    if (fn.variadic || (!fn.link_name.empty() && !fn.type_params.empty())) {
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
    // A child `trait(Parent)` requires this default too. The copy is the same function.
    for (int id = 0; id < static_cast<int>(traits_.size()); ++id) {
        if (id == trait_id) {
            continue;
        }
        int walk = traits_[static_cast<std::size_t>(id)].parent_id;
        bool child = false;
        for (int guard = 0; walk >= 0 && guard < static_cast<int>(traits_.size()); ++guard) {
            if (walk == trait_id) {
                child = true;
                break;
            }
            const int next = traits_[static_cast<std::size_t>(walk)].parent_id;
            if (next == walk) {
                break;
            }
            walk = next;
        }
        if (!child) {
            continue;
        }
        TraitInfo &desc = traits_[static_cast<std::size_t>(id)];
        bool skip = false;
        for (const FunctionDecl *existing : desc.defaults) {
            if (existing == &fn) {
                skip = true;
                break;
            }
            if (existing != nullptr && same_default_shape(*existing, fn)) {
                error(fn.loc, "redefinition");
                skip = true;
                break;
            }
        }
        if (!skip) {
            desc.defaults.push_back(&fn);
        }
    }
}

void Checker::register_method_template(const FunctionDecl &fn) {
    if (fn.type_params.empty()) {
        return;
    }
    for (const TypeParam &param : fn.type_params) {
        // `...T` is not a type parameter. The parser already reported it.
        if (param.is_pack) {
            return;
        }
    }
    const int struct_id = find_tag(file_id_, fn.method_struct);
    if (struct_id < 0) {
        // A numeric or slice alias can have an instance template. A static template can also sit
        // on a pointer alias, a sized-array alias, or a struct alias. The wrong alias is
        // `type mismatch`. An enum, or a name that is not an alias, is `not supported yet`.
        Type alias;
        const bool have_alias = package_typedef(file_id_, fn.method_struct, alias) && alias.alias_id != 0;
        if (!have_alias) {
            error(fn.loc, "not supported yet");
            return;
        }
        const bool owner_ok = fn.static_method ? static_type_owner(fn.method_struct)
                                                : (is_numeric(alias) || is_slice(alias));
        if (!owner_ok) {
            error(fn.loc, "type mismatch");
            return;
        }
    }
    if (fn.extern_c) {
        error(fn.loc, "extern(abi=\"c\") on a trait or a generic");
        return;
    }
    // A tlc `extern` is one symbol per specialization. `...` and a trait parameter are
    // still not one concrete method. An `extern` prototype names that specialization.
    if (fn.variadic || extern_generic_trait_param(file_id_, fn) ||
        (fn.body == nullptr && !extern_generic_prototype(fn))) {
        error(fn.loc, "not supported yet");
        return;
    }
    if (defined_in(fn.method_struct) != file_id_) {
        error(fn.loc, "method in another file");
        return;
    }
    if (!fn.static_method) {
        const bool receiver_ok = !fn.params.empty() && fn.params.front().is_receiver &&
                                 fn.params.front().type_name == fn.method_struct &&
                                 fn.params.front().type_package.empty() && !fn.params.front().is_array &&
                                 fn.params.front().indirections <= 1;
        if (!receiver_ok) {
            error(fn.loc, "type mismatch");
            return;
        }
    } else if (assoc_defined(file_id_, fn.method_struct, fn.name)) {
        error(fn.loc, "redefinition");
        return;
    }
    auto &bucket = method_templates_[file_id_][fn.method_struct][fn.name];
    for (std::size_t i = 0; i < bucket.size(); ++i) {
        const FunctionDecl *existing = bucket[i];
        // `id(value T)` and `id(self, value T)` are different functions.
        if (existing == nullptr || existing->static_method != fn.static_method || !same_default_shape(*existing, fn)) {
            continue;
        }
        // A later body defines the specialization. `extern` on either declaration names it.
        if ((existing->body != nullptr && fn.body != nullptr) || existing->unsafe != fn.unsafe) {
            error(fn.loc, "redefinition");
            return;
        }
        const bool keep_extern = existing->is_extern || fn.is_extern;
        const bool keep_export = existing->exported || fn.exported;
        if (fn.body != nullptr) {
            FunctionDecl &incoming = const_cast<FunctionDecl &>(fn);
            incoming.is_extern = keep_extern;
            incoming.exported = keep_export;
            bucket[i] = &incoming;
        } else {
            FunctionDecl *slot = const_cast<FunctionDecl *>(existing);
            slot->is_extern = keep_extern;
            slot->exported = keep_export;
        }
        return;
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

bool Checker::link_name_taken(const std::string &link_name) const {
    if (link_name.empty()) {
        return false;
    }
    const auto symbol_taken = [&](const Symbol &symbol) {
        if (symbol.link_name == link_name) {
            return true;
        }
        for (const FnList &list : symbol.lists) {
            if (list.link_name == link_name) {
                return true;
            }
        }
        return false;
    };
    for (const auto &file_entry : methods_) {
        for (const auto &owner_entry : file_entry.second) {
            for (const auto &name_entry : owner_entry.second) {
                const MethodInfo &head = name_entry.second;
                if (head.link_name == link_name) {
                    return true;
                }
                for (const MethodInfo &extra : head.more) {
                    if (extra.link_name == link_name) {
                        return true;
                    }
                }
            }
        }
    }
    for (const Scope &scope : scopes_) {
        for (const auto &entry : scope.symbols) {
            if (symbol_taken(entry.second)) {
                return true;
            }
        }
    }
    for (const auto &package_entry : package_syms_) {
        for (const auto &entry : package_entry.second) {
            if (symbol_taken(entry.second)) {
                return true;
            }
        }
    }
    for (const auto &file_entry : statics_) {
        for (const auto &entry : file_entry.second) {
            if (symbol_taken(entry.second)) {
                return true;
            }
        }
    }
    return false;
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
    // `register_trait_default` rejects this. Do not copy a C symbol onto the struct.
    if (method.extern_c) {
        return nullptr;
    }
    method.method_struct = owner;
    method.name = spec_name;
    const std::vector<TypeParam> params = templ.type_params;
    method.type_params.clear();
    if (!method.params.empty() && method.params.front().is_receiver) {
        method.params.front().type_name = owner;
        method.params.front().type_package.clear();
        // An alias receiver stays that alias. A struct receiver is the struct.
        method.params.front().is_struct = find_tag(struct_file, owner) >= 0;
    }
    substitute_function(method, params, bounds);
    const uint32_t saved_file = file_id_;
    file_id_ = struct_file;
    expose_bounds(struct_file, bounds);
    Type signature = function_signature(method);
    file_id_ = saved_file;
    if (signature.kind != TypeKind::Function || signature.result == nullptr || signature.result->kind == TypeKind::Invalid) {
        if (method.infer_result && method.results.empty()) {
            error(method.loc, "not supported yet");
        }
        return nullptr;
    }
    if (const MethodInfo *head = find_method_head(struct_file, owner, spec_name)) {
        const auto same_slot = [&](const MethodInfo &slot) {
            // A static method has no receiver to skip. Dropping its first parameter would
            // compare the wrong list.
            const bool skip_new = !templ.static_method;
            return same_selection(slot.type, !slot.is_static, signature, skip_new);
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
    // One `name=` is one object symbol. The next struct that would gain this copy clashes.
    if (link_name_taken(method.link_name)) {
        error(templ.loc, "redefinition");
        return nullptr;
    }
    deferred_methods_.push_back(OwnedFn{struct_file, std::move(method), generic_spec_depth_});
    FunctionDecl *stable = &deferred_methods_.back().fn;
    default_origin_.emplace(stable, &templ);
    MethodInfo info;
    info.type = std::move(signature);
    info.has_body = stable->body != nullptr;
    info.is_static = templ.static_method;
    info.unsafe_fn = templ.unsafe;
    info.exported = templ.exported;
    info.extern_c = templ.extern_c;
    info.link_name = templ.link_name;
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

Checker::MethodTemplateChoice Checker::pick_method_template(uint32_t home, const std::string &owner, const std::string &name,
                                                            const Expr::Call &call, bool want_static) const {
    MethodTemplateChoice out;
    const auto file = method_templates_.find(home);
    if (file == method_templates_.end()) {
        return out;
    }
    const auto group = file->second.find(owner);
    if (group == file->second.end()) {
        return out;
    }
    const auto found = group->second.find(name);
    if (found == group->second.end()) {
        return out;
    }
    std::vector<const FunctionDecl *> wanted;
    int other = 0;
    for (const FunctionDecl *item : found->second) {
        if (item == nullptr || !type_arg_count_ok(*item, call)) {
            continue;
        }
        if (item->static_method == want_static) {
            wanted.push_back(item);
        } else {
            ++other;
        }
    }
    if (wanted.size() > 1 || (wanted.empty() && other > 0)) {
        out.kind = 2;
        return out;
    }
    if (wanted.size() == 1) {
        out.kind = 1;
        out.templ = wanted.front();
    }
    return out;
}

std::optional<Type> Checker::apply_method_template(Expr &expr, const FunctionDecl &templ, uint32_t home,
                                                   const std::string &owner, bool value_base, bool used) {
    auto *call_ptr = std::get_if<Expr::Call>(&expr.data);
    if (call_ptr == nullptr || call_ptr->callee == nullptr) {
        return type_invalid();
    }
    Expr::Call &call = *call_ptr;
    auto *selector = std::get_if<Expr::Selector>(&call.callee->data);
    if (selector == nullptr) {
        return type_invalid();
    }
    auto check_args = [&]() {
        for (const auto &arg : call.args) {
            if (arg) {
                check_expr(*arg, true);
            }
        }
    };
    // A value call still tracks initialization. A type name is not an expression.
    auto note_base = [&]() {
        if (value_base && selector->base != nullptr) {
            check_expr(*selector->base, true);
        }
    };
    // A body defines the symbol. An `extern` prototype names that same specialization.
    if (templ.body == nullptr && !extern_generic_prototype(templ)) {
        note_base();
        error(call.loc, "not supported yet");
        check_args();
        return type_invalid();
    }
    // Report the source name. The field is rewritten to the specialization below.
    if (!package_owns(home) && !templ.exported) {
        note_base();
        error(call.loc, selector->field + " is not exported");
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
        note_base();
        return type_invalid();
    }
    const auto inferred = infer_generic_bounds(templ, call, arg_types);
    if (!inferred) {
        note_base();
        return type_invalid();
    }
    std::vector<SpecArg> bounds = *inferred;
    disambiguate_bounds(home, bounds);
    const std::string spec_name = mangle_spec(templ.name, bounds);
    const FunctionDecl *installed = install_default(templ, home, owner, spec_name, bounds);
    if (installed == nullptr) {
        note_base();
        return type_invalid();
    }
    selector->field = spec_name;
    call.type_args.clear();
    return check_raw(expr, used);
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
    // A missing template, or only the other staticness, is reported here. Kind 0 is absent.
    auto finish_static = [&](const MethodTemplateChoice &choice, uint32_t home, const std::string &owner,
                             bool value_base) -> std::optional<Type> {
        if (choice.kind == 2) {
            if (value_base && selector->base != nullptr) {
                check_expr(*selector->base, true);
            }
            error(call.loc, "type mismatch");
            check_args();
            return type_invalid();
        }
        if (choice.kind != 1 || choice.templ == nullptr) {
            if (value_base && selector->base != nullptr) {
                check_expr(*selector->base, true);
            }
            error(call.loc, "undeclared identifier");
            check_args();
            return type_invalid();
        }
        return apply_method_template(expr, *choice.templ, home, owner, value_base, used);
    };
    // `Point.id[i32](40)` and `Integer.id[i32](40)`. The name is a type, so it is not probed as a value.
    if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
        const Symbol *symbol = lookup(named->text);
        const bool shadowed = symbol != nullptr && !symbol->is_type;
        if (!shadowed && static_type_owner(named->text)) {
            return finish_static(pick_method_template(defined_in(named->text), named->text, selector->field, call, true),
                                 defined_in(named->text), named->text, false);
        }
    }
    // `math.Point.id[i32](40)`. Probing `math.Point` as a value is `type mismatch`.
    if (const auto *inner = std::get_if<Expr::Selector>(&selector->base->data);
        inner != nullptr && inner->base != nullptr && inner->type_args.empty()) {
        if (const auto *package_name = std::get_if<Expr::Name>(&inner->base->data)) {
            const Symbol *package = lookup(package_name->text);
            if (package != nullptr && package->is_package) {
                const Symbol *type = find_export(package->package_file, inner->field);
                const bool enumerator = type != nullptr && type->type.kind == TypeKind::Enum &&
                                        find_enum_member(type->type.enum_id, selector->field) != nullptr;
                if (type != nullptr && type->is_type && !enumerator) {
                    uint32_t home = package->package_file;
                    const Type &written = type->type;
                    if (written.kind == TypeKind::Struct && written.struct_id >= 0 &&
                        written.struct_id < static_cast<int>(structs_.size())) {
                        home = structs_[static_cast<std::size_t>(written.struct_id)].file_id;
                    } else if (written.kind == TypeKind::Enum && written.enum_id >= 0 &&
                               written.enum_id < static_cast<int>(enum_types_.size())) {
                        home = enum_types_[static_cast<std::size_t>(written.enum_id)].file_id;
                    } else if (written.alias_id > 0 &&
                               static_cast<std::size_t>(written.alias_id) < alias_file_.size()) {
                        home = alias_file_[static_cast<std::size_t>(written.alias_id)];
                    } else {
                        for (const uint32_t id : files_in_package(package->package_file)) {
                            const auto aliases = typedefs_.find(id);
                            if (aliases != typedefs_.end() && aliases->second.contains(inner->field)) {
                                home = id;
                                break;
                            }
                            const auto tags = tags_.find(id);
                            if (tags != tags_.end() && tags->second.contains(inner->field)) {
                                home = id;
                                break;
                            }
                        }
                    }
                    return finish_static(pick_method_template(home, inner->field, selector->field, call, true), home,
                                         inner->field, false);
                }
            }
        }
    }
    // `Vec[i32].get[U](value)`, `Buf[4, i32].get[U](value)`, and `pkg.Vec[i32].get[U](value)`.
    {
        uint32_t home = 0;
        std::string owner;
        if (generic_spec_base(*selector, home, owner)) {
            if (owner.empty()) {
                check_args();
                return type_invalid();
            }
            return finish_static(pick_method_template(home, owner, selector->field, call, true), home, owner, false);
        }
    }
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
        bool alias_pointer = false;
        const Type *alias = scalar_alias(probe, alias_pointer);
        if (alias == nullptr) {
            alias = slice_alias(probe, alias_pointer);
        }
        if (alias != nullptr) {
            (void)alias_pointer;
            uint32_t home = 0;
            if (alias->alias_id > 0 && static_cast<std::size_t>(alias->alias_id) < alias_file_.size()) {
                home = alias_file_[static_cast<std::size_t>(alias->alias_id)];
            }
            return finish_static(pick_method_template(home, type_desc(*alias), selector->field, call, false), home,
                                 type_desc(*alias), true);
        }
        check_expr(*selector->base, true);
        error(call.loc, "not supported yet");
        check_args();
        return type_invalid();
    }
    const uint32_t struct_file = structs_[static_cast<std::size_t>(object.struct_id)].file_id;
    const std::string struct_name = structs_[static_cast<std::size_t>(object.struct_id)].name;
    const MethodTemplateChoice choice = pick_method_template(struct_file, struct_name, selector->field, call, false);
    if (choice.kind == 2) {
        check_expr(*selector->base, true);
        error(call.loc, "type mismatch");
        check_args();
        return type_invalid();
    }
    if (choice.kind == 1 && choice.templ != nullptr) {
        return apply_method_template(expr, *choice.templ, struct_file, struct_name, true, used);
    }
    const FunctionDecl *templ = nullptr;
    {
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
    if (templ == nullptr) {
        check_expr(*selector->base, true);
        error(call.loc, "not supported yet");
        check_args();
        return type_invalid();
    }
    return apply_method_template(expr, *templ, struct_file, struct_name, true, used);
}

void Checker::drain_deferred() {
    std::size_t index = 0;
    while (index < deferred_methods_.size()) {
        OwnedFn &item = deferred_methods_[index];
        ++index;
        if (!item.fn.type_params.empty()) {
            continue;
        }
        const int saved = generic_spec_depth_;
        generic_spec_depth_ = item.spec_depth;
        check_isolated(item.file_id, item.fn);
        generic_spec_depth_ = saved;
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
                if (node.each_expr) {
                    relocate_expr(*node.each_expr, moved);
                }
                if (node.body) {
                    relocate_stmt(*node.body, moved);
                }
                for (const auto &part : node.unrolled) {
                    if (part) {
                        relocate_stmt(*part, moved);
                    }
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
    if (const PackageNode *node = find_node(file_id)) {
        return &const_cast<PackageNode *>(node)->unit;
    }
    return primary_unit_;
}

} // namespace tlc
