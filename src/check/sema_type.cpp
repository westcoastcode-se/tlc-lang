// Struct, trait, enum, and alias types, and how a type is spelled.
#include "check/check.hpp"
#include "check/check_util.hpp"

namespace tlc {

namespace {

std::unique_ptr<Expr> name_expr(std::string text) {
    auto expr = std::make_unique<Expr>();
    Expr::Name node;
    node.text = std::move(text);
    expr->data = std::move(node);
    return expr;
}

std::unique_ptr<Expr> sizeof_type(std::string type_name, bool align) {
    auto expr = std::make_unique<Expr>();
    Expr::Sizeof node;
    node.on_type = true;
    node.type_name = std::move(type_name);
    node.align = align;
    expr->data = std::move(node);
    return expr;
}

// `unsafe func Allocator::malloc[T type](self *this) ?*T`. The pointer stays in `owned_defaults_`.
FunctionDecl allocator_malloc_default() {
    FunctionDecl fn;
    fn.name = "malloc";
    fn.method_struct = "Allocator";
    fn.unsafe = true;
    fn.exported = true;
    TypeParam param;
    param.name = "T";
    param.is_type = true;
    fn.type_params.push_back(std::move(param));
    Param self;
    self.name = "self";
    self.type_name = "Allocator";
    self.is_receiver = true;
    self.indirections = 1;
    fn.params.push_back(std::move(self));
    ResultSlot result;
    result.type_name = "T";
    result.indirections = 1;
    result.optional = 1;
    fn.results.push_back(result);
    fn.return_type = "T";
    fn.indirections = 1;
    fn.optional = 1;

    auto callee = std::make_unique<Expr>();
    Expr::Selector selector;
    selector.base = name_expr("self");
    selector.field = "malloc";
    callee->data = std::move(selector);
    auto call_expr = std::make_unique<Expr>();
    Expr::Call call;
    call.callee = std::move(callee);
    call.args.push_back(sizeof_type("T", false));
    call.args.push_back(sizeof_type("T", true));
    call_expr->data = std::move(call);

    auto bind = std::make_unique<Stmt>();
    Stmt::Bind binding;
    binding.is_var = true;
    binding.names.push_back("value");
    binding.value = std::move(call_expr);
    bind->data = std::move(binding);

    auto cast_expr = std::make_unique<Expr>();
    Expr::Cast cast;
    cast.type_name = "T";
    cast.indirections = 1;
    cast.optional = 1;
    cast.expr = name_expr("value");
    cast_expr->data = std::move(cast);
    auto ret = std::make_unique<Stmt>();
    Stmt::Return ret_node;
    ret_node.value = std::move(cast_expr);
    ret->data = std::move(ret_node);

    auto block = std::make_unique<Stmt>();
    Stmt::Block body;
    body.statements.push_back(std::move(bind));
    body.statements.push_back(std::move(ret));
    block->data = std::move(body);
    fn.body = std::move(block);
    return fn;
}

} // namespace

void Checker::ensure_allocator_trait() {
    if (allocator_trait_id_ >= 0) {
        return;
    }
    // `*u8` and `?*u8`. The optional bit is what lets `malloc` return null.
    const Type byte = type_rank(1, true);
    Type pointer = type_pointer(byte);
    Type optional = pointer;
    optional.optional = true;
    const Type size = type_int();
    TraitInfo info;
    info.name = "Allocator";
    const int id = static_cast<int>(traits_.size());
    info.methods.push_back(TraitInfo::Method{"malloc", type_function(optional, {size, size}), true, {}, id});
    info.methods.push_back(TraitInfo::Method{"free", type_function(type_void(), {pointer}), false, {}, id});
    info.methods.push_back(TraitInfo::Method{"realloc", type_function(optional, {pointer, size, size}), true, {}, id});
    owned_defaults_.push_back(allocator_malloc_default());
    info.defaults.push_back(&owned_defaults_.back());
    allocator_trait_id_ = id;
    traits_.push_back(std::move(info));
}

void Checker::register_traits(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    ensure_allocator_trait();
    trait_names_[file_id].emplace("Allocator", allocator_trait_id_);
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<TraitDecl>(&decl.data);
        if (item == nullptr || item->name.empty()) {
            continue;
        }
        if (package_trait(file_id, item->name) >= 0 || package_has_enum(file_id, item->name) ||
            find_tag(file_id, item->name) >= 0) {
            error(item->loc, "redefinition");
            continue;
        }
        const int id = static_cast<int>(traits_.size());
        TraitInfo info;
        info.name = item->name;
        info.file_id = file_id;
        info.parent_name = item->parent_name;
        info.parent_package = item->parent_package;
        info.parent_loc = item->parent_loc;
        info.pending = item->fields;
        traits_.push_back(std::move(info));
        trait_names_[file_id][item->name] = id;
        for (const FunctionDecl &method : item->methods) {
            if (method.name.empty()) {
                continue;
            }
            bool duplicate = false;
            for (const TraitInfo::Method &existing : traits_[static_cast<std::size_t>(id)].methods) {
                if (existing.name == method.name) {
                    error(method.loc, "redefinition");
                    duplicate = true;
                }
            }
            if (duplicate) {
                continue;
            }
            bool wrote_receiver = false;
            for (const Param &param : method.params) {
                if (param.is_receiver) {
                    wrote_receiver = true;
                }
            }
            if (wrote_receiver || method.variadic) {
                error(method.loc, method.variadic && !wrote_receiver ? "not supported yet" : "type mismatch");
                continue;
            }
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
            traits_[static_cast<std::size_t>(id)].methods.push_back(std::move(saved));
        }
    }
}

void Checker::inherit_traits() {
    const int count = static_cast<int>(traits_.size());
    std::vector<uint8_t> done(static_cast<std::size_t>(count), 0);
    std::vector<uint8_t> stack(static_cast<std::size_t>(count), 0);
    for (int id = 0; id < count; ++id) {
        TraitInfo &info = traits_[static_cast<std::size_t>(id)];
        if (info.parent_name.empty()) {
            continue;
        }
        file_id_ = info.file_id;
        if (!info.parent_package.empty()) {
            // A trait in another package is not exported yet.
            error(info.parent_loc, "not supported yet");
            info.parent_name.clear();
            continue;
        }
        if (info.parent_name == info.name) {
            error(info.parent_loc, "type mismatch");
            info.parent_name.clear();
            continue;
        }
        const int parent = package_trait(info.file_id, info.parent_name);
        if (parent < 0) {
            const bool other = find_tag(info.file_id, info.parent_name) >= 0 ||
                               package_has_enum(info.file_id, info.parent_name);
            error(info.parent_loc, other ? "type mismatch" : "undeclared identifier");
            info.parent_name.clear();
            continue;
        }
        info.parent_id = parent;
    }
    const auto inherit_one = [&](auto &&self, int id) -> void {
        if (id < 0 || id >= count || done[static_cast<std::size_t>(id)] != 0) {
            return;
        }
        if (stack[static_cast<std::size_t>(id)] != 0) {
            error(traits_[static_cast<std::size_t>(id)].parent_loc, "type mismatch");
            traits_[static_cast<std::size_t>(id)].parent_id = -1;
            done[static_cast<std::size_t>(id)] = 1;
            return;
        }
        stack[static_cast<std::size_t>(id)] = 1;
        TraitInfo &info = traits_[static_cast<std::size_t>(id)];
        if (info.parent_id >= 0) {
            self(self, info.parent_id);
        }
        stack[static_cast<std::size_t>(id)] = 0;
        done[static_cast<std::size_t>(id)] = 1;
        if (info.parent_id < 0 || info.parent_id >= count) {
            return;
        }
        const TraitInfo &parent = traits_[static_cast<std::size_t>(info.parent_id)];
        for (const TraitInfo::Method &method : parent.methods) {
            bool clash = false;
            for (const TraitInfo::Method &own : info.methods) {
                if (own.name == method.name) {
                    error(own.loc, "redefinition");
                    clash = true;
                }
            }
            if (!clash) {
                // The copy is visible on the child. `home` still names the trait that declared it.
                TraitInfo::Method copy = method;
                if (copy.home < 0) {
                    copy.home = info.parent_id;
                }
                info.methods.push_back(std::move(copy));
            }
        }
        for (const FunctionDecl *def : parent.defaults) {
            if (def == nullptr) {
                continue;
            }
            bool have = false;
            for (const FunctionDecl *own : info.defaults) {
                if (own == def) {
                    have = true;
                    break;
                }
            }
            if (!have) {
                info.defaults.push_back(def);
            }
        }
    };
    for (int id = 0; id < count; ++id) {
        inherit_one(inherit_one, id);
    }
}

bool Checker::require_satisfies(int struct_id, int trait_id, bool through_pointer, SourceLoc loc, uint32_t body_home) {
    if (struct_id < 0 || trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        error(loc, "type mismatch");
        return false;
    }
    // A quiet probe counts a plain default as the required method. Grant only after that probe succeeds,
    // and copy the struct name first: installing a default can grow `structs_`.
    if (!quiet_ && struct_id < static_cast<int>(structs_.size())) {
        const bool saved = quiet_;
        quiet_ = true;
        const bool covered = require_satisfies(struct_id, trait_id, through_pointer, loc, body_home);
        quiet_ = saved;
        if (covered) {
            const uint32_t struct_file = structs_[static_cast<std::size_t>(struct_id)].file_id;
            const std::string struct_name = structs_[static_cast<std::size_t>(struct_id)].name;
            grant_trait_defaults(struct_file, struct_name, trait_id);
        }
    }
    const bool known_struct = struct_id < static_cast<int>(structs_.size());
    const StructInfo *info = known_struct ? &structs_[static_cast<std::size_t>(struct_id)] : nullptr;
    bool ok = true;
    const auto same_package = [&](uint32_t view, uint32_t file) {
        for (const uint32_t id : files_in_package(view)) {
            if (id == file) {
                return true;
            }
        }
        return false;
    };
    const auto visible = [&](const MethodInfo &candidate) {
        if (info == nullptr) {
            return false;
        }
        // The caller sees an unexported method only inside the struct's package.
        // The specialized body is compiled in `body_home` and has the same limit.
        if (!same_package(file_id_, info->file_id) && !candidate.exported) {
            return false;
        }
        if (body_home != 0 && !same_package(body_home, info->file_id) && !candidate.exported) {
            return false;
        }
        return true;
    };
    for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(trait_id)].methods) {
        const MethodInfo *head = nullptr;
        if (info != nullptr) {
            for (const uint32_t id : files_in_package(info->file_id)) {
                const auto methods = methods_.find(id);
                if (methods == methods_.end()) {
                    continue;
                }
                const auto group = methods->second.find(info->name);
                if (group == methods->second.end()) {
                    continue;
                }
                const auto found = group->second.find(method.name);
                if (found == group->second.end()) {
                    continue;
                }
                head = &found->second;
                break;
            }
        }
        if (head == nullptr) {
            if (quiet_ && plain_default_supplies(trait_id, method, through_pointer)) {
                continue;
            }
            error(loc, "undeclared identifier");
            ok = false;
            continue;
        }
        const auto signature_ok = [&](const MethodInfo &candidate) {
            if (!candidate.has_body || candidate.is_static || candidate.unsafe_fn != method.unsafe_fn ||
                !trait_method_matches(method.type, candidate.type)) {
                return false;
            }
            const bool pointer_method = candidate.type.kind == TypeKind::Function && !candidate.type.params.empty() &&
                                        candidate.type.params[0].kind == TypeKind::Pointer;
            return through_pointer || !pointer_method;
        };
        const MethodInfo *concrete = nullptr;
        bool hidden = false;
        bool mismatched = false;
        auto consider = [&](const MethodInfo &candidate) {
            if (!candidate.has_body) {
                return;
            }
            mismatched = true;
            if (!signature_ok(candidate)) {
                return;
            }
            if (!visible(candidate)) {
                hidden = true;
                return;
            }
            concrete = &candidate;
        };
        consider(*head);
        for (const MethodInfo &extra : head->more) {
            consider(extra);
        }
        if (concrete == nullptr) {
            error(loc, hidden ? method.name + " is not exported" : (mismatched ? "type mismatch" : "undeclared identifier"));
            ok = false;
        }
    }
    const Type object = type_struct(struct_id);
    for (const TraitInfo::Field &field : traits_[static_cast<std::size_t>(trait_id)].fields) {
        Type got;
        if (!lookup_field(object, field.name, got)) {
            error(loc, "undeclared identifier");
            ok = false;
            continue;
        }
        if (field.type.kind == TypeKind::Invalid || got.kind == TypeKind::Invalid || !same_type(field.type, got)) {
            error(loc, "type mismatch");
            ok = false;
        }
    }
    return ok;
}

void Checker::define_trait_fields() {
    for (TraitInfo &info : traits_) {
        file_id_ = info.file_id;
        for (const FieldDecl &source : info.pending) {
            if (source.name.empty()) {
                continue;
            }
            bool duplicate = false;
            for (const TraitInfo::Method &method : info.methods) {
                if (method.name == source.name) {
                    error(source.loc, "redefinition");
                    duplicate = true;
                }
            }
            for (const TraitInfo::Field &existing : info.fields) {
                if (existing.name == source.name) {
                    error(source.loc, "redefinition");
                    duplicate = true;
                }
            }
            if (duplicate) {
                continue;
            }
            // A trait lists the field. A value on it is not a trait form.
            if (source.init != nullptr) {
                error(source.loc, "not supported yet");
            }
            FieldDecl field = source;
            prepare_written(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                            field.optional, field.is_array, field.array_len, field.array_len_name, field.type_args,
                            field.loc);
            fold_shells(field.shells, field.loc);
            Type field_type =
                field.is_fn_ptr ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                 field.ret_indirections, field.indirections, field.fn_params,
                                                 field.fn_variadic, field.fn_results, field.loc, field.ret_optional,
                                                 field.ret_const_pointee)
                                : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                              false, field.type_package, field.is_struct, field.is_enum, false,
                                              field.optional, field.const_pointee, false, field.elem_const,
                                              field.elem_slice, field.shells, true);
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
            saved.home = static_cast<int>(&info - traits_.data());
            info.fields.push_back(std::move(saved));
        }
        info.pending.clear();
    }
}

void Checker::link_parent_fields() {
    const int count = static_cast<int>(traits_.size());
    std::vector<uint8_t> done(static_cast<std::size_t>(count), 0);
    const auto link = [&](auto &&self, int id) -> void {
        if (id < 0 || id >= count || done[static_cast<std::size_t>(id)] != 0) {
            return;
        }
        done[static_cast<std::size_t>(id)] = 1;
        TraitInfo &info = traits_[static_cast<std::size_t>(id)];
        if (info.parent_id < 0) {
            return;
        }
        self(self, info.parent_id);
        // The child's parent id stays. A copied field keeps the trait that declared it.
        const TraitInfo &parent = traits_[static_cast<std::size_t>(info.parent_id)];
        for (const TraitInfo::Field &field : parent.fields) {
            bool clash = false;
            for (const TraitInfo::Field &own : info.fields) {
                if (own.name == field.name) {
                    error(own.loc, "redefinition");
                    clash = true;
                }
            }
            if (clash) {
                continue;
            }
            TraitInfo::Field copy = field;
            if (copy.home < 0) {
                copy.home = info.parent_id;
            }
            info.fields.push_back(std::move(copy));
        }
    };
    for (int id = 0; id < count; ++id) {
        link(link, id);
    }
}

bool Checker::trait_is(int got, int want) const {
    if (got < 0 || want < 0) {
        return false;
    }
    const int count = static_cast<int>(traits_.size());
    int walk = got;
    for (int guard = 0; walk >= 0 && walk < count && guard < count; ++guard) {
        if (walk == want) {
            return true;
        }
        const int next = traits_[static_cast<std::size_t>(walk)].parent_id;
        if (next == walk) {
            return false;
        }
        walk = next;
    }
    return false;
}

bool Checker::field_waits(const FieldDecl &field) const {
    if (field.indirections > 0 || field.is_fn_ptr || !field.type_package.empty() || field.is_enum ||
        field.type_name.empty()) {
        return false;
    }
    // A sized array stores the element. A pointer or a slice does not, so it does not wait.
    if (!field.shells.empty()) {
        for (const TypeShell &shell : field.shells) {
            if (shell.kind != TypeShell::Kind::Array) {
                return false;
            }
        }
    } else if (field.elem_slice || (field.is_array && field.array_len < 0)) {
        return false;
    }
    const int id = find_tag(file_id_, field.type_name);
    if (id < 0 || id >= static_cast<int>(structs_.size())) {
        return false;
    }
    return !structs_[static_cast<std::size_t>(id)].complete;
}

void Checker::require_extern_struct(const Type &type, SourceLoc loc) {
    if (type.kind == TypeKind::Pointer && type.pointee) {
        require_extern_struct(*type.pointee, loc);
        return;
    }
    if (type.kind == TypeKind::Array && type.element) {
        require_extern_struct(*type.element, loc);
        return;
    }
    if (type.kind == TypeKind::Function) {
        if (type.result) {
            require_extern_struct(*type.result, loc);
        }
        for (const Type &param : type.params) {
            require_extern_struct(param, loc);
        }
        return;
    }
    if (type.kind == TypeKind::Tuple) {
        for (const Type &part : type.results) {
            require_extern_struct(part, loc);
        }
        return;
    }
    if (type.kind != TypeKind::Struct || type.struct_id < 0 ||
        type.struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    if (!structs_[static_cast<std::size_t>(type.struct_id)].extern_c) {
        error(loc, "struct must be extern(abi=\"c\")");
    }
}

bool Checker::mentions_tagged_union(const Type &type) const {
    if (type.kind == TypeKind::Pointer && type.pointee) {
        return mentions_tagged_union(*type.pointee);
    }
    if (type.kind == TypeKind::Array && type.element) {
        return mentions_tagged_union(*type.element);
    }
    if (type.kind == TypeKind::Function) {
        if (type.result != nullptr && mentions_tagged_union(*type.result)) {
            return true;
        }
        for (const Type &param : type.params) {
            if (mentions_tagged_union(param)) {
                return true;
            }
        }
        return false;
    }
    if (type.kind == TypeKind::Tuple) {
        for (const Type &part : type.results) {
            if (mentions_tagged_union(part)) {
                return true;
            }
        }
        return false;
    }
    if (type.kind != TypeKind::Struct || type.struct_id < 0 ||
        type.struct_id >= static_cast<int>(structs_.size())) {
        return false;
    }
    const StructInfo &info = structs_[static_cast<std::size_t>(type.struct_id)];
    return info.is_union && !info.tag_name.empty();
}

void Checker::declare_struct_tags(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    auto publish_tag = [&](const StructDecl &item, int id) {
        if (!item.exported) {
            return;
        }
        const auto same_tag = [&](const Symbol &symbol) {
            return symbol.is_struct_tag && symbol.type.kind == TypeKind::Struct && symbol.type.struct_id == id;
        };
        const auto local = exports_[file_id_].find(item.name);
        if (local != exports_[file_id_].end()) {
            if (!same_tag(local->second)) {
                error(item.loc, "redefinition");
            }
            return;
        }
        for (const uint32_t other : files_in_package(file_id_)) {
            if (other == file_id_) {
                continue;
            }
            const auto file = exports_.find(other);
            if (file == exports_.end()) {
                continue;
            }
            const auto found = file->second.find(item.name);
            if (found == file->second.end()) {
                continue;
            }
            if (!same_tag(found->second)) {
                error(item.loc, "redefinition");
            }
            return;
        }
        Symbol symbol;
        symbol.name = item.name;
        symbol.is_type = true;
        symbol.is_struct_tag = true;
        symbol.type = type_struct(id);
        exports_[file_id_].emplace(item.name, std::move(symbol));
    };
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<StructDecl>(&decl.data);
        if (item == nullptr || item->name.empty()) {
            continue;
        }
        if (!item->type_params.empty()) {
            // A C type is one concrete layout. A generic struct is specialized per use.
            if (item->extern_c) {
                error(item->loc, "extern(abi=\"c\") on a trait or a generic");
            } else if (item->is_extern || item->is_union) {
                error(item->loc, "not supported yet");
            }
            for (const TypeParam &param : item->type_params) {
                if (param.is_pack) {
                    error(item->loc, "not supported yet");
                    break;
                }
            }
            owned_structs_.push_back(*item);
            struct_templates_[file_id][item->name] = &owned_structs_.back();
            continue;
        }
        const int existing = find_tag(file_id, item->name);
        if (existing >= 0) {
            error(item->loc, "redefinition");
            continue;
        }
        const int id = static_cast<int>(structs_.size());
        StructInfo created;
        created.name = item->name;
        created.file_id = file_id;
        created.loc = item->loc;
        created.exported = item->exported;
        created.anonymous = item->anonymous;
        created.is_extern = item->is_extern;
        created.extern_c = item->extern_c;
        created.align = item->align;
        created.is_union = item->is_union;
        created.tag_name = item->tag_name;
        created.tag_package = item->tag_package;
        structs_.push_back(std::move(created));
        tags_[file_id][item->name] = id;
        if (item->fields.empty()) {
            // An empty tlc struct is zero-sized and may still have methods. An empty
            // extern(abi="c") struct is a one-byte opaque handle. An empty `union(Enum)` keeps
            // the tag. An empty C union is not a type.
            if (item->is_union && (item->extern_c || item->tag_name.empty())) {
                error(item->loc, "not supported yet");
            }
            structs_.back().complete = true;
        } else {
            pending_structs_.push_back(PendingStruct{file_id, item, id});
        }
        if (!item->anonymous) {
            publish_tag(*item, id);
        }
    }
}

int Checker::min_align_of(const Type &type) const {
    if (type.kind == TypeKind::Pointer || type.kind == TypeKind::Function || type.kind == TypeKind::Meta) {
        return 8;
    }
    if (type.kind == TypeKind::Array) {
        // A slice is `{ptr, i32}`. The pointer sets the alignment.
        if (type.length < 0) {
            return 8;
        }
        return type.element == nullptr ? 1 : min_align_of(*type.element);
    }
    if (type.kind == TypeKind::Float) {
        return type.rank >= 64 ? 8 : 4;
    }
    if (type.kind == TypeKind::Enum) {
        if (type.enum_id < 0 || type.enum_id >= static_cast<int>(enum_types_.size())) {
            return 4;
        }
        return min_align_of(enum_types_[static_cast<std::size_t>(type.enum_id)].underlying);
    }
    if (type.kind == TypeKind::Struct) {
        if (type.struct_id < 0 || type.struct_id >= static_cast<int>(structs_.size())) {
            return 1;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(type.struct_id)];
        int align = 1;
        for (const StructInfo::Field &field : info.fields) {
            align = std::max(align, min_align_of(field.type));
        }
        return std::max(align, info.align);
    }
    if (type.kind == TypeKind::Bool || type.kind == TypeKind::Char) {
        return 1;
    }
    const int bits = width_of(type);
    if (bits <= 8) {
        return 1;
    }
    if (bits <= 16) {
        return 2;
    }
    if (bits <= 32) {
        return 4;
    }
    if (bits <= 64) {
        return 8;
    }
    return 1;
}

std::optional<int> Checker::type_size(const Type &type) const {
    auto align_up = [](int offset, int align) {
        if (align <= 1) {
            return offset;
        }
        return (offset + align - 1) / align * align;
    };
    auto scalar = [](const Type &current) -> std::optional<int> {
        if (current.kind == TypeKind::Bool || current.kind == TypeKind::Char) {
            return 1;
        }
        if (current.kind == TypeKind::Float) {
            return current.rank >= 64 ? 8 : 4;
        }
        if (current.kind != TypeKind::Int) {
            return std::nullopt;
        }
        const int bits = width_of(current);
        if (bits <= 8) {
            return 1;
        }
        if (bits <= 16) {
            return 2;
        }
        if (bits <= 32) {
            return 4;
        }
        return 8;
    };
    std::unordered_set<int> seen;
    const auto walk = [&](const auto &self, const Type &current) -> std::optional<int> {
        if (current.kind == TypeKind::Pointer || current.kind == TypeKind::Meta) {
            return 8;
        }
        if (current.kind == TypeKind::Array) {
            if (current.element == nullptr) {
                return std::nullopt;
            }
            // `{ptr, i32}` with the pointer's alignment is 16 bytes.
            if (current.length < 0) {
                return 16;
            }
            if (current.length == 0) {
                return 0;
            }
            const std::optional<int> element = self(self, *current.element);
            if (!element || *element > 2147483647 / current.length) {
                return std::nullopt;
            }
            return *element * current.length;
        }
        if (current.kind == TypeKind::Enum) {
            if (current.enum_id < 0 || current.enum_id >= static_cast<int>(enum_types_.size())) {
                return std::nullopt;
            }
            return self(self, enum_types_[static_cast<std::size_t>(current.enum_id)].underlying);
        }
        if (current.kind != TypeKind::Struct) {
            return scalar(current);
        }
        if (current.struct_id < 0 || current.struct_id >= static_cast<int>(structs_.size())) {
            return std::nullopt;
        }
        if (!seen.insert(current.struct_id).second) {
            return std::nullopt;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(current.struct_id)];
        if (!info.complete) {
            return std::nullopt;
        }
        if (info.fields.empty()) {
            if (info.is_union) {
                // No arm. The value is the enum tag.
                if (info.tag_enum < 0 || info.tag_enum >= static_cast<int>(enum_types_.size())) {
                    return std::nullopt;
                }
                const Type &tag = enum_types_[static_cast<std::size_t>(info.tag_enum)].underlying;
                const std::optional<int> tag_size = self(self, tag);
                if (!tag_size) {
                    return std::nullopt;
                }
                return align_up(*tag_size, std::max(min_align_of(tag), std::max(info.align, 1)));
            }
            if (info.extern_c) {
                return 1;
            }
            return align_up(0, std::max(info.align, 1));
        }
        if (info.is_union && info.tag_enum < 0) {
            int size = 0;
            int align = 1;
            for (const StructInfo::Field &field : info.fields) {
                const std::optional<int> field_size = self(self, field.type);
                if (!field_size) {
                    return std::nullopt;
                }
                size = std::max(size, *field_size);
                align = std::max(align, min_align_of(field.type));
            }
            return align_up(size, std::max(align, info.align));
        }
        if (info.is_union) {
            if (info.tag_enum < 0 || info.tag_enum >= static_cast<int>(enum_types_.size())) {
                return std::nullopt;
            }
            const Type &tag = enum_types_[static_cast<std::size_t>(info.tag_enum)].underlying;
            const std::optional<int> tag_size = self(self, tag);
            if (!tag_size) {
                return std::nullopt;
            }
            int payload = 0;
            int payload_align = 1;
            for (const StructInfo::Field &field : info.fields) {
                const std::optional<int> field_size = self(self, field.type);
                if (!field_size) {
                    return std::nullopt;
                }
                payload = std::max(payload, *field_size);
                payload_align = std::max(payload_align, min_align_of(field.type));
            }
            payload = align_up(payload, payload_align);
            const int tag_align = std::max(min_align_of(tag), 1);
            const int align = std::max(tag_align, std::max(payload_align, info.align));
            // The payload starts at offset 0. The enum tag is placed after it.
            return align_up(align_up(payload, tag_align) + *tag_size, align);
        }
        int offset = 0;
        int align = 1;
        for (const StructInfo::Field &field : info.fields) {
            const std::optional<int> field_size = self(self, field.type);
            if (!field_size) {
                return std::nullopt;
            }
            const int field_align = std::max(min_align_of(field.type), 1);
            offset = align_up(offset, field_align);
            if (offset > 2147483647 - *field_size) {
                return std::nullopt;
            }
            offset += *field_size;
            align = std::max(align, field_align);
        }
        return align_up(offset, std::max(align, info.align));
    };
    return walk(walk, type);
}

void Checker::define_struct_fields() {
    // A slice header does not store the element. The element struct may still be incomplete,
    // including the struct being defined. A sized array of that struct still needs its size.
    auto slice_hides_leaf = [](const FieldDecl &decl) {
        if (!decl.shells.empty()) {
            if (decl.shells.front().kind == TypeShell::Kind::Pointer) {
                return false;
            }
            for (const TypeShell &shell : decl.shells) {
                if (shell.kind == TypeShell::Kind::Slice) {
                    return true;
                }
            }
            return false;
        }
        return decl.elem_slice || (decl.is_array && decl.array_len < 0);
    };
    // `kids *[2]Node` and `kids *[]Node` store a pointer. The element can still be incomplete.
    auto pointer_hides_leaf = [](const FieldDecl &decl) {
        for (const TypeShell &shell : decl.shells) {
            if (shell.kind == TypeShell::Kind::Pointer) {
                return true;
            }
        }
        return false;
    };
    // The struct stored by value, including through sized arrays. A pointer or a slice hides it.
    // `next Node` and `kids [2]Node` have no finite size. `kids [2]*Node` and `kids []Node` do.
    auto stored_by_value = [&](const FieldDecl &field) -> int {
        if (field.indirections > 0 || field.is_fn_ptr || !field.type_package.empty() || field.type_name.empty() ||
            field.is_enum || slice_hides_leaf(field)) {
            return -1;
        }
        for (const TypeShell &shell : field.shells) {
            if (shell.kind == TypeShell::Kind::Slice || shell.kind == TypeShell::Kind::Pointer) {
                return -1;
            }
        }
        const int id = find_tag(file_id_, field.type_name);
        if (id < 0 || id >= static_cast<int>(structs_.size())) {
            return -1;
        }
        return id;
    };
    // Struct ids from `from` to `target`, including both. Empty when `target` is not stored.
    // A failed branch is forgotten so a later field can walk the same struct.
    auto value_path = [&](auto &self, int from, int target, std::unordered_set<int> &seen) -> std::vector<int> {
        if (!seen.insert(from).second) {
            return {};
        }
        const PendingStruct *item = nullptr;
        for (const PendingStruct &pending : pending_structs_) {
            if (pending.id == from && pending.decl != nullptr) {
                item = &pending;
                break;
            }
        }
        if (item == nullptr) {
            seen.erase(from);
            return {};
        }
        const uint32_t saved = file_id_;
        file_id_ = item->file_id;
        std::vector<int> path;
        for (const FieldDecl &field : item->decl->fields) {
            const int leaf = stored_by_value(field);
            if (leaf < 0) {
                continue;
            }
            if (leaf == target) {
                path = {from, target};
                break;
            }
            std::vector<int> rest = self(self, leaf, target, seen);
            if (!rest.empty()) {
                rest.insert(rest.begin(), from);
                path = std::move(rest);
                break;
            }
        }
        file_id_ = saved;
        if (path.empty()) {
            seen.erase(from);
        }
        return path;
    };
    // Package qualifier for a struct. An imported package uses its directory or `package` name.
    // The entry file uses its `package` name, or the file name without `.tlc`.
    auto package_of = [&](int id) -> std::string {
        if (id < 0 || id >= static_cast<int>(structs_.size())) {
            return {};
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(id)];
        if (const PackageNode *node = find_node(info.file_id)) {
            return node->qualifier;
        }
        if (info.file_id == 0 && primary_unit_ != nullptr && primary_unit_->package &&
            !primary_unit_->package->name.empty()) {
            return primary_unit_->package->name;
        }
        const uint32_t source_id = info.file_id != 0 ? info.file_id : info.loc.file;
        const SourceFile *file = diags_.source(source_id);
        if (file == nullptr) {
            return {};
        }
        std::string name = file->path();
        const auto slash = name.find_last_of("/\\");
        if (slash != std::string::npos) {
            name.erase(0, slash + 1);
        }
        const auto dot = name.rfind('.');
        if (dot != std::string::npos) {
            name.resize(dot);
        }
        return name;
    };
    // `pkg.Node`. An anonymous struct or union has no name the program can write.
    auto cycle_name = [&](int id) -> std::string {
        if (id < 0 || id >= static_cast<int>(structs_.size())) {
            return "struct";
        }
        const std::string &name = structs_[static_cast<std::size_t>(id)].name;
        if (name.rfind(".struct.", 0) == 0) {
            return "struct";
        }
        if (name.rfind(".union.", 0) == 0 && name.find('.', std::string(".union.").size()) == std::string::npos) {
            return "union";
        }
        const std::string package = package_of(id);
        if (package.empty()) {
            return name;
        }
        return package + "." + name;
    };
    // `recursive struct pkg.A -> pkg.B -> pkg.C -> pkg.A`.
    auto recursive_message = [&](const std::vector<int> &ids) -> std::string {
        const bool as_union = !ids.empty() && ids.front() >= 0 && ids.front() < static_cast<int>(structs_.size()) &&
                              structs_[static_cast<std::size_t>(ids.front())].is_union;
        std::string message = as_union ? "recursive union " : "recursive struct ";
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (i != 0) {
                message += " -> ";
            }
            message += cycle_name(ids[i]);
        }
        return message;
    };
    auto type_fields = [&](PendingStruct &pending, bool complain_waiting) {
        file_id_ = pending.file_id;
        StructInfo &info = structs_[static_cast<std::size_t>(pending.id)];
        for (const FieldDecl &field : pending.decl->fields) {
            if (field.name.empty()) {
                continue;
            }
            for (const StructInfo::Field &existing : info.fields) {
                if (existing.name == field.name) {
                    error(field.loc, "redefinition");
                }
            }
            FieldDecl &written = const_cast<FieldDecl &>(field);
            prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum,
                            written.indirections, written.optional, written.is_array, written.array_len,
                            written.array_len_name, written.type_args, written.loc);
            fold_shells(written.shells, written.loc);
            // A by-value cycle has no finite size. Name it. This is not `type mismatch`.
            const int stored = stored_by_value(written);
            std::vector<int> cycle;
            if (stored == pending.id) {
                cycle = {pending.id, pending.id};
            } else if (stored >= 0 && !structs_[static_cast<std::size_t>(stored)].complete) {
                std::unordered_set<int> seen;
                cycle = value_path(value_path, stored, pending.id, seen);
                if (!cycle.empty()) {
                    cycle.insert(cycle.begin(), pending.id);
                }
            }
            if (!cycle.empty()) {
                error(field.loc, recursive_message(cycle));
                info.fields.push_back(StructInfo::Field{field.name, type_invalid(), field.loc, field.exported});
                continue;
            }
            if (complain_waiting && field_waits(field)) {
                error(field.loc, "not supported yet");
                info.fields.push_back(StructInfo::Field{field.name, type_invalid(), field.loc, field.exported});
                continue;
            }
            Type field_type =
                field.is_fn_ptr ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                 field.ret_indirections, field.indirections, field.fn_params,
                                                 field.fn_variadic, field.fn_results, field.loc, field.ret_optional,
                                                 field.ret_const_pointee)
                                : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                              false, field.type_package, field.is_struct, field.is_enum, false,
                                              field.optional, field.const_pointee,
                                              slice_hides_leaf(written) || pointer_hides_leaf(written),
                                              field.elem_const, field.elem_slice, written.shells, true);
            // A slice is a tlc value, `{ptr, i32}`. An `extern(abi="c")` struct keeps the C layout.
            if (is_slice(field_type) && info.extern_c) {
                error(field.loc, "not supported yet");
                info.fields.push_back(StructInfo::Field{field.name, type_invalid(), field.loc, field.exported});
                continue;
            }
            if (info.extern_c) {
                // A tagged union is not a C layout, and it cannot be marked `extern(abi="c")`.
                if (mentions_tagged_union(field_type)) {
                    error(field.loc, "extern(abi=\"c\") on a tagged union");
                } else {
                    require_extern_struct(field_type, field.loc);
                }
            }
            info.fields.push_back(StructInfo::Field{field.name, std::move(field_type), field.loc, field.exported});
            if (field.init != nullptr) {
                // A union arm is not a field value. The inline payload is an ordinary struct.
                if (info.is_union) {
                    error(field.loc, "not supported yet");
                } else {
                    info.fields.back().has_init = true;
                    info.fields.back().init = field.init.get();
                }
            }
        }
        if (pending.decl->align > 0) {
            int natural = 1;
            for (const StructInfo::Field &field : info.fields) {
                natural = std::max(natural, min_align_of(field.type));
            }
            if (pending.decl->align < natural) {
                error(pending.decl->loc, "type mismatch");
            }
        }
        info.complete = true;
    };
    bool progress = true;
    while (progress) {
        progress = false;
        for (PendingStruct &pending : pending_structs_) {
            if (pending.id < 0 || pending.decl == nullptr || structs_[static_cast<std::size_t>(pending.id)].complete) {
                continue;
            }
            file_id_ = pending.file_id;
            bool waits = false;
            for (const FieldDecl &field : pending.decl->fields) {
                if (!field_waits(field)) {
                    continue;
                }
                // `next Node` is rejected below. It does not keep this struct incomplete.
                if (find_tag(file_id_, field.type_name) == pending.id) {
                    continue;
                }
                waits = true;
                break;
            }
            if (waits) {
                continue;
            }
            type_fields(pending, false);
            progress = true;
        }
    }
    for (PendingStruct &pending : pending_structs_) {
        if (pending.id < 0 || pending.decl == nullptr || structs_[static_cast<std::size_t>(pending.id)].complete) {
            continue;
        }
        type_fields(pending, true);
    }
    pending_structs_.clear();
}

void Checker::register_structs(const TranslationUnit &unit, uint32_t file_id) {
    declare_struct_tags(unit, file_id);
    fold_early_consts(const_cast<TranslationUnit &>(unit), file_id);
    // Field types are filled in after aliases, so `n Integer` can name `type Integer i32`.
}

void Checker::register_enums(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    auto unsigned_rep = [](const Type &type) {
        return type.is_unsigned || (type.kind == TypeKind::Char && (type.plain_char || type.is_unsigned));
    };
    auto fits = [&](const Type &type, int64_t value) {
        if (type.kind == TypeKind::Bool) {
            return value == 0 || value == 1;
        }
        if (type.kind != TypeKind::Int && type.kind != TypeKind::Char) {
            return false;
        }
        const int bits = width_of(type);
        if (bits <= 0) {
            return false;
        }
        if (unsigned_rep(type)) {
            if (value < 0) {
                return false;
            }
            if (bits >= 63) {
                return true;
            }
            return static_cast<uint64_t>(value) <= ((1ULL << bits) - 1ULL);
        }
        if (bits >= 63) {
            return true;
        }
        const int64_t max = (1LL << (bits - 1)) - 1;
        const int64_t min = -(1LL << (bits - 1));
        return value >= min && value <= max;
    };
    auto at_max = [&](const Type &type, int64_t value) {
        if (type.kind == TypeKind::Bool) {
            return value >= 1;
        }
        const int bits = width_of(type);
        if (unsigned_rep(type)) {
            if (bits >= 63) {
                return false;
            }
            return value >= static_cast<int64_t>((1ULL << bits) - 1ULL);
        }
        if (bits >= 63) {
            return value == INT64_MAX;
        }
        return value >= ((1LL << (bits - 1)) - 1);
    };
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<EnumDecl>(&decl.data);
        if (item == nullptr) {
            continue;
        }
        if (item->name.empty()) {
            error(item->loc, "not supported yet");
            continue;
        }
        Type existing;
        if (package_has_enum(file_id, item->name) || find_tag(file_id, item->name) >= 0 ||
            package_trait(file_id, item->name) >= 0 || package_typedef(file_id, item->name, existing)) {
            error(item->loc, "redefinition");
            continue;
        }
        if (item->extern_c && !item->underlying.empty()) {
            error(item->loc, "extern enum");
        }
        Type underlying = type_int();
        if (!item->extern_c && !item->underlying.empty()) {
            Type parsed;
            if (!fill_builtin(item->underlying, parsed) ||
                (parsed.kind != TypeKind::Int && parsed.kind != TypeKind::Char && parsed.kind != TypeKind::Bool)) {
                error(item->loc, "type mismatch");
                continue;
            }
            underlying = parsed;
        }
        const int id = static_cast<int>(enum_types_.size());
        EnumInfo info;
        info.name = item->name;
        info.file_id = file_id;
        info.underlying = underlying;
        info.is_extern = item->extern_c;
        int64_t next = 0;
        bool next_ok = true;
        for (const Enumerator &enumerator : item->enumerators) {
            int64_t value = 0;
            if (enumerator.has_value) {
                value = enumerator.value;
                if (!fits(underlying, value)) {
                    error(enumerator.loc, "type mismatch");
                }
            } else if (!next_ok) {
                error(enumerator.loc, "not supported yet");
            } else {
                value = next;
            }
            if (!enumerator.name.empty()) {
                bool duplicate = false;
                for (const EnumInfo::Member &member : info.members) {
                    if (member.name == enumerator.name) {
                        duplicate = true;
                    }
                }
                if (duplicate) {
                    error(enumerator.loc, "redefinition");
                } else {
                    info.members.push_back(EnumInfo::Member{enumerator.name, value, enumerator.loc});
                }
            }
            if (at_max(underlying, value)) {
                next_ok = false;
            } else {
                next = value + 1;
                next_ok = true;
            }
        }
        enum_types_.push_back(std::move(info));
        enum_ids_[file_id][item->name] = id;
        if (!item->exported) {
            continue;
        }
        if (exports_[file_id].contains(item->name) || exported_elsewhere(item->name)) {
            error(item->loc, "redefinition");
            continue;
        }
        Symbol symbol;
        symbol.name = item->name;
        symbol.is_type = true;
        symbol.is_enum = true;
        symbol.type = type_enum(id);
        exports_[file_id].emplace(item->name, std::move(symbol));
    }
}

int Checker::imported_enum(uint32_t file, const std::string &package, const std::string &name, SourceLoc loc) {
    const PackageNode *node = find_node(file);
    if (node == nullptr) {
        error(loc, "undeclared identifier");
        return -1;
    }
    const PackageNode *target = nullptr;
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form == ImportDecl::Form::Selective) {
            continue;
        }
        const PackageNode *candidate = target_of(*node, import);
        if (candidate == nullptr) {
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : candidate->qualifier;
        if (binding == package) {
            target = candidate;
            break;
        }
    }
    if (target == nullptr) {
        error(loc, "undeclared identifier");
        return -1;
    }
    const Symbol *member = find_export(target->file_id, name);
    if (member == nullptr) {
        error(loc, name + " is not exported");
        return -1;
    }
    // `export type Alias Kind` is an enum. A struct or another export is not a tag.
    if (!member->is_type || member->type.kind != TypeKind::Enum || member->type.enum_id < 0) {
        error(loc, "type mismatch");
        return -1;
    }
    return member->type.enum_id;
}

void Checker::check_unions() {
    for (StructInfo &info : structs_) {
        if (info.tag_name.empty()) {
            continue;
        }
        file_id_ = info.file_id;
        int enum_id = -1;
        if (!info.tag_package.empty()) {
            // Package names are not bound yet. The import edge names the package.
            enum_id = imported_enum(info.file_id, info.tag_package, info.tag_name, info.loc);
            if (enum_id < 0) {
                continue;
            }
        } else {
            enum_id = find_enum(info.file_id, info.tag_name);
        }
        if (info.tag_package.empty() && enum_id < 0) {
            Type alias;
            if (package_typedef(info.file_id, info.tag_name, alias)) {
                if (alias.kind == TypeKind::Enum) {
                    enum_id = alias.enum_id;
                } else {
                    error(info.loc, "type mismatch");
                    continue;
                }
            } else if (find_tag(info.file_id, info.tag_name) >= 0 || package_trait(info.file_id, info.tag_name) >= 0) {
                error(info.loc, "type mismatch");
                continue;
            } else {
                error(info.loc, "undeclared identifier");
                continue;
            }
        }
        if (enum_id < 0 || enum_id >= static_cast<int>(enum_types_.size())) {
            error(info.loc, "type mismatch");
            continue;
        }
        info.tag_enum = enum_id;
        const EnumInfo &enumeration = enum_types_[static_cast<std::size_t>(enum_id)];
        for (const StructInfo::Field &field : info.fields) {
            if (find_enum_member(enum_id, field.name) == nullptr) {
                error(field.loc, "undeclared identifier");
            }
            // An invalid arm was already diagnosed. A second type mismatch adds nothing.
            if (field.type.kind != TypeKind::Invalid &&
                (field.type.kind != TypeKind::Struct || field.type.struct_id < 0 ||
                 field.type.struct_id >= static_cast<int>(structs_.size()) ||
                 structs_[static_cast<std::size_t>(field.type.struct_id)].is_union)) {
                error(field.loc, "type mismatch");
            }
        }
        for (const EnumInfo::Member &member : enumeration.members) {
            bool found = false;
            for (const StructInfo::Field &field : info.fields) {
                if (field.name == member.name) {
                    found = true;
                }
            }
            if (!found) {
                error(info.loc, "type mismatch");
            }
        }
    }
}

bool Checker::typedef_target_ready(const TypedefDecl &item) const {
    if (!item.type_package.empty() || item.is_struct || item.is_enum) {
        return true;
    }
    const std::string &name = item.type_name;
    if (name.empty() || name == "invalid") {
        return true;
    }
    Type builtin;
    if (fill_builtin(name, builtin)) {
        return true;
    }
    Type alias;
    if (package_typedef(file_id_, name, alias) || find_tag(file_id_, name) >= 0 || package_has_enum(file_id_, name) ||
        package_trait(file_id_, name) >= 0) {
        return true;
    }
    return false;
}

void Checker::register_one_typedef(const TypedefDecl &item) {
    TypedefDecl &written = const_cast<TypedefDecl &>(item);
    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                    written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args,
                    written.loc);
    if (item.name.empty()) {
        return;
    }
    Type existing;
    if (package_has_enum(file_id_, item.name) || package_typedef(file_id_, item.name, existing) ||
        find_tag(file_id_, item.name) >= 0 || package_trait(file_id_, item.name) >= 0) {
        error(item.loc, "redefinition");
        return;
    }
    if (item.type_package.empty() && item.indirections == 0 && !item.is_array && item.name == item.type_name &&
        (item.is_struct || find_tag(file_id_, item.name) >= 0)) {
        error(item.loc, "redefinition");
        return;
    }
    const Type type = item.is_fn_ptr
                          ? type_of_fn_ptr(item.type_name, item.type_package, item.is_struct, item.is_enum,
                                           item.ret_indirections, item.indirections, item.fn_params, item.fn_variadic,
                                           item.fn_results, item.loc, item.ret_optional)
                          : object_type(item.type_name, item.indirections, item.is_array, item.array_len, item.loc, false,
                                        item.type_package, item.is_struct, item.is_enum, false, item.optional,
                                        item.const_pointee, true, item.elem_const, item.elem_slice);
    if (type.kind == TypeKind::Invalid) {
        return;
    }
    if (type.kind == TypeKind::Trait) {
        error(item.loc, "not supported yet");
        return;
    }
    // The name is a different type from its representation. A cast confirms either side.
    Type distinct = type;
    distinct.alias_id = next_alias_id_++;
    alias_names_.push_back(item.name);
    alias_file_.push_back(file_id_);
    typedefs_[file_id_].emplace(item.name, distinct);
    if (!item.exported) {
        return;
    }
    auto &table = exports_[file_id_];
    const auto found = table.find(item.name);
    if (found == table.end()) {
        if (exported_elsewhere(item.name)) {
            error(item.loc, "redefinition");
            return;
        }
        Symbol symbol;
        symbol.name = item.name;
        symbol.is_type = true;
        symbol.is_typedef = true;
        symbol.type = distinct;
        table.emplace(item.name, std::move(symbol));
        return;
    }
    if (found->second.is_type && same_type(found->second.type, distinct)) {
        found->second.is_typedef = true;
        return;
    }
    error(item.loc, "redefinition");
}

void Checker::register_typedefs(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<TypedefDecl>(&decl.data);
        if (item == nullptr) {
            continue;
        }
        register_one_typedef(*item);
    }
}

bool Checker::name_taken(const std::string &name) const {
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return true;
    }
    return package_has_enum(file_id_, name);
}

Type Checker::published_object(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                               bool parameter, const std::string &package, bool is_struct, bool is_enum,
                               uint32_t optional, uint32_t const_pointee, bool elem_const, bool elem_slice,
                               const std::vector<TypeShell> &shells) {
    Type alias;
    if (is_struct || is_enum || !package.empty() || package_typedef(file_id_, name, alias) || find_tag(file_id_, name) >= 0 ||
        !shells.empty()) {
        return object_type(name, indirections, is_array, array_len, loc, parameter, package, is_struct, is_enum, false,
                           optional, const_pointee, false, elem_const, elem_slice, shells);
    }
    return quiet_object(name, indirections, is_array, array_len, parameter, optional, const_pointee, elem_slice);
}

Type Checker::published_result(const std::string &name, int indirections, SourceLoc loc, const std::string &package,
                               bool is_struct, bool is_enum, uint32_t optional, uint32_t const_pointee) {
    if (is_struct || is_enum || !package.empty()) {
        return object_type(name, indirections, false, 0, loc, false, package, is_struct, is_enum, false, optional,
                           const_pointee);
    }
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return with_indirections(std::move(alias), indirections, loc, optional, const_pointee);
    }
    if (find_tag(file_id_, name) >= 0) {
        return object_type(name, indirections, false, 0, loc, false, package, false, false, false, optional,
                           const_pointee);
    }
    return quiet_indirect(quiet_type(name), indirections, optional, const_pointee);
}

void Checker::note_struct_use(bool is_struct, const std::string &name, SourceLoc loc) {
    if (!is_struct || name.empty()) {
        return;
    }
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return;
    }
    error(loc, "struct outside definition");
}

Type Checker::spelled_type(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                           int indirections, SourceLoc loc, uint32_t optional, uint32_t const_pointee) {
    note_struct_use(is_struct, name, loc);
    if (!package.empty()) {
        return with_indirections(lookup_qualified_type(package, name, loc), indirections, loc, optional, const_pointee);
    }
    if (is_struct) {
        return with_indirections(lookup_tag(name, loc, indirections > 0), indirections, loc, optional, const_pointee);
    }
    if (is_enum) {
        if (name.empty()) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        const int enum_id = find_enum(file_id_, name);
        if (enum_id < 0) {
            error(loc, "undeclared identifier");
            return type_invalid();
        }
        return with_indirections(type_enum(enum_id), indirections, loc, optional, const_pointee);
    }
    return with_indirections(type_of_name(name, loc, indirections > 0), indirections, loc, optional, const_pointee);
}

void Checker::fold_shells(std::vector<TypeShell> &shells, SourceLoc loc) {
    for (TypeShell &shell : shells) {
        if (shell.kind != TypeShell::Kind::Array || shell.length_name.empty()) {
            continue;
        }
        const std::optional<int> value = lookup_const_value(shell.length_name);
        if (value && *value > 0) {
            shell.length = *value;
            shell.length_name.clear();
        } else if (value && *value <= 0) {
            error(loc, "not supported yet");
        }
    }
}

Type Checker::object_type(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                          bool parameter, const std::string &package, bool is_struct, bool is_enum, bool receiver,
                          uint32_t optional, uint32_t const_pointee, bool allow_incomplete, bool elem_const,
                          bool elem_slice, const std::vector<TypeShell> &shells, bool variable) {
    if (!receiver) {
        note_struct_use(is_struct, name, loc);
    }
    Type type;
    if (!package.empty()) {
        type = with_indirections(lookup_qualified_type(package, name, loc), indirections, loc, optional, const_pointee);
    } else if (is_struct) {
        const int tag = find_tag(file_id_, name);
        const int enumeration = find_enum(file_id_, name);
        Type alias;
        const bool have_alias = tag < 0 && package_typedef(file_id_, name, alias);
        if (tag < 0 && enumeration >= 0) {
            type = with_indirections(type_enum(enumeration), indirections, loc, optional, const_pointee);
        } else if (receiver && have_alias && alias.alias_id != 0 && (is_numeric(alias) || is_slice(alias))) {
            // `func Integer::twice(self this)` and `func string_view::equals(self this)` keep the alias.
            // The parser marks every receiver as a struct. `self *this` is a pointer to that alias.
            type = with_indirections(std::move(alias), indirections, loc, optional, const_pointee);
        } else if (receiver && have_alias) {
            // A pointer alias, a sized-array alias, or a struct alias is not a method owner.
            type = type_invalid();
        } else {
            type = with_indirections(lookup_tag(name, loc, indirections > 0 || allow_incomplete), indirections, loc,
                                     optional, const_pointee);
        }
    } else if (is_enum) {
        if (name.empty()) {
            error(loc, "not supported yet");
            type = type_invalid();
        } else if (const int enum_id = find_enum(file_id_, name); enum_id >= 0) {
            type = with_indirections(type_enum(enum_id), indirections, loc, optional, const_pointee);
        } else {
            error(loc, "undeclared identifier");
            type = type_invalid();
        }
    } else {
        type = with_indirections(type_of_name(name, loc, indirections > 0 || allow_incomplete), indirections, loc,
                                 optional, const_pointee);
    }
    // A trait has no size. An array or a slice of one has no element to store, whether
    // it is a variable or a parameter. `Shape` and `*Shape` stay legal as parameters.
    bool trait_aggregate = is_array || elem_slice;
    for (const TypeShell &shell : shells) {
        if (shell.kind == TypeShell::Kind::Array || shell.kind == TypeShell::Kind::Slice) {
            trait_aggregate = true;
        }
    }
    if (mentions_trait(type) && trait_aggregate) {
        error(loc, "trait has no size");
        return type_invalid();
    }
    if (mentions_trait(type) && !parameter) {
        const bool stored = shells.empty();
        error(loc, variable && stored ? "trait has no size" : "not supported yet");
        return type_invalid();
    }
    // Shells are the type. `[]T`, `[][][]T`, `*[]T`, and `[N][M]T` are the same walk.
    // A length that is still a name is a generic parameter. The flat path folds that name.
    bool shells_ready = !shells.empty();
    for (const TypeShell &shell : shells) {
        if (shell.kind == TypeShell::Kind::Array && shell.length <= 0) {
            shells_ready = false;
        }
    }
    if (shells_ready) {
        if (elem_const) {
            type.is_const = true;
        }
        bool rejected = false;
        type = apply_shells(std::move(type), shells, parameter, rejected);
        if (rejected) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        if (!parameter && type.kind == TypeKind::Void) {
            error(loc, "type mismatch");
            return type_invalid();
        }
        return type;
    }
    // One empty `[]` makes the named type a slice. `[][][]T` and `[][]Row` stay rejected.
    if (elem_slice) {
        if (type.kind == TypeKind::Invalid) {
            return type;
        }
        if (type.kind == TypeKind::Array || type.kind == TypeKind::Function || type.kind == TypeKind::Void ||
            type.kind == TypeKind::Tuple || type.kind == TypeKind::Meta) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        if (elem_const) {
            type.is_const = true;
        }
        type = type_array(std::move(type), -1);
    }
    if (!is_array) {
        if (type.kind == TypeKind::Void) {
            error(loc, "type mismatch");
            return type_invalid();
        }
        if (parameter && type.kind == TypeKind::Array && type.element) {
            // `type Row []i32` stays a slice. A sized array alias still decays to a pointer.
            if (type.length < 0) {
                return type;
            }
            Type pointer = type_pointer(*type.element);
            if (type.length > 0) {
                pointer.decayed_len = type.length;
            }
            return pointer;
        }
        return type;
    }
    if (type.kind == TypeKind::Invalid) {
        return type;
    }
    if (type.kind == TypeKind::Void || type.kind == TypeKind::Function) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    // `[]const u8` marks the leaf. The inner slice of `[][]const u8` already did.
    if (elem_const && !elem_slice) {
        type.is_const = true;
    }
    // `type Row []i32` used as `[]Row` or `[N]Row` is the same type as `[][]i32` or `[N][]i32`.
    if (is_slice(type)) {
        type.alias_id = 0;
    }
    // `[]T` is a slice. It does not decay, even as a parameter. One more slice is `[][]T`.
    if (array_len < 0) {
        if (nested_slice(type)) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        return type_array(std::move(type), -1);
    }
    // `[N][][]T`, including through an alias that is already two slices, stays rejected.
    if (nested_slice(type)) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    if (parameter) {
        Type pointer = type_pointer(std::move(type));
        if (array_len > 0) {
            pointer.decayed_len = array_len;
        }
        return pointer;
    }
    if (array_len <= 0) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    return type_array(std::move(type), array_len);
}

bool Checker::is_lvalue_form(const Expr &expr) const { return lvalue_form(expr); }

std::string Checker::type_desc(const Type &type) const {
    if (type.alias_id > 0 && static_cast<std::size_t>(type.alias_id) < alias_names_.size()) {
        return alias_names_[static_cast<std::size_t>(type.alias_id)];
    }
    switch (type.kind) {
    case TypeKind::Invalid:
        return "invalid";
    case TypeKind::Void:
        return "void";
    case TypeKind::Meta:
        return "Type";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Float:
        return type.rank >= 64 ? "f64" : "f32";
    case TypeKind::Nullptr:
        return "null";
    case TypeKind::Char:
        if (type.plain_char || type.is_unsigned) {
            return "u8";
        }
        return "i8";
    case TypeKind::Int: {
        const int rank = type.rank == 0 ? 3 : type.rank;
        if (rank <= 1) {
            return type.is_unsigned ? "u8" : "i8";
        }
        if (rank == 2) {
            return type.is_unsigned ? "u16" : "i16";
        }
        if (rank >= 4) {
            return type.is_unsigned ? "u64" : "i64";
        }
        return type.is_unsigned ? "u32" : "i32";
    }
    case TypeKind::Pointer: {
        std::string text = type.optional ? "?" : "";
        text += "*";
        if (type.pointee != nullptr && type.pointee->is_const) {
            text += "const ";
        }
        text += type_desc(type.pointee ? *type.pointee : type_invalid());
        return text;
    }
    case TypeKind::Array: {
        std::string element = type_desc(type.element ? *type.element : type_invalid());
        // `*const u8` already spells its const. `const u8` is written in front of the element.
        if (type.element != nullptr && type.element->is_const && type.element->kind != TypeKind::Pointer) {
            element = "const " + element;
        }
        if (type.length < 0) {
            return "[]" + element;
        }
        return "[" + std::to_string(type.length) + "]" + element;
    }
    case TypeKind::Struct:
        if (type.struct_id >= 0 && type.struct_id < static_cast<int>(structs_.size())) {
            const std::string &name = structs_[static_cast<std::size_t>(type.struct_id)].name;
            // `var s struct { ... }` has no name the program can write.
            if (name.rfind(".struct.", 0) == 0) {
                return "struct";
            }
            // `.union.1.A` is a payload. `.union.1` is the union itself.
            if (name.rfind(".union.", 0) == 0 && name.find('.', std::string(".union.").size()) == std::string::npos) {
                return "union";
            }
            return name;
        }
        return "struct";
    case TypeKind::Enum:
        if (type.enum_id >= 0 && type.enum_id < static_cast<int>(enum_types_.size())) {
            return enum_types_[static_cast<std::size_t>(type.enum_id)].name;
        }
        return "enum";
    case TypeKind::Trait:
        if (type.trait_id >= 0 && type.trait_id < static_cast<int>(traits_.size())) {
            return traits_[static_cast<std::size_t>(type.trait_id)].name;
        }
        return "trait";
    case TypeKind::Tuple: {
        std::string text = "(";
        for (std::size_t i = 0; i < type.results.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += type_desc(type.results[i]);
        }
        text += ")";
        return text;
    }
    case TypeKind::Function: {
        std::string text = "func(";
        for (std::size_t i = 0; i < type.params.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += type_desc(type.params[i]);
        }
        if (type.variadic) {
            text += type.params.empty() ? "..." : ", ...";
        }
        text += ") ";
        text += type.result ? type_desc(*type.result) : "void";
        return text;
    }
    }
    return "invalid";
}

} // namespace tlc
