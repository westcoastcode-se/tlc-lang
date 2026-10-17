// Names, scopes, and the types of declarations.
#include "check.hpp"
#include "check_util.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
            if (symbol.is_local_function && !symbol.has_body) {
                error(symbol.loc, "not supported yet");
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

Type Checker::type_of_fn_param(const FnParam &param, SourceLoc loc) {
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
    return object_type(param.type_name, param.indirections, param.is_array, param.array_len, loc, true, param.type_package,
                       param.is_struct, param.is_enum, false, param.optional, param.const_pointee);
}

Type Checker::type_of_fn_ptr(const std::string &ret_name, const std::string &ret_package, bool ret_struct, bool ret_enum,
                             int ret_indirections, int fn_depth, const std::vector<FnParam> &params, bool variadic,
                             const std::vector<FnParam> &results, SourceLoc loc, uint32_t ret_optional,
                             uint32_t ret_const) {
    Type result;
    if (results.size() > 1) {
        std::vector<Type> parts;
        parts.reserve(results.size());
        for (const FnParam &part : results) {
            FnParam &written = const_cast<FnParam &>(part);
            std::string array_len_name;
            prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum,
                            written.indirections, written.optional, written.is_array, written.array_len, array_len_name,
                            written.type_args, loc);
            Type one = part.is_array
                           ? object_type(part.type_name, part.indirections, true, part.array_len, loc, false,
                                         part.type_package, part.is_struct, part.is_enum, false, part.optional,
                                         part.const_pointee)
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

Type Checker::type_of_var(const VarDecl &var) {
    VarDecl &written = const_cast<VarDecl &>(var);
    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                    written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args,
                    written.loc);
    Type type = var.is_fn_ptr
                    ? type_of_fn_ptr(var.type_name, var.type_package, var.is_struct, var.is_enum, var.ret_indirections,
                                     var.indirections, var.fn_params, var.fn_variadic, var.fn_results, var.loc,
                                     var.ret_optional, var.ret_const_pointee)
                    : object_type(var.type_name, var.indirections, var.is_array, var.array_len, var.loc, false,
                                  var.type_package, var.is_struct, var.is_enum, false, var.optional, var.const_pointee,
                                  false, var.elem_const);
    if (!var.ptr_to_array || type.kind == TypeKind::Invalid) {
        return type;
    }
    if (type.kind != TypeKind::Array) {
        error(var.loc, "type mismatch");
        return type_invalid();
    }
    if ((var.const_pointee & 1u) != 0) {
        type.is_const = true;
    }
    return type_pointer(std::move(type));
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
    if (pointee.kind == TypeKind::Array) {
        if (pointee.element == nullptr) {
            return false;
        }
        if (pointee.element->kind == TypeKind::Void) {
            pointee_void = true;
            return false;
        }
        out = *pointee.element;
        return true;
    }
    out = pointee;
    return true;
}

Type Checker::type_of_param(const Param &param) {
    if (param.is_tuple) {
        std::vector<Type> parts;
        parts.reserve(param.tuple_parts.size());
        for (const ResultSlot &slot : param.tuple_parts) {
            parts.push_back(type_of_result(slot, param.loc));
        }
        return type_tuple(std::move(parts));
    }
    Param &written = const_cast<Param &>(param);
    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                    written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args,
                    written.loc);
    if (param.is_fn_ptr) {
        return type_of_fn_ptr(param.type_name, param.type_package, param.is_struct, param.is_enum, param.ret_indirections,
                              param.indirections, param.fn_params, param.fn_variadic, param.fn_results, param.loc,
                              param.ret_optional, param.ret_const_pointee);
    }
    return object_type(param.type_name, param.indirections, param.is_array, param.array_len, param.loc, true,
                       param.type_package, param.is_struct, param.is_enum, param.is_receiver, param.optional,
                       param.const_pointee, false, param.elem_const);
}

Type Checker::type_of_result(const ResultSlot &slot, SourceLoc loc) {
    ResultSlot &written = const_cast<ResultSlot &>(slot);
    prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum, written.indirections,
                    written.optional, written.is_array, written.array_len, written.array_len_name, written.type_args, loc);
    if (slot.is_fn_ptr) {
        return type_of_fn_ptr(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.ret_indirections,
                              slot.indirections, slot.fn_params, slot.fn_variadic, slot.fn_results, loc,
                              slot.ret_optional, slot.ret_const_pointee);
    }
    if (slot.is_array) {
        return object_type(slot.type_name, slot.indirections, true, slot.array_len, loc, false, slot.type_package,
                           slot.is_struct, slot.is_enum, false, slot.optional, slot.const_pointee, false,
                           slot.elem_const);
    }
    return slot.is_struct || slot.is_enum || !slot.type_package.empty()
               ? object_type(slot.type_name, slot.indirections, false, 0, loc, false, slot.type_package, slot.is_struct,
                             slot.is_enum, false, slot.optional, slot.const_pointee)
               : with_indirections(type_of_name(slot.type_name, loc, slot.indirections > 0), slot.indirections, loc,
                                   slot.optional, slot.const_pointee);
}

Type Checker::function_result(const FunctionDecl &fn) {
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
        error(fn.loc, "not supported yet");
    }
    auto &table = scopes_.back().symbols;
    const auto found = table.find(fn.name);
    if (found != table.end()) {
        Symbol &existing = found->second;
        if (!existing.is_local_function || !same_type(existing.type, signature) ||
            (existing.has_body && fn.body != nullptr) || existing.unsafe_fn != fn.unsafe) {
            error(fn.loc, "redefinition");
        } else if (fn.body != nullptr) {
            existing.has_body = true;
        }
    } else {
        Symbol symbol;
        symbol.name = fn.name;
        symbol.type = std::move(signature);
        symbol.is_function = true;
        symbol.is_local_function = true;
        symbol.unsafe_fn = fn.unsafe;
        symbol.has_body = fn.body != nullptr;
        symbol.loc = fn.loc;
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
    std::vector<uint32_t> ids;
    ids.push_back(file_id);
    const PackageNode *self = find_node(file_id);
    if (self == nullptr || self->package_key.empty() || graph_ == nullptr) {
        return ids;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.package_key == self->package_key && node.file_id != file_id) {
            ids.push_back(node.file_id);
        }
    }
    return ids;
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
    if (find_tag(file_id_, name) >= 0 || package_has_enum(file_id_, name)) {
        return true;
    }
    Type alias;
    return package_typedef(file_id_, name, alias) && alias.alias_id != 0 &&
           (is_numeric(alias) || is_slice(alias));
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

const Type *Checker::slice_alias(const Type &base) const {
    if (base.alias_id == 0 || !is_slice(base)) {
        return nullptr;
    }
    return &base;
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
    uint32_t owner = file_id_;
    if (find_struct_template(name, owner) != nullptr) {
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
            type.kind != TypeKind::Enum && type.kind != TypeKind::Void && type.kind != TypeKind::Meta) {
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
    // `*T` may be stored in `?*T`. `*T` may be stored in `*const T`. Dropping `const` is rejected.
    if (got.optional && !want.optional) {
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
        if (current.kind == TypeKind::Meta) {
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
        if (!method->second.exported) {
            hidden = true;
            return nullptr;
        }
        return &method->second;
    }
    return nullptr;
}

const Checker::MethodInfo *Checker::find_method(int struct_id, const std::string &name) const {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    return find_named_method(structs_[static_cast<std::size_t>(struct_id)].name, name);
}

const Checker::TraitInfo::Method *Checker::find_trait_method(int trait_id, const std::string &name) const {
    if (trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        return nullptr;
    }
    for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(trait_id)].methods) {
        if (method.name == name) {
            return &method;
        }
    }
    return nullptr;
}

const Checker::TraitInfo::Field *Checker::find_trait_field(int trait_id, const std::string &name) const {
    if (trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        return nullptr;
    }
    for (const TraitInfo::Field &field : traits_[static_cast<std::size_t>(trait_id)].fields) {
        if (field.name == name) {
            return &field;
        }
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
    info.methods.push_back(TraitInfo::Method{"malloc", type_function(optional, {size, size})});
    info.methods.push_back(TraitInfo::Method{"free", type_function(type_void(), {pointer})});
    info.methods.push_back(TraitInfo::Method{"realloc", type_function(optional, {pointer, size, size})});
    allocator_trait_id_ = static_cast<int>(traits_.size());
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
            if (method.results.size() > 1) {
                error(method.loc, "not supported yet");
                continue;
            }
            std::vector<Type> params;
            for (const Param &param : method.params) {
                params.push_back(type_of_param(param));
            }
            Type result = method.is_struct || method.is_enum || !method.type_package.empty()
                              ? object_type(method.return_type, method.indirections, false, 0, method.loc, false,
                                            method.type_package, method.is_struct, method.is_enum, false, method.optional)
                              : with_indirections(type_of_name(method.return_type, method.loc, method.indirections > 0),
                                                  method.indirections, method.loc, method.optional);
            if (mentions_trait(result)) {
                error(method.loc, "not supported yet");
                continue;
            }
            traits_[static_cast<std::size_t>(id)].methods.push_back(
                TraitInfo::Method{method.name, type_function(std::move(result), std::move(params), false)});
        }
    }
}

void Checker::require_satisfies(int struct_id, int trait_id, bool through_pointer, SourceLoc loc) {
    if (struct_id < 0 || trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        error(loc, "type mismatch");
        return;
    }
    for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(trait_id)].methods) {
        const MethodInfo *concrete = find_method(struct_id, method.name);
        if (concrete == nullptr || !concrete->has_body) {
            error(loc, "undeclared identifier");
            continue;
        }
        const bool pointer_method = concrete->type.kind == TypeKind::Function && !concrete->type.params.empty() &&
                                    concrete->type.params[0].kind == TypeKind::Pointer;
        if (!through_pointer && pointer_method) {
            error(loc, "type mismatch");
            continue;
        }
        if (!trait_method_matches(method.type, concrete->type)) {
            error(loc, "type mismatch");
        }
    }
    const Type object = type_struct(struct_id);
    for (const TraitInfo::Field &field : traits_[static_cast<std::size_t>(trait_id)].fields) {
        Type got;
        if (!lookup_field(object, field.name, got)) {
            error(loc, "undeclared identifier");
            continue;
        }
        if (field.type.kind == TypeKind::Invalid || got.kind == TypeKind::Invalid || !same_type(field.type, got)) {
            error(loc, "type mismatch");
        }
    }
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
            FieldDecl field = source;
            prepare_written(field.type_name, field.type_package, field.is_struct, field.is_enum, field.indirections,
                            field.optional, field.is_array, field.array_len, field.array_len_name, field.type_args,
                            field.loc);
            Type field_type =
                field.is_fn_ptr ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                 field.ret_indirections, field.indirections, field.fn_params,
                                                 field.fn_variadic, field.fn_results, field.loc, field.ret_optional,
                                                 field.ret_const_pointee)
                                : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                              false, field.type_package, field.is_struct, field.is_enum, false,
                                              field.optional, field.const_pointee, false, field.elem_const);
            if (field_type.kind == TypeKind::Invalid) {
                continue;
            }
            if (mentions_trait(field_type)) {
                error(field.loc, "not supported yet");
                continue;
            }
            info.fields.push_back(TraitInfo::Field{field.name, std::move(field_type), field.loc});
        }
        info.pending.clear();
    }
}

bool Checker::field_waits(const FieldDecl &field) const {
    if (field.is_array || field.indirections > 0 || !field.type_package.empty() || field.is_enum || field.type_name.empty()) {
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
            if (item->is_extern || item->extern_c || item->is_union) {
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
        structs_.push_back(std::move(created));
        tags_[file_id][item->name] = id;
        if (item->fields.empty()) {
            // An empty tlc struct is zero-sized and may still have methods. An empty
            // extern(abi="c") struct is a one-byte opaque handle. A union needs a field.
            if (item->is_union) {
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
                return std::nullopt;
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
            if (complain_waiting && field_waits(field)) {
                error(field.loc, "not supported yet");
                info.fields.push_back(StructInfo::Field{field.name, type_invalid(), field.loc, field.exported});
                continue;
            }
            FieldDecl &written = const_cast<FieldDecl &>(field);
            prepare_written(written.type_name, written.type_package, written.is_struct, written.is_enum,
                            written.indirections, written.optional, written.is_array, written.array_len,
                            written.array_len_name, written.type_args, written.loc);
            Type field_type =
                field.is_fn_ptr ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                 field.ret_indirections, field.indirections, field.fn_params,
                                                 field.fn_variadic, field.fn_results, field.loc, field.ret_optional,
                                                 field.ret_const_pointee)
                                : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                              false, field.type_package, field.is_struct, field.is_enum, false,
                                              field.optional, field.const_pointee, false, field.elem_const);
            // A slice is a tlc value, `{ptr, i32}`. An `extern(abi="c")` struct keeps the C layout.
            if (is_slice(field_type) && info.extern_c) {
                error(field.loc, "not supported yet");
                info.fields.push_back(StructInfo::Field{field.name, type_invalid(), field.loc, field.exported});
                continue;
            }
            if (info.extern_c) {
                require_extern_struct(field_type, field.loc);
            }
            info.fields.push_back(StructInfo::Field{field.name, std::move(field_type), field.loc, field.exported});
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
                if (field_waits(field)) {
                    waits = true;
                    break;
                }
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
        if (item->enumerators.empty()) {
            error(item->loc, "not supported yet");
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

void Checker::check_unions() {
    for (StructInfo &info : structs_) {
        if (info.tag_name.empty()) {
            continue;
        }
        file_id_ = info.file_id;
        int enum_id = find_enum(info.file_id, info.tag_name);
        if (enum_id < 0) {
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
            if (field.type.kind != TypeKind::Struct || field.type.struct_id < 0 ||
                field.type.struct_id >= static_cast<int>(structs_.size()) ||
                structs_[static_cast<std::size_t>(field.type.struct_id)].is_union) {
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
                                        item.const_pointee, true, item.elem_const);
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
                               uint32_t optional, uint32_t const_pointee) {
    Type alias;
    if (is_struct || is_enum || !package.empty() || package_typedef(file_id_, name, alias) || find_tag(file_id_, name) >= 0) {
        return object_type(name, indirections, is_array, array_len, loc, parameter, package, is_struct, is_enum, false,
                           optional, const_pointee);
    }
    return quiet_object(name, indirections, is_array, array_len, parameter, optional, const_pointee);
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

Type Checker::object_type(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                          bool parameter, const std::string &package, bool is_struct, bool is_enum, bool receiver,
                          uint32_t optional, uint32_t const_pointee, bool allow_incomplete, bool elem_const) {
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
            // The parser marks every receiver as a struct. A pointer receiver on a slice alias is not supported yet.
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
    if (mentions_trait(type) && (is_array || !parameter)) {
        error(loc, "not supported yet");
        return type_invalid();
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
            return type_pointer(*type.element);
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
    // `[]const u8` marks the element. A parameter `[N]const u8` decays to `*const u8`.
    if (elem_const) {
        type.is_const = true;
    }
    // `[]T` is a slice. It does not decay, even as a parameter.
    if (array_len < 0) {
        if (is_slice(type)) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        return type_array(std::move(type), -1);
    }
    if (is_slice(type)) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    if (parameter) {
        return type_pointer(std::move(type));
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
            return structs_[static_cast<std::size_t>(type.struct_id)].name;
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
