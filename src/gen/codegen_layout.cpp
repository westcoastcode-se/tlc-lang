// Struct, union, enum, and alias layout, and the symbols codegen looks up.
#include "gen/gen.hpp"

namespace tlc {

bool CodeGen::addressable(const Expr &expr) const {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(name->text);
        return binding != nullptr && binding->value != nullptr && !binding->is_function && !binding->is_array;
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data)) {
        return star->op == TokenKind::Star && star->expr != nullptr;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        return !index->is_range && index->base != nullptr && index->index != nullptr;
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

const TraitDecl *CodeGen::trait_parent(uint32_t file, const TraitDecl *trait) const {
    if (trait == nullptr || trait->parent_name.empty() || !trait->parent_package.empty()) {
        return nullptr;
    }
    const int id = trait_named(file, trait->parent_name);
    if (id < 0 || id >= static_cast<int>(trait_decls_.size())) {
        return nullptr;
    }
    return trait_decls_[static_cast<std::size_t>(id)];
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
    // A specialized trait lives on the package that defines the template.
    // A user name cannot contain `.spec`, so this spelling is the compiler's.
    if (name.find(".spec") == std::string::npos) {
        return -1;
    }
    int found_id = -1;
    for (const auto &entry : trait_names_) {
        const auto trait = entry.second.find(name);
        if (trait == entry.second.end()) {
            continue;
        }
        if (found_id >= 0 && found_id != trait->second) {
            return -1;
        }
        found_id = trait->second;
    }
    return found_id;
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
    // Same index the checker reserves, so an initializer's struct id still names that struct.
    install_field_struct();
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
    // `value Pair` cannot be lowered until that tuple alias exists. The alias may name a struct
    // whose body is set in this loop, so the two passes alternate.
    auto tuple_alias_pending = [&](uint32_t file, const FieldDecl &field) {
        if (field.indirections > 0 || !field.type_package.empty() || field.type_name.empty()) {
            return false;
        }
        if (find_alias(file, field.type_name) != nullptr) {
            return false;
        }
        for (const uint32_t id : package_files(file)) {
            const PackageNode *node = find_node(id);
            if (node == nullptr) {
                continue;
            }
            for (const TopDecl &decl : node->unit.decls) {
                const auto *alias = std::get_if<TypedefDecl>(&decl.data);
                if (alias != nullptr && alias->is_tuple && alias->name == field.type_name) {
                    return true;
                }
            }
        }
        return false;
    };
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
                if (tuple_alias_pending(file, field)) {
                    waiting = true;
                    break;
                }
                const Lowered low = lower(file, field.type_name, field.type_package, field.is_struct, field.indirections,
                                          field.is_array, field.array_len, false, field.elem_slice, field.shells);
                // A slice or a pointer does not store the element. A sized array does, so the
                // element struct has to be ready before this struct gets a body.
                if (low.struct_id < 0 || low.depth > 0 || low.is_slice || low.elem_slice || low.ptr_to_array ||
                    structs_[static_cast<std::size_t>(low.struct_id)].ready) {
                    continue;
                }
                bool array_only = true;
                for (const TypeShell &shell : low.shells) {
                    if (shell.kind != TypeShell::Kind::Array) {
                        array_only = false;
                        break;
                    }
                }
                if (array_only) {
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
            structs_[static_cast<std::size_t>(id)].file_id = file;
            if (!item->tag_name.empty()) {
                int tag_id = -1;
                if (item->tag_package.empty()) {
                    tag_id = enum_id_of(file, item->tag_name);
                } else {
                    const uint32_t target = package_file(file, item->tag_package);
                    if (target != 0) {
                        tag_id = enum_id_of(target, item->tag_name);
                    }
                }
                structs_[static_cast<std::size_t>(id)].tag_enum = tag_id;
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
                    field.is_tuple
                        ? lower_tuple_parts(file, field.tuple_parts)
                        : field.is_fn_ptr ? lower_fnptr(file, field.type_name, field.type_package, field.is_struct,
                                                        field.is_enum, field.ret_indirections, field.indirections,
                                                        field.fn_params, field.fn_variadic, field.fn_results)
                                         : lower(file, field.type_name, field.type_package, field.is_struct,
                                                 field.indirections, field.is_array, field.array_len, false,
                                                 field.elem_slice, field.shells);
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
                slot.elem_slice = low.elem_slice;
                slot.shells = low.shells;
                slot.fn_ty = low.fn_ty;
                slot.fn_ret_depth = low.fn_ret_depth;
                slot.fn_ret_float = low.fn_ret_float;
                slot.fn_ret_bits = low.fn_ret_bits;
                slot.fn_ret_unsigned = low.fn_ret_unsigned;
                slot.fn_ret_char = low.fn_ret_char;
                slot.fn_ret_bool = low.fn_ret_bool;
                slot.fn_ret_struct = low.fn_ret_struct;
                slot.fn_results = low.fn_results;
                slot.is_tuple = low.is_tuple;
                slot.tuple_parts = low.tuple_parts;
                // A union arm is not a field value. The inline payload keeps the values on its fields.
                if (field.init != nullptr && !item->is_union) {
                    slot.init = field.init.get();
                }
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
            // An empty `union(Enum)` is the tag alone.
            if (body.empty() && item->is_union && structs_[static_cast<std::size_t>(id)].tag_enum >= 0) {
                body.push_back(enum_mem_type(structs_[static_cast<std::size_t>(id)].tag_enum));
            } else if (body.empty() && (item->is_union || item->extern_c)) {
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
        if (register_tuple_aliases()) {
            progress = true;
        }
    }
    for (StructLayout &layout : structs_) {
        if (!layout.ready && layout.type != nullptr && layout.type->isOpaque()) {
            layout.type->setBody(llvm::ArrayRef<llvm::Type *>{static_cast<llvm::Type *>(i8())});
            layout.ready = true;
        }
    }
    finish_tuple_aliases();
    publish_spec_types();
    // `builtin.Field` is the slice element `Type.fields()` returns. The struct at index 0
    // stays, so every other struct id still matches the checker.
    for (const PackageNode &node : graph_->nodes) {
        if (!is_compiler_builtin(&node)) {
            continue;
        }
        const int id = tag_id(node.file_id, "Field");
        if (id < 0 || id >= static_cast<int>(structs_.size())) {
            continue;
        }
        const StructDecl *decl = nullptr;
        for (const TopDecl &top : node.unit.decls) {
            const auto *item = std::get_if<StructDecl>(&top.data);
            if (item != nullptr && item->name == "Field") {
                decl = item;
                break;
            }
        }
        if (decl == nullptr || decl->fields.size() != 2 || !decl->fields[0].elem_const) {
            continue;
        }
        const StructLayout &layout = structs_[static_cast<std::size_t>(id)];
        const FieldLayout *name = layout.fields.size() == 2 ? &layout.fields[0] : nullptr;
        const FieldLayout *kind = name != nullptr ? &layout.fields[1] : nullptr;
        const bool name_text = name != nullptr && name->name == "name" && name->is_slice && name->elem_unsigned &&
                               name->elem_bits == 8;
        if (!layout.ready || !name_text || kind->name != "kind" || kind->depth != 2) {
            continue;
        }
        field_struct_id_ = id;
        break;
    }
}

void CodeGen::install_field_struct() {
    if (field_struct_id_ >= 0) {
        return;
    }
    const int id = static_cast<int>(structs_.size());
    StructLayout layout;
    layout.name = ".Field";
    layout.ready = true;
    layout.type = llvm::StructType::create(context_, "tlc.field");
    layout.type->setBody({slice_type(), ptr_type()});
    FieldLayout name;
    name.name = "name";
    name.type = slice_type();
    name.is_slice = true;
    name.elem_char = true;
    name.elem_bits = 8;
    name.elem_unsigned = true;
    FieldLayout kind;
    kind.name = "kind";
    kind.type = ptr_type();
    kind.depth = 2;
    layout.fields.push_back(std::move(name));
    layout.fields.push_back(std::move(kind));
    structs_.push_back(std::move(layout));
    field_struct_id_ = id;
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

bool CodeGen::register_tuple_aliases() {
    if (graph_ == nullptr) {
        return false;
    }
    bool any = false;
    bool progress = true;
    while (progress) {
        progress = false;
        for (const PackageNode &node : graph_->nodes) {
            for (const TopDecl &decl : node.unit.decls) {
                const auto *item = std::get_if<TypedefDecl>(&decl.data);
                if (item == nullptr || !item->is_tuple || item->name.empty() || !item->type_params.empty() ||
                    aliases_[node.file_id].contains(item->name)) {
                    continue;
                }
                Alias alias;
                if (!make_tuple_alias(node.file_id, *item, alias)) {
                    continue;
                }
                aliases_[node.file_id].emplace(item->name, std::move(alias));
                progress = true;
                any = true;
            }
        }
    }
    return any;
}

void CodeGen::finish_tuple_aliases() {
    if (graph_ == nullptr) {
        return;
    }
    register_tuple_aliases();
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item == nullptr || !item->is_tuple || item->name.empty() || !item->type_params.empty() ||
                aliases_[node.file_id].contains(item->name)) {
                continue;
            }
            error(item->loc, "not supported yet");
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
            if (item == nullptr || item->name.empty() || !item->type_params.empty() ||
                aliases_[node.file_id].contains(item->name)) {
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
            if (item.item->is_tuple) {
                Alias alias;
                // A struct element is not sized until that struct's body is set.
                if (!make_tuple_alias(item.file, *item.item, alias)) {
                    continue;
                }
                aliases_[item.file].emplace(item.item->name, std::move(alias));
                progress = true;
                continue;
            }
            const Lowered low =
                item.item->is_fn_ptr
                    ? lower_fnptr(item.file, item.item->type_name, item.item->type_package, item.item->is_struct,
                                  item.item->is_enum, item.item->ret_indirections, item.item->indirections,
                                  item.item->fn_params, item.item->fn_variadic, item.item->fn_results)
                    : lower(item.file, item.item->type_name, item.item->type_package, item.item->is_struct,
                            item.item->indirections, item.item->is_array, item.item->array_len, false,
                            item.item->elem_slice, item.item->shells);
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
            // `type Alias Kind` keeps the enum. `union(Alias)` and `union(pkg.Alias)` read it back.
            alias.enum_id = low.enum_id;
            alias.is_slice = low.is_slice;
            alias.elem_slice = low.elem_slice;
            alias.shells = low.shells;
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

int CodeGen::enum_id_of(uint32_t file, const std::string &name) const {
    const int id = enum_named(file, name);
    if (id >= 0) {
        return id;
    }
    const Alias *alias = find_alias(file, name);
    if (alias == nullptr || alias->enum_id < 0) {
        return -1;
    }
    return alias->enum_id;
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
        const int id = enum_named(target->file_id, name);
        if (id >= 0) {
            return id;
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

const PackageNode *CodeGen::find_node(uint32_t file_id) const { return package_node(graph_, file_id); }

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

const Binding *CodeGen::trait_spec_method(uint32_t caller, uint32_t resolved_file, const std::string &key,
                                          const Expr::Selector &selector, const Expr::Call &node) const {
    if (emitting_fn_ == nullptr || active_spec_.empty() || key.empty() || selector.base == nullptr || graph_ == nullptr) {
        return nullptr;
    }
    const auto *base_name = std::get_if<Expr::Name>(&selector.base->data);
    if (base_name == nullptr) {
        return nullptr;
    }
    const TraitDecl *trait = nullptr;
    int concrete = -1;
    for (std::size_t index = 0; index < emitting_fn_->params.size(); ++index) {
        const Param &param = emitting_fn_->params[index];
        if (param.name != base_name->text || !param_is_trait(caller, param)) {
            continue;
        }
        if (index >= active_spec_.size() || active_spec_[index] < 0) {
            return nullptr;
        }
        concrete = active_spec_[index];
        const int trait_id = trait_named(caller, param.type_name);
        if (trait_id < 0 || static_cast<std::size_t>(trait_id) >= trait_decls_.size()) {
            return nullptr;
        }
        trait = trait_decls_[static_cast<std::size_t>(trait_id)];
        break;
    }
    if (trait == nullptr) {
        return nullptr;
    }
    // The required method of this name is the call. A default is used only when the trait
    // does not require the name, and only one default has this argument count.
    const FunctionDecl *signature = nullptr;
    const TraitDecl *listed = trait;
    std::vector<const TraitDecl *> chain;
    for (int guard = 0; listed != nullptr && signature == nullptr && guard < static_cast<int>(trait_decls_.size());
         ++guard) {
        chain.push_back(listed);
        for (const FunctionDecl &method : listed->methods) {
            if (method.name == selector.field) {
                signature = &method;
                break;
            }
        }
        listed = trait_parent(caller, listed);
    }
    if (signature == nullptr) {
        int hits = 0;
        for (const PackageNode &pkg : graph_->nodes) {
            for (const TopDecl &decl : pkg.unit.decls) {
                const auto *fn = std::get_if<FunctionDecl>(&decl.data);
                bool on_chain = fn != nullptr && fn->method_struct == trait->name;
                if (fn != nullptr && !on_chain) {
                    for (const TraitDecl *item : chain) {
                        if (item != nullptr && fn->method_struct == item->name) {
                            on_chain = true;
                            break;
                        }
                    }
                }
                if (fn == nullptr || fn->name != selector.field || !on_chain || !fn->type_params.empty() ||
                    fn->body == nullptr) {
                    continue;
                }
                const std::size_t skip = !fn->static_method && !fn->params.empty() && fn->params.front().is_receiver ? 1 : 0;
                if (fn->params.size() < skip || fn->params.size() - skip != node.args.size()) {
                    continue;
                }
                ++hits;
                signature = fn;
            }
        }
        if (hits != 1) {
            return nullptr;
        }
    }
    const std::string want = param_list_key(*signature);
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
    const uint32_t home = resolved_file != 0 && !defined_here(caller) ? resolved_file : caller;
    const Binding *found = nullptr;
    auto scan = [&](uint32_t package) {
        for (const uint32_t id : package_files(package)) {
            const auto file = files_.find(id);
            if (file == files_.end()) {
                continue;
            }
            for (const auto &[name, binding] : file->second.functions) {
                const bool plain = name == key;
                const bool listed = name.size() > key.size() && name[key.size()] == '.' && name.starts_with(key);
                if (!plain && !listed) {
                    continue;
                }
                if (!binding.is_function || binding.decl == nullptr || binding.decl->static_method) {
                    continue;
                }
                if (binding.is_static && id != caller) {
                    continue;
                }
                if (param_list_key(*binding.decl) != want) {
                    continue;
                }
                if (found == nullptr || (found->value == nullptr && binding.value != nullptr)) {
                    found = &binding;
                }
            }
        }
    };
    const uint32_t concrete_file = concrete >= 0 && concrete < static_cast<int>(structs_.size())
                                       ? structs_[static_cast<std::size_t>(concrete)].file_id
                                       : 0;
    auto in_package = [&](uint32_t file, uint32_t package) {
        if (file == 0 || package == 0) {
            return false;
        }
        for (const uint32_t id : package_files(package)) {
            if (id == file) {
                return true;
            }
        }
        return false;
    };
    // The struct may be in the caller's package while this body is specialized elsewhere.
    if (concrete_file != 0 && !in_package(concrete_file, home)) {
        scan(concrete_file);
    }
    if (found == nullptr) {
        scan(home);
    }
    if (found == nullptr && home != resolved_file && resolved_file != 0) {
        scan(resolved_file);
    }
    if (found == nullptr && concrete_file != 0) {
        scan(concrete_file);
    }
    return found;
}

const Binding *CodeGen::call_callee(uint32_t file, const Expr &callee) const {
    auto usable = [](const Binding *fn) {
        return fn != nullptr && fn->is_function && !fn->is_generic && fn->decl != nullptr && fn->value != nullptr;
    };
    auto selective = [&](const std::string &name) -> const Binding * {
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
        low.elem_slice = binding.elem_slice;
        // `var band = grid[1:]` is `[][M]T`. The row length is what `band[0][i]` steps by.
        low.inner_array_len = binding.inner_array_len;
        low.shells = binding.shells;
        low.ptr_to_array = binding.ptr_to_array;
        low.ptr_array_len = binding.ptr_array_len;
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
        // A sized array stays `[N]T`. A pointer is the cast `*T(array)`.
        if (low.is_array) {
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
        if (fn.results.size() == 1 && fn.results.front().is_tuple) {
            return lower_tuple_parts(file, fn.results.front().tuple_parts);
        }
        if (fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty()) {
            return std::nullopt;
        }
        if (fn.results.size() == 1) {
            const ResultSlot &slot = fn.results.front();
            const bool result_array = slot.is_array && !slot.is_fn_ptr && slot.array_len != 0;
            return promote(lower(file, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, result_array,
                                 result_array ? slot.array_len : 0, false, result_array && slot.elem_slice,
                                 slot.shells));
        }
        return promote(lower(file, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0, false));
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
                    // A folded slice call is that constant. `memeql` is a bool. `memcmp` is an `i32`.
                    if (const FunctionDecl *fn = mem_compare_decl(item); fn != nullptr && fold(node)) {
                        if (mem_compare_bool(mem_compare_kind(*fn))) {
                            Lowered low;
                            low.elem_bool = true;
                            low.type = i1();
                            return low;
                        }
                        return as_int(32, false);
                    }
                    // `typeof(T).name()` is `*u8`. `typeof(T).fields()` is a slice of `{ name []const u8, kind }`.
                    if (item.type_name) {
                        Lowered low;
                        low.depth = 1;
                        low.elem_bits = 8;
                        low.elem_unsigned = true;
                        low.type = ptr_type();
                        return low;
                    }
                    if (item.type_fields) {
                        Lowered low;
                        low.is_slice = true;
                        low.array_len = -1;
                        low.depth = 0;
                        low.struct_id = field_struct_id_;
                        low.type = slice_type();
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
                } else if constexpr (std::is_same_v<T, Expr::Index>) {
                    // `var view = numbers[0:2]` is a slice. The header is filled later.
                    // `"hi"[1]` is a byte. The binding is `i32`, the same promotion as `'a'`.
                    if (!item.is_range) {
                        if (fold(node)) {
                            return as_int(32, false);
                        }
                        return std::nullopt;
                    }
                    if (item.base == nullptr) {
                        return std::nullopt;
                    }
                    if (std::holds_alternative<Expr::String>(item.base->data)) {
                        Lowered low;
                        low.is_slice = true;
                        low.array_len = -1;
                        low.elem_bits = 8;
                        low.elem_unsigned = true;
                        low.elem_char = true;
                        low.type = slice_type();
                        return low;
                    }
                    const auto base = raw(*item.base);
                    if (!base || (!base->is_array && !base->is_slice) || base->type == nullptr) {
                        return std::nullopt;
                    }
                    Lowered low = *base;
                    if (low.is_array && !low.is_slice && !low.shells.empty() &&
                        low.shells.front().kind == TypeShell::Kind::Array) {
                        low.shells.front().kind = TypeShell::Kind::Slice;
                        low.shells.front().length = -1;
                    }
                    low.is_slice = true;
                    low.is_array = false;
                    low.array_len = -1;
                    low.type = slice_type();
                    return low;
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

    // `var row = [_]i32{1, 2}` keeps the array. A use of that name is that array.
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
