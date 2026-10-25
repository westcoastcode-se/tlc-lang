// Templates, substitution, and the spelling of a generic type.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/generic.hpp"

namespace tlc {

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
    if (lhs.is_text || rhs.is_text) {
        return lhs.is_text && rhs.is_text && lhs.text == rhs.text;
    }
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

ResultSlot slot_from_spec(const SpecArg &arg) {
    ResultSlot slot;
    apply_spec(slot, arg);
    return slot;
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

void append_shaped_name(std::string &out, const std::string &package, const std::string &name,
                        const std::vector<TypeParam> &params) {
    if (package.empty()) {
        const int slot = param_index(params, name);
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
void append_mixed_fn_results(std::string &out, const std::vector<FnParam> &results, const std::vector<TypeParam> &params);

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

void append_mixed_fn_results(std::string &out, const std::vector<FnParam> &results, const std::vector<TypeParam> &params) {
    out.push_back('(');
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_fn_shape(out, results[i], params);
    }
    out.push_back(')');
}

void append_fn_shape(std::string &out, const FnParam &param, const std::vector<TypeParam> &params) {
    if (param.is_tuple) {
        out.push_back('(');
        for (std::size_t i = 0; i < param.results.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            append_fn_shape(out, param.results[i], params);
        }
        out.push_back(')');
        return;
    }
    if (param.is_array) {
        out.push_back('[');
        if (param.array_len >= 0) {
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
    if (param.results.size() == 1 && param.results.front().is_tuple) {
        out += "((";
        const std::vector<FnParam> &parts = param.results.front().results;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            append_fn_shape(out, parts[i], params);
        }
        out += "))";
    } else if (param.results.size() == 1 && param.results.front().is_fn_ptr) {
        out.push_back('(');
        append_fn_shape(out, param.results.front(), params);
        out.push_back(')');
    } else if (param.results.size() > 1) {
        for (const FnParam &part : param.results) {
            if (part.is_tuple) {
                append_mixed_fn_results(out, param.results, params);
                break;
            }
        }
    }
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
    if (slot.is_tuple) {
        out.push_back('(');
        for (std::size_t i = 0; i < slot.tuple_parts.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            append_result_shape(out, slot.tuple_parts[i], params);
        }
        out.push_back(')');
        return;
    }
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
    if (slot.fn_results.size() == 1 && slot.fn_results.front().is_tuple) {
        out += "((";
        const std::vector<FnParam> &parts = slot.fn_results.front().results;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            append_fn_shape(out, parts[i], params);
        }
        out += "))";
    } else if (slot.fn_results.size() == 1 && slot.fn_results.front().is_fn_ptr) {
        out.push_back('(');
        append_fn_shape(out, slot.fn_results.front(), params);
        out.push_back(')');
    } else if (slot.fn_results.size() > 1) {
        for (const FnParam &part : slot.fn_results) {
            if (part.is_tuple) {
                append_mixed_fn_results(out, slot.fn_results, params);
                break;
            }
        }
    }
}

void append_param_shape(std::string &out, const Param &param, const std::vector<TypeParam> &params) {
    // `comptime T` and `T` are different overloads. The marker keeps their keys apart.
    if (param.is_comptime) {
        out += "comptime.";
    }
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
    if (param.fn_results.size() == 1 && param.fn_results.front().is_tuple) {
        out += "((";
        const std::vector<FnParam> &parts = param.fn_results.front().results;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            append_fn_shape(out, parts[i], params);
        }
        out += "))";
    } else if (param.fn_results.size() == 1 && param.fn_results.front().is_fn_ptr) {
        out.push_back('(');
        append_fn_shape(out, param.fn_results.front(), params);
        out.push_back(')');
    } else if (param.fn_results.size() > 1) {
        for (const FnParam &part : param.fn_results) {
            if (part.is_tuple) {
                append_mixed_fn_results(out, param.fn_results, params);
                break;
            }
        }
    }
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
    // Look through pointers only to find an array. A plain `*T` stays on `cursor` and is counted below.
    const Type *look = cursor;
    int outer = 0;
    while (look->kind == TypeKind::Pointer && look->pointee != nullptr) {
        ++outer;
        look = look->pointee.get();
        if (outer > 31) {
            return false;
        }
    }
    if (look->kind == TypeKind::Array) {
        const Type *layer = &type;
        bool saw_array = false;
        while (layer != nullptr && (layer->kind == TypeKind::Pointer || layer->kind == TypeKind::Array)) {
            if (layer->kind == TypeKind::Pointer) {
                if (layer->pointee == nullptr) {
                    return false;
                }
                // Pointers after the last array stay in `indirections`, as they always have.
                if (saw_array) {
                    break;
                }
                TypeShell shell;
                shell.kind = TypeShell::Kind::Pointer;
                shell.optional = layer->optional;
                shell.const_pointee = layer->pointee->is_const;
                out.shells.push_back(shell);
                layer = layer->pointee.get();
                continue;
            }
            if (layer->element == nullptr || (layer->length == 0)) {
                return false;
            }
            saw_array = true;
            TypeShell shell;
            if (layer->length < 0) {
                shell.kind = TypeShell::Kind::Slice;
            } else {
                shell.kind = TypeShell::Kind::Array;
                shell.length = layer->length;
            }
            out.shells.push_back(shell);
            layer = layer->element.get();
        }
        if (!saw_array) {
            return false;
        }
        out.is_array = true;
        out.array_len = -1;
        for (const TypeShell &shell : out.shells) {
            if (shell.kind == TypeShell::Kind::Slice) {
                out.array_len = -1;
                break;
            }
            if (shell.kind == TypeShell::Kind::Array) {
                out.array_len = shell.length;
                break;
            }
        }
        if (out.shells.size() >= 2 && out.shells[1].kind == TypeShell::Kind::Slice && out.shells.size() == 2) {
            out.elem_slice = true;
        }
        if (layer != nullptr && layer->is_const && layer->kind != TypeKind::Pointer) {
            out.elem_const = true;
        }
        cursor = layer;
        outer = 0;
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
    case TypeKind::Tuple: {
        // `T` is the whole tuple. A pointer to a tuple, or a tuple inside a tuple, stays rejected.
        if (depth != 0 || out.is_array || cursor->results.size() < 2) {
            return false;
        }
        std::vector<SpecArg> parts;
        parts.reserve(cursor->results.size());
        for (const Type &part : cursor->results) {
            SpecArg spelled;
            if (part.kind == TypeKind::Tuple || !spell_type(part, spelled) || spelled.is_pack) {
                return false;
            }
            parts.push_back(std::move(spelled));
        }
        out = pack_bound(std::move(parts));
        return out.is_pack;
    }
    default:
        return false;
    }
}

std::string Checker::mangle_spec(const std::string &base, const std::vector<SpecArg> &args) const {
    std::string name = base + ".spec";
    auto append = [&](auto &&self, const SpecArg &arg) -> void {
        name += '.';
        if (arg.is_pack) {
            name += "tuple";
            for (const SpecArg &part : arg.parts) {
                self(self, part);
            }
            return;
        }
        if (arg.is_value) {
            name += std::to_string(arg.value);
            return;
        }
        if (!arg.shells.empty()) {
            for (const TypeShell &shell : arg.shells) {
                if (shell.kind == TypeShell::Kind::Slice) {
                    name += "slice.";
                } else if (shell.kind == TypeShell::Kind::Array) {
                    name += "arr.";
                    name += std::to_string(shell.length);
                    name += '.';
                } else {
                    name += shell.optional ? "opt." : "ptr.";
                    if (shell.const_pointee) {
                        name += "const.";
                    }
                }
            }
        } else if (arg.is_array) {
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

const TraitDecl *Checker::find_trait_template(const std::string &name, uint32_t &owner) const {
    if (name.empty()) {
        return nullptr;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        const auto file = trait_templates_.find(id);
        if (file == trait_templates_.end()) {
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

const TypedefDecl *Checker::find_tuple_alias_template(const std::string &name, uint32_t &owner) const {
    if (name.empty()) {
        return nullptr;
    }
    for (const uint32_t id : files_in_package(file_id_)) {
        const auto file = typedef_templates_.find(id);
        if (file == typedef_templates_.end()) {
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
        // `extern(abi="c") const Box[T]::limit` is one C name. It is not specialized.
        if (src->extern_c) {
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

void Checker::disambiguate_bounds(uint32_t owner, std::vector<SpecArg> &bounds) {
    for (SpecArg &bound : bounds) {
        if (bound.is_pack) {
            disambiguate_bounds(owner, bound.parts);
            continue;
        }
        if (bound.struct_id < 0 || bound.struct_id >= static_cast<int>(structs_.size())) {
            continue;
        }
        const StructInfo &info = structs_[static_cast<std::size_t>(bound.struct_id)];
        if (info.name.empty()) {
            continue;
        }
        const int existing = find_tag(owner, info.name);
        if (existing == bound.struct_id) {
            bound.type_name = info.name;
            bound.type_package.clear();
            continue;
        }
        bool ambiguous = existing >= 0;
        if (!ambiguous) {
            for (const StructInfo &other : structs_) {
                if (other.name == info.name && other.file_id != info.file_id) {
                    ambiguous = true;
                    break;
                }
            }
        }
        if (!ambiguous) {
            bound.type_name = info.name;
            bound.type_package.clear();
            continue;
        }
        // Codegen reads this spelling back in `file_alias`. The bare name would select the other struct.
        bound.type_name = info.name + ".file." + std::to_string(info.file_id);
        bound.type_package.clear();
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
    fold_shells(concrete.shells, arg.loc);
    const Type type = object_type(concrete.type_name, concrete.indirections, concrete.is_array, concrete.array_len, arg.loc,
                                  false, concrete.type_package, false, concrete.is_enum, false, concrete.optional,
                                  concrete.const_pointee, false, concrete.elem_const, false, concrete.shells);
    if (type.kind == TypeKind::Invalid) {
        return false;
    }
    if (!spell_type(type, out)) {
        error(arg.loc, "type mismatch");
        return false;
    }
    return true;
}

void Checker::substitute_slot(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, ResultSlot &slot,
                              SourceLoc loc) {
    if (slot.is_tuple) {
        for (ResultSlot &part : slot.tuple_parts) {
            substitute_slot(params, bounds, part, loc);
        }
        return;
    }
    substitute_type(params, bounds, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections,
                    slot.optional, slot.is_array, slot.array_len, slot.array_len_name, slot.type_args, loc, &slot.shells,
                    &slot.elem_const, &slot.const_pointee);
}

void Checker::substitute_type(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds,
                              std::string &type_name, std::string &type_package, bool &is_struct, bool &is_enum,
                              int &indirections, uint32_t &optional, bool &is_array, int &array_len,
                              std::string &array_len_name, std::vector<TypeArg> &type_args, SourceLoc loc,
                              std::vector<TypeShell> *shells, bool *elem_const, uint32_t *const_pointee) {
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
    if (shells != nullptr) {
        for (TypeShell &shell : *shells) {
            if (shell.kind != TypeShell::Kind::Array || shell.length_name.empty()) {
                continue;
            }
            if (const SpecArg *bound = find_arg(params, bounds, shell.length_name); bound != nullptr && bound->is_value) {
                if (bound->value <= 0) {
                    error(loc, "not supported yet");
                } else {
                    shell.length = bound->value;
                    shell.length_name.clear();
                }
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
        if (bound->is_text) {
            error(arg.loc, "type mismatch");
            return;
        }
        const int written = arg.indirections;
        const uint32_t written_opt = arg.optional;
        const uint32_t written_const = arg.const_pointee;
        const bool written_array = arg.is_array;
        const int written_len = arg.array_len;
        if (bound->is_array && written_array) {
            error(arg.loc, "not supported yet");
            return;
        }
        // `*T` when `T` is `[4]i32` or `[]i32` is a pointer shell around that type.
        if (bound->is_array && written > 0) {
            if (bound->shells.empty()) {
                error(arg.loc, "not supported yet");
                return;
            }
            std::vector<TypeShell> wrapped;
            wrapped.reserve(static_cast<std::size_t>(written) + bound->shells.size());
            for (int star = 0; star < written; ++star) {
                TypeShell shell;
                shell.kind = TypeShell::Kind::Pointer;
                shell.optional = (written_opt & (1u << star)) != 0;
                shell.const_pointee = (written_const & (1u << star)) != 0;
                wrapped.push_back(shell);
            }
            wrapped.insert(wrapped.end(), bound->shells.begin(), bound->shells.end());
            arg.shells = std::move(wrapped);
            arg.type_name = bound->type_name;
            arg.type_package = bound->type_package;
            arg.is_struct = false;
            arg.is_enum = bound->is_enum;
            arg.indirections = bound->indirections;
            arg.optional = bound->optional;
            arg.const_pointee = bound->const_pointee;
            arg.is_array = false;
            arg.array_len = 0;
            arg.array_len_name.clear();
            arg.elem_const = bound->elem_const;
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
    if (bound->is_value || bound->is_text) {
        error(loc, "type mismatch");
        type_name = "invalid";
        return;
    }
    const int written = indirections;
    const uint32_t written_opt = optional;
    const uint32_t written_const = const_pointee != nullptr ? *const_pointee : 0;
    const bool written_array = is_array;
    const int written_len = array_len;
    if (bound->is_array && written_array) {
        error(loc, "not supported yet");
        return;
    }
    // `*T` when `T` is `[4]i32` or `[]i32`. The stars become pointer shells. The leaf stays the array.
    if (bound->is_array && written > 0) {
        if (shells == nullptr || bound->shells.empty()) {
            error(loc, "not supported yet");
            return;
        }
        std::vector<TypeShell> wrapped;
        wrapped.reserve(static_cast<std::size_t>(written) + bound->shells.size());
        for (int star = 0; star < written; ++star) {
            TypeShell shell;
            shell.kind = TypeShell::Kind::Pointer;
            shell.optional = (written_opt & (1u << star)) != 0;
            shell.const_pointee = (written_const & (1u << star)) != 0;
            wrapped.push_back(shell);
        }
        wrapped.insert(wrapped.end(), bound->shells.begin(), bound->shells.end());
        *shells = std::move(wrapped);
        type_name = bound->type_name;
        type_package = bound->type_package;
        is_enum = bound->is_enum;
        indirections = bound->indirections;
        optional = bound->optional;
        if (const_pointee != nullptr) {
            *const_pointee = bound->const_pointee;
        }
        is_array = false;
        array_len = 0;
        array_len_name.clear();
        if (elem_const != nullptr) {
            *elem_const = bound->elem_const;
        }
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
    if (shells != nullptr && !bound->shells.empty() && written == 0 && !written_array) {
        *shells = bound->shells;
        // `const` sits on the leaf. A shell list does not carry it.
        if (elem_const != nullptr) {
            *elem_const = bound->elem_const;
        }
    }
}

void Checker::substitute_expr(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, Expr &expr) {
    std::visit(
        [&](auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const SpecArg *bound = find_arg(params, bounds, node.text);
                    bound != nullptr && (bound->is_value || bound->is_text || bound->is_enum_value)) {
                    const SourceLoc loc = node.loc;
                    if (bound->is_enum_value) {
                        // The copy compares and returns the enum. An integer would be `type mismatch`.
                        auto named = [&](const std::string &text) {
                            auto item = std::make_unique<Expr>();
                            Expr::Name name;
                            name.loc = loc;
                            name.text = text;
                            item->data = std::move(name);
                            return item;
                        };
                        std::unique_ptr<Expr> base = named(bound->type_package.empty() ? bound->type_name : bound->type_package);
                        if (!bound->type_package.empty()) {
                            auto item = std::make_unique<Expr>();
                            Expr::Selector type;
                            type.loc = loc;
                            type.base = std::move(base);
                            type.field = bound->type_name;
                            item->data = std::move(type);
                            base = std::move(item);
                        }
                        Expr::Selector selector;
                        selector.loc = loc;
                        selector.base = std::move(base);
                        selector.field = bound->enum_member;
                        expr.data = std::move(selector);
                        return;
                    }
                    if (bound->is_text) {
                        Expr::String text;
                        text.loc = loc;
                        text.text = bound->text;
                        expr.data = std::move(text);
                        return;
                    }
                    const int value = bound->value;
                    if (value < 0) {
                        if (value == -2147483647 - 1) {
                            error(loc, "not supported yet");
                            return;
                        }
                        Expr::Int inner;
                        inner.loc = loc;
                        inner.value = static_cast<uint64_t>(-value);
                        auto literal = std::make_unique<Expr>();
                        literal->data = std::move(inner);
                        Expr::Unary unary;
                        unary.loc = loc;
                        unary.op = TokenKind::Minus;
                        unary.expr = std::move(literal);
                        expr.data = std::move(unary);
                        return;
                    }
                    Expr::Int literal;
                    literal.loc = loc;
                    literal.value = static_cast<uint64_t>(value);
                    expr.data = std::move(literal);
                    return;
                }
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                // `sizeof(T)` is parsed as a type. The parameter is replaced before the value is folded.
                if (node.on_type) {
                    if (const SpecArg *bound = find_arg(params, bounds, node.type_name);
                        bound != nullptr && !bound->is_value && !bound->is_text) {
                        if (bound->is_array || node.is_array || (!bound->shells.empty() && node.indirections != 0)) {
                            error(node.loc, "not supported yet");
                        }
                        const int stars = node.indirections;
                        node.type_name = bound->type_name;
                        node.type_package = bound->type_package;
                        node.is_struct = bound->is_struct;
                        node.is_enum = bound->is_enum;
                        node.indirections = stars + bound->indirections;
                        node.optional = bound->optional << static_cast<unsigned>(stars);
                        node.const_pointee = bound->const_pointee << static_cast<unsigned>(stars);
                        node.is_array = bound->is_array;
                        node.array_len = bound->array_len;
                        node.elem_const = bound->elem_const;
                        node.elem_slice = bound->elem_slice;
                        node.shells = bound->shells;
                    }
                }
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
                    if (const SpecArg *bound = find_arg(params, bounds, named->text); bound != nullptr && !bound->is_value && !bound->is_text) {
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
                    if (const SpecArg *bound = find_arg(params, bounds, named->text); bound != nullptr && !bound->is_value && !bound->is_text) {
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
                        bound != nullptr && !bound->is_value && !bound->is_text) {
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
                            bound != nullptr && !bound->is_value && !bound->is_text && !bound->is_pack && !bound->is_array &&
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
                } else {
                    // `?*T(value)`. A function-pointer cast is handled above.
                    bool is_array = false;
                    int array_len = 0;
                    std::string array_len_name;
                    uint32_t const_pointee = node.const_pointee;
                    substitute_type(params, bounds, node.type_name, node.type_package, node.is_struct, node.is_enum,
                                    node.indirections, node.optional, is_array, array_len, array_len_name,
                                    node.type_args, node.loc, nullptr, nullptr, &const_pointee);
                    node.const_pointee = const_pointee;
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
                if (node.each_expr) {
                    substitute_expr(params, bounds, *node.each_expr);
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
                                    var.type_args, var.loc, &var.shells, &var.elem_const, &var.const_pointee);
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
    // `return args` of an empty pack is `return;`. The name is not a value.
    const auto drop_empty_returns = [](auto &&self, Stmt &stmt, const std::vector<std::string> &names) -> void {
        std::visit(
            [&](auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Stmt::Block>) {
                    for (const auto &child : node.statements) {
                        if (child) {
                            self(self, *child, names);
                        }
                    }
                } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                    if (node.extra.empty() && node.value != nullptr) {
                        const auto *name = std::get_if<Expr::Name>(&node.value->data);
                        if (name != nullptr) {
                            for (const std::string &erased_name : names) {
                                if (erased_name == name->text) {
                                    node.value.reset();
                                    break;
                                }
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<T, Stmt::If>) {
                    if (node.then_arm) {
                        self(self, *node.then_arm, names);
                    }
                    if (node.else_arm) {
                        self(self, *node.else_arm, names);
                    }
                } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile> ||
                                     std::is_same_v<T, Stmt::Switch>) {
                    if (node.body) {
                        self(self, *node.body, names);
                    }
                } else if constexpr (std::is_same_v<T, Stmt::For>) {
                    if (node.init) {
                        self(self, *node.init, names);
                    }
                    if (node.body) {
                        self(self, *node.body, names);
                    }
                } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                    if (node.stmt) {
                        self(self, *node.stmt, names);
                    }
                }
            },
            stmt.data);
    };
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
        if (param.is_tuple) {
            for (FnParam &inner : param.results) {
                self(self, inner, loc);
            }
            return;
        }
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
    // `(P1, P2)` is one parameter. Each element is substituted the same way as a result.
    auto subst_slot = [&](auto &&self, ResultSlot &slot, SourceLoc loc) -> void {
        if (slot.is_tuple) {
            for (ResultSlot &part : slot.tuple_parts) {
                self(self, part, loc);
            }
            return;
        }
        substitute_type(params, bounds, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.indirections,
                        slot.optional, slot.is_array, slot.array_len, slot.array_len_name, slot.type_args, loc,
                        &slot.shells, &slot.elem_const, &slot.const_pointee);
        for (FnParam &inner : slot.fn_params) {
            subst_fn_param(subst_fn_param, inner, loc);
        }
        for (FnParam &inner : slot.fn_results) {
            subst_fn_param(subst_fn_param, inner, loc);
        }
    };
    std::vector<Param> kept;
    std::vector<std::string> erased;
    kept.reserve(fn.params.size());
    for (Param &param : fn.params) {
        if (const SpecArg *pack = bare_pack(param.type_name, param.type_package, param.indirections, param.is_array,
                                            param.type_args)) {
            // An empty pack is not passed. `keep()` and `head(40)` omit this parameter.
            if (pack->parts.empty()) {
                if (!param.name.empty()) {
                    erased.push_back(param.name);
                }
                continue;
            }
            param.is_tuple = true;
            param.tuple_parts.clear();
            for (const SpecArg &part : pack->parts) {
                param.tuple_parts.push_back(slot_from_spec(part));
            }
            param.type_name.clear();
            kept.push_back(std::move(param));
            continue;
        }
        substitute_type(params, bounds, param.type_name, param.type_package, param.is_struct, param.is_enum,
                        param.indirections, param.optional, param.is_array, param.array_len, param.array_len_name,
                        param.type_args, param.loc, &param.shells, &param.elem_const, &param.const_pointee);
        for (ResultSlot &part : param.tuple_parts) {
            subst_slot(subst_slot, part, param.loc);
        }
        for (FnParam &inner : param.fn_params) {
            subst_fn_param(subst_fn_param, inner, param.loc);
        }
        for (FnParam &inner : param.fn_results) {
            subst_fn_param(subst_fn_param, inner, param.loc);
        }
        kept.push_back(std::move(param));
    }
    fn.params = std::move(kept);
    bool empty_result = false;
    if (fn.results.size() == 1) {
        ResultSlot &slot = fn.results.front();
        if (const SpecArg *pack =
                bare_pack(slot.type_name, slot.type_package, slot.indirections, slot.is_array, slot.type_args)) {
            if (pack->parts.empty()) {
                // No parts. The result is `void`, and `return args` returns nothing.
                empty_result = true;
                fn.results.clear();
                fn.result_typeof.clear();
                fn.return_type = "void";
                fn.type_package.clear();
                fn.is_struct = false;
                fn.is_enum = false;
                fn.indirections = 0;
                fn.optional = 0;
                fn.const_pointee = 0;
            } else {
                std::vector<ResultSlot> expanded;
                expanded.reserve(pack->parts.size());
                for (const SpecArg &part : pack->parts) {
                    expanded.push_back(slot_from_spec(part));
                }
                fn.results = std::move(expanded);
                fn.result_typeof.clear();
            }
        }
    }
    for (ResultSlot &slot : fn.results) {
        subst_slot(subst_slot, slot, fn.loc);
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
        if (empty_result) {
            drop_empty_returns(drop_empty_returns, *fn.body, erased);
        }
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
        symbol.comptime_param = param.is_comptime;
        symbol.fixed_receiver = param.is_receiver && param.indirections > 0;
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
                                       node->const_pointee, node->is_array, node->array_len, node->elem_const,
                                       node->elem_slice);
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

} // namespace tlc
