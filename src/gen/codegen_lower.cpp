// Symbol tables and lowering a type to LLVM.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

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

std::vector<uint32_t> CodeGen::package_files(uint32_t file) const { return package_file_ids(graph_, file); }

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
    if (slot.is_tuple) {
        return lower_tuple_parts(file, slot.tuple_parts);
    }
    // A slice is `array_len < 0`. A sized array keeps its length. Zero is not an array.
    const bool array = slot.is_array && !slot.is_fn_ptr && slot.array_len != 0;
    return lower(file, slot.type_name, slot.type_package, slot.is_struct, slot.indirections, array,
                 array ? slot.array_len : 0, false, slot.elem_slice, slot.shells);
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
    // `[2][2]i32` keeps the row in `shells`. The outer length alone would be `[2]i32`.
    bool nested_array = false;
    if (!many && fn.results.size() == 1 && !fn.results.front().is_fn_ptr) {
        const std::vector<TypeShell> &shells = fn.results.front().shells;
        for (std::size_t index = 1; index < shells.size(); ++index) {
            if (shells[index].kind == TypeShell::Kind::Array) {
                nested_array = true;
                break;
            }
        }
    }
    auto lowered = [&]() {
        if (fn.results.size() == 1 && fn.results.front().is_tuple) {
            return lower_tuple_parts(file, fn.results.front().tuple_parts);
        }
        if (fn.results.size() == 1 && fn.results.front().is_fn_ptr) {
            return lower_fnptr(file, fn.results.front().type_name, fn.results.front().type_package,
                               fn.results.front().is_struct, fn.results.front().is_enum,
                               fn.results.front().ret_indirections, fn.results.front().indirections,
                               fn.results.front().fn_params, fn.results.front().fn_variadic,
                               fn.results.front().fn_results);
        }
        if (nested_array) {
            return lower_result(file, fn.results.front());
        }
        return lower(file, fn.return_type, fn.type_package, fn.is_struct, fn.indirections,
                     result_array || result_slice,
                     result_slice ? fn.results.front().array_len
                                 : (result_array ? fn.results.front().array_len : 0),
                     false, (result_array || result_slice) && fn.results.front().elem_slice);
    };
    llvm::Type *ret = many ? aggregate_return(fn, file)
                           : (returns_void ? llvm::Type::getVoidTy(context_) : stored_type(lowered()));
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
    if (node == nullptr) {
        return 0;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form == ImportDecl::Form::Selective) {
            continue;
        }
        const PackageNode *target = import_target(graph_, file, import);
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
    if (node == nullptr) {
        return -1;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (!import_selects(import, name)) {
            continue;
        }
        const PackageNode *target = import_target(graph_, file, import);
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

const CodeGen::Alias *CodeGen::spec_alias(const std::string &name) const {
    if (name.find(".spec") == std::string::npos) {
        return nullptr;
    }
    const Alias *found = nullptr;
    for (const auto &file : aliases_) {
        const auto alias = file.second.find(name);
        if (alias == file.second.end()) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = &alias->second;
    }
    return found;
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
    if (node == nullptr) {
        return nullptr;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (!import_selects(import, name)) {
            continue;
        }
        const PackageNode *target = import_target(graph_, file, import);
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
    auto name_ready = [&](const std::string &type_name, const std::string &type_package, bool is_struct, bool is_enum,
                          bool self) {
        if (is_struct || is_enum || type_name.empty() || type_name == "void" || type_name == "bool" ||
            type_name == "invalid" || type_name == "i8" || type_name == "i16" || type_name == "i32" ||
            type_name == "i64" || type_name == "u8" || type_name == "u16" || type_name == "u32" || type_name == "u64" ||
            type_name == "f32" || type_name == "f64") {
            return true;
        }
        uint32_t target = file;
        if (!type_package.empty()) {
            target = package_file(file, type_package);
            if (target == 0) {
                return true;
            }
        }
        if (find_alias(target, type_name) != nullptr) {
            return true;
        }
        // `type Point Point` names the struct tag. The alias is not waiting on itself.
        if (has_typedef(target, type_name) && !(self && type_package.empty())) {
            return false;
        }
        if (type_package.empty()) {
            const PackageNode *node = find_node(file);
            if (node != nullptr) {
                for (const ImportDecl &import : node->unit.imports) {
                    if (!import_selects(import, type_name)) {
                        continue;
                    }
                    const PackageNode *imported = import_target(graph_, file, import);
                    if (imported != nullptr && has_typedef(imported->file_id, type_name) &&
                        find_alias(imported->file_id, type_name) == nullptr) {
                        return false;
                    }
                }
            }
        }
        return true;
    };
    if (item.is_tuple) {
        for (const ResultSlot &slot : item.tuple_parts) {
            const bool self = slot.type_package.empty() && slot.type_name == item.name;
            if (!name_ready(slot.type_name, slot.type_package, slot.is_struct, slot.is_enum, self)) {
                return false;
            }
        }
        return true;
    }
    return name_ready(item.type_name, item.type_package, item.is_struct, item.is_enum,
                      item.type_package.empty() && item.name == item.type_name);
}

bool CodeGen::make_tuple_alias(uint32_t file, const TypedefDecl &item, Alias &out) const {
    if (!item.is_tuple || item.tuple_parts.size() < 2) {
        return false;
    }
    std::vector<llvm::Type *> fields;
    fields.reserve(item.tuple_parts.size());
    for (const ResultSlot &slot : item.tuple_parts) {
        const Lowered part =
            slot.is_fn_ptr ? lower_fnptr(file, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum,
                                         slot.ret_indirections, slot.indirections, slot.fn_params, slot.fn_variadic,
                                         slot.fn_results)
                           : lower(file, slot.type_name, slot.type_package, slot.is_struct, slot.indirections,
                                   slot.is_array, slot.array_len, false, slot.elem_slice, slot.shells);
        llvm::Type *type = stored_type(part);
        if (type == nullptr || !type->isSized()) {
            return false;
        }
        fields.push_back(type);
    }
    out = Alias{};
    out.is_tuple = true;
    out.tuple_parts = item.tuple_parts;
    out.tuple_ty = llvm::StructType::get(context_, fields);
    return out.tuple_ty != nullptr;
}

llvm::Type *CodeGen::shell_type(const std::vector<TypeShell> &shells, int depth, bool elem_char, int struct_id,
                                   bool elem_bool, int elem_bits, bool elem_float) const {
    llvm::Type *type = leaf_type(depth, elem_char, struct_id, elem_bool, elem_bits, elem_float);
    for (int index = static_cast<int>(shells.size()) - 1; index >= 0; --index) {
        const TypeShell &shell = shells[static_cast<std::size_t>(index)];
        if (shell.kind == TypeShell::Kind::Slice) {
            type = slice_type();
        } else if (shell.kind == TypeShell::Kind::Array) {
            if (shell.length <= 0) {
                return i32();
            }
            // `*[2]Node` is stored as a pointer. An array of a struct that is still
            // opaque is not an LLVM array type, and a pointer or a slice outside it
            // does not put that array in the field.
            if (type != nullptr && !type->isSized()) {
                bool covered = false;
                for (int outer = index - 1; outer >= 0; --outer) {
                    const TypeShell::Kind kind = shells[static_cast<std::size_t>(outer)].kind;
                    if (kind == TypeShell::Kind::Pointer || kind == TypeShell::Kind::Slice) {
                        covered = true;
                        break;
                    }
                }
                if (covered) {
                    continue;
                }
            }
            type = llvm::ArrayType::get(type, static_cast<uint64_t>(shell.length));
        } else {
            type = ptr_type();
        }
    }
    return type;
}

CodeGen::Lowered CodeGen::compose_shells(Lowered leaf, std::vector<TypeShell> shells, bool parameter) const {
    auto peel = [](Lowered &inner, std::vector<TypeShell> &out) {
        if (inner.ptr_to_array) {
            TypeShell pointer;
            pointer.kind = TypeShell::Kind::Pointer;
            out.push_back(pointer);
            TypeShell array;
            array.kind = TypeShell::Kind::Array;
            array.length = inner.ptr_array_len;
            out.push_back(array);
            if (inner.depth > 0) {
                inner.depth -= 1;
            }
            inner.ptr_to_array = false;
            inner.ptr_array_len = 0;
            inner.is_array = false;
            return;
        }
        if (inner.is_slice) {
            if (inner.elem_slice) {
                TypeShell header;
                header.kind = TypeShell::Kind::Slice;
                out.push_back(header);
            }
            TypeShell header;
            header.kind = TypeShell::Kind::Slice;
            out.push_back(header);
            inner.is_slice = false;
            inner.elem_slice = false;
            return;
        }
        if (inner.is_array) {
            TypeShell array;
            array.kind = TypeShell::Kind::Array;
            array.length = inner.array_len;
            out.push_back(array);
            if (inner.elem_slice) {
                TypeShell header;
                header.kind = TypeShell::Kind::Slice;
                out.push_back(header);
            } else if (inner.inner_array_len > 0) {
                TypeShell row;
                row.kind = TypeShell::Kind::Array;
                row.length = inner.inner_array_len;
                out.push_back(row);
            }
            inner.is_array = false;
            inner.array_len = 0;
            inner.elem_slice = false;
            inner.inner_array_len = 0;
        }
    };
    peel(leaf, shells);
    // `[2]Duo` stores the tuple. The array itself is not a tuple. `tuple_parts` describes one element.
    if (leaf.is_tuple && leaf.type != nullptr && leaf.type->isStructTy() && leaf.type->isSized()) {
        for (const TypeShell &shell : shells) {
            if (shell.kind == TypeShell::Kind::Slice) {
                Lowered bad;
                bad.type = i32();
                return bad;
            }
        }
        const std::vector<ResultSlot> parts = leaf.tuple_parts;
        llvm::Type *element = leaf.type;
        int decayed_len = 0;
        if (parameter && !shells.empty() && shells.front().kind == TypeShell::Kind::Array) {
            decayed_len = shells.front().length;
            shells.erase(shells.begin());
            if (shells.empty()) {
                Lowered out;
                out.depth = 1;
                out.type = ptr_type();
                out.tuple_parts = parts;
                out.array_len = decayed_len;
                return out;
            }
            TypeShell pointer;
            pointer.kind = TypeShell::Kind::Pointer;
            shells.insert(shells.begin(), pointer);
        }
        llvm::Type *type = element;
        for (int index = static_cast<int>(shells.size()) - 1; index >= 0; --index) {
            const TypeShell &shell = shells[static_cast<std::size_t>(index)];
            if (shell.kind == TypeShell::Kind::Array) {
                if (shell.length <= 0 || type == nullptr || !type->isSized()) {
                    Lowered bad;
                    bad.type = i32();
                    return bad;
                }
                type = llvm::ArrayType::get(type, static_cast<uint64_t>(shell.length));
                continue;
            }
            type = ptr_type();
        }
        Lowered out;
        out.type = type;
        out.tuple_parts = parts;
        out.shells = shells;
        out.is_slice = !shells.empty() && shells.front().kind == TypeShell::Kind::Slice;
        out.is_array = !shells.empty() && shells.front().kind == TypeShell::Kind::Array;
        out.array_len = out.is_slice ? -1 : out.is_array ? shells.front().length : 0;
        out.ptr_to_array = shells.size() >= 2 && shells.front().kind == TypeShell::Kind::Pointer &&
                           shells[1].kind == TypeShell::Kind::Array;
        out.ptr_array_len = out.ptr_to_array ? shells[1].length : 0;
        out.inner_array_len = shells.size() >= 2 && shells[1].kind == TypeShell::Kind::Array ? shells[1].length : 0;
        if (decayed_len > 0 && !out.is_array && !out.is_slice) {
            out.array_len = decayed_len;
        }
        return out;
    }
    // A parameter `[N]T` is a pointer to the element. `array_len` keeps `N` for a range.
    int decayed_len = 0;
    if (parameter && !shells.empty() && shells.front().kind == TypeShell::Kind::Array) {
        decayed_len = shells.front().length;
        shells.erase(shells.begin());
        if (shells.empty()) {
            leaf.depth += 1;
            leaf.type = ptr_type();
            leaf.is_slice = false;
            leaf.is_array = false;
            leaf.array_len = decayed_len;
            leaf.shells.clear();
            return leaf;
        }
        TypeShell pointer;
        pointer.kind = TypeShell::Kind::Pointer;
        shells.insert(shells.begin(), pointer);
    }
    leaf.shells = shells;
    leaf.type = shell_type(shells, leaf.depth, leaf.elem_char, leaf.struct_id, leaf.elem_bool, leaf.elem_bits, leaf.elem_float);
    leaf.is_slice = !shells.empty() && shells.front().kind == TypeShell::Kind::Slice;
    leaf.is_array = !shells.empty() && shells.front().kind == TypeShell::Kind::Array;
    leaf.array_len = leaf.is_slice ? -1 : leaf.is_array ? shells.front().length : 0;
    leaf.elem_slice = shells.size() >= 2 && shells[1].kind == TypeShell::Kind::Slice &&
                      (shells.front().kind == TypeShell::Kind::Slice || shells.front().kind == TypeShell::Kind::Array);
    leaf.ptr_to_array = shells.size() >= 2 && shells.front().kind == TypeShell::Kind::Pointer &&
                        shells[1].kind == TypeShell::Kind::Array;
    leaf.ptr_array_len = leaf.ptr_to_array ? shells[1].length : 0;
    leaf.inner_array_len = shells.size() >= 2 && shells[1].kind == TypeShell::Kind::Array ? shells[1].length : 0;
    if (decayed_len > 0 && !leaf.is_array && !leaf.is_slice) {
        leaf.array_len = decayed_len;
    }
    return leaf;
}

bool CodeGen::meta_type(uint32_t file, const std::string &package, const std::string &name) const {
    if (name != "Type" || graph_ == nullptr) {
        return false;
    }
    const auto from_builtin = [&](uint32_t id) { return is_compiler_builtin(find_node(id)); };
    if (!package.empty()) {
        const uint32_t target = package_file(file, package);
        return target != 0 && from_builtin(target);
    }
    if (from_builtin(file)) {
        return true;
    }
    const PackageNode *node = find_node(file);
    if (node == nullptr) {
        return false;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (!import_selects(import, name)) {
            continue;
        }
        const PackageNode *target = import_target(graph_, file, import);
        if (is_compiler_builtin(target)) {
            return true;
        }
    }
    return false;
}

CodeGen::Lowered CodeGen::lower(uint32_t file, const std::string &name, const std::string &package, bool is_struct,
                                 int indirections, bool is_array, int array_len, bool parameter, bool elem_slice,
                                 const std::vector<TypeShell> &shells) const {
    if (!shells.empty()) {
        Lowered leaf = lower(file, name, package, is_struct, indirections, false, 0, false, false);
        return compose_shells(std::move(leaf), shells, parameter);
    }
    Lowered out;
    Alias base;
    bool have = false;
    auto take = [&](const Alias &alias) {
        base = alias;
        have = true;
    };
    if (meta_type(file, package, name)) {
        base.depth = 1;
        have = true;
    } else if (!package.empty()) {
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
                       !alias.elem_void && !alias.is_tuple;
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
    } else if (name == "bool") {
        base.elem_bool = true;
        have = true;
    } else if (name != "invalid" && !name.empty()) {
        if (const Alias *local = find_alias(file, name)) {
            take(*local);
        } else if (const Alias *imported = selective_alias(file, name)) {
            take(*imported);
        } else if (const Alias *spec = spec_alias(name)) {
            take(*spec);
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
    const bool base_headers = have && base.elem_slice;
    // `type Pair (i32, i32)` is that aggregate. A pointer to the name is still a pointer.
    if (have && base.is_tuple && base.tuple_ty != nullptr && !is_array) {
        if (indirections == 0) {
            out.is_tuple = true;
            out.tuple_parts = base.tuple_parts;
            out.type = base.tuple_ty;
            return out;
        }
        out.depth = indirections;
        out.type = ptr_type();
        return out;
    }
    // `[]Row` when `Row` is `[]i32`. The written flag is the second empty `[]`.
    const bool nest_slice = elem_slice || (base_slice && is_array && !base_headers);
    auto fill = [&](Lowered &dest, int depth) {
        dest.depth = depth;
        dest.elem_char = elem_char;
        dest.elem_void = elem_void;
        dest.elem_bool = elem_bool;
        dest.elem_bits = elem_bits;
        dest.elem_unsigned = elem_unsigned;
        dest.elem_float = elem_float;
        dest.struct_id = sid;
        dest.enum_id = have ? base.enum_id : -1;
    };
    // A third slice, or a pointer to a slice that is already nested, has no representation.
    if ((base_headers && (indirections > 0 || is_array)) || (elem_slice && (base_slice || base_array))) {
        out.type = i32();
        return out;
    }
    // `*Row` when `Row` is a slice or an array. The star is a pointer shell around that alias.
    if ((base_slice || base_array) && indirections > 0 && !is_array) {
        std::vector<TypeShell> lifted;
        lifted.reserve(static_cast<std::size_t>(indirections));
        for (int star = 0; star < indirections; ++star) {
            TypeShell shell;
            shell.kind = TypeShell::Kind::Pointer;
            lifted.push_back(shell);
        }
        Lowered leaf = lower(file, name, package, is_struct, 0, false, 0, false, false);
        return compose_shells(std::move(leaf), std::move(lifted), parameter);
    }
    // `[][]T` and `[]Row` are one header whose element is a header.
    if (nest_slice && is_array && array_len < 0) {
        out.is_slice = true;
        out.elem_slice = true;
        out.is_array = false;
        out.array_len = -1;
        fill(out, base_depth + indirections);
        out.type = slice_type();
        return out;
    }
    // `[N][]T` decays to a pointer that steps by one header.
    if (nest_slice && is_array && array_len > 0 && parameter) {
        out.elem_slice = true;
        out.array_len = array_len;
        fill(out, base_depth + indirections + 1);
        out.type = ptr_type();
        return out;
    }
    if (nest_slice && is_array && array_len > 0) {
        out.is_array = true;
        out.elem_slice = true;
        out.array_len = array_len;
        fill(out, base_depth + indirections);
        out.type = llvm::ArrayType::get(slice_type(), static_cast<uint64_t>(array_len));
        return out;
    }
    // `[]T` and `type Rows [][]i32` stay a `{ ptr, i32 }` value. A parameter does not decay.
    if ((is_array && array_len < 0) || (base_slice && indirections == 0 && !is_array)) {
        out.is_slice = true;
        out.elem_slice = base_headers;
        out.is_array = false;
        out.array_len = -1;
        fill(out, base_depth + indirections);
        out.type = slice_type();
        // `type Row []i32` keeps the alias. `[]i32` and `[]Row` do not.
        if (base_slice && indirections == 0 && !is_array) {
            out.alias_owner = name;
            out.alias_file = package.empty() ? file : package_file(file, package);
        }
        return out;
    }
    if (base_array && (indirections > 0 || is_array)) {
        out.type = i32();
        return out;
    }
    if (parameter && (is_array || base_array)) {
        out.elem_slice = base_headers;
        out.array_len = is_array ? array_len : base.array_len;
        fill(out, base_depth + indirections + 1);
        out.type = ptr_type();
        return out;
    }
    if (is_array && array_len > 0) {
        out.is_array = true;
        out.array_len = array_len;
        fill(out, base_depth + indirections);
        out.type = llvm::ArrayType::get(leaf_type(out.depth, elem_char, sid, elem_bool, elem_bits, elem_float),
                                        static_cast<uint64_t>(array_len));
        return out;
    }
    if (base_array && base.array_len > 0) {
        out.is_array = true;
        out.array_len = base.array_len;
        out.elem_slice = base_headers;
        fill(out, base_depth);
        llvm::Type *element = base_headers ? static_cast<llvm::Type *>(slice_type())
                                           : leaf_type(base_depth, elem_char, sid, elem_bool, elem_bits, elem_float);
        out.type = llvm::ArrayType::get(element, static_cast<uint64_t>(base.array_len));
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

CodeGen::Lowered CodeGen::lower_tuple_parts(uint32_t file, const std::vector<ResultSlot> &parts) const {
    Lowered out;
    out.is_tuple = true;
    out.tuple_parts = parts;
    std::vector<llvm::Type *> fields;
    fields.reserve(parts.size());
    for (const ResultSlot &slot : parts) {
        // One result of several may itself be a tuple. `lower` has no parts for that slot.
        const Lowered part =
            slot.is_tuple ? lower_tuple_parts(file, slot.tuple_parts)
            : slot.is_fn_ptr
                ? lower_fnptr(file, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum,
                              slot.ret_indirections, slot.indirections, slot.fn_params, slot.fn_variadic,
                              slot.fn_results)
                : lower(file, slot.type_name, slot.type_package, slot.is_struct, slot.indirections, slot.is_array,
                        slot.array_len, false, slot.elem_slice, slot.shells);
        llvm::Type *type = stored_type(part);
        fields.push_back(type == nullptr ? i32() : type);
    }
    if (fields.size() < 2) {
        out.type = i32();
        out.is_tuple = false;
        out.tuple_parts.clear();
        return out;
    }
    out.type = llvm::StructType::get(context_, fields);
    return out;
}

llvm::Type *CodeGen::tuple_shell_type(const std::vector<ResultSlot> &parts, const std::vector<TypeShell> &shells) const {
    const Lowered leaf = lower_tuple_parts(current_file_, parts);
    if (!leaf.is_tuple || leaf.type == nullptr) {
        return nullptr;
    }
    llvm::Type *type = leaf.type;
    for (int index = static_cast<int>(shells.size()) - 1; index >= 0; --index) {
        const TypeShell &shell = shells[static_cast<std::size_t>(index)];
        if (shell.kind == TypeShell::Kind::Slice) {
            return nullptr;
        }
        if (shell.kind == TypeShell::Kind::Array) {
            if (shell.length <= 0 || !type->isSized()) {
                return nullptr;
            }
            type = llvm::ArrayType::get(type, static_cast<uint64_t>(shell.length));
            continue;
        }
        type = ptr_type();
    }
    return type;
}

CodeGen::Lowered CodeGen::lower_declared(uint32_t file, const VarDecl &var) const {
    if (var.is_tuple) {
        return lower_tuple_parts(file, var.tuple_parts);
    }
    if (var.is_fn_ptr) {
        return lower_fnptr(file, var.type_name, var.type_package, var.is_struct, var.is_enum, var.ret_indirections,
                           var.indirections, var.fn_params, var.fn_variadic, var.fn_results);
    }
    Lowered low = lower(file, var.type_name, var.type_package, var.is_struct, var.indirections, var.is_array,
                        var.array_len, false, var.elem_slice, var.shells);
    if (!var.shells.empty() || !var.ptr_to_array) {
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
                     slice ? part.array_len : 0, parameter, part.elem_slice, part.shells);
    };
    for (const FnParam &param : params) {
        args.push_back(stored_type(lower_part(param, true)));
    }
    Lowered out;
    out.depth = fn_depth < 1 ? 1 : fn_depth;
    out.type = ptr_type();
    if (results.size() == 1 && results.front().is_tuple) {
        // `func () ((i32, i32))` returns that tuple. Several results are a longer list.
        std::vector<llvm::Type *> fields;
        fields.reserve(results.front().results.size());
        for (const FnParam &part : results.front().results) {
            fields.push_back(stored_type(lower_part(part, false)));
        }
        if (fields.size() >= 2) {
            out.fn_ty = llvm::FunctionType::get(llvm::StructType::get(context_, fields), args, variadic);
            out.fn_results = results;
            return out;
        }
    }
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
    if (results.size() == 1 && results.front().is_fn_ptr) {
        // The bits are the address of the next function. Its signature is that one result.
        out.fn_ty = llvm::FunctionType::get(ptr_type(), args, variadic);
        out.fn_ret_depth = 1;
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
                                       slot.is_array, slot.array_len, false, slot.elem_slice, slot.shells);
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
                 param.array_len, true, param.elem_slice, param.shells);
}

std::optional<CodeGen::Lowered> CodeGen::lower_array_literal(uint32_t file, const Expr::Init &item) const {
    if (!item.array_literal || (item.elem_ptr_to_array && item.elem_shells.empty()) ||
        (item.type_name.empty() && !item.elem_fn)) {
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
    } else if (!item.elem_shells.empty()) {
        elem = lower(file, item.type_name, item.type_package, item.is_struct, item.indirections, false, 0, false, false,
                     item.elem_shells);
        if (elem.type == nullptr) {
            return std::nullopt;
        }
    } else if (item.elem_slice) {
        return std::nullopt;
    } else if (item.elem_is_array) {
        const bool element_slice = item.elem_array_len_name.empty() && item.elem_array_len < 0;
        if (!element_slice && item.elem_array_len <= 0) {
            return std::nullopt;
        }
        elem = lower(file, item.type_name, item.type_package, item.is_struct, item.indirections, true,
                     element_slice ? -1 : item.elem_array_len, false, false);
        if (element_slice) {
            if (!elem.is_slice || elem.type == nullptr) {
                return std::nullopt;
            }
        } else if (!elem.is_array || elem.type == nullptr || !elem.type->isArrayTy()) {
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
    low.is_slice = false;
    low.array_len = count;
    low.elem_slice = elem.is_slice;
    low.inner_array_len = item.elem_is_array && !elem.is_slice ? item.elem_array_len : 0;
    low.type = llvm::ArrayType::get(elem.type, static_cast<uint64_t>(count));
    if (!elem.shells.empty()) {
        TypeShell outer;
        outer.kind = TypeShell::Kind::Array;
        outer.length = count;
        low.shells.clear();
        low.shells.push_back(outer);
        low.shells.insert(low.shells.end(), elem.shells.begin(), elem.shells.end());
    }
    return low;
}

void CodeGen::apply_scalar_result(Binding &binding, const Lowered &ret, bool returns_void) const {
    binding.depth = returns_void ? 0 : ret.depth;
    binding.elem_char = !returns_void && ret.elem_char;
    binding.struct_id = returns_void ? -1 : ret.struct_id;
    binding.elem_void = !returns_void && ret.elem_void;
    binding.elem_bool = !returns_void && ret.elem_bool;
    binding.elem_bits = returns_void ? 0 : ret.elem_bits;
    binding.elem_unsigned = !returns_void && ret.elem_unsigned;
    binding.elem_float = !returns_void && ret.elem_float;
    binding.trait_id = returns_void ? -1 : ret.trait_id;
    binding.enum_id = returns_void ? -1 : ret.enum_id;
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
    binding.elem_slice = low.elem_slice;
    binding.shells = low.shells;
    binding.is_array = low.is_array;
    binding.array_len = low.array_len;
    binding.alias_owner = low.alias_owner;
    binding.alias_file = low.alias_file;
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
        // A parameter `[N]T` keeps `N` in `array_len`. `*[N]T` keeps it in `ptr_array_len`,
        // or in the array shell when a field load did not copy that flag. A string uses
        // `array_len` 0, so that length is still packed.
        int len = -1;
        if (value.array_len > 0 || (value.is_slice && value.array_len >= 0)) {
            len = value.array_len;
        } else if (value.ptr_to_array && value.ptr_array_len > 0) {
            len = value.ptr_array_len;
        } else if (value.shells.size() >= 2 && value.shells[0].kind == TypeShell::Kind::Pointer &&
                   value.shells[1].kind == TypeShell::Kind::Array && value.shells[1].length > 0) {
            len = value.shells[1].length;
        }
        if (len >= 0 && value.value->getType()->isPointerTy()) {
            llvm::Value *packed = llvm::ConstantAggregateZero::get(slice_ty);
            packed = builder_.CreateInsertValue(packed, value.value, 0);
            packed = builder_.CreateInsertValue(packed, const_i32(len), 1);
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
        out.elem_slice = slot->elem_slice;
        out.shells = slot->shells;
        out.is_tuple = slot->is_tuple;
        out.tuple_parts = slot->tuple_parts;
        copy_fn_sig(out, *slot);
        // `h.raw = row` stores this array. The address is the field, not a decayed pointer.
        if (slot->type != nullptr && slot->type->isArrayTy()) {
            out.array_type = slot->type;
        }
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

} // namespace tlc
