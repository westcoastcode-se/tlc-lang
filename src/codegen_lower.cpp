// Symbol tables, type lowering, and struct, union, and enum layout.
#include "gen.hpp"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tlc {

const Binding *CodeGen::lookup(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    const auto file = files_.find(current_file_);
    if (file != files_.end()) {
        const auto function = file->second.functions.find(name);
        if (function != file->second.functions.end()) {
            return &function->second;
        }
    }
    for (const uint32_t id : package_files(current_file_)) {
        if (id == current_file_) {
            continue;
        }
        const auto other = files_.find(id);
        if (other == files_.end()) {
            continue;
        }
        const auto sibling = other->second.functions.find(name);
        if (sibling == other->second.functions.end() || sibling->second.is_static) {
            continue;
        }
        return &sibling->second;
    }
    return nullptr;
}

std::vector<uint32_t> CodeGen::package_files(uint32_t file) const {
    std::vector<uint32_t> ids;
    ids.push_back(file);
    const PackageNode *self = find_node(file);
    if (self == nullptr || self->package_key.empty() || graph_ == nullptr) {
        return ids;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.package_key == self->package_key && node.file_id != file) {
            ids.push_back(node.file_id);
        }
    }
    return ids;
}

const Binding *CodeGen::binding_for_decl(const FunctionDecl *decl) const {
    if (decl == nullptr) {
        return nullptr;
    }
    const auto local = local_fns_.find(decl);
    if (local != local_fns_.end()) {
        return &local->second;
    }
    const auto key = decl_keys_.find(decl);
    if (key == decl_keys_.end()) {
        return nullptr;
    }
    const auto file = files_.find(key->second.file_id);
    if (file == files_.end()) {
        return nullptr;
    }
    const auto found = file->second.functions.find(key->second.key);
    if (found == file->second.functions.end()) {
        return nullptr;
    }
    return &found->second;
}

const Binding *CodeGen::visible(uint32_t file, const std::string &name, bool function) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = files_.find(id);
        if (found == files_.end()) {
            continue;
        }
        const auto &table = function ? found->second.functions : found->second.globals;
        const auto it = table.find(name);
        if (it == table.end() || it->second.is_static) {
            continue;
        }
        return &it->second;
    }
    return nullptr;
}

void CodeGen::bind_package_globals() {
    for (const uint32_t id : package_files(current_file_)) {
        if (id == current_file_) {
            continue;
        }
        const auto found = files_.find(id);
        if (found == files_.end()) {
            continue;
        }
        for (const auto &[name, global] : found->second.globals) {
            if (global.is_static) {
                continue;
            }
            scopes_.back()[name] = global;
        }
    }
    const auto self = files_.find(current_file_);
    if (self == files_.end()) {
        return;
    }
    for (const auto &[name, global] : self->second.globals) {
        scopes_.back()[name] = global;
    }
}

llvm::AllocaInst *CodeGen::alloca_typed(llvm::Type *type, const std::string &name) {
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    stamp_builder(allocas);
    return allocas.CreateAlloca(type, nullptr, name);
}

bool CodeGen::is_struct_value(const Lowered &low) const {
    return !low.is_slice && low.struct_id >= 0 && low.depth == 0 && !low.is_array && low.type != nullptr &&
           low.type->isStructTy();
}

CodeGen::Lowered CodeGen::lower_result(uint32_t file, const ResultSlot &slot) const {
    // A slice is `array_len < 0`. A sized array keeps its length. Zero is not an array.
    const bool array = slot.is_array && !slot.is_fn_ptr && slot.array_len != 0;
    return lower(file, slot.type_name, slot.type_package, slot.is_struct, slot.indirections, array,
                 array ? slot.array_len : 0, false);
}

llvm::Type *CodeGen::aggregate_return(const FunctionDecl &fn, uint32_t file) const {
    std::vector<llvm::Type *> fields;
    fields.reserve(fn.results.size());
    for (const ResultSlot &slot : fn.results) {
        fields.push_back(stored_type(lower_result(file, slot)));
    }
    if (fields.empty()) {
        fields.push_back(i32());
    }
    return llvm::StructType::get(context_, fields);
}

llvm::FunctionType *CodeGen::function_type(const FunctionDecl &fn, uint32_t file) const {
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const bool result_array = !many && fn.results.size() == 1 && fn.results.front().is_array &&
                              fn.results.front().array_len > 0 && !fn.results.front().is_fn_ptr;
    const bool result_slice = !many && fn.results.size() == 1 && fn.results.front().is_array &&
                             fn.results.front().array_len < 0 && !fn.results.front().is_fn_ptr;
    llvm::Type *ret = many ? aggregate_return(fn, file)
                           : (returns_void ? llvm::Type::getVoidTy(context_)
                                           : stored_type(fn.results.size() == 1 && fn.results.front().is_fn_ptr
                                                            ? lower_fnptr(file, fn.results.front().type_name,
                                                                          fn.results.front().type_package,
                                                                          fn.results.front().is_struct,
                                                                          fn.results.front().is_enum,
                                                                          fn.results.front().ret_indirections,
                                                                          fn.results.front().indirections,
                                                                          fn.results.front().fn_params,
                                                                          fn.results.front().fn_variadic,
                                                                          fn.results.front().fn_results)
                                                            : lower(file, fn.return_type, fn.type_package, fn.is_struct,
                                                                    fn.indirections, result_array || result_slice,
                                                                    result_slice ? fn.results.front().array_len
                                                                                : (result_array ? fn.results.front().array_len : 0),
                                                                    false)));
    std::vector<llvm::Type *> params;
    params.reserve(fn.params.size());
    for (const Param &param : fn.params) {
        params.push_back(stored_type(lower_param_type(file, param)));
    }
    return llvm::FunctionType::get(ret, params, fn.variadic);
}

int CodeGen::file_alias(const std::string &name) const {
    const auto pos = name.rfind(".file.");
    if (pos == std::string::npos || pos == 0 || pos + 6 >= name.size()) {
        return -1;
    }
    uint32_t file = 0;
    for (std::size_t i = pos + 6; i < name.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (ch < '0' || ch > '9') {
            return -1;
        }
        file = file * 10u + static_cast<uint32_t>(ch - '0');
    }
    const auto found = tags_.find(file);
    if (found == tags_.end()) {
        return -1;
    }
    const auto tag = found->second.find(name.substr(0, pos));
    if (tag == found->second.end()) {
        return -1;
    }
    return tag->second;
}

int CodeGen::tag_id(uint32_t file, const std::string &name) const {
    if (const int aliased = file_alias(name); aliased >= 0) {
        return aliased;
    }
    for (const uint32_t id : package_files(file)) {
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

int CodeGen::unique_tag(const std::string &name) const {
    int found = -1;
    for (const auto &file : tags_) {
        const auto tag = file.second.find(name);
        if (tag == file.second.end()) {
            continue;
        }
        if (found >= 0 && found != tag->second) {
            return -1;
        }
        found = tag->second;
    }
    return found;
}

int CodeGen::spec_tag(const std::string &name) const {
    if (name.find(".spec") == std::string::npos) {
        return -1;
    }
    return unique_tag(name);
}

uint32_t CodeGen::assoc_home(const Expr::Selector &node) const {
    const Expr *cursor = node.base.get();
    if (cursor != nullptr) {
        if (const auto *index = std::get_if<Expr::Index>(&cursor->data)) {
            cursor = index->base.get();
        }
    }
    const auto *inner = cursor == nullptr ? nullptr : std::get_if<Expr::Selector>(&cursor->data);
    if (inner == nullptr || inner->base == nullptr) {
        return current_file_;
    }
    const auto *package_name = std::get_if<Expr::Name>(&inner->base->data);
    if (package_name == nullptr) {
        return current_file_;
    }
    const uint32_t id = package_file(current_file_, package_name->text);
    return id == 0 ? current_file_ : id;
}

uint32_t CodeGen::package_file(uint32_t file, const std::string &package) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return 0;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form == ImportDecl::Form::Selective) {
            continue;
        }
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (binding == package) {
            return target->file_id;
        }
    }
    return 0;
}

int CodeGen::selective_tag(uint32_t file, const std::string &name) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return -1;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form != ImportDecl::Form::Selective) {
            continue;
        }
        bool wanted = false;
        for (const std::string &item : import.names) {
            if (item == name) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        const int id = tag_id(target->file_id, name);
        if (id >= 0) {
            return id;
        }
    }
    return -1;
}

const CodeGen::Alias *CodeGen::find_alias(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = aliases_.find(id);
        if (found == aliases_.end()) {
            continue;
        }
        const auto alias = found->second.find(name);
        if (alias != found->second.end()) {
            return &alias->second;
        }
    }
    return nullptr;
}

const CodeGen::Alias *CodeGen::selective_alias(uint32_t file, const std::string &name) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return nullptr;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form != ImportDecl::Form::Selective) {
            continue;
        }
        bool wanted = false;
        for (const std::string &item : import.names) {
            if (item == name) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        if (const Alias *alias = find_alias(target->file_id, name)) {
            return alias;
        }
    }
    return nullptr;
}

bool CodeGen::has_typedef(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const PackageNode *node = find_node(id);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item != nullptr && item->name == name) {
                return true;
            }
        }
    }
    return false;
}

bool CodeGen::alias_base_ready(uint32_t file, const TypedefDecl &item) const {
    if (item.is_struct || item.is_enum || item.type_name.empty() || item.type_name == "void" ||
        item.type_name == "bool" || item.type_name == "invalid" || item.type_name == "i8" || item.type_name == "i16" ||
        item.type_name == "i32" || item.type_name == "i64" || item.type_name == "u8" || item.type_name == "u16" ||
        item.type_name == "u32" || item.type_name == "u64" || item.type_name == "f32" || item.type_name == "f64") {
        return true;
    }
    uint32_t target = file;
    if (!item.type_package.empty()) {
        target = package_file(file, item.type_package);
        if (target == 0) {
            return true;
        }
    }
    if (find_alias(target, item.type_name) != nullptr) {
        return true;
    }
    // `type Point Point` names the struct tag. The alias is not waiting on itself.
    if (has_typedef(target, item.type_name) && !(item.type_package.empty() && item.name == item.type_name)) {
        return false;
    }
    if (item.type_package.empty()) {
        const PackageNode *node = find_node(file);
        if (node != nullptr && graph_ != nullptr) {
            for (const ImportDecl &import : node->unit.imports) {
                if (import.form != ImportDecl::Form::Selective) {
                    continue;
                }
                bool wanted = false;
                for (const std::string &imported : import.names) {
                    if (imported == item.type_name) {
                        wanted = true;
                    }
                }
                if (!wanted) {
                    continue;
                }
                for (const ImportEdge &edge : graph_->edges) {
                    if (edge.from_file != file || edge.spec != import.path || edge.loc.file != import.loc.file ||
                        edge.loc.offset != import.loc.offset) {
                        continue;
                    }
                    if (has_typedef(edge.to_file, item.type_name) && find_alias(edge.to_file, item.type_name) == nullptr) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

CodeGen::Lowered CodeGen::lower(uint32_t file, const std::string &name, const std::string &package, bool is_struct,
                                 int indirections, bool is_array, int array_len, bool parameter) const {
    Lowered out;
    Alias base;
    bool have = false;
    auto take = [&](const Alias &alias) {
        base = alias;
        have = true;
    };
    if (!package.empty()) {
        const uint32_t target = package_file(file, package);
        const int tag = target == 0 ? -1 : tag_id(target, name);
        const Alias *remote = target == 0 ? nullptr : find_alias(target, name);
        if (remote != nullptr && !is_struct) {
            take(*remote);
        } else if (tag >= 0) {
            base.struct_id = tag;
            have = true;
        } else if (const int eid = target == 0 ? -1 : enum_named(target, name); eid >= 0) {
            note_enum(base, eid);
            have = true;
        }
    } else if (is_struct) {
        int sid = tag_id(file, name);
        if (sid < 0) {
            sid = selective_tag(file, name);
        }
        if (sid < 0) {
            sid = spec_tag(name);
        }
        if (sid >= 0) {
            base.struct_id = sid;
            have = true;
        } else if (const int eid = enum_named(file, name); eid >= 0) {
            note_enum(base, eid);
            have = true;
        } else if (const int imported = selective_enum(file, name); imported >= 0) {
            note_enum(base, imported);
            have = true;
        } else {
            // A method receiver is marked as a struct. A primitive alias such as `type Integer i32`,
            // and a slice alias such as `type string_view []const u8`, are not structs. A sized-array
            // alias has `is_array` set and stays out.
            const auto scalar = [](const Alias &alias) {
                return alias.depth == 0 && !alias.is_array && alias.struct_id < 0 && alias.fn_ty == nullptr &&
                       !alias.elem_void;
            };
            if (const Alias *local = find_alias(file, name); local != nullptr && scalar(*local)) {
                take(*local);
            } else if (const Alias *imported = selective_alias(file, name); imported != nullptr && scalar(*imported)) {
                take(*imported);
            }
        }
    } else if (name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "u8" || name == "u16" ||
               name == "u32" || name == "u64") {
        base.elem_unsigned = name.starts_with("u");
        if (name == "i8" || name == "u8") {
            base.elem_bits = 8;
        } else if (name == "i16" || name == "u16") {
            base.elem_bits = 16;
        } else if (name == "i64" || name == "u64") {
            base.elem_bits = 64;
        } else {
            base.elem_bits = 32;
        }
        have = true;
    } else if (name == "f32") {
        base.elem_float = true;
        base.elem_bits = 32;
        have = true;
    } else if (name == "f64") {
        base.elem_float = true;
        base.elem_bits = 64;
        have = true;
    } else if (name == "void") {
        base.elem_void = true;
        have = true;
    } else if (name == "Type") {
        base.depth = 1;
        have = true;
    } else if (name == "bool") {
        base.elem_bool = true;
        have = true;
    } else if (name != "invalid" && !name.empty()) {
        if (const Alias *local = find_alias(file, name)) {
            take(*local);
        } else if (const Alias *imported = selective_alias(file, name)) {
            take(*imported);
        } else {
            int sid = tag_id(file, name);
            if (sid < 0) {
                sid = selective_tag(file, name);
            }
            if (sid < 0) {
                sid = spec_tag(name);
            }
            if (sid >= 0) {
                base.struct_id = sid;
                have = true;
            } else if (const int eid = enum_named(file, name); eid >= 0) {
                note_enum(base, eid);
                have = true;
            } else if (const int imported = selective_enum(file, name); imported >= 0) {
                note_enum(base, imported);
                have = true;
            } else if (const int tid = trait_named(file, name); tid >= 0) {
                base.trait_id = tid;
                have = true;
            }
        }
    }
    const int base_depth = have ? base.depth : 0;
    const bool elem_char = have && base.elem_char;
    const bool elem_void = have && base.elem_void;
    const bool elem_bool = have && base.elem_bool;
    const int elem_bits = have ? base.elem_bits : 0;
    const bool elem_unsigned = have && base.elem_unsigned;
    const bool elem_float = have && base.elem_float;
    const int sid = have ? base.struct_id : -1;
    const bool base_array = have && base.is_array;
    const bool base_slice = have && base.is_slice;
    // `*[]T` and `[N][]T` have no representation. Sema already rejects them.
    if (base_slice && (indirections > 0 || is_array)) {
        out.type = i32();
        return out;
    }
    // `[]T` and `type Row []T` stay a `{ ptr, i32 }` value. A parameter does not decay.
    if ((is_array && array_len < 0) || (base_slice && indirections == 0 && !is_array)) {
        out.is_slice = true;
        out.is_array = false;
        out.array_len = -1;
        out.depth = base_depth + indirections;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = slice_type();
        return out;
    }
    if (base_array && (indirections > 0 || is_array)) {
        out.type = i32();
        return out;
    }
    if (parameter && (is_array || base_array)) {
        out.depth = base_depth + indirections + 1;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = ptr_type();
        return out;
    }
    if (is_array && array_len > 0) {
        out.is_array = true;
        out.array_len = array_len;
        out.depth = base_depth + indirections;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = llvm::ArrayType::get(leaf_type(out.depth, elem_char, sid, elem_bool, elem_bits, elem_float),
                                        static_cast<uint64_t>(array_len));
        return out;
    }
    if (base_array && base.array_len > 0) {
        out.is_array = true;
        out.array_len = base.array_len;
        out.depth = base_depth;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = llvm::ArrayType::get(leaf_type(base_depth, elem_char, sid, elem_bool, elem_bits, elem_float),
                                        static_cast<uint64_t>(base.array_len));
        return out;
    }
    out.depth = base_depth + indirections;
    out.elem_char = elem_char;
    out.elem_void = elem_void;
    out.elem_bool = elem_bool;
    out.elem_bits = elem_bits;
    out.elem_unsigned = elem_unsigned;
    out.elem_float = elem_float;
    out.struct_id = sid;
    out.enum_id = have ? base.enum_id : -1;
    if (indirections == 0 && !is_array) {
        out.fn_ty = have ? base.fn_ty : nullptr;
        out.fn_ret_depth = have ? base.fn_ret_depth : 0;
        out.fn_ret_float = have && base.fn_ret_float;
        out.fn_ret_bits = have ? base.fn_ret_bits : 0;
        out.fn_ret_unsigned = have && base.fn_ret_unsigned;
        out.fn_ret_char = have && base.fn_ret_char;
        out.fn_ret_bool = have && base.fn_ret_bool;
        out.fn_ret_struct = have ? base.fn_ret_struct : -1;
        out.fn_results = have ? base.fn_results : std::vector<FnParam>{};
    }
    out.type = leaf_type(out.depth, elem_char, sid, elem_bool, elem_bits, elem_float);
    return out;
}

CodeGen::Lowered CodeGen::lower_declared(uint32_t file, const VarDecl &var) const {
    if (var.is_fn_ptr) {
        return lower_fnptr(file, var.type_name, var.type_package, var.is_struct, var.is_enum, var.ret_indirections,
                           var.indirections, var.fn_params, var.fn_variadic, var.fn_results);
    }
    Lowered low = lower(file, var.type_name, var.type_package, var.is_struct, var.indirections, var.is_array,
                        var.array_len, false);
    if (!var.ptr_to_array) {
        return low;
    }
    low.ptr_to_array = true;
    low.ptr_array_len = low.array_len;
    low.depth += 1;
    low.is_array = false;
    low.array_len = 0;
    low.type = ptr_type();
    return low;
}

CodeGen::Lowered CodeGen::lower_fnptr(uint32_t file, const std::string &ret_name, const std::string &ret_package,
                                      bool ret_struct, bool ret_enum, int ret_indirections, int fn_depth,
                                      const std::vector<FnParam> &params, bool variadic,
                                      const std::vector<FnParam> &results) const {
    (void)ret_enum;
    std::vector<llvm::Type *> args;
    args.reserve(params.size());
    auto lower_part = [&](const FnParam &part, bool parameter) {
        if (part.is_fn_ptr) {
            return lower_fnptr(file, part.type_name, part.type_package, part.is_struct, part.is_enum,
                               part.ret_indirections, part.indirections, part.params, part.variadic, part.results);
        }
        const bool slice = part.is_array && part.array_len < 0;
        return lower(file, part.type_name, part.type_package, part.is_struct, part.indirections, slice,
                     slice ? part.array_len : 0, parameter);
    };
    for (const FnParam &param : params) {
        args.push_back(stored_type(lower_part(param, true)));
    }
    Lowered out;
    out.depth = fn_depth < 1 ? 1 : fn_depth;
    out.type = ptr_type();
    if (results.size() > 1) {
        std::vector<llvm::Type *> fields;
        fields.reserve(results.size());
        for (const FnParam &part : results) {
            fields.push_back(stored_type(lower_part(part, false)));
        }
        out.fn_ty = llvm::FunctionType::get(llvm::StructType::get(context_, fields), args, variadic);
        out.fn_results = results;
        return out;
    }
    const Lowered ret = lower(file, ret_name, ret_package, ret_struct, ret_indirections, false, 0, false);
    llvm::Type *ret_ty = ret.depth == 0 && ret.elem_void ? llvm::Type::getVoidTy(context_) : stored_type(ret);
    out.fn_ty = llvm::FunctionType::get(ret_ty, args, variadic);
    out.fn_ret_depth = ret.depth;
    out.fn_ret_float = ret.elem_float;
    out.fn_ret_bits = ret.elem_bits;
    out.fn_ret_unsigned = ret.elem_unsigned;
    out.fn_ret_char = ret.elem_char;
    out.fn_ret_bool = ret.elem_bool;
    out.fn_ret_struct = ret.struct_id;
    return out;
}

CodeGen::Lowered CodeGen::lower_param_type(uint32_t file, const Param &param) const {
    if (param.is_tuple) {
        std::vector<llvm::Type *> fields;
        Lowered out;
        out.is_tuple = true;
        out.tuple_parts = param.tuple_parts;
        for (const ResultSlot &slot : param.tuple_parts) {
            const Lowered part = lower(file, slot.type_name, slot.type_package, slot.is_struct, slot.indirections,
                                       slot.is_array, slot.array_len, false);
            fields.push_back(stored_type(part));
        }
        if (fields.empty()) {
            fields.push_back(i32());
        }
        out.type = llvm::StructType::get(context_, fields);
        return out;
    }
    if (param.is_fn_ptr) {
        return lower_fnptr(file, param.type_name, param.type_package, param.is_struct, param.is_enum, param.ret_indirections,
                           param.indirections, param.fn_params, param.fn_variadic, param.fn_results);
    }
    return lower(file, param.type_name, param.type_package, param.is_struct, param.indirections, param.is_array,
                 param.array_len, true);
}

std::optional<CodeGen::Lowered> CodeGen::lower_array_literal(uint32_t file, const Expr::Init &item) const {
    if (!item.array_literal || item.elem_ptr_to_array || (item.type_name.empty() && !item.elem_fn)) {
        return std::nullopt;
    }
    const int count = static_cast<int>(item.elements.size());
    Lowered elem;
    if (item.elem_fn) {
        elem = lower_fnptr(file, item.type_name, item.type_package, item.is_struct, item.is_enum, item.elem_ret_indirections,
                           item.indirections < 1 ? 1 : item.indirections, item.elem_fn_params, item.elem_fn_variadic,
                           item.elem_fn_results);
        if (elem.fn_ty == nullptr || elem.type == nullptr) {
            return std::nullopt;
        }
    } else if (item.elem_is_array) {
        if (item.elem_array_len <= 0) {
            return std::nullopt;
        }
        elem = lower(file, item.type_name, item.type_package, item.is_struct, item.indirections, true, item.elem_array_len,
                     false);
        if (!elem.is_array || elem.type == nullptr || !elem.type->isArrayTy()) {
            return std::nullopt;
        }
    } else {
        elem = lower(file, item.type_name, item.type_package, item.is_struct, item.indirections, false, 0, false);
        if (elem.type == nullptr || elem.is_array || elem.elem_void || elem.type->isVoidTy()) {
            return std::nullopt;
        }
    }
    Lowered low = elem;
    low.is_array = true;
    low.array_len = count;
    low.inner_array_len = item.elem_is_array ? item.elem_array_len : 0;
    low.type = llvm::ArrayType::get(elem.type, static_cast<uint64_t>(count));
    return low;
}

void CodeGen::apply_low(Binding &binding, const Lowered &low) const {
    binding.depth = low.depth;
    binding.elem_char = low.elem_char;
    binding.struct_id = low.struct_id;
    binding.elem_void = low.elem_void;
    binding.elem_bool = low.elem_bool;
    binding.elem_bits = low.elem_bits;
    binding.elem_unsigned = low.elem_unsigned;
    binding.elem_float = low.elem_float;
    binding.trait_id = low.trait_id;
    binding.fn_ty = low.fn_ty;
    binding.fn_ret_depth = low.fn_ret_depth;
    binding.fn_ret_float = low.fn_ret_float;
    binding.fn_ret_bits = low.fn_ret_bits;
    binding.fn_ret_unsigned = low.fn_ret_unsigned;
    binding.fn_ret_char = low.fn_ret_char;
    binding.fn_ret_bool = low.fn_ret_bool;
    binding.fn_ret_struct = low.fn_ret_struct;
    binding.fn_results = low.fn_results;
    binding.enum_id = low.enum_id;
    binding.inner_array_len = low.inner_array_len;
    binding.ptr_to_array = low.ptr_to_array;
    binding.ptr_array_len = low.ptr_array_len;
    binding.is_tuple = low.is_tuple;
    binding.tuple_parts = low.tuple_parts;
    binding.is_slice = low.is_slice;
    binding.is_array = low.is_array;
    binding.array_len = low.array_len;
}

llvm::StructType *CodeGen::slice_type() const {
    if (slice_ty_ != nullptr) {
        return slice_ty_;
    }
    auto *type = llvm::StructType::create(context_, "tlc.slice");
    type->setBody({ptr_type(), i32()});
    slice_ty_ = type;
    return type;
}

llvm::Value *CodeGen::as_slice_value(const TypedValue &value, SourceLoc loc) {
    llvm::StructType *slice_ty = slice_type();
    if (value.value != nullptr && (value.is_slice || value.value->getType() == slice_ty)) {
        if (value.value->getType() == slice_ty) {
            return value.value;
        }
    }
    if (value.value != nullptr) {
        if (llvm::Type *object = object_type(value.value); object != nullptr && object->isArrayTy()) {
            auto *array = llvm::cast<llvm::ArrayType>(object);
            llvm::Value *data =
                builder_.CreateInBoundsGEP(object, value.value, llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(0)});
            // A string literal sets `array_len` to the bytes, which is 0 for `""`.
            const int len = value.array_len > 0 || (value.is_slice && value.array_len >= 0)
                                ? value.array_len
                                : static_cast<int>(array->getNumElements());
            llvm::Value *packed = llvm::ConstantAggregateZero::get(slice_ty);
            packed = builder_.CreateInsertValue(packed, data, 0);
            packed = builder_.CreateInsertValue(packed, const_i32(len), 1);
            return packed;
        }
        if ((value.array_len > 0 || (value.is_slice && value.array_len >= 0)) &&
            value.value->getType()->isPointerTy()) {
            llvm::Value *packed = llvm::ConstantAggregateZero::get(slice_ty);
            packed = builder_.CreateInsertValue(packed, value.value, 0);
            packed = builder_.CreateInsertValue(packed, const_i32(value.array_len), 1);
            return packed;
        }
    }
    error(loc, "not supported yet");
    return llvm::ConstantAggregateZero::get(slice_ty);
}

llvm::Value *CodeGen::adapt(const TypedValue &value, llvm::Type *slot, SourceLoc loc) {
    if (slot == slice_type()) {
        return as_slice_value(value, loc);
    }
    return as_stored(value.value, value.depth, slot, value.elem_unsigned);
}

const CodeGen::FieldLayout *CodeGen::find_field(int struct_id, const std::string &name, int &index) const {
    index = -1;
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    const std::vector<FieldLayout> &fields = structs_[static_cast<std::size_t>(struct_id)].fields;
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        if (fields[static_cast<std::size_t>(i)].name == name) {
            index = i;
            return &fields[static_cast<std::size_t>(i)];
        }
    }
    return nullptr;
}

TypedValue CodeGen::field_addr(int struct_id, llvm::Value *pointer, const std::string &field, SourceLoc loc) {
    int field_index = -1;
    const FieldLayout *slot = find_field(struct_id, field, field_index);
    if (pointer == nullptr || slot == nullptr || field_index < 0 || struct_id < 0 ||
        struct_id >= static_cast<int>(structs_.size()) ||
        structs_[static_cast<std::size_t>(struct_id)].type == nullptr) {
        error(loc, slot == nullptr ? "undeclared identifier" : "type mismatch");
        return {};
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    auto placed = [&](llvm::Value *addr) {
        TypedValue out{addr,       slot->depth,     slot->elem_char, slot->struct_id, slot->is_array, slot->elem_void,
                       slot->elem_bool, slot->elem_bits, slot->elem_unsigned, -1,         slot->elem_float};
        out.array_len = slot->array_len;
        out.is_slice = slot->is_slice;
        return out;
    };
    // A C union overlays every field at offset 0. A tlc union does the same, and the enum tag sits after the payload.
    if (layout.is_union) {
        llvm::Value *addr = pointer;
        if (layout.tag_enum >= 0) {
            if (arm_union_tag_) {
                store_union_tag(struct_id, pointer, field);
            }
            if (layout.type->getNumElements() < 3) {
                error(loc, "type mismatch");
                return {};
            }
            addr = builder_.CreateInBoundsGEP(layout.type, pointer,
                                               llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(1)});
        }
        return placed(addr);
    }
    llvm::Value *zero_index = const_i32(0);
    llvm::Value *member = const_i32(field_index);
    llvm::Value *gep = builder_.CreateInBoundsGEP(structs_[static_cast<std::size_t>(struct_id)].type, pointer,
                                                   llvm::ArrayRef<llvm::Value *>{zero_index, member});
    return placed(gep);
}

bool CodeGen::addressable(const Expr &expr) const {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(name->text);
        return binding != nullptr && binding->value != nullptr && !binding->is_function && !binding->is_array;
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data)) {
        return star->op == TokenKind::Star && star->expr != nullptr;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        return index->end == nullptr && index->base != nullptr && index->index != nullptr;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return false;
        }
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            const auto package = packages_.find(base->text);
            if (package != packages_.end()) {
                const Binding *found = visible(package->second, selector->field, false);
                return found != nullptr && !found->is_function && !found->is_array;
            }
        }
        return addressable(*selector->base);
    }
    return false;
}

int CodeGen::trait_named(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
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

void CodeGen::prepare_traits() {
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TraitDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || trait_named(node.file_id, item->name) >= 0) {
                continue;
            }
            trait_names_[node.file_id][item->name] = static_cast<int>(trait_decls_.size());
            trait_decls_.push_back(item);
        }
    }
}

void CodeGen::prepare_structs() {
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<StructDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || !item->type_params.empty() ||
                tag_id(node.file_id, item->name) >= 0) {
                continue;
            }
            const int id = static_cast<int>(structs_.size());
            StructLayout layout;
            layout.name = item->name;
            layout.type = llvm::StructType::create(context_, node.qualifier + "." + item->name);
            structs_.push_back(std::move(layout));
            tags_[node.file_id][item->name] = id;
        }
    }
    std::vector<const StructDecl *> decls(structs_.size(), nullptr);
    std::vector<uint32_t> owners(structs_.size(), 0);
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<StructDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || !item->type_params.empty()) {
                continue;
            }
            const int id = tag_id(node.file_id, item->name);
            if (id >= 0 && decls[static_cast<std::size_t>(id)] == nullptr) {
                decls[static_cast<std::size_t>(id)] = item;
                owners[static_cast<std::size_t>(id)] = node.file_id;
            }
        }
    }
    prepare_aliases();
    bool progress = true;
    while (progress) {
        progress = false;
        for (int id = 0; id < static_cast<int>(structs_.size()); ++id) {
            if (structs_[static_cast<std::size_t>(id)].ready) {
                continue;
            }
            const StructDecl *item = decls[static_cast<std::size_t>(id)];
            if (item == nullptr) {
                continue;
            }
            const uint32_t file = owners[static_cast<std::size_t>(id)];
            // A type argument from the importing file is not a name in this package.
            if (item->name.find(".spec") != std::string::npos) {
                for (const FieldDecl &field : item->fields) {
                    if (!field.type_package.empty() || field.type_name.empty() || tag_id(file, field.type_name) >= 0) {
                        continue;
                    }
                    const int foreign = unique_tag(field.type_name);
                    if (foreign >= 0) {
                        tags_[file][field.type_name] = foreign;
                    }
                }
            }
            bool waiting = false;
            for (const FieldDecl &field : item->fields) {
                if (field.indirections > 0) {
                    continue;
                }
                const Lowered low = lower(file, field.type_name, field.type_package, field.is_struct, field.indirections,
                                          field.is_array, field.array_len, false);
                if (low.depth == 0 && !low.is_array && low.struct_id >= 0 &&
                    !structs_[static_cast<std::size_t>(low.struct_id)].ready) {
                    waiting = true;
                    break;
                }
            }
            if (waiting) {
                continue;
            }
            structs_[static_cast<std::size_t>(id)].is_extern = item->is_extern;
            structs_[static_cast<std::size_t>(id)].extern_c = item->extern_c;
            structs_[static_cast<std::size_t>(id)].is_union = item->is_union;
            if (!item->tag_name.empty()) {
                structs_[static_cast<std::size_t>(id)].tag_enum = enum_named(file, item->tag_name);
            }
            auto c_memory = [&](auto &&self, llvm::Type *ty) -> llvm::Type * {
                if (ty == nullptr) {
                    return ty;
                }
                if (ty->isIntegerTy(1)) {
                    return i8();
                }
                if (auto *array = llvm::dyn_cast<llvm::ArrayType>(ty)) {
                    llvm::Type *elem = self(self, array->getElementType());
                    if (elem != array->getElementType()) {
                        return llvm::ArrayType::get(elem, array->getNumElements());
                    }
                }
                return ty;
            };
            std::vector<llvm::Type *> body;
            std::vector<FieldLayout> fields;
            body.reserve(item->fields.size());
            for (const FieldDecl &field : item->fields) {
                const Lowered low =
                    field.is_fn_ptr ? lower_fnptr(file, field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                  field.ret_indirections, field.indirections, field.fn_params,
                                                  field.fn_variadic, field.fn_results)
                                   : lower(file, field.type_name, field.type_package, field.is_struct, field.indirections,
                                           field.is_array, field.array_len, false);
                FieldLayout slot;
                slot.name = field.name;
                slot.type = stored_type(low);
                slot.depth = low.depth;
                slot.elem_char = low.elem_char;
                slot.is_array = low.is_array;
                slot.array_len = low.array_len;
                slot.struct_id = low.struct_id;
                slot.elem_void = low.elem_void;
                slot.elem_bool = low.elem_bool;
                slot.elem_bits = low.elem_bits;
                slot.elem_unsigned = low.elem_unsigned;
                slot.elem_float = low.elem_float;
                slot.is_slice = low.is_slice;
                // C _Bool is one byte. An extern(abi="c") struct keeps that layout.
                if (item->extern_c && low.elem_bool && low.depth == 0) {
                    slot.type = c_memory(c_memory, slot.type);
                    slot.elem_bool = false;
                    slot.elem_bits = 8;
                    slot.elem_unsigned = true;
                    slot.elem_char = false;
                }
                body.push_back(slot.type);
                fields.push_back(std::move(slot));
            }
            if (item->is_union && !fields.empty()) {
                const llvm::DataLayout &data_layout = module_.getDataLayout();
                int prefix = 0;
                uint64_t prefix_size = 0;
                unsigned best_align = 1;
                uint64_t union_size = 0;
                for (int index = 0; index < static_cast<int>(fields.size()); ++index) {
                    llvm::Type *field_type = fields[static_cast<std::size_t>(index)].type;
                    if (field_type == nullptr || !field_type->isSized()) {
                        continue;
                    }
                    const uint64_t field_size = data_layout.getTypeAllocSize(field_type).getFixedValue();
                    unsigned field_align = static_cast<unsigned>(data_layout.getABITypeAlign(field_type).value());
                    if (field_align == 0) {
                        field_align = 1;
                    }
                    union_size = std::max(union_size, field_size);
                    if (index == 0 || field_align > best_align ||
                        (field_align == best_align && field_size > prefix_size)) {
                        prefix = index;
                        prefix_size = field_size;
                        best_align = field_align;
                    }
                }
                const uint64_t rounded = (union_size + best_align - 1) / best_align * best_align;
                llvm::Type *prefix_type = fields[static_cast<std::size_t>(prefix)].type;
                if (!item->tag_name.empty() && prefix_type != nullptr &&
                    structs_[static_cast<std::size_t>(id)].tag_enum >= 0) {
                    // A zero-length array carries the payload alignment. The payload bytes start at offset 0.
                    // The enum tag is stored after that payload. A load uses the variant's own type.
                    const int tag_id = structs_[static_cast<std::size_t>(id)].tag_enum;
                    body.clear();
                    body.push_back(llvm::ArrayType::get(prefix_type, 0));
                    body.push_back(llvm::ArrayType::get(i8(), rounded == 0 ? 1 : rounded));
                    body.push_back(enum_mem_type(tag_id));
                } else {
                    body.clear();
                    body.push_back(prefix_type);
                    if (prefix_size < rounded) {
                        body.push_back(llvm::ArrayType::get(i8(), rounded - prefix_size));
                    }
                }
            }
            // An empty tlc struct stays zero-sized. An empty extern(abi="c") struct is one byte.
            if (body.empty() && (item->is_union || item->extern_c)) {
                body.push_back(i8());
            }
            if (item->align > 0) {
                const llvm::DataLayout &data_layout = module_.getDataLayout();
                llvm::StructType *probe = llvm::StructType::get(context_, body);
                if (probe->isSized()) {
                    const uint64_t size = data_layout.getTypeAllocSize(probe).getFixedValue();
                    unsigned natural = static_cast<unsigned>(data_layout.getABITypeAlign(probe).value());
                    if (natural == 0) {
                        natural = 1;
                    }
                    const unsigned want = static_cast<unsigned>(item->align);
                    if (want < natural) {
                        error(item->loc, "type mismatch");
                    } else {
                        const uint64_t rounded = (size + want - 1) / want * want;
                        if (rounded > size) {
                            body.push_back(llvm::ArrayType::get(i8(), rounded - size));
                        }
                        if (natural < want) {
                            llvm::Type *carrier = nullptr;
                            for (unsigned bits = 8; bits <= 65536 && carrier == nullptr; bits *= 2) {
                                llvm::IntegerType *ty = llvm::IntegerType::get(context_, bits);
                                if (static_cast<unsigned>(data_layout.getABITypeAlign(ty).value()) == want) {
                                    carrier = ty;
                                }
                            }
                            for (unsigned count = 1; count <= 64 && carrier == nullptr; count *= 2) {
                                llvm::Type *ty = llvm::FixedVectorType::get(i8(), count);
                                if (static_cast<unsigned>(data_layout.getABITypeAlign(ty).value()) == want) {
                                    carrier = ty;
                                }
                            }
                            for (unsigned count = 1; count <= 16 && carrier == nullptr; count *= 2) {
                                llvm::Type *ty =
                                    llvm::FixedVectorType::get(llvm::Type::getInt64Ty(context_), count);
                                if (static_cast<unsigned>(data_layout.getABITypeAlign(ty).value()) == want) {
                                    carrier = ty;
                                }
                            }
                            if (carrier == nullptr) {
                                error(item->loc, "not supported yet");
                            } else {
                                body.push_back(llvm::ArrayType::get(carrier, 0));
                            }
                        }
                    }
                }
            }
            structs_[static_cast<std::size_t>(id)].type->setBody(body);
            structs_[static_cast<std::size_t>(id)].fields = std::move(fields);
            structs_[static_cast<std::size_t>(id)].ready = true;
            progress = true;
        }
    }
    for (StructLayout &layout : structs_) {
        if (!layout.ready && layout.type != nullptr && layout.type->isOpaque()) {
            layout.type->setBody(llvm::ArrayRef<llvm::Type *>{static_cast<llvm::Type *>(i8())});
            layout.ready = true;
        }
    }
    publish_spec_types();
}

void CodeGen::publish_spec_types() {
    if (graph_ == nullptr) {
        return;
    }
    auto note = [&](uint32_t file, const std::string &name, const std::string &package) {
        if (!package.empty() || name.empty() || tag_id(file, name) >= 0) {
            return;
        }
        const int foreign = unique_tag(name);
        if (foreign >= 0) {
            tags_[file][name] = foreign;
        }
    };
    auto note_args = [&](auto &&self, uint32_t file, const std::vector<TypeArg> &args) -> void {
        for (const TypeArg &arg : args) {
            if (arg.has_value || arg.is_value) {
                continue;
            }
            note(file, arg.type_name, arg.type_package);
            self(self, file, arg.args);
        }
    };
    auto note_slot = [&](uint32_t file, const ResultSlot &slot) {
        note(file, slot.type_name, slot.type_package);
        note_args(note_args, file, slot.type_args);
    };
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *fn = std::get_if<FunctionDecl>(&decl.data);
            if (fn == nullptr) {
                continue;
            }
            // `measure.spec.Rect` and `func Box.spec.i32::get` both carry a concrete type.
            if (fn->name.find(".spec") == std::string::npos && fn->method_struct.find(".spec") == std::string::npos) {
                continue;
            }
            for (const Param &param : fn->params) {
                note(node.file_id, param.type_name, param.type_package);
                note_args(note_args, node.file_id, param.type_args);
                for (const ResultSlot &part : param.tuple_parts) {
                    note_slot(node.file_id, part);
                }
            }
            note(node.file_id, fn->return_type, fn->type_package);
            for (const ResultSlot &slot : fn->results) {
                note_slot(node.file_id, slot);
            }
        }
    }
}

void CodeGen::prepare_aliases() {
    if (graph_ == nullptr) {
        return;
    }
    struct Pending {
        uint32_t file = 0;
        const TypedefDecl *item = nullptr;
    };
    std::vector<Pending> pending;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || aliases_[node.file_id].contains(item->name)) {
                continue;
            }
            pending.push_back(Pending{node.file_id, item});
        }
    }
    bool progress = true;
    while (progress) {
        progress = false;
        std::vector<Pending> later;
        for (const Pending &item : pending) {
            if (item.item == nullptr || aliases_[item.file].contains(item.item->name)) {
                continue;
            }
            if (!alias_base_ready(item.file, *item.item)) {
                later.push_back(item);
                continue;
            }
            const Lowered low =
                item.item->is_fn_ptr
                    ? lower_fnptr(item.file, item.item->type_name, item.item->type_package, item.item->is_struct,
                                  item.item->is_enum, item.item->ret_indirections, item.item->indirections,
                                  item.item->fn_params, item.item->fn_variadic, item.item->fn_results)
                    : lower(item.file, item.item->type_name, item.item->type_package, item.item->is_struct,
                            item.item->indirections, item.item->is_array, item.item->array_len, false);
            Alias alias;
            alias.depth = low.depth;
            alias.elem_char = low.elem_char;
            alias.is_array = low.is_array;
            alias.array_len = low.array_len;
            alias.struct_id = low.struct_id;
            alias.elem_void = low.elem_void;
            alias.elem_bool = low.elem_bool;
            alias.elem_bits = low.elem_bits;
            alias.elem_unsigned = low.elem_unsigned;
            alias.elem_float = low.elem_float;
            alias.fn_ty = low.fn_ty;
            alias.fn_ret_depth = low.fn_ret_depth;
            alias.fn_ret_float = low.fn_ret_float;
            alias.fn_ret_bits = low.fn_ret_bits;
            alias.fn_ret_unsigned = low.fn_ret_unsigned;
            alias.fn_ret_char = low.fn_ret_char;
            alias.fn_ret_bool = low.fn_ret_bool;
            alias.fn_ret_struct = low.fn_ret_struct;
            alias.fn_results = low.fn_results;
            alias.is_slice = low.is_slice;
            aliases_[item.file].emplace(item.item->name, alias);
            progress = true;
        }
        pending = std::move(later);
    }
    for (const Pending &item : pending) {
        if (item.item == nullptr || aliases_[item.file].contains(item.item->name)) {
            continue;
        }
        aliases_[item.file].emplace(item.item->name, Alias{});
    }
}

llvm::Type *CodeGen::enum_mem_type(int enum_id) const {
    if (enum_id < 0 || enum_id >= static_cast<int>(enums_.size())) {
        return i32();
    }
    const EnumLayout &layout = enums_[static_cast<std::size_t>(enum_id)];
    return leaf_type(0, layout.is_char, -1, layout.is_bool, layout.is_bool ? 0 : layout.bits, false);
}

void CodeGen::describe_enum(const std::string &underlying, EnumLayout &layout) const {
    const std::string &name = underlying.empty() ? std::string("i32") : underlying;
    layout.is_bool = name == "bool";
    layout.is_char = false;
    layout.is_unsigned = name == "u8" || name == "u16" || name == "u32" || name == "u64";
    if (layout.is_bool) {
        layout.bits = 1;
    } else if (name == "i8" || name == "u8") {
        layout.bits = 8;
    } else if (name == "i16" || name == "u16") {
        layout.bits = 16;
    } else if (name == "i64" || name == "u64") {
        layout.bits = 64;
    } else {
        layout.bits = 32;
    }
}

void CodeGen::note_enum(Alias &base, int id) const {
    if (id < 0 || id >= static_cast<int>(enums_.size())) {
        return;
    }
    const EnumLayout &layout = enums_[static_cast<std::size_t>(id)];
    base.enum_id = id;
    base.elem_bool = layout.is_bool;
    base.elem_char = layout.is_char;
    base.elem_unsigned = layout.is_unsigned;
    base.elem_bits = layout.is_bool ? 0 : layout.bits;
}

int CodeGen::enum_named(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
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

int CodeGen::selective_enum(uint32_t file, const std::string &name) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return -1;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form != ImportDecl::Form::Selective) {
            continue;
        }
        bool wanted = false;
        for (const std::string &item : import.names) {
            if (item == name) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file != file || edge.spec != import.path || edge.loc.file != import.loc.file ||
                edge.loc.offset != import.loc.offset) {
                continue;
            }
            const int id = enum_named(edge.to_file, name);
            if (id >= 0) {
                return id;
            }
        }
    }
    return -1;
}

void CodeGen::prepare_enums() {
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<EnumDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || enum_named(node.file_id, item->name) >= 0) {
                continue;
            }
            EnumLayout layout;
            layout.name = item->name;
            describe_enum(item->underlying, layout);
            int64_t next = 0;
            bool next_ok = true;
            const bool wide = layout.bits >= 63 && !layout.is_bool;
            for (const Enumerator &enumerator : item->enumerators) {
                int64_t value = 0;
                if (enumerator.has_value) {
                    value = enumerator.value;
                } else if (next_ok) {
                    value = next;
                }
                if (!enumerator.name.empty() && !layout.values.contains(enumerator.name)) {
                    layout.values.emplace(enumerator.name, value);
                }
                bool full = false;
                if (layout.is_bool) {
                    full = value >= 1;
                } else if (layout.is_unsigned || layout.is_char) {
                    full = !wide && value >= ((1LL << layout.bits) - 1);
                } else if (wide) {
                    full = value == INT64_MAX;
                } else {
                    full = value >= ((1LL << (layout.bits - 1)) - 1);
                }
                if (full) {
                    next_ok = false;
                } else {
                    next = value + 1;
                    next_ok = true;
                }
            }
            enum_ids_[node.file_id][item->name] = static_cast<int>(enums_.size());
            enums_.push_back(std::move(layout));
        }
    }
}

int CodeGen::enum_type_designator(const Expr &expr) const {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        if (lookup(named->text) != nullptr) {
            return -1;
        }
        const int local = enum_named(current_file_, named->text);
        return local >= 0 ? local : selective_enum(current_file_, named->text);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return -1;
        }
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            if (packages_.contains(base->text)) {
                const uint32_t target = package_file(current_file_, base->text);
                return target == 0 ? -1 : enum_named(target, selector->field);
            }
        }
    }
    return -1;
}

int CodeGen::enum_value_id(const Expr &expr) const {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(named->text);
        return binding == nullptr ? -1 : binding->enum_id;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return -1;
        }
        const int id = enum_type_designator(*selector->base);
        if (id < 0 || id >= static_cast<int>(enums_.size())) {
            return -1;
        }
        if (enums_[static_cast<std::size_t>(id)].values.contains(selector->field)) {
            return id;
        }
    }
    return -1;
}

llvm::Constant *CodeGen::enum_constant(int enum_id, int64_t value) const {
    if (enum_id < 0 || enum_id >= static_cast<int>(enums_.size())) {
        return const_i32(0);
    }
    const EnumLayout &layout = enums_[static_cast<std::size_t>(enum_id)];
    if (layout.is_bool) {
        return llvm::ConstantInt::get(i1(), value == 0 ? 0 : 1, false);
    }
    if (layout.bits == 8) {
        return llvm::ConstantInt::get(i8(), static_cast<uint64_t>(value) & 0xFFULL, false);
    }
    if (layout.bits == 16) {
        return llvm::ConstantInt::get(i16(), static_cast<uint64_t>(value) & 0xFFFFULL, false);
    }
    if (layout.bits == 64) {
        return llvm::ConstantInt::get(i64(), static_cast<uint64_t>(value), true);
    }
    return const_i32(static_cast<int32_t>(value));
}

CodeGen::SymbolForm CodeGen::symbol_form(const PackageNode &node, const std::string &name, bool is_extern,
                                          bool extern_c, bool is_static, bool exported, bool entry_main,
                                          const std::string &link_name) const {
    SymbolForm form;
    // `name="symbol"` is the exact object name. The identifier in the program is unchanged.
    if (!link_name.empty()) {
        form.name = link_name;
        form.linkage = llvm::GlobalValue::ExternalLinkage;
        return form;
    }
    // A C library symbol keeps the name the C object file exports.
    if (extern_c) {
        form.name = name;
        form.linkage = llvm::GlobalValue::ExternalLinkage;
        return form;
    }
    if (entry_main) {
        form.name = "main";
        form.linkage = llvm::GlobalValue::ExternalLinkage;
        return form;
    }
    if (is_static && package_files(node.file_id).size() > 1) {
        form.name = node.qualifier + "." + source_stem(node.path) + "." + name;
    } else {
        form.name = node.qualifier + "." + name;
    }
    // A tlc library symbol uses the same name as an exported tlc function.
    if (is_extern) {
        form.linkage = llvm::GlobalValue::ExternalLinkage;
        return form;
    }
    form.linkage = exported && !is_static ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;
    return form;
}

const PackageNode *CodeGen::find_node(uint32_t file_id) const {
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

bool CodeGen::type_known(const std::string &name) const {
    if (name.empty()) {
        return false;
    }
    for (const StructLayout &item : structs_) {
        if (item.name == name) {
            return true;
        }
    }
    for (const EnumLayout &item : enums_) {
        if (item.name == name) {
            return true;
        }
    }
    for (const auto &file : aliases_) {
        if (file.second.contains(name)) {
            return true;
        }
    }
    return false;
}

bool CodeGen::unshadowed_type_name(const std::string &name) const {
    if (!type_known(name) || packages_.contains(name) || lookup(name) != nullptr) {
        return false;
    }
    return true;
}

const Binding *CodeGen::function_binding(uint32_t file, const std::string &name) const {
    const auto own = files_.find(file);
    if (own != files_.end()) {
        const auto it = own->second.functions.find(name);
        if (it != own->second.functions.end()) {
            return &it->second;
        }
    }
    return visible(file, name, true);
}

const Binding *CodeGen::static_method_binding(uint32_t file, const std::string &type, const std::string &field) const {
    if (type.empty() || field.empty()) {
        return nullptr;
    }
    const std::string key = type + "." + field;
    for (const uint32_t id : package_files(file)) {
        const auto found_file = files_.find(id);
        if (found_file == files_.end()) {
            continue;
        }
        const auto found = found_file->second.functions.find(key);
        if (found == found_file->second.functions.end() || !found->second.is_function) {
            continue;
        }
        if (found->second.is_static && id != file) {
            continue;
        }
        if (found->second.decl == nullptr || !found->second.decl->static_method) {
            return nullptr;
        }
        return &found->second;
    }
    return nullptr;
}

const Binding *CodeGen::static_selector_binding(uint32_t file, const Expr::Selector &selector) const {
    if (selector.resolved_owner.empty()) {
        return nullptr;
    }
    const std::string key = selector.resolved_owner + "." + selector.field;
    const Binding *method = nullptr;
    if (selector.resolved_file != 0) {
        method = method_binding(file, selector.resolved_file, key);
    }
    if (method == nullptr) {
        method = static_method_binding(file, selector.resolved_owner, selector.field);
    }
    if (method != nullptr && method->decl != nullptr && !method->decl->static_method) {
        return nullptr;
    }
    return method;
}

const Binding *CodeGen::function_named(uint32_t home, const std::string &key, uint32_t caller) const {
    if (home == 0 || key.empty()) {
        return nullptr;
    }
    for (const uint32_t id : package_files(home)) {
        const auto found_file = files_.find(id);
        if (found_file == files_.end()) {
            continue;
        }
        const auto found = found_file->second.functions.find(key);
        if (found == found_file->second.functions.end() || !found->second.is_function) {
            continue;
        }
        if (found->second.is_static && id != caller) {
            continue;
        }
        return &found->second;
    }
    return nullptr;
}

const Binding *CodeGen::method_binding(uint32_t caller, uint32_t resolved_file, const std::string &key) const {
    auto defined_here = [&](uint32_t file) {
        if (file == 0 || resolved_file == 0) {
            return false;
        }
        for (const uint32_t id : package_files(file)) {
            if (id == resolved_file) {
                return true;
            }
        }
        return false;
    };
    // A same-named method in the caller package belongs to a different type.
    if (resolved_file != 0 && !defined_here(caller)) {
        return function_named(resolved_file, key, caller);
    }
    const Binding *method = function_named(caller, key, caller);
    if (method == nullptr && resolved_file != 0) {
        return function_named(resolved_file, key, caller);
    }
    return method;
}

const Binding *CodeGen::call_callee(uint32_t file, const Expr &callee) const {
    auto usable = [](const Binding *fn) {
        return fn != nullptr && fn->is_function && !fn->is_generic && fn->decl != nullptr && fn->value != nullptr;
    };
    auto selective = [&](const std::string &name) -> const Binding * {
        const PackageNode *node = find_node(file);
        if (node == nullptr || graph_ == nullptr) {
            return nullptr;
        }
        for (const ImportDecl &import : node->unit.imports) {
            if (import.form != ImportDecl::Form::Selective) {
                continue;
            }
            bool wanted = false;
            for (const std::string &item : import.names) {
                if (item == name) {
                    wanted = true;
                }
            }
            if (!wanted) {
                continue;
            }
            const PackageNode *target = nullptr;
            for (const ImportEdge &edge : graph_->edges) {
                if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                    edge.loc.offset == import.loc.offset) {
                    target = find_node(edge.to_file);
                    break;
                }
            }
            if (target == nullptr) {
                continue;
            }
            if (const Binding *fn = visible(target->file_id, name, true); usable(fn)) {
                return fn;
            }
        }
        return nullptr;
    };
    if (const auto *name = std::get_if<Expr::Name>(&callee.data)) {
        if (const Binding *fn = function_binding(file, name->text); usable(fn)) {
            return fn;
        }
        return selective(name->text);
    }
    const auto *selector = std::get_if<Expr::Selector>(&callee.data);
    if (selector == nullptr || selector->base == nullptr) {
        return nullptr;
    }
    if (!selector->resolved_owner.empty()) {
        const std::string key = selector->resolved_owner + "." + selector->field;
        if (const Binding *method = method_binding(file, selector->resolved_file, key); usable(method)) {
            return method;
        }
    }
    const auto *base = std::get_if<Expr::Name>(&selector->base->data);
    if (base == nullptr) {
        return nullptr;
    }
    if (const uint32_t pkg = package_file(file, base->text); pkg != 0) {
        if (const Binding *fn = visible(pkg, selector->field, true); usable(fn)) {
            return fn;
        }
    }
    if (const Binding *method = static_method_binding(file, base->text, selector->field); usable(method)) {
        return method;
    }
    const Binding *receiver = nullptr;
    if (const auto own = files_.find(file); own != files_.end()) {
        const auto global = own->second.globals.find(base->text);
        if (global != own->second.globals.end()) {
            receiver = &global->second;
        }
    }
    if (receiver == nullptr) {
        receiver = visible(file, base->text, false);
    }
    if (receiver == nullptr || receiver->struct_id < 0 ||
        static_cast<std::size_t>(receiver->struct_id) >= structs_.size()) {
        return nullptr;
    }
    const std::string key = structs_[static_cast<std::size_t>(receiver->struct_id)].name + "." + selector->field;
    if (const Binding *method = method_binding(file, selector->resolved_file, key); usable(method)) {
        return method;
    }
    return nullptr;
}

const Binding *CodeGen::function_designator(uint32_t file, const Expr &expr) const {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *fn = function_binding(file, name->text);
        if (fn == nullptr || !fn->is_function || fn->is_generic || fn->value == nullptr) {
            return nullptr;
        }
        if (fn->decl != nullptr && !fn->decl->method_struct.empty() && !fn->decl->static_method) {
            return nullptr;
        }
        return fn;
    }
    const auto *selector = std::get_if<Expr::Selector>(&expr.data);
    if (selector == nullptr || selector->base == nullptr) {
        return nullptr;
    }
    // `Box[i32].from`. Sema stored the specialization on the selector.
    if (!selector->resolved_owner.empty()) {
        const Binding *method = static_selector_binding(file, *selector);
        if (method != nullptr && method->value != nullptr && !method->is_generic) {
            return method;
        }
    }
    const int enum_id = enum_type_designator(*selector->base);
    if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size()) &&
        enums_[static_cast<std::size_t>(enum_id)].values.contains(selector->field)) {
        return nullptr;
    }
    const auto *base = std::get_if<Expr::Name>(&selector->base->data);
    if (base == nullptr || packages_.contains(base->text)) {
        return nullptr;
    }
    if (function_binding(file, base->text) != nullptr) {
        return nullptr;
    }
    const auto own = files_.find(file);
    if (own != files_.end() && own->second.globals.contains(base->text)) {
        return nullptr;
    }
    if (visible(file, base->text, false) != nullptr || !type_known(base->text)) {
        return nullptr;
    }
    const Binding *method = static_method_binding(file, base->text, selector->field);
    if (method == nullptr || method->value == nullptr || method->is_generic) {
        return nullptr;
    }
    return method;
}

std::optional<CodeGen::Lowered> CodeGen::function_pointer_type(const Binding &fn) const {
    if (!fn.is_function || fn.is_generic || fn.value == nullptr) {
        return std::nullopt;
    }
    auto *function = llvm::dyn_cast<llvm::Function>(fn.value);
    if (function == nullptr) {
        return std::nullopt;
    }
    if (fn.decl != nullptr && !fn.decl->method_struct.empty() && !fn.decl->static_method) {
        return std::nullopt;
    }
    Lowered low;
    low.depth = 1;
    low.type = ptr_type();
    low.fn_ty = function->getFunctionType();
    low.fn_ret_depth = fn.depth;
    low.fn_ret_float = fn.elem_float;
    low.fn_ret_bits = fn.elem_bits;
    low.fn_ret_unsigned = fn.elem_unsigned;
    low.fn_ret_char = fn.elem_char;
    low.fn_ret_bool = fn.elem_bool;
    low.fn_ret_struct = fn.struct_id;
    if (fn.decl != nullptr && fn.decl->results.size() > 1) {
        low.fn_results.reserve(fn.decl->results.size());
        for (const ResultSlot &slot : fn.decl->results) {
            low.fn_results.push_back(fn_param_from_slot(slot));
        }
    }
    return low;
}

bool CodeGen::infer_global(uint32_t file, const Expr &expr, Lowered &out) const {
    const auto stored = [&](const Binding &binding) {
        Lowered low;
        low.depth = binding.depth;
        low.elem_char = binding.elem_char;
        low.is_array = binding.is_array;
        low.array_len = binding.array_len;
        low.struct_id = binding.struct_id;
        low.elem_void = binding.elem_void;
        low.elem_bool = binding.elem_bool;
        low.elem_bits = binding.elem_bits;
        low.elem_unsigned = binding.elem_unsigned;
        low.trait_id = binding.trait_id;
        low.elem_float = binding.elem_float;
        low.fn_ty = binding.fn_ty;
        low.fn_ret_depth = binding.fn_ret_depth;
        low.fn_ret_float = binding.fn_ret_float;
        low.fn_ret_bits = binding.fn_ret_bits;
        low.fn_ret_unsigned = binding.fn_ret_unsigned;
        low.fn_ret_char = binding.fn_ret_char;
        low.fn_ret_bool = binding.fn_ret_bool;
        low.fn_ret_struct = binding.fn_ret_struct;
        low.fn_results = binding.fn_results;
        low.enum_id = binding.enum_id;
        low.is_slice = binding.is_slice;
        if (binding.value != nullptr) {
            if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(binding.value)) {
                low.type = global->getValueType();
                return low;
            }
        }
        low.type = leaf_type(low.depth, low.elem_char, low.struct_id, low.elem_bool, low.elem_bits, low.elem_float);
        return low;
    };
    const auto find_global = [&](const std::string &name) -> const Binding * {
        const auto own = files_.find(file);
        if (own != files_.end()) {
            const auto it = own->second.globals.find(name);
            if (it != own->second.globals.end()) {
                return &it->second;
            }
        }
        return visible(file, name, false);
    };
    const auto find_function = [&](const std::string &name) -> const Binding * {
        const auto own = files_.find(file);
        if (own != files_.end()) {
            const auto it = own->second.functions.find(name);
            if (it != own->second.functions.end()) {
                return &it->second;
            }
        }
        return visible(file, name, true);
    };
    const auto as_int = [&](int bits, bool uns) {
        Lowered low;
        low.elem_bits = bits;
        low.elem_unsigned = uns;
        low.type = leaf_type(0, false, -1, false, bits, false);
        return low;
    };
    const auto as_float = [&](int bits) {
        Lowered low;
        low.elem_float = true;
        low.elem_bits = bits;
        low.type = leaf_type(0, false, -1, false, bits, true);
        return low;
    };
    const auto scalar = [](const Lowered &low) {
        return !low.is_slice && low.depth == 0 && !low.is_array && low.struct_id < 0 && low.trait_id < 0 &&
               low.enum_id < 0 && low.fn_ty == nullptr && !low.elem_void;
    };
    const auto promote = [&](Lowered low) {
        if (low.is_slice) {
            return low;
        }
        if (low.is_array) {
            low.depth += 1;
            low.is_array = false;
            low.array_len = 0;
            low.type = ptr_type();
            return low;
        }
        if (!scalar(low) || low.elem_float) {
            return low;
        }
        if (low.elem_bool || low.elem_char || low.elem_bits == 8 || low.elem_bits == 16) {
            return as_int(32, false);
        }
        if (low.elem_bits == 0) {
            return as_int(32, false);
        }
        return low;
    };
    const auto rank_of = [](const Lowered &low) { return low.elem_bits >= 64 ? 5 : 3; };
    const auto usual = [&](const Lowered &lhs, const Lowered &rhs) -> std::optional<Lowered> {
        if (!scalar(lhs) || !scalar(rhs) || lhs.elem_float || rhs.elem_float || lhs.elem_bool || rhs.elem_bool) {
            return std::nullopt;
        }
        if (lhs.elem_unsigned == rhs.elem_unsigned && lhs.elem_bits == rhs.elem_bits) {
            return lhs;
        }
        if (lhs.elem_unsigned == rhs.elem_unsigned) {
            return lhs.elem_bits >= rhs.elem_bits ? lhs : rhs;
        }
        const Lowered &signed_type = lhs.elem_unsigned ? rhs : lhs;
        const Lowered &unsigned_type = lhs.elem_unsigned ? lhs : rhs;
        if (rank_of(unsigned_type) >= rank_of(signed_type)) {
            return unsigned_type;
        }
        if (signed_type.elem_bits > unsigned_type.elem_bits) {
            return signed_type;
        }
        return as_int(signed_type.elem_bits, true);
    };
    const auto function_result = [&](const Binding &binding) -> std::optional<Lowered> {
        if (binding.decl == nullptr || binding.decl->results.size() > 1 || binding.is_generic) {
            return std::nullopt;
        }
        const FunctionDecl &fn = *binding.decl;
        if (fn.results.size() == 1 && fn.results.front().is_fn_ptr) {
            const ResultSlot &slot = fn.results.front();
            return lower_fnptr(file, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, slot.ret_indirections,
                               slot.indirections, slot.fn_params, slot.fn_variadic, slot.fn_results);
        }
        if (fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty()) {
            return std::nullopt;
        }
        const bool result_slice = fn.results.size() == 1 && fn.results.front().is_array &&
                                 fn.results.front().array_len < 0 && !fn.results.front().is_fn_ptr;
        return promote(lower(file, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, result_slice,
                             result_slice ? fn.results.front().array_len : 0, false));
    };

    std::function<std::optional<Lowered>(const Expr &)> value;
    std::function<std::optional<Lowered>(const Expr &)> raw;
    raw = [&](const Expr &node) -> std::optional<Lowered> {
        return std::visit(
            [&](const auto &item) -> std::optional<Lowered> {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Expr::Int>) {
                    const int bits = item.suffix.bits;
                    const bool uns = item.suffix.is_unsigned;
                    if (bits == 64) {
                        return as_int(64, uns);
                    }
                    if (bits == 32 && uns) {
                        return as_int(32, true);
                    }
                    if (bits == 8) {
                        return as_int(8, uns);
                    }
                    if (bits == 16) {
                        return as_int(16, uns);
                    }
                    return as_int(32, false);
                } else if constexpr (std::is_same_v<T, Expr::Char>) {
                    return as_int(8, true);
                } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                    Lowered low;
                    low.elem_bool = true;
                    low.type = i1();
                    return low;
                } else if constexpr (std::is_same_v<T, Expr::Float>) {
                    return as_float(item.is_f32 ? 32 : 64);
                } else if constexpr (std::is_same_v<T, Expr::String>) {
                    Lowered low;
                    low.depth = 1;
                    low.elem_bits = 8;
                    low.elem_unsigned = true;
                    low.type = ptr_type();
                    return low;
                } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                    return as_int(32, false);
                } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                    Lowered low;
                    low.depth = 1;
                    low.type = ptr_type();
                    return low;
                } else if constexpr (std::is_same_v<T, Expr::Name>) {
                    if (const Binding *global = find_global(item.text)) {
                        return stored(*global);
                    }
                    if (const Binding *fn = function_designator(file, node)) {
                        return function_pointer_type(*fn);
                    }
                    return std::nullopt;
                } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                    if (const Binding *fn = function_designator(file, node)) {
                        return function_pointer_type(*fn);
                    }
                    return std::nullopt;
                } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                    if (item.expr == nullptr) {
                        return std::nullopt;
                    }
                    if (item.op == TokenKind::Amp) {
                        const auto inner = raw(*item.expr);
                        if (!inner || inner->is_array || inner->fn_ty != nullptr) {
                            return std::nullopt;
                        }
                        Lowered low = *inner;
                        low.depth += 1;
                        low.type = ptr_type();
                        return low;
                    }
                    return value(*item.expr);
                } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                    if (item.is_fn_ptr) {
                        return lower_fnptr(file, item.type_name, item.type_package, item.is_struct, item.is_enum,
                                           item.ret_indirections, item.indirections, item.fn_params, item.fn_variadic,
                                           item.fn_results);
                    }
                    return lower(file, item.type_name, item.type_package, item.is_struct, item.indirections, false, 0,
                                 false);
                } else if constexpr (std::is_same_v<T, Expr::Call>) {
                    // `typeof(T).name()` is `*u8`.
                    if (item.type_name) {
                        Lowered low;
                        low.depth = 1;
                        low.elem_bits = 8;
                        low.elem_unsigned = true;
                        low.type = ptr_type();
                        return low;
                    }
                    if (item.callee == nullptr) {
                        return std::nullopt;
                    }
                    const Binding *fn = item.selected != nullptr ? binding_for_decl(item.selected)
                                                                : call_callee(file, *item.callee);
                    if (fn != nullptr) {
                        return function_result(*fn);
                    }
                    // `var n = fp()` when `fp` is a function pointer already declared in this file.
                    const auto *named = std::get_if<Expr::Name>(&item.callee->data);
                    if (named == nullptr) {
                        return std::nullopt;
                    }
                    const Binding *global = find_global(named->text);
                    if (global == nullptr || global->fn_ty == nullptr || global->fn_results.size() > 1) {
                        return std::nullopt;
                    }
                    llvm::Type *ret = global->fn_ty->getReturnType();
                    if (ret == nullptr || ret->isVoidTy()) {
                        return std::nullopt;
                    }
                    Lowered low;
                    low.depth = global->fn_ret_depth;
                    low.elem_float = global->fn_ret_float;
                    low.elem_bits = global->fn_ret_bits;
                    low.elem_unsigned = global->fn_ret_unsigned;
                    low.elem_char = global->fn_ret_char;
                    low.elem_bool = global->fn_ret_bool;
                    low.struct_id = global->fn_ret_struct;
                    low.type = ret;
                    low.is_slice = ret == slice_type();
                    return promote(low);
                } else if constexpr (std::is_same_v<T, Expr::Init>) {
                    if (item.array_literal) {
                        // `var row = [_]i32{1, 2}` keeps the array. The elements are filled later.
                        return lower_array_literal(file, item);
                    }
                    if (item.type_name.empty()) {
                        return std::nullopt;
                    }
                    const Lowered low =
                        lower(file, item.type_name, item.type_package, false, 0, false, 0, false);
                    if (low.struct_id < 0 || low.depth != 0 || low.is_array || low.enum_id >= 0) {
                        return std::nullopt;
                    }
                    return low;
                } else {
                    return std::nullopt;
                }
            },
            node.data);
    };
    value = [&](const Expr &node) -> std::optional<Lowered> {
        if (const auto *unary = std::get_if<Expr::Unary>(&node.data)) {
            if (unary->expr == nullptr) {
                return std::nullopt;
            }
            if (unary->op == TokenKind::Amp || unary->op == TokenKind::Star) {
                if (unary->op == TokenKind::Amp) {
                    return raw(node);
                }
                const auto inner = value(*unary->expr);
                if (!inner || inner->depth < 1) {
                    return std::nullopt;
                }
                Lowered low = *inner;
                low.depth -= 1;
                low.type = leaf_type(low.depth, low.elem_char, low.struct_id, low.elem_bool, low.elem_bits, low.elem_float);
                return promote(low);
            }
            const auto inner = value(*unary->expr);
            if (!inner) {
                return std::nullopt;
            }
            if (unary->op == TokenKind::Bang) {
                if (unary->postfix) {
                    return inner;
                }
                return as_int(32, false);
            }
            if (inner->elem_float && (unary->op == TokenKind::Plus || unary->op == TokenKind::Minus)) {
                return inner;
            }
            if (!scalar(*inner) || inner->elem_float) {
                return std::nullopt;
            }
            return inner;
        }
        if (const auto *binary = std::get_if<Expr::Binary>(&node.data)) {
            if (binary->lhs == nullptr || binary->rhs == nullptr) {
                return std::nullopt;
            }
            const auto lhs = value(*binary->lhs);
            const auto rhs = value(*binary->rhs);
            if (!lhs || !rhs) {
                return std::nullopt;
            }
            const bool compare = binary->op == TokenKind::EqEq || binary->op == TokenKind::BangEq ||
                                 binary->op == TokenKind::Lt || binary->op == TokenKind::Gt ||
                                 binary->op == TokenKind::Le || binary->op == TokenKind::Ge;
            if (lhs->elem_float || rhs->elem_float) {
                if (compare) {
                    return as_int(32, false);
                }
                if (binary->op == TokenKind::Percent || binary->op == TokenKind::Amp || binary->op == TokenKind::Pipe ||
                    binary->op == TokenKind::Caret || binary->op == TokenKind::LtLt || binary->op == TokenKind::GtGt) {
                    return std::nullopt;
                }
                const bool wide = (lhs->elem_float && lhs->elem_bits >= 64) || (rhs->elem_float && rhs->elem_bits >= 64);
                return as_float(wide ? 64 : 32);
            }
            if (compare) {
                return as_int(32, false);
            }
            const auto common = usual(*lhs, *rhs);
            if (!common) {
                return std::nullopt;
            }
            return common;
        }
        const auto got = raw(node);
        if (!got) {
            return std::nullopt;
        }
        return promote(*got);
    };

    // `var row = [_]i32{1, 2}` keeps the array. A use of that name still decays to a pointer.
    if (const auto *init = std::get_if<Expr::Init>(&expr.data); init != nullptr && init->array_literal) {
        const auto got = raw(expr);
        if (!got || !got->is_array || got->type == nullptr) {
            return false;
        }
        out = *got;
        return true;
    }
    const auto got = value(expr);
    if (!got || got->type == nullptr || got->elem_void) {
        return false;
    }
    out = *got;
    return true;
}

} // namespace tlc
