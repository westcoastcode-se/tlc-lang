// Names, scopes, and finding a declaration.
#include "check/check.hpp"
#include "check/check_util.hpp"

namespace tlc {

void Checker::error(SourceLoc loc, const std::string &message) {
    if (quiet_) {
        return;
    }
    diags_.error(loc, message);
}

void Checker::push() {
    const int id = scopes_.empty() ? 0 : scopes_.back().function_id;
    scopes_.emplace_back();
    scopes_.back().function_id = id;
}

void Checker::push_function() {
    scopes_.emplace_back();
    scopes_.back().function_id = ++function_seq_;
}

void Checker::pop() {
    if (!scopes_.empty()) {
        for (const auto &[name, symbol] : scopes_.back().symbols) {
            (void)name;
            if (symbol.is_local_function) {
                if (symbol.lists.empty()) {
                    if (!symbol.has_body) {
                        error(symbol.loc, "not supported yet");
                    }
                } else {
                    for (const FnList &list : symbol.lists) {
                        if (!list.has_body) {
                            error(list.loc, "not supported yet");
                        }
                    }
                }
            }
        }
    }
    scopes_.pop_back();
}

bool Checker::captured(const Symbol &symbol) const {
    if (!symbol.is_auto || scopes_.empty()) {
        return false;
    }
    return symbol.function_id != scopes_.back().function_id;
}

bool Checker::fn_pointer(const Type &type) const {
    return type.kind == TypeKind::Pointer && type.pointee != nullptr && type.pointee->kind == TypeKind::Function;
}

void Checker::note_capture(FunctionDecl &fn, const std::string &name, int function_id) {
    if (name.empty()) {
        return;
    }
    for (const FnCapture &item : fn.captures) {
        if (item.name == name) {
            return;
        }
    }
    fn.captures.push_back(FnCapture{name, function_id});
}

void Checker::note_owned(FunctionDecl &fn, const std::string &name) {
    if (name.empty()) {
        return;
    }
    for (const std::string &item : fn.captured_here) {
        if (item == name) {
            return;
        }
    }
    fn.captured_here.push_back(name);
}

void Checker::adopt_captures(const FunctionDecl &fn) {
    if (checking_.size() < 2 || scopes_.empty()) {
        return;
    }
    FunctionDecl *parent = checking_[checking_.size() - 2];
    const int here = scopes_.back().function_id;
    int parent_id = 0;
    for (std::size_t index = scopes_.size(); index-- > 0;) {
        if (scopes_[index].function_id != here) {
            parent_id = scopes_[index].function_id;
            break;
        }
    }
    for (const FnCapture &item : fn.captures) {
        if (item.function_id == parent_id) {
            note_owned(*parent, item.name);
        } else {
            note_capture(*parent, item.name, item.function_id);
        }
    }
}

Type Checker::type_of_fn_param(const FnParam &param, SourceLoc loc, bool parameter) {
    if (param.is_tuple) {
        std::vector<Type> parts;
        parts.reserve(param.results.size());
        for (const FnParam &part : param.results) {
            // `[2]i32` inside `(...)` is one field of that value. It does not decay.
            Type one = type_of_fn_param(part, loc, false);
            if (one.kind == TypeKind::Void) {
                error(loc, "type mismatch");
            }
            parts.push_back(std::move(one));
        }
        return type_tuple(std::move(parts));
    }
    FnParam &written = const_cast<FnParam &>(param);
    bool is_array = false;
    int array_len = 0;
    std::string array_len_name;
    // A function pointer stores the result in `type_name`. `Box[i32]` is `type_args` on that result.
    if (written.is_fn_ptr) {
        prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum,
                        written.ret_indirections, written.ret_optional, is_array, array_len, array_len_name,
                        written.type_args, loc);
        return type_of_fn_ptr(param.type_name, param.type_package, param.is_struct, param.is_enum, param.ret_indirections,
                              param.indirections, param.params, param.variadic, param.results, loc, param.ret_optional,
                              param.ret_const_pointee);
    }
    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                    written.optional, written.is_array, written.array_len, array_len_name, written.type_args, loc);
    fold_shells(written.shells, loc);
    return object_type(param.type_name, param.indirections, param.is_array, param.array_len, loc, parameter,
                       param.type_package, param.is_struct, param.is_enum, false, param.optional, param.const_pointee,
                       false, param.elem_const, param.elem_slice, written.shells);
}

Type Checker::type_of_fn_ptr(const std::string &ret_name, const std::string &ret_package, bool ret_struct, bool ret_enum,
                             int ret_indirections, int fn_depth, const std::vector<FnParam> &params, bool variadic,
                             const std::vector<FnParam> &results, SourceLoc loc, uint32_t ret_optional,
                             uint32_t ret_const) {
    Type result;
    if (results.size() == 1 && results.front().is_tuple) {
        // `func () ((i32, i32))`. One tuple result, not two results.
        result = type_of_fn_param(results.front(), loc);
    } else if (results.size() > 1) {
        std::vector<Type> parts;
        parts.reserve(results.size());
        for (const FnParam &part : results) {
            if (part.is_tuple) {
                // `func () ((i32, i32), i32)`. That result is the tuple, not two more results.
                Type one = type_of_fn_param(part, loc);
                if (one.kind == TypeKind::Void) {
                    error(loc, "type mismatch");
                }
                parts.push_back(std::move(one));
                continue;
            }
            FnParam &written = const_cast<FnParam &>(part);
            std::string array_len_name;
            prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum,
                            written.indirections, written.optional, written.is_array, written.array_len, array_len_name,
                            written.type_args, loc);
            fold_shells(written.shells, loc);
            Type one = part.is_array || !written.shells.empty()
                           ? object_type(part.type_name, part.indirections, true, part.array_len, loc, false,
                                         part.type_package, part.is_struct, part.is_enum, false, part.optional,
                                         part.const_pointee, false, part.elem_const, part.elem_slice, written.shells)
                           : (part.is_struct || part.is_enum || !part.type_package.empty()
                                  ? object_type(part.type_name, part.indirections, false, 0, loc, false, part.type_package,
                                                part.is_struct, part.is_enum, false, part.optional, part.const_pointee)
                                  : with_indirections(type_of_name(part.type_name, loc, part.indirections > 0),
                                                      part.indirections, loc, part.optional, part.const_pointee));
            if (one.kind == TypeKind::Void) {
                error(loc, "type mismatch");
            }
            parts.push_back(std::move(one));
        }
        result = type_tuple(std::move(parts));
    } else if (results.size() == 1 && results.front().is_fn_ptr) {
        // `func (i32) func (i32) i32`. The result is that pointer, not the flattened name.
        result = type_of_fn_param(results.front(), loc);
    } else {
        // Same result spelling as a function result, so `void` and a bare struct tag stay valid.
        result = ret_struct || ret_enum || !ret_package.empty()
                     ? object_type(ret_name, ret_indirections, false, 0, loc, false, ret_package, ret_struct, ret_enum, false,
                                   ret_optional, ret_const)
                     : with_indirections(type_of_name(ret_name, loc, ret_indirections > 0), ret_indirections, loc,
                                         ret_optional, ret_const);
    }
    std::vector<Type> args;
    args.reserve(params.size());
    for (const FnParam &param : params) {
        args.push_back(type_of_fn_param(param, loc));
    }
    Type pointer = type_function(std::move(result), std::move(args), variadic);
    const int depth = fn_depth < 1 ? 1 : fn_depth;
    for (int i = 0; i < depth; ++i) {
        pointer = type_pointer(std::move(pointer));
    }
    return pointer;
}

Type Checker::type_of_written_tuple(const std::vector<ResultSlot> &parts, int indirections, bool is_array,
                                    const std::vector<TypeShell> &shells, SourceLoc loc) {
    // A shell or a star around `(i32, i32)` is not this form. `[2]Duo` wraps the name.
    if (indirections != 0 || is_array || !shells.empty()) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    std::vector<Type> typed;
    typed.reserve(parts.size());
    for (const ResultSlot &slot : parts) {
        typed.push_back(type_of_result(slot, loc));
    }
    return type_tuple(std::move(typed));
}

Type Checker::type_of_var(const VarDecl &var) {
    Type type = type_invalid();
    if (var.is_tuple) {
        type = type_of_written_tuple(var.tuple_parts, var.indirections, var.is_array, var.shells, var.loc);
    } else {
        VarDecl &written = const_cast<VarDecl &>(var);
        prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                        written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args,
                        written.loc);
        fold_shells(written.shells, written.loc);
        type = var.is_fn_ptr
                   ? type_of_fn_ptr(var.type_name, var.type_package, var.is_struct, var.is_enum, var.ret_indirections,
                                    var.indirections, var.fn_params, var.fn_variadic, var.fn_results, var.loc,
                                    var.ret_optional, var.ret_const_pointee)
                   : object_type(var.type_name, var.indirections, var.is_array, var.array_len, var.loc, false,
                                 var.type_package, var.is_struct, var.is_enum, false, var.optional, var.const_pointee,
                                 false, var.elem_const, var.elem_slice, written.shells, true);
        // A pointer shell is already applied. The flag is the flat spelling of `*[N]T`.
        if (written.shells.empty() && var.ptr_to_array && type.kind != TypeKind::Invalid) {
            if (type.kind != TypeKind::Array) {
                error(var.loc, "type mismatch");
                type = type_invalid();
            } else {
                if ((var.const_pointee & 1u) != 0) {
                    type.is_const = true;
                }
                type = type_pointer(std::move(type));
            }
        }
    }
    // `var name const Type`. The value is constant. A pointer to constant data is `*const T`.
    if (var.is_const && type.kind != TypeKind::Invalid) {
        type.is_const = true;
    }
    return type;
}

bool Checker::indexed_type(const Type &base, Type &out, bool &pointee_void) const {
    pointee_void = false;
    if (base.kind != TypeKind::Pointer || base.pointee == nullptr) {
        return false;
    }
    const Type &pointee = *base.pointee;
    if (pointee.kind == TypeKind::Void) {
        pointee_void = true;
        return false;
    }
    // `arr[1]` on a pointer to an array is `(*arr)[1]`.
    // A decayed `[N][]T` points at slice headers. The step is one header, not one `T`.
    if (pointee.kind == TypeKind::Array) {
        if (pointee.length < 0) {
            out = pointee;
            return true;
        }
        if (pointee.element == nullptr) {
            return false;
        }
        if (pointee.element->kind == TypeKind::Void) {
            pointee_void = true;
            return false;
        }
        out = *pointee.element;
        // `*const [N]T`: the array is const, so the element is const.
        if (pointee.is_const) {
            out.is_const = true;
        }
        return true;
    }
    out = pointee;
    return true;
}

Type Checker::type_of_param(const Param &param) {
    Type type = type_invalid();
    if (param.is_tuple) {
        std::vector<Type> parts;
        parts.reserve(param.tuple_parts.size());
        for (const ResultSlot &slot : param.tuple_parts) {
            parts.push_back(type_of_result(slot, param.loc));
        }
        type = type_tuple(std::move(parts));
    } else {
        Param &written = const_cast<Param &>(param);
        prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                        written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args,
                        written.loc);
        fold_shells(written.shells, written.loc);
        if (param.is_fn_ptr) {
            type = type_of_fn_ptr(param.type_name, param.type_package, param.is_struct, param.is_enum,
                                  param.ret_indirections, param.indirections, param.fn_params, param.fn_variadic,
                                  param.fn_results, param.loc, param.ret_optional, param.ret_const_pointee);
        } else {
            type = object_type(param.type_name, param.indirections, param.is_array, param.array_len, param.loc, true,
                               param.type_package, param.is_struct, param.is_enum, param.is_receiver, param.optional,
                               param.const_pointee, false, param.elem_const, param.elem_slice, written.shells);
        }
    }
    // `name const Type`. The parameter has storage and cannot be assigned.
    if (param.is_const && type.kind != TypeKind::Invalid) {
        type.is_const = true;
    }
    return type;
}

Type Checker::type_of_result(const ResultSlot &slot, SourceLoc loc, bool allow_incomplete) {
    if (slot.is_tuple) {
        Type type = type_of_written_tuple(slot.tuple_parts, slot.indirections, slot.is_array, slot.shells, loc);
        if (type.kind == TypeKind::Tuple) {
            for (const Type &part : type.results) {
                if (part.kind == TypeKind::Void) {
                    error(loc, "type mismatch");
                    break;
                }
            }
        }
        return type;
    }
    ResultSlot &written = const_cast<ResultSlot &>(slot);
    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                    written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args, loc);
    fold_shells(written.shells, loc);
    if (slot.is_fn_ptr) {
        return type_of_fn_ptr(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.ret_indirections,
                              slot.indirections, slot.fn_params, slot.fn_variadic, slot.fn_results, loc,
                              slot.ret_optional, slot.ret_const_pointee);
    }
    if (slot.is_array || !written.shells.empty()) {
        return object_type(slot.type_name, slot.indirections, true, slot.array_len, loc, false, slot.type_package,
                           slot.is_struct, slot.is_enum, false, slot.optional, slot.const_pointee, allow_incomplete,
                           slot.elem_const, slot.elem_slice, written.shells);
    }
    return slot.is_struct || slot.is_enum || !slot.type_package.empty()
               ? object_type(slot.type_name, slot.indirections, false, 0, loc, false, slot.type_package, slot.is_struct,
                             slot.is_enum, false, slot.optional, slot.const_pointee, allow_incomplete)
               : with_indirections(type_of_name(slot.type_name, loc, slot.indirections > 0 || allow_incomplete),
                                   slot.indirections, loc, slot.optional, slot.const_pointee);
}

Type Checker::function_result(const FunctionDecl &fn) {
    if (fn.infer_result && fn.results.empty() && fn.body != nullptr && fn.type_params.empty() && allow_lazy_infer_) {
        infer_omitted_result(const_cast<FunctionDecl &>(fn));
    }
    // The body has not been read yet. Callers see no result until `infer_file_results`.
    if (fn.infer_result && fn.results.empty()) {
        return type_invalid();
    }
    if (fn.results.size() > 1) {
        std::vector<Type> parts;
        parts.reserve(fn.results.size());
        for (const ResultSlot &slot : fn.results) {
            Type part = type_of_result(slot, fn.loc);
            if (part.kind == TypeKind::Void) {
                error(fn.loc, "type mismatch");
            }
            parts.push_back(std::move(part));
        }
        return type_tuple(std::move(parts));
    }
    if (fn.results.size() == 1) {
        return type_of_result(fn.results.front(), fn.loc);
    }
    if (fn.is_struct || fn.is_enum || !fn.type_package.empty()) {
        return object_type(fn.return_type, fn.indirections, false, 0, fn.loc, false, fn.type_package, fn.is_struct,
                           fn.is_enum, false, fn.optional, fn.const_pointee);
    }
    return with_indirections(type_of_name(fn.return_type, fn.loc, fn.indirections > 0), fn.indirections, fn.loc,
                             fn.optional, fn.const_pointee);
}

Type Checker::function_signature(const FunctionDecl &fn) {
    std::vector<Type> params;
    params.reserve(fn.params.size());
    for (const Param &param : fn.params) {
        params.push_back(type_of_param(param));
    }
    return type_function(function_result(fn), std::move(params), fn.variadic);
}

void Checker::check_local_function(const FunctionDecl &fn) {
    if (fn.name.empty() || scopes_.empty()) {
        return;
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
    if (generic) {
        error(fn.loc, "not supported yet");
    }
    if (!fn.method_struct.empty()) {
        return;
    }
    Type signature = function_signature(fn);
    if (signature.result != nullptr && mentions_trait(*signature.result)) {
        error(fn.loc, "trait has no size");
    }
    auto &table = scopes_.back().symbols;
    const auto found = table.find(fn.name);
    if (found != table.end()) {
        Symbol &existing = found->second;
        if (!existing.is_local_function) {
            error(fn.loc, "redefinition");
        } else {
            absorb_function(existing, fn, std::move(signature));
        }
    } else {
        Symbol symbol;
        symbol.name = fn.name;
        symbol.type = signature;
        symbol.is_function = true;
        symbol.is_local_function = true;
        symbol.unsafe_fn = fn.unsafe;
        symbol.has_body = fn.body != nullptr;
        symbol.loc = fn.loc;
        symbol.lists.push_back(function_list(fn, std::move(signature)));
        declare(std::move(symbol), fn.loc);
    }
    if (fn.body != nullptr) {
        check_function(fn);
        if (!fn.captures.empty()) {
            Symbol *symbol = lookup_scope(fn.name);
            if (symbol != nullptr && symbol->is_local_function) {
                symbol->fn_captures = true;
            }
        }
    }
}

Symbol *Checker::lookup_scope(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->symbols.find(name);
        if (found != it->symbols.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

Symbol *Checker::lookup(const std::string &name) {
    if (Symbol *local = lookup_scope(name)) {
        return local;
    }
    if (graph_ != nullptr) {
        const PackageNode *self = find_node(file_id_);
        if (self != nullptr && !self->package_key.empty()) {
            const auto package = package_syms_.find(self->package_key);
            if (package != package_syms_.end()) {
                const auto found = package->second.find(name);
                if (found != package->second.end()) {
                    return &found->second;
                }
            }
        }
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        const auto specs = spec_symbols_.find(id);
        if (specs == spec_symbols_.end()) {
            continue;
        }
        const auto spec = specs->second.find(name);
        if (spec == specs->second.end()) {
            continue;
        }
        if (id != file_id_ && static_spec_keys_.contains(std::to_string(id) + "\n" + name)) {
            continue;
        }
        return &spec->second;
    }
    return nullptr;
}

bool Checker::remember_shared(const Symbol &symbol, SourceLoc loc) {
    if (graph_ == nullptr || symbol.name.empty()) {
        return true;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr) {
        return true;
    }
    auto &table = package_syms_[self->package_key];
    const auto found = table.find(symbol.name);
    if (found != table.end()) {
        if (found->second.is_constant && symbol.is_constant && plain_int_constant(found->second.type) &&
            plain_int_constant(symbol.type) && found->second.const_value == symbol.const_value) {
            return true;
        }
        error(loc, "redefinition");
        return false;
    }
    for (const auto &[file, symbols] : statics_) {
        if (symbols.contains(symbol.name)) {
            (void)file;
            error(loc, "redefinition");
            return false;
        }
    }
    table.emplace(symbol.name, symbol);
    return true;
}

void Checker::note_body(const std::string &name) {
    if (graph_ == nullptr) {
        return;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr) {
        return;
    }
    const auto package = package_syms_.find(self->package_key);
    if (package == package_syms_.end()) {
        return;
    }
    const auto found = package->second.find(name);
    if (found != package->second.end()) {
        found->second.has_body = true;
    }
}

bool Checker::shared_has(const std::string &name) const {
    if (graph_ == nullptr) {
        return false;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr) {
        return false;
    }
    const auto package = package_syms_.find(self->package_key);
    return package != package_syms_.end() && package->second.contains(name);
}

std::vector<uint32_t> Checker::files_in_package(uint32_t file_id) const {
    return package_file_ids(graph_, file_id);
}

int Checker::find_tag(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = tags_.find(id);
        if (found == tags_.end()) {
            continue;
        }
        const auto tag = found->second.find(name);
        if (tag != found->second.end()) {
            return tag->second;
        }
    }
    return -1;
}

int Checker::find_enum(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = enum_ids_.find(id);
        if (found == enum_ids_.end()) {
            continue;
        }
        const auto tag = found->second.find(name);
        if (tag != found->second.end()) {
            return tag->second;
        }
    }
    return -1;
}

bool Checker::package_has_enum(uint32_t file, const std::string &name) const { return find_enum(file, name) >= 0; }

uint32_t Checker::defined_in(const std::string &name) const {
    const int struct_id = find_tag(file_id_, name);
    if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size())) {
        return structs_[static_cast<std::size_t>(struct_id)].file_id;
    }
    const int enum_id = find_enum(file_id_, name);
    if (enum_id >= 0 && enum_id < static_cast<int>(enum_types_.size())) {
        return enum_types_[static_cast<std::size_t>(enum_id)].file_id;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        const auto found = typedefs_.find(id);
        if (found == typedefs_.end()) {
            continue;
        }
        if (found->second.contains(name)) {
            return id;
        }
    }
    return file_id_;
}

bool Checker::named_type_owner(const std::string &name) const {
    if (name.empty()) {
        return false;
    }
    // `func Type::name` is written in the compiler package. The union is the owner.
    if (name == "Type" && is_std_builtin_file(file_id_)) {
        return true;
    }
    if (find_tag(file_id_, name) >= 0 || package_has_enum(file_id_, name)) {
        return true;
    }
    Type alias;
    return package_typedef(file_id_, name, alias) && alias.alias_id != 0 &&
           (is_numeric(alias) || is_slice(alias));
}

bool Checker::static_type_owner(const std::string &name) const {
    if (named_type_owner(name)) {
        return true;
    }
    Type alias;
    if (!package_typedef(file_id_, name, alias) || alias.alias_id == 0) {
        return false;
    }
    // `type Op func (i32) i32` is a pointer to a function. It is not `type Ptr *Point`.
    if (alias.kind == TypeKind::Pointer) {
        return alias.pointee == nullptr || alias.pointee->kind != TypeKind::Function;
    }
    if (alias.kind == TypeKind::Struct) {
        return true;
    }
    return alias.kind == TypeKind::Array && alias.length >= 0;
}

const Type *Checker::scalar_alias(const Type &base, bool &through_pointer) const {
    through_pointer = false;
    const Type *type = &base;
    if (base.kind == TypeKind::Pointer && base.pointee != nullptr) {
        through_pointer = true;
        type = base.pointee.get();
    }
    if (type->alias_id == 0 || !is_numeric(*type)) {
        return nullptr;
    }
    return type;
}

const Type *Checker::slice_alias(const Type &base, bool &through_pointer) const {
    through_pointer = false;
    const Type *type = &base;
    if (base.kind == TypeKind::Pointer && base.pointee != nullptr) {
        through_pointer = true;
        type = base.pointee.get();
    }
    if (type->alias_id == 0 || !is_slice(*type)) {
        return nullptr;
    }
    return type;
}

bool Checker::is_c_enum(const Type &type) const {
    return type.kind == TypeKind::Enum && type.enum_id >= 0 && type.enum_id < static_cast<int>(enum_types_.size()) &&
           enum_types_[static_cast<std::size_t>(type.enum_id)].is_extern;
}

const Checker::EnumInfo::Member *Checker::find_enum_member(int enum_id, const std::string &name) const {
    if (enum_id < 0 || enum_id >= static_cast<int>(enum_types_.size())) {
        return nullptr;
    }
    for (const EnumInfo::Member &member : enum_types_[static_cast<std::size_t>(enum_id)].members) {
        if (member.name == name) {
            return &member;
        }
    }
    return nullptr;
}

int Checker::enum_designator(const Expr &expr) {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        if (lookup(named->text) != nullptr) {
            return -1;
        }
        return find_enum(file_id_, named->text);
    }
    // A package selector is an enum only when that name is an exported enum.
    // A missing name is not an error here. `math.Buf[4, i32].count` is not an enumerator.
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr || !selector->type_args.empty()) {
            return -1;
        }
        const auto *package_name = std::get_if<Expr::Name>(&selector->base->data);
        if (package_name == nullptr) {
            return -1;
        }
        const Symbol *package = lookup(package_name->text);
        if (package == nullptr || !package->is_package) {
            return -1;
        }
        const Symbol *member = find_export(package->package_file, selector->field);
        if (member != nullptr && member->is_type && member->type.kind == TypeKind::Enum) {
            return member->type.enum_id;
        }
    }
    return -1;
}

int Checker::package_trait(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = trait_names_.find(id);
        if (found == trait_names_.end()) {
            continue;
        }
        const auto trait = found->second.find(name);
        if (trait != found->second.end()) {
            return trait->second;
        }
    }
    return -1;
}



bool Checker::package_typedef(uint32_t file, const std::string &name, Type &out) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = typedefs_.find(id);
        if (found == typedefs_.end()) {
            continue;
        }
        const auto alias = found->second.find(name);
        if (alias != found->second.end()) {
            out = alias->second;
            return true;
        }
    }
    return false;
}

bool Checker::exported_elsewhere(const std::string &name) const {
    for (const uint32_t id : files_in_package(file_id_)) {
        if (id == file_id_) {
            continue;
        }
        const auto file = exports_.find(id);
        if (file != exports_.end() && file->second.contains(name)) {
            return true;
        }
    }
    return false;
}

bool Checker::declare(Symbol symbol, SourceLoc loc) {
    if (symbol.name.empty()) {
        return false;
    }
    auto &table = scopes_.back().symbols;
    if (table.contains(symbol.name)) {
        error(loc, "redefinition");
        return false;
    }
    table.emplace(symbol.name, std::move(symbol));
    return true;
}

bool Checker::shadow(Symbol symbol, SourceLoc loc) {
    if (symbol.name.empty() || scopes_.empty()) {
        return false;
    }
    (void)loc;
    scopes_.back().symbols.insert_or_assign(symbol.name, std::move(symbol));
    return true;
}

Type Checker::type_of_name(const std::string &name, SourceLoc loc, bool allow_incomplete) {
    Type builtin;
    if (fill_builtin(name, builtin)) {
        return builtin;
    }
    if (name == "invalid") {
        return type_invalid();
    }
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return alias;
    }
    if (find_tag(file_id_, name) >= 0) {
        return lookup_tag(name, loc, allow_incomplete);
    }
    if (const int enum_id = find_enum(file_id_, name); enum_id >= 0) {
        return type_enum(enum_id);
    }
    if (const int trait = package_trait(file_id_, name); trait >= 0) {
        return type_trait(trait);
    }
    // `kind *const Type` on `builtin.Field`, before that tag is visible.
    if (name == "Type" && is_std_builtin_file(file_id_)) {
        return type_value();
    }
    uint32_t owner = file_id_;
    if (find_struct_template(name, owner) != nullptr || find_trait_template(name, owner) != nullptr ||
        find_tuple_alias_template(name, owner) != nullptr) {
        error(loc, "type mismatch");
        return type_invalid();
    }
    error(loc, "not supported yet");
    return type_invalid();
}

Type Checker::with_indirections(Type type, int count, SourceLoc loc, uint32_t optional, uint32_t const_pointee) {
    if (type.kind == TypeKind::Trait) {
        if (count > 1) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        if (count == 1) {
            if ((const_pointee & 1u) != 0) {
                type.is_const = true;
            }
            Type pointer = type_pointer(std::move(type));
            pointer.optional = (optional & 1u) != 0;
            return pointer;
        }
        return type;
    }
    for (int i = 0; i < count; ++i) {
        if (type.kind == TypeKind::Invalid) {
            return type;
        }
        if (type.kind != TypeKind::Int && type.kind != TypeKind::Char && type.kind != TypeKind::Bool &&
            type.kind != TypeKind::Float && type.kind != TypeKind::Pointer && type.kind != TypeKind::Struct &&
            type.kind != TypeKind::Enum && type.kind != TypeKind::Void && type.kind != TypeKind::Meta &&
            type.kind != TypeKind::Array && type.kind != TypeKind::Tuple) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        const int level = count - 1 - i;
        if ((const_pointee & (1u << level)) != 0) {
            type.is_const = true;
        }
        Type pointer = type_pointer(std::move(type));
        pointer.optional = (optional & (1u << level)) != 0;
        type = std::move(pointer);
    }
    return type;
}

Type Checker::lookup_tag(const std::string &name, SourceLoc loc, bool allow_incomplete) {
    const int id = find_tag(file_id_, name);
    if (id < 0) {
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    if (id < 0 || id >= static_cast<int>(structs_.size())) {
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    if (!allow_incomplete && !structs_[static_cast<std::size_t>(id)].complete) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    return type_struct(id);
}

Type Checker::lookup_qualified_type(const std::string &package, const std::string &name, SourceLoc loc) {
    const Symbol *symbol = lookup(package);
    if (symbol == nullptr || !symbol->is_package) {
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    const Symbol *member = find_export(symbol->package_file, name);
    if (member == nullptr) {
        error(loc, name + " is not exported");
        return type_invalid();
    }
    if (!member->is_type) {
        error(loc, "type mismatch");
        return type_invalid();
    }
    return member->type;
}

bool Checker::struct_open(int struct_id, std::unordered_set<int> &seen) const {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return false;
    }
    const StructInfo &info = structs_[static_cast<std::size_t>(struct_id)];
    if (info.is_extern || info.is_union) {
        return true;
    }
    if (!info.exported) {
        return false;
    }
    if (!seen.insert(struct_id).second) {
        return true;
    }
    for (const StructInfo::Field &field : info.fields) {
        if (field.type.kind == TypeKind::Struct && !struct_open(field.type.struct_id, seen)) {
            return false;
        }
    }
    return true;
}

bool Checker::field_visible(int struct_id, const std::string &name) const {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return true;
    }
    const StructInfo &info = structs_[static_cast<std::size_t>(struct_id)];
    const StructInfo::Field *field = nullptr;
    for (const StructInfo::Field &item : info.fields) {
        if (item.name == name) {
            field = &item;
            break;
        }
    }
    if (field == nullptr || info.is_extern || info.is_union) {
        return true;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        if (id == info.file_id) {
            return true;
        }
    }
    if (!field->exported) {
        return false;
    }
    if (field->type.kind != TypeKind::Struct) {
        return true;
    }
    std::unordered_set<int> seen;
    return struct_open(field->type.struct_id, seen);
}

bool Checker::hidden_field(const Type &object, const std::string &name, SourceLoc loc) {
    if (object.kind != TypeKind::Struct || field_visible(object.struct_id, name)) {
        return false;
    }
    error(loc, name + " is not exported");
    return true;
}

bool Checker::lookup_field(const Type &object, const std::string &name, Type &out) const {
    if (object.kind != TypeKind::Struct || object.struct_id < 0 ||
        object.struct_id >= static_cast<int>(structs_.size())) {
        return false;
    }
    for (const StructInfo::Field &field : structs_[static_cast<std::size_t>(object.struct_id)].fields) {
        if (field.name == name) {
            out = field.type;
            return true;
        }
    }
    return false;
}

bool Checker::accepts_pointer(const Type &got, const Type &want) const {
    if (same_type(got, want)) {
        return true;
    }
    if (got.kind != TypeKind::Pointer || want.kind != TypeKind::Pointer || got.pointee == nullptr ||
        want.pointee == nullptr) {
        return false;
    }
    // `*T` stays non-null. It is not stored in `?*T`, and `?*T` is not stored in `*T`.
    // `*T` may be stored in `*const T`. Dropping `const` is rejected.
    if (got.optional != want.optional) {
        return false;
    }
    if (got.pointee->is_const && !want.pointee->is_const) {
        return false;
    }
    Type source = *got.pointee;
    Type target = *want.pointee;
    source.is_const = target.is_const;
    if (same_type(source, target)) {
        return true;
    }
    return accepts_pointer(source, target);
}

bool Checker::needs_nonnull_pointer(const Type &type) const {
    std::unordered_set<int> seen;
    const auto walk = [&](const auto &self, const Type &current) -> bool {
        // A zero `builtin.Type` has a null descriptor. `name()` would load it.
        if (is_type_value(current)) {
            return true;
        }
        if (current.kind == TypeKind::Pointer) {
            return !current.optional;
        }
        if (current.kind == TypeKind::Array && current.length < 0) {
            return false;
        }
        if (current.kind == TypeKind::Array && current.element != nullptr) {
            return self(self, *current.element);
        }
        if (current.kind != TypeKind::Struct || current.struct_id < 0 ||
            current.struct_id >= static_cast<int>(structs_.size())) {
            return false;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(current.struct_id)];
        // A union payload is one variant. Zero storage is tag 0, not every field at once.
        if (info.is_union || !seen.insert(current.struct_id).second) {
            return false;
        }
        for (const StructInfo::Field &field : info.fields) {
            // A value on the field initializes it, including a `*T` that would otherwise be missing.
            if (field.has_init) {
                continue;
            }
            if (self(self, field.type)) {
                return true;
            }
        }
        return false;
    };
    return walk(walk, type);
}

bool Checker::dot_object(const Type &base, Type &object, bool &through_pointer) const {
    through_pointer = false;
    if (base.kind == TypeKind::Struct) {
        object = base;
        return true;
    }
    if (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Struct) {
        object = *base.pointee;
        through_pointer = true;
        return true;
    }
    return false;
}

const Checker::AssocVar *Checker::find_assoc(const std::string &owner, const std::string &name) const {
    return find_assoc_in(file_id_, owner, name);
}

const Checker::AssocVar *Checker::find_assoc_in(uint32_t file, const std::string &owner, const std::string &name) const {
    if (owner.empty() || name.empty()) {
        return nullptr;
    }
    for (const uint32_t id : files_in_package(file)) {
        const auto files = assoc_vars_.find(id);
        if (files == assoc_vars_.end()) {
            continue;
        }
        const auto group = files->second.find(owner);
        if (group == files->second.end()) {
            continue;
        }
        const auto found = group->second.find(name);
        if (found != group->second.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

bool Checker::assoc_defined(uint32_t file, const std::string &owner, const std::string &name) const {
    const auto files = assoc_vars_.find(file);
    if (files == assoc_vars_.end()) {
        return false;
    }
    const auto group = files->second.find(owner);
    if (group == files->second.end()) {
        return false;
    }
    return group->second.contains(name);
}

const Checker::MethodInfo *Checker::find_named_method(const std::string &type_name, const std::string &name) const {
    if (type_name.empty()) {
        return nullptr;
    }
    for (const uint32_t file : files_in_package(file_id_)) {
        const auto methods = methods_.find(file);
        if (methods == methods_.end()) {
            continue;
        }
        const auto group = methods->second.find(type_name);
        if (group == methods->second.end()) {
            continue;
        }
        const auto method = group->second.find(name);
        if (method != group->second.end()) {
            return &method->second;
        }
    }
    return nullptr;
}

const Checker::MethodInfo *Checker::find_visible_method(uint32_t home, const std::string &owner,
                                                        const std::string &name, bool &hidden) const {
    hidden = false;
    if (owner.empty() || name.empty()) {
        return nullptr;
    }
    bool local = home == 0;
    if (!local) {
        for (const uint32_t id : files_in_package(file_id_)) {
            if (id == home) {
                local = true;
                break;
            }
        }
    }
    if (local) {
        return find_named_method(owner, name);
    }
    for (const uint32_t id : files_in_package(home)) {
        const auto methods = methods_.find(id);
        if (methods == methods_.end()) {
            continue;
        }
        const auto group = methods->second.find(owner);
        if (group == methods->second.end()) {
            continue;
        }
        const auto method = group->second.find(name);
        if (method == group->second.end()) {
            continue;
        }
        if (method->second.more.empty()) {
            if (!method->second.exported) {
                hidden = true;
                return nullptr;
            }
            return &method->second;
        }
        bool exported = method->second.exported;
        if (!exported) {
            for (const MethodInfo &extra : method->second.more) {
                if (extra.exported) {
                    exported = true;
                    break;
                }
            }
        }
        if (!exported) {
            hidden = true;
            return nullptr;
        }
        return &method->second;
    }
    return nullptr;
}

bool Checker::package_owns(uint32_t home) const {
    if (home == 0) {
        return true;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        if (id == home) {
            return true;
        }
    }
    return false;
}

const Checker::MethodInfo *Checker::find_method(int struct_id, const std::string &name) const {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    return find_named_method(structs_[static_cast<std::size_t>(struct_id)].name, name);
}

const Checker::TraitInfo::Method *Checker::find_trait_method(int trait_id, const std::string &name) const {
    const int count = static_cast<int>(traits_.size());
    int walk = trait_id;
    for (int guard = 0; walk >= 0 && walk < count && guard < count; ++guard) {
        for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(walk)].methods) {
            if (method.name == name) {
                return &method;
            }
        }
        const int next = traits_[static_cast<std::size_t>(walk)].parent_id;
        if (next == walk) {
            break;
        }
        walk = next;
    }
    return nullptr;
}

const Checker::TraitInfo::Field *Checker::find_trait_field(int trait_id, const std::string &name) const {
    const int count = static_cast<int>(traits_.size());
    int walk = trait_id;
    for (int guard = 0; walk >= 0 && walk < count && guard < count; ++guard) {
        for (const TraitInfo::Field &field : traits_[static_cast<std::size_t>(walk)].fields) {
            if (field.name == name) {
                return &field;
            }
        }
        const int next = traits_[static_cast<std::size_t>(walk)].parent_id;
        if (next == walk) {
            break;
        }
        walk = next;
    }
    return nullptr;
}

bool Checker::as_trait_field(const Type &base, const std::string &name, Type &out) const {
    const Type *trait = nullptr;
    if (base.kind == TypeKind::Trait) {
        trait = &base;
    } else if (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait) {
        trait = base.pointee.get();
    } else {
        return false;
    }
    const TraitInfo::Field *field = find_trait_field(trait->trait_id, name);
    out = field == nullptr ? type_invalid() : field->type;
    return true;
}

bool Checker::trait_method_matches(const Type &trait_fn, const Type &concrete) const {
    if (trait_fn.kind != TypeKind::Function || concrete.kind != TypeKind::Function || trait_fn.result == nullptr ||
        concrete.result == nullptr) {
        return false;
    }
    if (trait_fn.variadic || concrete.variadic || concrete.params.empty() ||
        trait_fn.params.size() + 1 != concrete.params.size()) {
        return false;
    }
    if (!same_type(*trait_fn.result, *concrete.result)) {
        return false;
    }
    const Type &receiver = concrete.params[0];
    if (receiver.kind == TypeKind::Pointer) {
        if (receiver.pointee == nullptr || receiver.pointee->kind != TypeKind::Struct) {
            return false;
        }
    } else if (receiver.kind != TypeKind::Struct) {
        return false;
    }
    for (std::size_t i = 0; i < trait_fn.params.size(); ++i) {
        if (!same_type(trait_fn.params[i], concrete.params[i + 1])) {
            return false;
        }
    }
    return true;
}

} // namespace tlc
