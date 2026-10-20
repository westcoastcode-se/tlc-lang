// Statements, bindings, and local variables.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

void CodeGen::collect_cases(const Stmt &stmt, std::vector<CaseArm> &arms) const {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                CaseArm arm;
                arm.node = &node;
                arm.is_default = node.is_default;
                if (!node.is_default && node.value) {
                    if (const std::optional<int32_t> folded = fold(*node.value)) {
                        arm.value = *folded;
                        arm.has_value = true;
                    }
                }
                arms.push_back(arm);
                if (node.stmt) {
                    collect_cases(*node.stmt, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (child) {
                        collect_cases(*child, arms);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.then_arm) {
                    collect_cases(*node.then_arm, arms);
                }
                if (node.else_arm) {
                    collect_cases(*node.else_arm, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                if (node.body) {
                    collect_cases(*node.body, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.init) {
                    collect_cases(*node.init, arms);
                }
                if (node.body) {
                    collect_cases(*node.body, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                return;
            } else {
                return;
            }
        },
        stmt.data);
}

void CodeGen::emit_case(const Stmt::Case &node) {
    llvm::BasicBlock *block = nullptr;
    for (auto it = case_blocks_.rbegin(); it != case_blocks_.rend(); ++it) {
        const auto found = it->find(&node);
        if (found != it->end()) {
            block = found->second;
            break;
        }
    }
    if (block == nullptr) {
        error(node.loc, node.is_default ? "default outside switch" : "case outside switch");
        if (node.stmt && !terminated()) {
            emit_stmt(*node.stmt);
        }
        return;
    }
    if (!terminated()) {
        builder_.CreateBr(block);
    }
    builder_.SetInsertPoint(block);
    if (node.stmt) {
        emit_stmt(*node.stmt);
    }
}

void CodeGen::emit_switch(const Stmt::Switch &node) {
    const TypedValue cond = node.cond ? emit_rvalue(*node.cond) : TypedValue{zero(), 0};
    if (terminated()) {
        return;
    }
    std::vector<CaseArm> arms;
    if (node.body) {
        collect_cases(*node.body, arms);
    }
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "switch.end", function_);
    llvm::BasicBlock *default_block = end_block;
    unsigned cases = 0;
    for (CaseArm &arm : arms) {
        if (arm.is_default) {
            if (default_block == end_block) {
                arm.block = llvm::BasicBlock::Create(context_, "switch.default", function_);
                default_block = arm.block;
            }
            continue;
        }
        arm.block = llvm::BasicBlock::Create(context_, "switch.case", function_);
        if (arm.has_value) {
            ++cases;
        }
    }
    llvm::SwitchInst *inst = builder_.CreateSwitch(cond.value == nullptr ? zero() : cond.value, default_block, cases);
    for (const CaseArm &arm : arms) {
        if (!arm.is_default && arm.has_value && arm.block != nullptr) {
            inst->addCase(llvm::cast<llvm::ConstantInt>(const_i32(arm.value)), arm.block);
        }
    }
    std::unordered_map<const Stmt::Case *, llvm::BasicBlock *> blocks;
    for (const CaseArm &arm : arms) {
        if (arm.node != nullptr && arm.block != nullptr) {
            blocks.emplace(arm.node, arm.block);
        }
    }
    case_blocks_.push_back(std::move(blocks));
    llvm::BasicBlock *step = loops_.empty() ? nullptr : loops_.back().step;
    loops_.push_back(LoopTargets{step, end_block, true});
    llvm::BasicBlock *skip_block = llvm::BasicBlock::Create(context_, "switch.skip", function_);
    builder_.SetInsertPoint(skip_block);
    if (node.body) {
        emit_stmt(*node.body);
    }
    if (!terminated()) {
        builder_.CreateBr(end_block);
    }
    loops_.pop_back();
    case_blocks_.pop_back();
    builder_.SetInsertPoint(end_block);
}

const FunctionDecl *CodeGen::callee_decl(const Expr &callee) const {
    if (const auto *fn = std::get_if<Expr::Func>(&callee.data)) {
        return fn->decl.get();
    }
    if (const auto *name = std::get_if<Expr::Name>(&callee.data)) {
        const Binding *binding = lookup(name->text);
        return binding == nullptr ? nullptr : binding->decl;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data); selector != nullptr && selector->base != nullptr) {
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            const auto package = packages_.find(base->text);
            if (package != packages_.end()) {
                const Binding *binding = visible(package->second, selector->field, true);
                return binding == nullptr ? nullptr : binding->decl;
            }
        }
    }
    return nullptr;
}

void CodeGen::zero_bytes(llvm::Value *slot, uint64_t offset, uint64_t size) {
    uint64_t at = offset;
    const uint64_t end = offset + size;
    while (at < end) {
        uint64_t chunk = 1;
        llvm::Type *ty = i8();
        if (at % 8 == 0 && end - at >= 8) {
            chunk = 8;
            ty = i64();
        } else if (at % 4 == 0 && end - at >= 4) {
            chunk = 4;
            ty = i32();
        } else if (at % 2 == 0 && end - at >= 2) {
            chunk = 2;
            ty = i16();
        }
        llvm::Value *dest = slot;
        if (at != 0) {
            dest = builder_.CreateInBoundsGEP(i8(), slot, {llvm::ConstantInt::get(i64(), at)});
        }
        builder_.CreateStore(llvm::Constant::getNullValue(ty), dest);
        at += chunk;
    }
}

void CodeGen::zero_leaves(llvm::Value *slot, llvm::Type *ty) {
    if (ty == nullptr || !ty->isSized()) {
        return;
    }
    if (auto *array = llvm::dyn_cast<llvm::ArrayType>(ty)) {
        llvm::Type *element = array->getElementType();
        for (uint64_t index = 0; index < array->getNumElements(); ++index) {
            llvm::Value *at = builder_.CreateInBoundsGEP(
                ty, slot, {const_i32(0), llvm::ConstantInt::get(i64(), index)});
            zero_leaves(at, element);
        }
        return;
    }
    if (auto *structure = llvm::dyn_cast<llvm::StructType>(ty)) {
        const llvm::DataLayout &layout_data = module_.getDataLayout();
        const llvm::StructLayout *layout = layout_data.getStructLayout(structure);
        uint64_t cursor = 0;
        for (unsigned index = 0; index < structure->getNumElements(); ++index) {
            const uint64_t field_at = layout->getElementOffset(index);
            if (field_at > cursor) {
                zero_bytes(slot, cursor, field_at - cursor);
            }
            llvm::Type *field = structure->getElementType(index);
            zero_leaves(builder_.CreateStructGEP(structure, slot, index), field);
            cursor = field_at + layout_data.getTypeAllocSize(field).getFixedValue();
        }
        const uint64_t total = layout_data.getTypeAllocSize(structure).getFixedValue();
        if (total > cursor) {
            zero_bytes(slot, cursor, total - cursor);
        }
        return;
    }
    builder_.CreateStore(llvm::Constant::getNullValue(ty), slot);
}

void CodeGen::zero_slot(llvm::Value *slot, llvm::Type *ty) {
    if (slot == nullptr || ty == nullptr || !ty->isSized()) {
        return;
    }
    if (module_.getDataLayout().getTypeAllocSize(ty).getFixedValue() == 0) {
        return;
    }
    // One store per leaf. A later store, or a leaf that is never read, can be deleted.
    // One aggregate zero cannot: a single live byte keeps every element.
    zero_leaves(slot, ty);
}

void CodeGen::emit_bind(const Stmt::Bind &node) {
    if (node.value == nullptr || node.names.empty() || scopes_.empty()) {
        error(node.loc, "type mismatch");
        return;
    }
    auto note = [&](const std::string &name, const Binding &binding) {
        SourceLoc loc = node.loc;
        for (std::size_t i = 0; i < node.names.size() && i < node.name_locs.size(); ++i) {
            if (node.names[i] == name) {
                loc = node.name_locs[i];
                break;
            }
        }
        debug_declare(name, loc, binding, 0);
    };
    auto create_scalar = [&](const std::string &name, const TypedValue &got) {
        if (got.is_tuple && got.value != nullptr && got.value->getType()->isStructTy()) {
            llvm::AllocaInst *slot = alloca_typed(got.value->getType(), name);
            builder_.CreateStore(got.value, slot);
            Binding binding;
            binding.value = slot;
            binding.is_tuple = true;
            binding.tuple_parts = got.tuple_parts;
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        // A designator's depth is the return depth. The value itself is already a function pointer.
        if (got.is_slice) {
            llvm::AllocaInst *slot = alloca_typed(slice_type(), name);
            llvm::Value *stored = got.value != nullptr && got.value->getType() == slice_type() ? got.value
                                                                                              : as_slice_value(got, node.loc);
            builder_.CreateStore(stored, slot);
            Binding binding;
            binding.value = slot;
            binding.is_slice = true;
            binding.array_len = -1;
            // A string literal is still a pointer here. The slice element is what that pointer addresses.
            binding.depth = got.value != nullptr && got.value->getType() != slice_type() && got.depth > 0 ? got.depth - 1
                                                                                                          : got.depth;
            binding.elem_char = got.elem_char;
            binding.struct_id = got.struct_id;
            binding.elem_void = got.elem_void;
            binding.elem_bool = got.elem_bool;
            binding.elem_bits = got.elem_bits;
            binding.elem_unsigned = got.elem_unsigned;
            binding.elem_float = got.elem_float;
            binding.enum_id = got.enum_id;
            binding.inner_array_len = got.inner_array_len;
            binding.elem_slice = got.elem_slice;
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        // `var copy = row` copies the array. `*i32(row)` has no array type and stays a pointer.
        if (got.array_type != nullptr && got.array_type->isArrayTy() && got.value != nullptr) {
            auto *array_ty = llvm::cast<llvm::ArrayType>(got.array_type);
            llvm::AllocaInst *slot = alloca_typed(array_ty, name);
            llvm::Value *loaded = array_bits(got.value, array_ty, node.loc);
            if (loaded == nullptr || terminated()) {
                return;
            }
            builder_.CreateStore(loaded, slot);
            Binding binding;
            binding.value = slot;
            binding.depth = 0;
            binding.is_array = true;
            binding.array_len = static_cast<int>(array_ty->getNumElements());
            binding.elem_char = got.elem_char;
            binding.struct_id = got.struct_id;
            binding.elem_void = got.elem_void;
            binding.elem_bool = got.elem_bool;
            binding.elem_bits = got.elem_bits;
            binding.elem_unsigned = got.elem_unsigned;
            binding.elem_float = got.elem_float;
            binding.enum_id = got.enum_id;
            binding.elem_slice = got.elem_slice;
            TypeShell outer;
            outer.kind = TypeShell::Kind::Array;
            outer.length = binding.array_len;
            binding.shells.push_back(outer);
            binding.shells.insert(binding.shells.end(), got.shells.begin(), got.shells.end());
            if (binding.shells.size() >= 2 && binding.shells[1].kind == TypeShell::Kind::Array) {
                binding.inner_array_len = binding.shells[1].length;
            }
            if (binding.shells.size() >= 2 && binding.shells[1].kind == TypeShell::Kind::Slice) {
                binding.elem_slice = true;
            }
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        if (auto *function = llvm::dyn_cast_or_null<llvm::Function>(got.value)) {
            llvm::AllocaInst *slot = alloca_typed(ptr_type(), name);
            builder_.CreateStore(function, slot);
            Binding binding;
            binding.value = slot;
            binding.depth = 1;
            binding.fn_ty = got.fn_ty != nullptr ? got.fn_ty : function->getFunctionType();
            binding.fn_ret_depth = got.depth;
            binding.fn_ret_float = got.elem_float;
            binding.fn_ret_bits = got.elem_bits;
            binding.fn_ret_unsigned = got.elem_unsigned;
            binding.fn_ret_char = got.elem_char;
            binding.fn_ret_bool = got.elem_bool;
            binding.fn_ret_struct = got.struct_id;
            binding.fn_results = got.fn_results;
            scopes_.back()[name] = binding;
            alias_capture(name, binding);
            note(name, binding);
            return;
        }
        llvm::Type *ty = i32();
        Binding binding;
        if (got.enum_id >= 0 && got.enum_id < static_cast<int>(enums_.size())) {
            const EnumLayout &layout = enums_[static_cast<std::size_t>(got.enum_id)];
            binding.enum_id = got.enum_id;
            binding.elem_bool = layout.is_bool;
            binding.elem_char = layout.is_char;
            binding.elem_unsigned = layout.is_unsigned;
            binding.elem_bits = layout.is_bool ? 0 : layout.bits;
            ty = leaf_type(0, binding.elem_char, -1, binding.elem_bool, binding.elem_bits, false);
        } else if (got.depth > 0) {
            ty = ptr_type();
            binding.depth = got.depth;
            binding.elem_char = got.elem_char;
            binding.struct_id = got.struct_id;
            binding.elem_void = got.elem_void;
            binding.elem_bool = got.elem_bool;
            binding.elem_bits = got.elem_bits;
            binding.elem_unsigned = got.elem_unsigned;
            binding.elem_float = got.elem_float;
            binding.ptr_to_array = got.ptr_to_array;
            binding.ptr_array_len = got.ptr_array_len;
            binding.elem_slice = got.elem_slice;
            binding.fn_ty = got.fn_ty;
            binding.fn_ret_depth = got.fn_ret_depth;
            binding.fn_ret_float = got.fn_ret_float;
            binding.fn_ret_bits = got.fn_ret_bits;
            binding.fn_ret_unsigned = got.fn_ret_unsigned;
            binding.fn_ret_char = got.fn_ret_char;
            binding.fn_ret_bool = got.fn_ret_bool;
            binding.fn_ret_struct = got.fn_ret_struct;
            binding.fn_results = got.fn_results;
        } else if (got.struct_id >= 0 && got.struct_id < static_cast<int>(structs_.size()) &&
                   structs_[static_cast<std::size_t>(got.struct_id)].type != nullptr) {
            ty = structs_[static_cast<std::size_t>(got.struct_id)].type;
            binding.struct_id = got.struct_id;
        } else if (got.elem_float || (got.value != nullptr && got.value->getType()->isFloatingPointTy())) {
            // An f64 value is 64 bits. Checking the width first stored it as i64.
            const bool value_is_float = got.value != nullptr && got.value->getType()->isFloatingPointTy();
            const bool wide = value_is_float ? got.value->getType()->isDoubleTy() : got.elem_bits >= 64;
            ty = wide ? f64_ty() : f32_ty();
            binding.elem_float = true;
            binding.elem_bits = wide ? 64 : 32;
        } else if (got.elem_bits == 64) {
            ty = i64();
            binding.elem_bits = 64;
            binding.elem_unsigned = got.elem_unsigned;
        } else if (got.elem_unsigned) {
            binding.elem_bits = 32;
            binding.elem_unsigned = true;
        }
        llvm::AllocaInst *slot = alloca_typed(ty, name);
        llvm::Value *stored = got.value == nullptr ? llvm::Constant::getNullValue(ty) : got.value;
        if (stored->getType() != ty) {
            stored = as_stored(stored, got.depth, ty, got.elem_unsigned);
        }
        builder_.CreateStore(stored, slot);
        binding.value = slot;
        scopes_.back()[name] = binding;
        alias_capture(name, binding);
        note(name, binding);
    };
    auto destination = [&](const std::string &name, bool &create) -> llvm::Value * {
        create = node.is_var || node.is_const;
        if (create) {
            return nullptr;
        }
        if (node.is_define) {
            const auto found = scopes_.back().find(name);
            if (found == scopes_.back().end()) {
                create = true;
                return nullptr;
            }
            return found->second.value;
        }
        const Binding *found = lookup(name);
        return found == nullptr ? nullptr : found->value;
    };
    if (node.names.size() == 1) {
        if (const auto *init = std::get_if<Expr::Init>(&node.value->data); init != nullptr && init->array_literal) {
            bool create = false;
            destination(node.names[0], create);
            if (!create) {
                error(node.loc, "not an lvalue");
                return;
            }
            Lowered low;
            llvm::AllocaInst *slot = emit_array_literal(*init, low);
            if (slot == nullptr || terminated()) {
                return;
            }
            Binding binding;
            apply_low(binding, low);
            binding.value = slot;
            binding.is_array = true;
            binding.array_len = low.array_len;
            scopes_.back()[node.names[0]] = binding;
            alias_capture(node.names[0], binding);
            note(node.names[0], binding);
            return;
        }
        const TypedValue value = emit_rvalue(*node.value);
        if (terminated()) {
            return;
        }
        bool create = false;
        llvm::Value *dest = destination(node.names[0], create);
        if (create) {
            create_scalar(node.names[0], value);
        } else if (dest != nullptr) {
            const Binding *found = lookup(node.names[0]);
            if (found != nullptr && found->is_slice) {
                builder_.CreateStore(as_slice_value(value, node.loc), dest);
            } else {
                llvm::Type *ty = object_type(dest);
                builder_.CreateStore(as_stored(value.value, value.depth, ty, value.elem_unsigned), dest);
            }
        }
        return;
    }
    // A call's component types live on the function, or on the function-pointer type.
    // A tuple value carries the same slots on `tuple_parts`.
    const FunctionDecl *decl = nullptr;
    std::vector<FnParam> fn_results;
    TypedValue value;
    if (const auto *call = std::get_if<Expr::Call>(&node.value->data)) {
        const PreparedCall prepared = prepare_call(*call);
        decl = prepared.decl;
        fn_results = prepared.fn_results;
        value = emit_prepared(prepared);
    } else {
        value = emit_rvalue(*node.value);
    }
    if (terminated()) {
        return;
    }
    std::vector<Lowered> parts;
    if (decl != nullptr && value.value != nullptr && decl->results.size() == node.names.size() &&
        value.value->getType()->isStructTy()) {
        parts.reserve(decl->results.size());
        for (const ResultSlot &slot : decl->results) {
            parts.push_back(lower_result(current_file_, slot));
        }
    } else if (decl == nullptr && value.value != nullptr && fn_results.size() == node.names.size() &&
               value.value->getType()->isStructTy()) {
        parts.reserve(fn_results.size());
        for (const FnParam &part : fn_results) {
            const bool array = part.is_array && !part.is_fn_ptr && part.array_len != 0;
            parts.push_back(lower(current_file_, part.type_name, part.type_package, part.is_struct, part.indirections,
                                  array, array ? part.array_len : 0, false, part.elem_slice));
        }
    } else if (value.is_tuple && value.value != nullptr && value.value->getType()->isStructTy() &&
               value.tuple_parts.size() == node.names.size()) {
        parts.reserve(value.tuple_parts.size());
        for (const ResultSlot &slot : value.tuple_parts) {
            parts.push_back(lower(current_file_, slot.type_name, slot.type_package, slot.is_struct, slot.indirections,
                                  slot.is_array, slot.array_len, false, slot.elem_slice));
        }
    } else if (value.value != nullptr && value.value->getType()->isStructTy() && value.tuple_parts.empty()) {
        auto *aggregate = llvm::cast<llvm::StructType>(value.value->getType());
        if (aggregate->getNumElements() != node.names.size()) {
            error(node.loc, "type mismatch");
            return;
        }
        parts.reserve(node.names.size());
        for (unsigned index = 0; index < aggregate->getNumElements(); ++index) {
            llvm::Type *ty = aggregate->getElementType(index);
            Lowered low;
            if (ty != nullptr && ty->isIntegerTy(1)) {
                low.elem_bool = true;
                low.type = leaf_type(0, false, -1, true, 0, false);
            } else if (ty != nullptr && ty->isIntegerTy(8)) {
                low.elem_bits = 8;
                low.elem_unsigned = true;
                low.type = leaf_type(0, false, -1, false, 8, false);
            } else if (ty != nullptr && ty->isIntegerTy(16)) {
                low.elem_bits = 16;
                low.type = leaf_type(0, false, -1, false, 16, false);
            } else if (ty != nullptr && ty->isIntegerTy(64)) {
                low.elem_bits = 64;
                low.type = leaf_type(0, false, -1, false, 64, false);
            } else if (ty != nullptr && ty->isFloatingPointTy()) {
                low.elem_float = true;
                low.elem_bits = ty->isDoubleTy() ? 64 : 32;
                low.type = leaf_type(0, false, -1, false, low.elem_bits, true);
            } else if (ty == slice_type()) {
                low.is_slice = true;
                low.array_len = -1;
                low.type = ty;
            } else if (ty != nullptr && ty->isArrayTy()) {
                low.is_array = true;
                low.array_len = static_cast<int>(llvm::cast<llvm::ArrayType>(ty)->getNumElements());
                low.type = ty;
            } else if (ty != nullptr && ty->isPointerTy()) {
                low.depth = 1;
                low.type = ptr_type();
            } else if (ty != nullptr && ty->isStructTy()) {
                for (int id = 0; id < static_cast<int>(structs_.size()); ++id) {
                    if (structs_[static_cast<std::size_t>(id)].type == ty) {
                        low.struct_id = id;
                        break;
                    }
                }
                low.type = ty;
            } else {
                low.type = ty == nullptr ? i32() : ty;
            }
            parts.push_back(std::move(low));
        }
    } else {
        error(node.loc, "type mismatch");
        return;
    }
    for (std::size_t index = 0; index < node.names.size(); ++index) {
        const Lowered &low = parts[index];
        llvm::Value *field = builder_.CreateExtractValue(value.value, static_cast<unsigned>(index));
        bool create = false;
        llvm::Value *dest = destination(node.names[index], create);
        if (create) {
            llvm::Type *ty = stored_type(low);
            llvm::AllocaInst *slot_alloca = alloca_typed(ty, node.names[index]);
            llvm::Value *stored = field;
            if (stored == nullptr) {
                stored = llvm::Constant::getNullValue(ty);
            } else if (low.is_slice && stored->getType() != ty) {
                TypedValue part;
                part.value = stored;
                part.is_slice = true;
                stored = as_slice_value(part, node.loc);
            } else if (stored->getType() != ty) {
                stored = as_stored(stored, low.depth, ty, low.elem_unsigned);
            }
            builder_.CreateStore(stored, slot_alloca);
            Binding binding;
            binding.value = slot_alloca;
            apply_low(binding, low);
            scopes_.back()[node.names[index]] = binding;
            alias_capture(node.names[index], binding);
            note(node.names[index], binding);
        } else if (dest != nullptr) {
            if (low.is_slice) {
                llvm::Value *stored = field;
                if (stored == nullptr || stored->getType() != slice_type()) {
                    TypedValue part;
                    part.value = stored == nullptr ? llvm::ConstantAggregateZero::get(slice_type()) : stored;
                    part.is_slice = true;
                    stored = as_slice_value(part, node.loc);
                }
                builder_.CreateStore(stored, dest);
            } else {
                llvm::Type *ty = object_type(dest);
                builder_.CreateStore(as_stored(field, low.depth, ty, low.elem_unsigned), dest);
            }
        }
        if (terminated()) {
            return;
        }
    }
}

void CodeGen::emit_stmt(const Stmt &stmt) {
    set_loc(std::visit(
        [](const auto &node) -> SourceLoc {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Func>) {
                return node.decl ? node.decl->loc : SourceLoc{};
            } else {
                return node.loc;
            }
        },
        stmt.data));
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                emit_case(node);
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                const bool share = share_function_scope_;
                share_function_scope_ = false;
                if (!share) {
                    push();
                    debug_enter(node.loc);
                }
                defers_.push_back(DeferFrame{static_cast<int>(loops_.size()), {}});
                for (const auto &child : node.statements) {
                    if (child) {
                        emit_stmt(*child);
                    }
                }
                if (!terminated() && !defers_.empty()) {
                    emit_defer_frame(defers_.back());
                }
                if (!defers_.empty()) {
                    defers_.pop_back();
                }
                if (!share) {
                    debug_leave();
                    pop();
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    emit_local_function(*node.decl);
                }
            } else if (terminated()) {
                return;
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (extern_struct_ret_ && extern_ret_struct_ != nullptr) {
                    llvm::Value *value = nullptr;
                    if (node.value) {
                        const TypedValue got = emit_rvalue(*node.value);
                        value = got.value;
                    }
                    if (value == nullptr || value->getType() != extern_ret_struct_) {
                        value = llvm::ConstantAggregateZero::get(extern_ret_struct_);
                    }
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (terminated()) {
                        return;
                    }
                    if (extern_sret_ && extern_sret_ptr_ != nullptr) {
                        builder_.CreateStore(value, extern_sret_ptr_);
                        builder_.CreateRetVoid();
                        return;
                    }
                    const unsigned align = std::max(extern_ret_abi_.natural_align, 8u);
                    llvm::AllocaInst *slot = aligned_slot(extern_ret_struct_, align, "ret");
                    builder_.CreateStore(value, slot);
                    builder_.CreateRet(pack_return(slot, extern_ret_abi_));
                    return;
                }
                if (return_many_ && return_type_ != nullptr && return_type_->isStructTy()) {
                    std::vector<const Expr *> exprs;
                    if (node.value) {
                        exprs.push_back(node.value.get());
                    }
                    for (const auto &extra : node.extra) {
                        if (extra) {
                            exprs.push_back(extra.get());
                        }
                    }
                    auto *aggregate_type = llvm::cast<llvm::StructType>(return_type_);
                    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(aggregate_type);
                    const unsigned count = aggregate_type->getNumElements();
                    auto fit_piece = [&](llvm::Type *field, const TypedValue &value) -> llvm::Value * {
                        if (value.value == nullptr || field == nullptr) {
                            return field == nullptr ? zero() : llvm::Constant::getNullValue(field);
                        }
                        if (field == slice_type()) {
                            return as_slice_value(value, node.loc);
                        }
                        if (field->isPointerTy()) {
                            // `*[N]T`, `?*[N]T`, and `**[N]T` are pointers at leaf depth 0.
                            // A non-pointer still becomes null rather than a bitcast of an integer.
                            return value.value->getType()->isPointerTy() ? value.value : null_ptr();
                        }
                        // A local `[N]T` decays to a pointer. The result slot still holds the array.
                        if (field->isArrayTy() && value.array_len > 0 && value.value != nullptr &&
                            (value.value->getType() == field || value.value->getType()->isPointerTy())) {
                            if (llvm::Value *loaded = array_bits(value.value, field, node.loc)) {
                                return loaded;
                            }
                        }
                        if (field->isIntegerTy() && value.value->getType()->isIntegerTy()) {
                            return resize_int(value.value, field, value.elem_unsigned);
                        }
                        if (value.value->getType() == field) {
                            return value.value;
                        }
                        return llvm::Constant::getNullValue(field);
                    };
                    // `return args` and `return (40, 'a')` are one tuple. Spread it across the results.
                    if (exprs.size() == 1 && exprs[0] != nullptr) {
                        const TypedValue value = emit_rvalue(*exprs[0]);
                        if (terminated()) {
                            return;
                        }
                        const bool matching = value.is_tuple && value.value != nullptr &&
                                              value.value->getType()->isStructTy() &&
                                              llvm::cast<llvm::StructType>(value.value->getType())->getNumElements() == count;
                        if (matching) {
                            llvm::Value *converted = as_stored(value.value, 0, aggregate_type, value.elem_unsigned);
                            if (converted != nullptr && converted->getType() == aggregate_type) {
                                aggregate = converted;
                            }
                        } else if (count > 0) {
                            aggregate = builder_.CreateInsertValue(aggregate, fit_piece(aggregate_type->getElementType(0), value),
                                                                   0);
                        }
                        if (!terminated()) {
                            emit_defers_from(0);
                        }
                        if (terminated()) {
                            return;
                        }
                        builder_.CreateRet(aggregate);
                        return;
                    }
                    for (unsigned index = 0; index < count; ++index) {
                        llvm::Type *field = aggregate_type->getElementType(index);
                        TypedValue value;
                        if (index < exprs.size() && exprs[index] != nullptr) {
                            value = emit_rvalue(*exprs[index]);
                        }
                        if (terminated()) {
                            return;
                        }
                        aggregate = builder_.CreateInsertValue(aggregate, fit_piece(field, value), index);
                    }
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (terminated()) {
                        return;
                    }
                    builder_.CreateRet(aggregate);
                    return;
                }
                if (return_type_ == slice_type() && node.value != nullptr) {
                    const TypedValue value = emit_rvalue(*node.value);
                    llvm::Value *packed = value.value == nullptr ? llvm::ConstantAggregateZero::get(slice_type())
                                                                : as_slice_value(value, node.loc);
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (!terminated()) {
                        builder_.CreateRet(packed);
                    }
                    return;
                }
                if (return_type_ != nullptr && return_type_->isArrayTy() && node.value != nullptr) {
                    llvm::Value *loaded = nullptr;
                    if (const auto *init = std::get_if<Expr::Init>(&node.value->data);
                        init != nullptr && array_brace(*init)) {
                        // `{4, 6}` and `[_]i32{4, 6}` are lists. Store them in the result array.
                        auto *array_ty = llvm::cast<llvm::ArrayType>(return_type_);
                        llvm::AllocaInst *slot = alloca_typed(return_type_, "array");
                        store_array_list(slot, array_ty, *init);
                        if (!terminated()) {
                            loaded = builder_.CreateLoad(return_type_, slot);
                        }
                    } else {
                        // A name is the array address. A call yields the address of its first element.
                        const TypedValue value = emit_rvalue(*node.value);
                        if (!terminated()) {
                            loaded = array_bits(value.value, return_type_, node.loc);
                        }
                    }
                    if (terminated() || loaded == nullptr) {
                        return;
                    }
                    emit_defers_from(0);
                    if (!terminated()) {
                        builder_.CreateRet(loaded);
                    }
                    return;
                }
                llvm::Value *ret = nullptr;
                const bool returns_void = return_type_ != nullptr && return_type_->isVoidTy();
                if (!returns_void && node.value) {
                    const TypedValue value = emit_rvalue(*node.value);
                    ret = value.value;
                    if (return_depth_ > 0 && value.depth == 0 && ret != nullptr && !ret->getType()->isPointerTy()) {
                        ret = null_ptr();
                    } else if (ret != nullptr && return_type_ != nullptr &&
                               (return_type_->isFloatingPointTy() || ret->getType()->isFloatingPointTy())) {
                        ret = as_stored(ret, 0, return_type_, value.elem_unsigned);
                    } else if (ret != nullptr && return_type_ != nullptr && ret->getType()->isIntegerTy() &&
                               return_type_->isIntegerTy() && ret->getType() != return_type_) {
                        ret = resize_int(ret, return_type_, value.elem_unsigned);
                    }
                }
                if (!terminated()) {
                    emit_defers_from(0);
                }
                if (terminated()) {
                    return;
                }
                if (returns_void) {
                    builder_.CreateRetVoid();
                } else if (node.value) {
                    builder_.CreateRet(ret);
                } else if (return_type_ == slice_type()) {
                    builder_.CreateRet(llvm::ConstantAggregateZero::get(slice_type()));
                } else if (return_depth_ > 0) {
                    builder_.CreateRet(null_ptr());
                } else if (return_type_ != nullptr && !return_type_->isIntegerTy(32)) {
                    builder_.CreateRet(llvm::Constant::getNullValue(return_type_));
                } else {
                    builder_.CreateRet(zero());
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    emit_rvalue(*node.expr);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                const SourceLoc if_loc = node.loc;
                llvm::Value *cond = emit_rvalue(*node.cond).value;
                if (terminated()) {
                    return;
                }
                llvm::BasicBlock *then_block = llvm::BasicBlock::Create(context_, "if.then", function_);
                llvm::BasicBlock *else_block = llvm::BasicBlock::Create(context_, "if.else", function_);
                llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(context_, "if.end", function_);
                // The branch itself is the if line. The arms must not leak their
                // lines onto the path that skips them.
                set_loc(if_loc);
                builder_.CreateCondBr(is_nonzero(cond), then_block, else_block);
                builder_.SetInsertPoint(then_block);
                if (node.then_arm) {
                    emit_stmt(*node.then_arm);
                }
                if (!terminated()) {
                    builder_.CreateBr(merge_block);
                }
                builder_.SetInsertPoint(else_block);
                set_loc(if_loc);
                if (node.else_arm) {
                    emit_stmt(*node.else_arm);
                }
                if (!terminated()) {
                    if (node.else_arm == nullptr) {
                        set_loc(if_loc);
                    }
                    builder_.CreateBr(merge_block);
                }
                builder_.SetInsertPoint(merge_block);
                set_loc(if_loc);
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "while.cond", function_);
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "while.body", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "while.end", function_);
                builder_.CreateBr(cond_block);
                builder_.SetInsertPoint(cond_block);
                llvm::Value *cond = emit_rvalue(*node.cond).value;
                if (!terminated()) {
                    builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                }
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{cond_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(end_block);
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "do.body", function_);
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "do.cond", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "do.end", function_);
                builder_.CreateBr(body_block);
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{cond_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(cond_block);
                llvm::Value *cond = node.cond ? emit_rvalue(*node.cond).value : zero();
                if (!terminated()) {
                    builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                }
                builder_.SetInsertPoint(end_block);
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                emit_switch(node);
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                push();
                if (node.init) {
                    emit_stmt(*node.init);
                }
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "for.cond", function_);
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "for.body", function_);
                llvm::BasicBlock *step_block = llvm::BasicBlock::Create(context_, "for.step", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "for.end", function_);
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(cond_block);
                if (node.cond) {
                    llvm::Value *cond = emit_rvalue(*node.cond).value;
                    if (!terminated()) {
                        builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                    }
                } else if (!terminated()) {
                    builder_.CreateBr(body_block);
                }
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{step_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(step_block);
                }
                builder_.SetInsertPoint(step_block);
                if (node.step && !terminated()) {
                    emit_rvalue(*node.step);
                }
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(end_block);
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                if (!loops_.empty()) {
                    emit_defers_from(static_cast<int>(loops_.size()));
                    if (!terminated()) {
                        builder_.CreateBr(loops_.back().after);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                int target = -1;
                for (int index = static_cast<int>(loops_.size()) - 1; index >= 0; --index) {
                    if (!loops_[static_cast<std::size_t>(index)].is_switch) {
                        target = index;
                        break;
                    }
                }
                if (target >= 0 && loops_[static_cast<std::size_t>(target)].step != nullptr) {
                    emit_defers_from(target + 1);
                    if (!terminated()) {
                        builder_.CreateBr(loops_[static_cast<std::size_t>(target)].step);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call == nullptr) {
                    return;
                }
                const auto *call = std::get_if<Expr::Call>(&node.call->data);
                if (call == nullptr) {
                    error(node.loc, "type mismatch");
                    return;
                }
                if (call->callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&call->callee->data)) {
                        if (const Binding *binding = lookup(named->text); binding != nullptr && binding->is_generic) {
                            error(node.loc, "not supported yet");
                            return;
                        }
                    }
                }
                // Arguments and the receiver are evaluated here. The call runs when the block is left.
                const PreparedCall prepared = prepare_call(*call);
                if (prepared.function == nullptr || terminated() || defers_.empty()) {
                    return;
                }
                DeferredCall item;
                item.flag = defer_flag();
                item.function = prepared.function;
                for (llvm::Value *arg : prepared.args) {
                    if (arg == nullptr) {
                        continue;
                    }
                    llvm::AllocaInst *slot = alloca_typed(arg->getType(), "defer.arg");
                    builder_.CreateStore(arg, slot);
                    item.args.push_back(slot);
                }
                builder_.CreateStore(llvm::ConstantInt::getTrue(context_), item.flag);
                defers_.back().calls.push_back(std::move(item));
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                emit_bind(node);
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    if (var.name.empty()) {
                        continue;
                    }
                    const Lowered low = lower_declared(current_file_, var);
                    if (low.is_slice) {
                        llvm::AllocaInst *slot = alloca_typed(slice_type(), var.name);
                        Binding binding;
                        binding.value = slot;
                        apply_low(binding, low);
                        binding.is_slice = true;
                        scopes_.back()[var.name] = binding;
                        alias_capture(var.name, binding);
                        debug_declare(var.name, var.loc, binding, 0);
                        if (var.init != nullptr) {
                            const TypedValue value = emit_rvalue(*var.init);
                            if (!terminated()) {
                                builder_.CreateStore(as_slice_value(value, var.loc), slot);
                            }
                        } else if (!var.is_extern && !var.is_const_decl) {
                            zero_slot(slot, slice_type());
                        }
                        continue;
                    }
                    if (!var.ptr_to_array && (var.is_array || low.is_array)) {
                        if (!low.is_array || low.array_len <= 0) {
                            error(var.loc, "not supported yet");
                            continue;
                        }
                        llvm::Type *ty = stored_type(low);
                        llvm::AllocaInst *slot = alloca_typed(ty, var.name);
                        auto *array_ty = ty->isArrayTy() ? llvm::cast<llvm::ArrayType>(ty) : nullptr;
                        Binding binding;
                        binding.value = slot;
                        binding.depth = low.depth;
                        binding.elem_char = low.elem_char;
                        binding.is_array = true;
                        binding.array_len = low.array_len;
                        binding.struct_id = low.struct_id;
                        binding.elem_void = low.elem_void;
                        binding.elem_bool = low.elem_bool;
                        binding.elem_bits = low.elem_bits;
                        binding.elem_unsigned = low.elem_unsigned;
                        binding.elem_float = low.elem_float;
                        binding.enum_id = low.enum_id;
                        binding.elem_slice = low.elem_slice;
                        binding.inner_array_len = low.inner_array_len;
                        binding.shells = low.shells;
                        scopes_.back()[var.name] = binding;
                        alias_capture(var.name, binding);
                        debug_declare(var.name, var.loc, binding, 0);
                        // A brace list and an array value share one store. The name is in scope first,
                        // so an element can read it. A struct value is not this list.
                        if (var.init != nullptr && array_ty != nullptr) {
                            const auto *list = std::get_if<Expr::Init>(&var.init->data);
                            if (list == nullptr || array_brace(*list)) {
                                store_array(slot, array_ty, *var.init, var.loc);
                            }
                        }
                        // A bare array of structs stores each field that has a value. A list does not.
                        if (var.init == nullptr && array_ty != nullptr && low.struct_id >= 0 && low.depth == 0 &&
                            !low.is_slice && !low.elem_slice && !var.is_extern && !var.is_const_decl) {
                            std::unordered_set<int> seen;
                            paint_omitted_elements(slot, array_ty, low.struct_id, nullptr, seen);
                        }
                        continue;
                    }
                    llvm::Type *ty = stored_type(low);
                    llvm::AllocaInst *slot = alloca_typed(ty, var.name);
                    Binding binding;
                    binding.value = slot;
                    binding.depth = low.depth;
                    binding.elem_char = low.elem_char;
                    binding.struct_id = low.struct_id;
                    binding.elem_void = low.elem_void;
                    binding.elem_bool = low.elem_bool;
                    apply_low(binding, low);
                    scopes_.back()[var.name] = binding;
                    alias_capture(var.name, binding);
                    debug_declare(var.name, var.loc, binding, 0);
                    if (var.init) {
                        const auto *made = std::get_if<Expr::Init>(&var.init->data);
                        if (made != nullptr && made->struct_id >= 0 && made->elements.empty() && !made->is_tuple) {
                            if (made->zero_rest) {
                                builder_.CreateStore(llvm::ConstantAggregateZero::get(ty), slot);
                            } else if (low.struct_id >= 0) {
                                std::unordered_set<int> seen;
                                store_struct_defaults(slot, low.struct_id, seen, made);
                            }
                            store_init_fields(made->struct_id, slot, *made);
                        } else {
                            // The initializer is the first value. Clearing the slot first would store a
                            // zero the programmer did not write.
                            const TypedValue value = emit_rvalue(*var.init);
                            if (!terminated()) {
                                builder_.CreateStore(
                                    as_stored(value.value, value.depth, object_type(slot), value.elem_unsigned), slot);
                            }
                        }
                    } else if (!var.is_extern && !var.is_const_decl && low.struct_id >= 0 && low.depth == 0 &&
                               !low.is_array && !low.is_slice) {
                        std::unordered_set<int> seen;
                        store_struct_defaults(slot, low.struct_id, seen);
                    }
                }
            }
        },
        stmt.data);
}

} // namespace tlc
