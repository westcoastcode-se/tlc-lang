// Monomorphization of `[T type, N int]` functions and `Vec[T]` structs.
#include "check.hpp"
#include "check_util.hpp"

#include <string>
#include <utility>
#include <vector>

namespace tlc {

namespace {

const SpecArg *find_arg(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, const std::string &name) {
    for (std::size_t i = 0; i < params.size() && i < bounds.size(); ++i) {
        if (params[i].name == name) {
            return &bounds[i];
        }
    }
    return nullptr;
}

int param_index(const std::vector<TypeParam> &params, const std::string &name) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (params[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool same_spec(const SpecArg &lhs, const SpecArg &rhs) {
    if (lhs.is_value || rhs.is_value) {
        return lhs.is_value && rhs.is_value && lhs.value == rhs.value;
    }
    if (lhs.is_pack || rhs.is_pack) {
        if (!lhs.is_pack || !rhs.is_pack || lhs.parts.size() != rhs.parts.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.parts.size(); ++i) {
            if (!same_spec(lhs.parts[i], rhs.parts[i])) {
                return false;
            }
        }
        return true;
    }
    return lhs.type_name == rhs.type_name && lhs.type_package == rhs.type_package && lhs.is_struct == rhs.is_struct &&
           lhs.is_enum == rhs.is_enum && lhs.indirections == rhs.indirections && lhs.optional == rhs.optional &&
           lhs.const_pointee == rhs.const_pointee && lhs.is_array == rhs.is_array && lhs.elem_const == rhs.elem_const &&
           lhs.array_len == rhs.array_len;
}

ResultSlot slot_from_spec(const SpecArg &arg) {
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
    slot.array_len = arg.array_len;
    return slot;
}

void apply_spec(ResultSlot &slot, const SpecArg &arg) {
    slot.type_name = arg.type_name;
    slot.type_package = arg.type_package;
    slot.is_struct = arg.is_struct;
    slot.is_enum = arg.is_enum;
    slot.indirections = arg.indirections;
    slot.optional = arg.optional;
    slot.const_pointee = arg.const_pointee;
    slot.is_array = arg.is_array;
    slot.elem_const = arg.elem_const;
    slot.array_len = arg.array_len;
}

SpecArg pack_bound(std::vector<SpecArg> parts) {
    if (parts.size() == 1) {
        return std::move(parts[0]);
    }
    SpecArg packed;
    packed.is_pack = true;
    packed.parts = std::move(parts);
    return packed;
}

int type_param_index(const std::vector<TypeParam> &params, const std::string &name) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (params[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void append_shaped_name(std::string &out, const std::string &package, const std::string &name,
                        const std::vector<TypeParam> &params) {
    if (package.empty()) {
        const int slot = type_param_index(params, name);
        if (slot >= 0) {
            out.push_back('#');
            out += std::to_string(slot);
            const TypeParam &param = params[static_cast<std::size_t>(slot)];
            if (param.is_pack) {
                out += ":pack";
            } else if (param.is_value) {
                out += ":value";
            } else if (param.is_trait) {
                out += ":trait:";
                out += param.constraint;
            } else {
                out += ":type";
            }
            return;
        }
    }
    if (!package.empty()) {
        out += package;
        out.push_back('.');
    }
    out += name;
}

void append_type_arg_shape(std::string &out, const TypeArg &arg, const std::vector<TypeParam> &params);
void append_type_arg_shape_list(std::string &out, const std::vector<TypeArg> &args, const std::vector<TypeParam> &params);
void append_fn_shape(std::string &out, const FnParam &param, const std::vector<TypeParam> &params);

void append_type_arg_shape(std::string &out, const TypeArg &arg, const std::vector<TypeParam> &params) {
    if (arg.has_value || arg.is_value) {
        out += std::to_string(arg.value);
        return;
    }
    if (arg.is_array) {
        out.push_back('[');
        if (!arg.array_len_name.empty()) {
            append_shaped_name(out, "", arg.array_len_name, params);
        } else if (arg.array_len >= 0) {
            out += std::to_string(arg.array_len);
        }
        out.push_back(']');
        if (arg.elem_const) {
            out += "const.";
        }
    }
    for (int bit = arg.indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((arg.optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((arg.const_pointee & mask) != 0) {
            out += "const";
        }
    }
    append_shaped_name(out, arg.type_package, arg.type_name, params);
    if (arg.args.empty()) {
        return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < arg.args.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_type_arg_shape(out, arg.args[i], params);
    }
    out.push_back(']');
}

void append_fn_shape(std::string &out, const FnParam &param, const std::vector<TypeParam> &params) {
    if (param.is_array) {
        out.push_back('[');
        if (param.array_len >= 0) {
            out += std::to_string(param.array_len);
        }
        out.push_back(']');
    }
    for (int bit = param.indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((param.optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((param.const_pointee & mask) != 0) {
            out += "const";
        }
    }
    if (param.is_fn_ptr) {
        out += "fn";
    }
    append_shaped_name(out, param.type_package, param.type_name, params);
    append_type_arg_shape_list(out, param.type_args, params);
    if (!param.is_fn_ptr) {
        return;
    }
    out.push_back('(');
    for (std::size_t i = 0; i < param.params.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_fn_shape(out, param.params[i], params);
    }
    if (param.variadic) {
        out += "...";
    }
    out.push_back(')');
}

void append_type_arg_shape_list(std::string &out, const std::vector<TypeArg> &args, const std::vector<TypeParam> &params) {
    if (args.empty()) {
        return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_type_arg_shape(out, args[i], params);
    }
    out.push_back(']');
}

void append_result_shape(std::string &out, const ResultSlot &slot, const std::vector<TypeParam> &params) {
    if (slot.is_array) {
        out.push_back('[');
        if (!slot.array_len_name.empty()) {
            append_shaped_name(out, "", slot.array_len_name, params);
        } else if (slot.array_len >= 0) {
            out += std::to_string(slot.array_len);
        }
        out.push_back(']');
        if (slot.elem_const) {
            out += "const.";
        }
    }
    for (int bit = slot.indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((slot.optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((slot.const_pointee & mask) != 0) {
            out += "const";
        }
    }
    if (slot.is_fn_ptr) {
        out += "fn";
    }
    append_shaped_name(out, slot.type_package, slot.type_name, params);
    append_type_arg_shape_list(out, slot.type_args, params);
    if (!slot.is_fn_ptr) {
        return;
    }
    out.push_back('(');
    for (std::size_t i = 0; i < slot.fn_params.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_fn_shape(out, slot.fn_params[i], params);
    }
    out.push_back(')');
}

void append_param_shape(std::string &out, const Param &param, const std::vector<TypeParam> &params) {
    if (param.is_tuple) {
        out.push_back('(');
        for (std::size_t i = 0; i < param.tuple_parts.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            append_result_shape(out, param.tuple_parts[i], params);
        }
        out.push_back(')');
        return;
    }
    if (param.is_array) {
        out.push_back('[');
        if (!param.array_len_name.empty()) {
            append_shaped_name(out, "", param.array_len_name, params);
        } else if (param.array_len >= 0) {
            out += std::to_string(param.array_len);
        }
        out.push_back(']');
        if (param.elem_const) {
            out += "const.";
        }
    }
    for (int bit = param.indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((param.optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((param.const_pointee & mask) != 0) {
            out += "const";
        }
    }
    if (param.is_fn_ptr) {
        out += "fn";
    }
    append_shaped_name(out, param.type_package, param.type_name, params);
    append_type_arg_shape_list(out, param.type_args, params);
    if (!param.is_fn_ptr) {
        return;
    }
    out.push_back('(');
    for (std::size_t i = 0; i < param.fn_params.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_fn_shape(out, param.fn_params[i], params);
    }
    if (param.fn_variadic) {
        out += "...";
    }
    out.push_back(')');
}

// The value parameters a call uses. A method omits the receiver. Type-parameter names are indices.
std::string generic_param_key(const FunctionDecl &fn, const std::vector<TypeParam> &params, bool skip_receiver) {
    std::string key;
    if (fn.variadic) {
        key.push_back('v');
    }
    std::size_t start = 0;
    if (skip_receiver && !fn.params.empty() && fn.params.front().is_receiver) {
        start = 1;
    }
    for (std::size_t i = start; i < fn.params.size(); ++i) {
        if (!key.empty()) {
            key.push_back(',');
        }
        append_param_shape(key, fn.params[i], params);
    }
    return key;
}

std::string generic_full_key(const FunctionDecl &fn, const std::vector<TypeParam> &params) {
    std::string key = generic_param_key(fn, params, false);
    key += "->";
    if (!fn.results.empty()) {
        for (std::size_t i = 0; i < fn.results.size(); ++i) {
            if (i != 0) {
                key.push_back(',');
            }
            append_result_shape(key, fn.results[i], params);
        }
        return key;
    }
    ResultSlot slot;
    slot.type_name = fn.return_type;
    slot.type_package = fn.type_package;
    slot.is_struct = fn.is_struct;
    slot.is_enum = fn.is_enum;
    slot.indirections = fn.indirections;
    slot.optional = fn.optional;
    slot.const_pointee = fn.const_pointee;
    append_result_shape(key, slot, params);
    return key;
}

// A suffix that can sit in an object name. Distinct lists stay distinct.
std::string generic_list_suffix(const FunctionDecl &fn) {
    std::string raw = generic_param_key(fn, fn.type_params, false);
    std::string out;
    for (const char c : raw) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (plain) {
            out.push_back(c);
        } else if (c == '#' ) {
            out.push_back('p');
        } else if (c == ',' || c == ':') {
            out.push_back('.');
        } else if (c == '*') {
            out += "ptr";
        } else if (c == '?') {
            out += "opt";
        } else if (c == '[' || c == '(') {
            out.push_back('.');
        }
    }
    return out;
}

} // namespace

void Checker::collect_templates(TranslationUnit &unit, uint32_t file_id) {
    std::unordered_set<std::string> generic_structs;
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<StructDecl>(&decl.data);
        if (item != nullptr && !item->name.empty() && !item->type_params.empty()) {
            generic_structs.insert(item->name);
        }
    }
    for (const TopDecl &decl : unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->name.empty()) {
            continue;
        }
        const bool function_template = !fn->type_params.empty() && fn->method_struct.empty();
        const bool method_template = !fn->method_struct.empty() && generic_structs.contains(fn->method_struct);
        if (!function_template && !method_template) {
            continue;
        }
        owned_fns_.push_back(OwnedFn{file_id, clone_function(*fn)});
        if (function_template) {
            fn_templates_[file_id][fn->name].push_back(&owned_fns_.back().fn);
        }
    }
    for (const TopDecl &decl : unit.decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        if (var == nullptr || var->owner.empty() || var->name.empty() || !generic_structs.contains(var->owner)) {
            continue;
        }
        owned_assoc_.push_back(OwnedVar{file_id, clone_var(*var)});
    }
}

namespace {

// Early folding runs before typedefs. Only an inferred constant, or one written as a builtin
// integer or bool, becomes the compile-time `i32` used as `[N]i32`. `const value Integer = 10`
// keeps `Integer` and is checked later.
bool early_int_const(const VarDecl &var) {
    if (var.inferred) {
        return true;
    }
    if (var.is_fn_ptr || var.is_struct || var.is_enum || var.is_array || var.ptr_to_array || var.indirections != 0 ||
        !var.type_package.empty() || !var.type_args.empty()) {
        return false;
    }
    Type named;
    if (!fill_builtin(var.type_name, named)) {
        return false;
    }
    return named.alias_id == 0 && (is_integer(named) || named.kind == TypeKind::Bool);
}

} // namespace

void Checker::fold_early_consts(TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    push();
    for (TopDecl &decl : unit.decls) {
        auto *var = std::get_if<VarDecl>(&decl.data);
        if (var == nullptr || !var->is_const_decl || var->has_const || var->name.empty() || var->init == nullptr ||
            !var->owner.empty()) {
            continue;
        }
        if (!early_int_const(*var)) {
            continue;
        }
        const std::optional<int> value = fold_int(*var->init);
        if (!value) {
            continue;
        }
        var->has_const = true;
        var->const_value = *value;
        const_values_[file_id][var->name] = ConstBinding{*value, var->is_static};
        Symbol symbol;
        symbol.name = var->name;
        symbol.type = type_int();
        symbol.is_constant = true;
        symbol.const_value = *value;
        declare(std::move(symbol), var->loc);
    }
    pop();
}

std::optional<int> Checker::lookup_const_value(const std::string &name) {
    if (const Symbol *symbol = lookup(name); symbol != nullptr && symbol->is_constant) {
        if (plain_int_constant(symbol->type)) {
            return symbol->const_value;
        }
        return std::nullopt;
    }
    auto search = [&](uint32_t file, bool allow_static) -> std::optional<int> {
        const auto table = const_values_.find(file);
        if (table == const_values_.end()) {
            return std::nullopt;
        }
        const auto found = table->second.find(name);
        if (found == table->second.end()) {
            return std::nullopt;
        }
        if (found->second.is_static && !allow_static) {
            return std::nullopt;
        }
        return found->second.value;
    };
    if (const std::optional<int> local = search(file_id_, true)) {
        return local;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        if (id == file_id_) {
            continue;
        }
        if (const std::optional<int> value = search(id, false)) {
            return value;
        }
    }
    return std::nullopt;
}

bool Checker::spell_type(const Type &type, SpecArg &out) const {
    const Type *cursor = &type;
    int outer = 0;
    while (cursor->kind == TypeKind::Pointer && cursor->pointee != nullptr) {
        ++outer;
        cursor = cursor->pointee.get();
        if (outer > 31) {
            return false;
        }
    }
    if (cursor->kind == TypeKind::Array) {
        if (outer > 0 || cursor->element == nullptr) {
            return false;
        }
        out.is_array = true;
        out.array_len = cursor->length;
        out.elem_const = cursor->element->is_const;
        cursor = cursor->element.get();
    }
    int depth = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    while (cursor->kind == TypeKind::Pointer && cursor->pointee != nullptr) {
        if (cursor->optional) {
            optional |= 1u << depth;
        }
        if (cursor->pointee->is_const) {
            const_pointee |= 1u << depth;
        }
        ++depth;
        cursor = cursor->pointee.get();
        if (depth > 31) {
            return false;
        }
    }
    out.indirections = depth;
    out.optional = optional;
    out.const_pointee = const_pointee;
    out.is_value = false;
    out.type_package.clear();
    out.struct_id = -1;
    out.enum_id = -1;
    out.is_struct = false;
    out.is_enum = false;
    switch (cursor->kind) {
    case TypeKind::Int:
    case TypeKind::Char:
    case TypeKind::Bool:
    case TypeKind::Float:
    case TypeKind::Void:
    case TypeKind::Meta:
        out.type_name = type_desc(*cursor);
        return out.type_name != "invalid";
    case TypeKind::Struct:
        if (cursor->struct_id < 0 || cursor->struct_id >= static_cast<int>(structs_.size())) {
            return false;
        }
        out.type_name = structs_[static_cast<std::size_t>(cursor->struct_id)].name;
        out.is_struct = true;
        out.struct_id = cursor->struct_id;
        return true;
    case TypeKind::Enum:
        if (cursor->enum_id < 0 || cursor->enum_id >= static_cast<int>(enum_types_.size())) {
            return false;
        }
        out.type_name = enum_types_[static_cast<std::size_t>(cursor->enum_id)].name;
        out.is_enum = true;
        out.enum_id = cursor->enum_id;
        return true;
    default:
        return false;
    }
}

std::string Checker::mangle_spec(const std::string &base, const std::vector<SpecArg> &args) const {
    std::string name = base + ".spec";
    auto append = [&](auto &&self, const SpecArg &arg) -> void {
        name += '.';
        if (arg.is_pack) {
            name += "pack";
            for (const SpecArg &part : arg.parts) {
                self(self, part);
            }
            return;
        }
        if (arg.is_value) {
            name += std::to_string(arg.value);
            return;
        }
        if (arg.is_array) {
            name += "arr.";
            if (arg.elem_const) {
                name += "const.";
            }
            name += std::to_string(arg.array_len);
            name += '.';
        }
        for (int i = 0; i < arg.indirections; ++i) {
            name += (arg.optional & (1u << i)) != 0 ? "opt." : "ptr.";
            if ((arg.const_pointee & (1u << i)) != 0) {
                name += "const.";
            }
        }
        if (!arg.type_package.empty()) {
            name += arg.type_package;
            name += '.';
        }
        name += arg.type_name.empty() ? "invalid" : arg.type_name;
    };
    for (const SpecArg &arg : args) {
        append(append, arg);
    }
    return name;
}

const StructDecl *Checker::find_struct_template(const std::string &name, uint32_t &owner) const {
    for (const uint32_t id : files_in_package(file_id_)) {
        const auto file = struct_templates_.find(id);
        if (file == struct_templates_.end()) {
            continue;
        }
        const auto found = file->second.find(name);
        if (found == file->second.end()) {
            continue;
        }
        owner = id;
        return found->second;
    }
    return nullptr;
}

void Checker::require_method_params(const FunctionDecl &fn) {
    uint32_t owner = file_id_;
    const StructDecl *templ = find_struct_template(fn.method_struct, owner);
    if (templ == nullptr) {
        if (!fn.method_params.empty()) {
            error(fn.loc, "type mismatch");
        }
        return;
    }
    if (owner != file_id_) {
        error(fn.loc, "method in another file");
    }
    if (fn.method_params.empty()) {
        error(fn.loc, "type mismatch");
        return;
    }
    if (fn.method_params.size() != templ->type_params.size()) {
        error(fn.loc, "argument count");
        return;
    }
    for (std::size_t i = 0; i < fn.method_params.size(); ++i) {
        if (fn.method_params[i] != templ->type_params[i].name) {
            error(fn.loc, "type mismatch");
            return;
        }
    }
}

void Checker::require_owner_params(const VarDecl &var) {
    uint32_t owner = file_id_;
    const StructDecl *templ = find_struct_template(var.owner, owner);
    if (templ == nullptr) {
        if (!var.owner_params.empty()) {
            error(var.loc, "type mismatch");
        }
        return;
    }
    if (owner != file_id_) {
        error(var.loc, "method in another file");
    }
    if (var.owner_params.empty()) {
        error(var.loc, "type mismatch");
        return;
    }
    if (var.owner_params.size() != templ->type_params.size()) {
        error(var.loc, "argument count");
        return;
    }
    for (std::size_t i = 0; i < var.owner_params.size(); ++i) {
        if (var.owner_params[i] != templ->type_params[i].name) {
            error(var.loc, "type mismatch");
            return;
        }
    }
}

void Checker::instantiate_assoc(uint32_t owner_file, const StructDecl &templ, const std::string &spec_name,
                                const std::vector<SpecArg> &bounds) {
    const std::string busy = std::to_string(owner_file) + "\n" + spec_name;
    if (!assoc_busy_.insert(busy).second) {
        return;
    }
    std::vector<const VarDecl *> matches;
    for (const OwnedVar &owned : owned_assoc_) {
        if (owned.file_id == owner_file && owned.var.owner == templ.name) {
            matches.push_back(&owned.var);
        }
    }
    for (const VarDecl *src : matches) {
        if (src == nullptr || src->name.empty() || assoc_defined(owner_file, spec_name, src->name)) {
            continue;
        }
        VarDecl copy = clone_var(*src);
        copy.owner = spec_name;
        copy.owner_params.clear();
        substitute_type(templ.type_params, bounds, copy.type_name, copy.type_package, copy.is_struct, copy.is_enum,
                        copy.indirections, copy.optional, copy.is_array, copy.array_len, copy.array_len_name,
                        copy.type_args, copy.loc);
        if (copy.init) {
            substitute_expr(templ.type_params, bounds, *copy.init);
        }
        if (copy.is_const_decl) {
            const std::size_t before = diags_.size();
            store_assoc_const(owner_file, copy);
            const AssocVar *stored = find_assoc_in(owner_file, spec_name, copy.name);
            if (diags_.size() == before && stored != nullptr && stored->type.kind != TypeKind::Invalid) {
                PendingAppend appended;
                appended.file_id = owner_file;
                appended.decl.data = std::move(copy);
                pending_appends_.push_back(std::move(appended));
            }
            continue;
        }
        Type type = type_invalid();
        if (copy.inferred) {
            if (copy.init == nullptr) {
                error(copy.loc, "type mismatch");
            } else {
                type = check_expr(*copy.init, true);
                if (type.kind == TypeKind::Tuple || type.kind == TypeKind::Void || type.kind == TypeKind::Nullptr) {
                    error(copy.loc, "type mismatch");
                    type = type_invalid();
                }
            }
        } else {
            type = type_of_var(copy);
            if (copy.init) {
                if (mentions_trait(type)) {
                    error(copy.loc, "not supported yet");
                } else {
                    check_against(*copy.init, type);
                }
            } else if (!copy.is_extern && needs_nonnull_pointer(type)) {
                error(copy.loc, "type mismatch");
            }
        }
        assoc_vars_[owner_file][spec_name].emplace(copy.name, AssocVar{type, copy.exported});
        PendingAppend appended;
        appended.file_id = owner_file;
        appended.decl.data = std::move(copy);
        pending_appends_.push_back(std::move(appended));
    }
    assoc_busy_.erase(busy);
}

bool Checker::generic_struct_name(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = struct_templates_.find(id);
        if (found != struct_templates_.end() && found->second.contains(name)) {
            return true;
        }
    }
    return false;
}

void Checker::expose_bounds(uint32_t owner, const std::vector<SpecArg> &bounds) {
    for (const SpecArg &bound : bounds) {
        if (bound.is_pack) {
            expose_bounds(owner, bound.parts);
            continue;
        }
        if (bound.is_value || bound.type_name.empty()) {
            continue;
        }
        if (bound.struct_id >= 0 && find_tag(owner, bound.type_name) < 0) {
            tags_[owner][bound.type_name] = bound.struct_id;
        }
        if (bound.enum_id >= 0 && find_enum(owner, bound.type_name) < 0) {
            enum_ids_[owner][bound.type_name] = bound.enum_id;
        }
    }
}

void Checker::prepare_written(std::string &type_name, std::string &type_package, bool &is_struct, bool &is_enum,
                              int &indirections, uint32_t &optional, bool &is_array, int &array_len,
                              std::string &array_len_name, std::vector<TypeArg> &type_args, SourceLoc loc) {
    (void)is_enum;
    (void)indirections;
    (void)optional;
    if (!array_len_name.empty()) {
        const std::optional<int> value = lookup_const_value(array_len_name);
        if (!value || *value <= 0) {
            error(loc, "not supported yet");
        } else {
            array_len = *value;
            array_len_name.clear();
            is_array = true;
        }
    }
    if (type_args.empty()) {
        return;
    }
    const std::string spec = specialize_type_app(type_package, type_name, type_args, loc);
    if (spec.empty()) {
        type_name = "invalid";
        type_package.clear();
        type_args.clear();
        return;
    }
    type_name = spec;
    type_package.clear();
    // `is_struct` means the source wrote `struct Name`. A specialized name is an ordinary type.
    is_struct = false;
    is_enum = false;
    type_args.clear();
}

bool Checker::bind_type_arg(const TypeParam &param, const TypeArg &arg, SpecArg &out, SourceLoc loc) {
    if (param.is_value) {
        if (arg.has_value) {
            out = {};
            out.is_value = true;
            out.value = arg.value;
            return true;
        }
        if (!arg.type_name.empty() && arg.type_package.empty() && arg.indirections == 0 && arg.args.empty() && !arg.is_array) {
            if (const std::optional<int> value = lookup_const_value(arg.type_name)) {
                out = {};
                out.is_value = true;
                out.value = *value;
                return true;
            }
        }
        error(loc.valid() ? loc : arg.loc, "type mismatch");
        return false;
    }
    if (arg.has_value || arg.is_value) {
        error(arg.loc, "type mismatch");
        return false;
    }
    TypeArg concrete = arg;
    if (!concrete.array_len_name.empty()) {
        const std::optional<int> len = lookup_const_value(concrete.array_len_name);
        if (!len || *len <= 0) {
            error(arg.loc, "not supported yet");
            return false;
        }
        concrete.array_len = *len;
        concrete.is_array = true;
        concrete.array_len_name.clear();
    }
    if (!concrete.args.empty()) {
        const std::string spec = specialize_type_app(concrete.type_package, concrete.type_name, concrete.args, arg.loc);
        if (spec.empty()) {
            return false;
        }
        concrete.type_name = spec;
        concrete.type_package.clear();
        concrete.args.clear();
        concrete.is_struct = true;
    }
    const Type type = object_type(concrete.type_name, concrete.indirections, concrete.is_array, concrete.array_len, arg.loc,
                                  false, concrete.type_package, false, concrete.is_enum, false, concrete.optional,
                                  concrete.const_pointee, false, concrete.elem_const);
    if (type.kind == TypeKind::Invalid) {
        return false;
    }
    if (!spell_type(type, out)) {
        error(arg.loc, "type mismatch");
        return false;
    }
    return true;
}

void Checker::substitute_type(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds,
                              std::string &type_name, std::string &type_package, bool &is_struct, bool &is_enum,
                              int &indirections, uint32_t &optional, bool &is_array, int &array_len,
                              std::string &array_len_name, std::vector<TypeArg> &type_args, SourceLoc loc) {
    (void)is_struct;
    if (!array_len_name.empty()) {
        if (const SpecArg *bound = find_arg(params, bounds, array_len_name); bound != nullptr && bound->is_value) {
            if (bound->value <= 0) {
                error(loc, "not supported yet");
            } else {
                array_len = bound->value;
                array_len_name.clear();
                is_array = true;
            }
        }
    }
    auto subst_arg = [&](auto &&self, TypeArg &arg) -> void {
        if (arg.has_value) {
            return;
        }
        if (!arg.array_len_name.empty()) {
            if (const SpecArg *bound = find_arg(params, bounds, arg.array_len_name); bound != nullptr && bound->is_value) {
                arg.array_len = bound->value;
                arg.array_len_name.clear();
                arg.is_array = true;
            }
        }
        for (TypeArg &nested : arg.args) {
            self(self, nested);
        }
        if (!arg.type_package.empty()) {
            return;
        }
        const SpecArg *bound = find_arg(params, bounds, arg.type_name);
        if (bound == nullptr) {
            return;
        }
        if (bound->is_pack) {
            error(arg.loc, "not supported yet");
            return;
        }
        if (bound->is_value) {
            if (arg.indirections != 0 || arg.is_array || !arg.args.empty()) {
                error(arg.loc, "type mismatch");
                return;
            }
            arg.is_value = true;
            arg.has_value = true;
            arg.value = bound->value;
            arg.type_name.clear();
            return;
        }
        const int written = arg.indirections;
        const uint32_t written_opt = arg.optional;
        const uint32_t written_const = arg.const_pointee;
        const bool written_array = arg.is_array;
        const int written_len = arg.array_len;
        if (bound->is_array && (written_array || written > 0)) {
            error(arg.loc, "not supported yet");
            return;
        }
        arg.type_name = bound->type_name;
        arg.type_package = bound->type_package;
        arg.is_struct = false;
        arg.is_enum = bound->is_enum;
        arg.indirections = written + bound->indirections;
        arg.optional = written_opt | (bound->optional << written);
        arg.const_pointee = written_const | (bound->const_pointee << written);
        if (bound->is_array) {
            arg.is_array = true;
            arg.array_len = bound->array_len;
        } else if (written_array) {
            arg.is_array = true;
            arg.array_len = written_len;
        }
    };
    for (TypeArg &arg : type_args) {
        subst_arg(subst_arg, arg);
    }
    if (!type_package.empty()) {
        return;
    }
    const SpecArg *bound = find_arg(params, bounds, type_name);
    if (bound == nullptr) {
        return;
    }
    if (bound->is_pack) {
        error(loc, "not supported yet");
        type_name = "invalid";
        return;
    }
    if (bound->is_value) {
        error(loc, "type mismatch");
        type_name = "invalid";
        return;
    }
    const int written = indirections;
    const uint32_t written_opt = optional;
    const bool written_array = is_array;
    const int written_len = array_len;
    if (bound->is_array && (written_array || written > 0)) {
        error(loc, "not supported yet");
        return;
    }
    type_name = bound->type_name;
    type_package = bound->type_package;
    is_enum = bound->is_enum;
    indirections = written + bound->indirections;
    optional = written_opt | (bound->optional << written);
    if (bound->is_array) {
        is_array = true;
        array_len = bound->array_len;
    } else if (written_array) {
        is_array = true;
        array_len = written_len;
    }
}

void Checker::substitute_expr(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, Expr &expr) {
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const SpecArg *bound = find_arg(params, bounds, node.text); bound != nullptr && bound->is_value) {
                    Expr::Int literal;
                    literal.loc = node.loc;
                    literal.value = static_cast<uint64_t>(static_cast<unsigned>(bound->value));
                    expr.data = std::move(literal);
                }
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                if (node.expr) {
                    substitute_expr(params, bounds, *node.expr);
                }
                Expr *cursor = node.expr.get();
                int stars = 0;
                while (cursor != nullptr) {
                    auto *unary = std::get_if<Expr::Unary>(&cursor->data);
                    if (unary == nullptr || unary->postfix || unary->op != TokenKind::Star || unary->expr == nullptr) {
                        break;
                    }
                    ++stars;
                    cursor = unary->expr.get();
                }
                const auto *named = cursor == nullptr ? nullptr : std::get_if<Expr::Name>(&cursor->data);
                if (named != nullptr) {
                    if (const SpecArg *bound = find_arg(params, bounds, named->text); bound != nullptr && !bound->is_value) {
                        if (bound->is_array) {
                            error(node.loc, "not supported yet");
                        }
                        node.on_type = true;
                        node.type_name = bound->type_name;
                        node.type_package = bound->type_package;
                        node.is_struct = bound->is_struct;
                        node.is_enum = bound->is_enum;
                        node.indirections = stars + bound->indirections;
                        node.optional = bound->optional << stars;
                        node.expr.reset();
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                if (node.expr) {
                    substitute_expr(params, bounds, *node.expr);
                }
                Expr *cursor = node.expr.get();
                int stars = 0;
                while (cursor != nullptr) {
                    auto *unary = std::get_if<Expr::Unary>(&cursor->data);
                    if (unary == nullptr || unary->postfix || unary->op != TokenKind::Star || unary->expr == nullptr) {
                        break;
                    }
                    ++stars;
                    cursor = unary->expr.get();
                }
                const auto *named = cursor == nullptr ? nullptr : std::get_if<Expr::Name>(&cursor->data);
                if (named != nullptr) {
                    if (const SpecArg *bound = find_arg(params, bounds, named->text); bound != nullptr && !bound->is_value) {
                        if (bound->is_array) {
                            error(node.loc, "not supported yet");
                        }
                        node.on_type = true;
                        node.type_name = bound->type_name;
                        node.type_package = bound->type_package;
                        node.is_struct = bound->is_struct;
                        node.is_enum = bound->is_enum;
                        node.indirections = stars + bound->indirections;
                        node.optional = bound->optional << stars;
                        node.canonical.clear();
                        node.expr.reset();
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                if (node.allocator) {
                    substitute_expr(params, bounds, *node.allocator);
                }
                if (node.count) {
                    substitute_expr(params, bounds, *node.count);
                }
                if (node.init) {
                    substitute_expr(params, bounds, *node.init);
                }
                for (TypeArg &arg : node.type_args) {
                    if (!arg.has_value && arg.type_package.empty() && arg.indirections == 0 && !arg.is_array &&
                        arg.args.empty()) {
                        if (const SpecArg *bound = find_arg(params, bounds, arg.type_name);
                            bound != nullptr && bound->is_value) {
                            arg.is_value = true;
                            arg.has_value = true;
                            arg.value = bound->value;
                            arg.type_name.clear();
                            continue;
                        }
                    }
                    std::string len_name = arg.array_len_name;
                    substitute_type(params, bounds, arg.type_name, arg.type_package, arg.is_struct, arg.is_enum,
                                    arg.indirections, arg.optional, arg.is_array, arg.array_len, len_name, arg.args,
                                    arg.loc);
                    arg.array_len_name = std::move(len_name);
                }
                if (node.type_package.empty()) {
                    if (const SpecArg *bound = find_arg(params, bounds, node.type_name);
                        bound != nullptr && !bound->is_value) {
                        node.type_name = bound->type_name;
                        node.type_package = bound->type_package;
                        // `is_struct` means the source wrote `struct Name`. A specialized name is an ordinary type.
                        node.is_struct = false;
                        node.indirections += bound->indirections;
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                if (node.allocator) {
                    substitute_expr(params, bounds, *node.allocator);
                }
                if (node.expr) {
                    substitute_expr(params, bounds, *node.expr);
                }
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.expr) {
                    substitute_expr(params, bounds, *node.expr);
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary> || std::is_same_v<T, Expr::Assign>) {
                if (node.lhs) {
                    substitute_expr(params, bounds, *node.lhs);
                }
                if (node.rhs) {
                    substitute_expr(params, bounds, *node.rhs);
                }
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.callee) {
                    substitute_expr(params, bounds, *node.callee);
                }
                for (TypeArg &arg : node.type_args) {
                    if (!arg.has_value && arg.type_package.empty() && arg.indirections == 0 && !arg.is_array && arg.args.empty()) {
                        if (const SpecArg *bound = find_arg(params, bounds, arg.type_name); bound != nullptr && bound->is_value) {
                            arg.is_value = true;
                            arg.has_value = true;
                            arg.value = bound->value;
                            arg.type_name.clear();
                            continue;
                        }
                    }
                    std::string len_name = arg.array_len_name;
                    substitute_type(params, bounds, arg.type_name, arg.type_package, arg.is_struct, arg.is_enum,
                                    arg.indirections, arg.optional, arg.is_array, arg.array_len, len_name, arg.args, arg.loc);
                    arg.array_len_name = std::move(len_name);
                }
                for (const auto &arg : node.args) {
                    if (arg) {
                        substitute_expr(params, bounds, *arg);
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base) {
                    substitute_expr(params, bounds, *node.base);
                }
                // `Buf[N, T].width` keeps the arguments on the selector. Substitute them as types.
                if (!node.type_args.empty()) {
                    std::string type_name;
                    std::string type_package;
                    bool is_struct = false;
                    bool is_enum = false;
                    int indirections = 0;
                    uint32_t optional = 0;
                    bool is_array = false;
                    int array_len = 0;
                    std::string array_len_name;
                    substitute_type(params, bounds, type_name, type_package, is_struct, is_enum, indirections, optional,
                                    is_array, array_len, array_len_name, node.type_args, node.loc);
                }
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.base) {
                    substitute_expr(params, bounds, *node.base);
                }
                if (node.end) {
                    substitute_expr(params, bounds, *node.end);
                }
                if (node.index) {
                    substitute_expr(params, bounds, *node.index);
                    // `Box[T].from` is an index. A plain type parameter becomes that type's name.
                    if (auto *named = std::get_if<Expr::Name>(&node.index->data)) {
                        if (const SpecArg *bound = find_arg(params, bounds, named->text);
                            bound != nullptr && !bound->is_value && !bound->is_pack && !bound->is_array &&
                            bound->indirections == 0 && bound->type_package.empty() && !bound->type_name.empty()) {
                            named->text = bound->type_name;
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.expr) {
                    substitute_expr(params, bounds, *node.expr);
                }
                if (node.is_fn_ptr) {
                    auto subst_spelling = [&](std::string &type_name, std::string &type_package, bool &is_struct,
                                              bool &is_enum, int &indirections, uint32_t &optional) {
                        bool is_array = false;
                        int array_len = 0;
                        std::string array_len_name;
                        std::vector<TypeArg> type_args;
                        substitute_type(params, bounds, type_name, type_package, is_struct, is_enum, indirections,
                                        optional, is_array, array_len, array_len_name, type_args, node.loc);
                    };
                    auto subst_param = [&](auto &&self, FnParam &param) -> void {
                        if (param.is_fn_ptr) {
                            bool is_array = false;
                            int array_len = 0;
                            std::string array_len_name;
                            substitute_type(params, bounds, param.type_name, param.type_package, param.is_struct,
                                            param.is_enum, param.ret_indirections, param.ret_optional, is_array, array_len,
                                            array_len_name, param.type_args, node.loc);
                            for (FnParam &inner : param.params) {
                                self(self, inner);
                            }
                            for (FnParam &inner : param.results) {
                                self(self, inner);
                            }
                            return;
                        }
                        std::string array_len_name;
                        substitute_type(params, bounds, param.type_name, param.type_package, param.is_struct, param.is_enum,
                                        param.indirections, param.optional, param.is_array, param.array_len, array_len_name,
                                        param.type_args, node.loc);
                    };
                    subst_spelling(node.type_name, node.type_package, node.is_struct, node.is_enum, node.ret_indirections,
                                   node.ret_optional);
                    for (FnParam &param : node.fn_params) {
                        subst_param(subst_param, param);
                    }
                    for (FnParam &part : node.fn_results) {
                        subst_param(subst_param, part);
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                if (node.array_literal) {
                    bool is_array = node.elem_is_array;
                    int array_len = 0;
                    std::string array_len_name;
                    substitute_type(params, bounds, node.type_name, node.type_package, node.is_struct, node.is_enum,
                                    node.indirections, node.optional, is_array, array_len, array_len_name, node.type_args,
                                    node.loc);
                    node.elem_is_array = is_array || !array_len_name.empty();
                }
                for (Expr::FieldInit &field : node.fields) {
                    if (field.value) {
                        substitute_expr(params, bounds, *field.value);
                    }
                }
                for (const auto &element : node.elements) {
                    if (element) {
                        substitute_expr(params, bounds, *element);
                    }
                }
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (node.decl) {
                    substitute_function(*node.decl, params, bounds);
                }
            }
        },
        expr.data);
}

void Checker::substitute_stmt(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, Stmt &stmt) {
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (child) {
                        substitute_stmt(params, bounds, *child);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (node.value) {
                    substitute_expr(params, bounds, *node.value);
                }
                for (const auto &extra : node.extra) {
                    if (extra) {
                        substitute_expr(params, bounds, *extra);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    substitute_expr(params, bounds, *node.expr);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call) {
                    substitute_expr(params, bounds, *node.call);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.cond) {
                    substitute_expr(params, bounds, *node.cond);
                }
                if (node.then_arm) {
                    substitute_stmt(params, bounds, *node.then_arm);
                }
                if (node.else_arm) {
                    substitute_stmt(params, bounds, *node.else_arm);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                if (node.cond) {
                    substitute_expr(params, bounds, *node.cond);
                }
                if (node.body) {
                    substitute_stmt(params, bounds, *node.body);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.init) {
                    substitute_stmt(params, bounds, *node.init);
                }
                if (node.cond) {
                    substitute_expr(params, bounds, *node.cond);
                }
                if (node.step) {
                    substitute_expr(params, bounds, *node.step);
                }
                if (node.body) {
                    substitute_stmt(params, bounds, *node.body);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                if (node.cond) {
                    substitute_expr(params, bounds, *node.cond);
                }
                if (node.body) {
                    substitute_stmt(params, bounds, *node.body);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                if (node.value) {
                    substitute_expr(params, bounds, *node.value);
                }
                if (node.stmt) {
                    substitute_stmt(params, bounds, *node.stmt);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (VarDecl &var : node.vars) {
                    substitute_type(params, bounds, var.type_name, var.type_package, var.is_struct, var.is_enum,
                                    var.indirections, var.optional, var.is_array, var.array_len, var.array_len_name,
                                    var.type_args, var.loc);
                    if (var.init) {
                        substitute_expr(params, bounds, *var.init);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    substitute_function(*node.decl, params, bounds);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value) {
                    substitute_expr(params, bounds, *node.value);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                if (node.cond) {
                    substitute_expr(params, bounds, *node.cond);
                }
            }
        },
        stmt.data);
}

void Checker::substitute_function(FunctionDecl &fn, const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds) {
    auto bare_pack = [&](const std::string &type_name, const std::string &type_package, int indirections, bool is_array,
                         const std::vector<TypeArg> &type_args) -> const SpecArg * {
        if (!type_package.empty() || indirections != 0 || is_array || !type_args.empty()) {
            return nullptr;
        }
        const SpecArg *bound = find_arg(params, bounds, type_name);
        if (bound == nullptr || !bound->is_pack) {
            return nullptr;
        }
        return bound;
    };
    auto subst_fn_param = [&](auto &&self, FnParam &param, SourceLoc loc) -> void {
        if (param.is_fn_ptr) {
            bool is_array = false;
            int array_len = 0;
            std::string array_len_name;
            substitute_type(params, bounds, param.type_name, param.type_package, param.is_struct, param.is_enum,
                            param.ret_indirections, param.ret_optional, is_array, array_len, array_len_name,
                            param.type_args, loc);
            for (FnParam &inner : param.params) {
                self(self, inner, loc);
            }
            for (FnParam &inner : param.results) {
                self(self, inner, loc);
            }
            return;
        }
        std::string array_len_name;
        substitute_type(params, bounds, param.type_name, param.type_package, param.is_struct, param.is_enum,
                        param.indirections, param.optional, param.is_array, param.array_len, array_len_name,
                        param.type_args, loc);
    };
    for (Param &param : fn.params) {
        if (const SpecArg *pack = bare_pack(param.type_name, param.type_package, param.indirections, param.is_array,
                                            param.type_args)) {
            param.is_tuple = true;
            param.tuple_parts.clear();
            for (const SpecArg &part : pack->parts) {
                param.tuple_parts.push_back(slot_from_spec(part));
            }
            param.type_name.clear();
            continue;
        }
        substitute_type(params, bounds, param.type_name, param.type_package, param.is_struct, param.is_enum,
                        param.indirections, param.optional, param.is_array, param.array_len, param.array_len_name,
                        param.type_args, param.loc);
        for (FnParam &inner : param.fn_params) {
            subst_fn_param(subst_fn_param, inner, param.loc);
        }
        for (FnParam &inner : param.fn_results) {
            subst_fn_param(subst_fn_param, inner, param.loc);
        }
    }
    if (fn.results.size() == 1) {
        ResultSlot &slot = fn.results.front();
        if (const SpecArg *pack =
                bare_pack(slot.type_name, slot.type_package, slot.indirections, slot.is_array, slot.type_args)) {
            std::vector<ResultSlot> expanded;
            expanded.reserve(pack->parts.size());
            for (const SpecArg &part : pack->parts) {
                expanded.push_back(slot_from_spec(part));
            }
            fn.results = std::move(expanded);
            fn.result_typeof.clear();
        }
    }
    for (ResultSlot &slot : fn.results) {
        substitute_type(params, bounds, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections,
                        slot.optional, slot.is_array, slot.array_len, slot.array_len_name, slot.type_args, fn.loc);
        for (FnParam &inner : slot.fn_params) {
            subst_fn_param(subst_fn_param, inner, fn.loc);
        }
        for (FnParam &inner : slot.fn_results) {
            subst_fn_param(subst_fn_param, inner, fn.loc);
        }
    }
    if (!fn.results.empty()) {
        const ResultSlot &first = fn.results.front();
        fn.return_type = first.type_name;
        fn.type_package = first.type_package;
        fn.is_struct = first.is_struct;
        fn.is_enum = first.is_enum;
        fn.indirections = first.indirections;
        fn.optional = first.optional;
        fn.const_pointee = first.const_pointee;
    }
    for (const auto &expr : fn.result_typeof) {
        if (expr) {
            substitute_expr(params, bounds, *expr);
        }
    }
    if (fn.body) {
        substitute_stmt(params, bounds, *fn.body);
    }
}

bool Checker::resolve_result_typeof(FunctionDecl &fn) {
    bool any = false;
    for (const auto &expr : fn.result_typeof) {
        if (expr) {
            any = true;
        }
    }
    if (!any) {
        return true;
    }
    push();
    for (const Param &param : fn.params) {
        if (param.name.empty()) {
            continue;
        }
        Symbol symbol;
        symbol.name = param.name;
        symbol.type = type_of_param(param);
        symbol.is_auto = true;
        symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
        declare(std::move(symbol), param.loc);
    }
    bool ok = true;
    for (std::size_t i = 0; i < fn.result_typeof.size() && i < fn.results.size(); ++i) {
        const Expr *expr = fn.result_typeof[i].get();
        if (expr == nullptr) {
            continue;
        }
        const auto *node = std::get_if<Expr::Typeof>(&expr->data);
        if (node == nullptr) {
            error(fn.loc, "type mismatch");
            ok = false;
            continue;
        }
        const Type type = operand_type(node->on_type, node->type_name, node->type_package, node->is_struct, node->is_enum,
                                       node->indirections, node->optional, node->expr.get(), node->loc,
                                       node->const_pointee, node->is_array, node->array_len, node->elem_const);
        if (type.kind == TypeKind::Invalid) {
            ok = false;
            continue;
        }
        if (type.kind == TypeKind::Function || type.kind == TypeKind::Tuple || mentions_trait(type)) {
            error(node->loc, "not supported yet");
            ok = false;
            continue;
        }
        SpecArg spelled;
        if (!spell_type(type, spelled)) {
            error(node->loc, "type mismatch");
            ok = false;
            continue;
        }
        apply_spec(fn.results[i], spelled);
    }
    pop();
    if (!ok) {
        return false;
    }
    fn.result_typeof.clear();
    if (!fn.results.empty()) {
        const ResultSlot &first = fn.results.front();
        fn.return_type = first.type_name;
        fn.type_package = first.type_package;
        fn.is_struct = first.is_struct;
        fn.is_enum = first.is_enum;
        fn.indirections = first.indirections;
        fn.optional = first.optional;
        fn.const_pointee = first.const_pointee;
    }
    return true;
}

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
            require_satisfies(struct_id, trait_id, bound.indirections == 1, loc);
        }
        bounds.push_back(std::move(bound));
    }
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
                        field.type_args, field.loc);
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
        Type field_type = field.is_fn_ptr
                              ? type_of_fn_ptr(field.type_name, field.type_package, field.is_struct, field.is_enum,
                                               field.ret_indirections, field.indirections, field.fn_params, field.fn_variadic,
                                               field.fn_results, field.loc, field.ret_optional, field.ret_const_pointee)
                              : object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                            false, field.type_package, field.is_struct, field.is_enum, false,
                                            field.optional, field.const_pointee);
        structs_[static_cast<std::size_t>(id)].fields.push_back(
            StructInfo::Field{field.name, std::move(field_type), field.loc, field.exported});
    }
    structs_[static_cast<std::size_t>(id)].complete = true;
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
    if (call.args.size() != templ.params.size()) {
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
        const Type &got = arg_types[param_i];
        if (param.is_receiver || got.kind == TypeKind::Invalid) {
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
        require_satisfies(struct_id, trait_id, through_pointer, call.loc);
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
            error(call.loc, "not supported yet");
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
    const std::vector<SpecArg> &bounds = *inferred;
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
