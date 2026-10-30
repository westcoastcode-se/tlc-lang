// Addresses, indexes, field loads, and slices.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

TypedValue CodeGen::emit_addr(const Expr &expr) {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(name->text);
        if (binding == nullptr || binding->is_function) {
            error(name->loc, "not an lvalue");
            return {};
        }
        // `copy = row` stores into this array. The address is the binding, not a decayed pointer.
        if (binding->is_array) {
            TypedValue slot{binding->value, binding->depth, binding->elem_char, binding->struct_id, true,
                            binding->elem_void, binding->elem_bool, binding->elem_bits, binding->elem_unsigned,
                            binding->trait_id, binding->elem_float, binding->fn_ty, binding->fn_ret_depth,
                            binding->fn_ret_float, binding->fn_ret_bits, binding->fn_ret_unsigned, binding->fn_ret_char,
                            binding->fn_ret_bool, binding->fn_ret_struct, binding->fn_results, binding->enum_id};
            slot.array_len = binding->array_len;
            slot.inner_array_len = binding->inner_array_len;
            slot.elem_slice = binding->elem_slice;
            slot.shells = binding->shells;
            slot.tuple_parts = binding->tuple_parts;
            if (llvm::Type *object = object_type(binding->value); object != nullptr && object->isArrayTy()) {
                slot.array_type = object;
            }
            return slot;
        }
        TypedValue slot{binding->value, binding->depth, binding->elem_char, binding->struct_id, false, binding->elem_void,
                        binding->elem_bool, binding->elem_bits, binding->elem_unsigned, binding->trait_id, binding->elem_float,
                        binding->fn_ty, binding->fn_ret_depth, binding->fn_ret_float, binding->fn_ret_bits,
                        binding->fn_ret_unsigned, binding->fn_ret_char, binding->fn_ret_bool, binding->fn_ret_struct,
                        binding->fn_results, binding->enum_id};
        slot.is_slice = binding->is_slice;
        slot.elem_slice = binding->elem_slice;
        slot.shells = binding->shells;
        if (binding->is_tuple) {
            slot.is_tuple = true;
            slot.tuple_parts = binding->tuple_parts;
        }
        return slot;
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data); star != nullptr && star->op == TokenKind::Star) {
        const TypedValue inner = emit_rvalue(*star->expr);
        // `*Adder` passed a function is that function pointer. `*a` does not load through it.
        if (inner.value != nullptr && trait_fn_argument(inner)) {
            TypedValue fn = inner;
            fn.depth = 0;
            return fn;
        }
        const bool pointer_shell = !inner.shells.empty() && inner.shells.front().kind == TypeShell::Kind::Pointer;
        if ((!pointer_shell && inner.depth <= 0) || inner.value == nullptr) {
            error(star->loc, "not an lvalue");
            return {};
        }
        // `(*p)[0] = 1` stores into the tuple. The pointer is that address.
        if (!pointer_shell && inner.depth == 1 && !inner.tuple_parts.empty() && !inner.ptr_to_array &&
            inner.value->getType()->isPointerTy()) {
            TypedValue addr;
            addr.value = inner.value;
            addr.is_tuple = true;
            addr.tuple_parts = inner.tuple_parts;
            return addr;
        }
        if (pointer_shell) {
            TypedValue peeled = inner;
            peeled.shells.erase(peeled.shells.begin());
            peeled.ptr_to_array = !peeled.shells.empty() && peeled.shells.front().kind == TypeShell::Kind::Array;
            peeled.ptr_array_len = peeled.ptr_to_array ? peeled.shells.front().length : 0;
            peeled.is_slice = !peeled.shells.empty() && peeled.shells.front().kind == TypeShell::Kind::Slice;
            if (peeled.is_slice) {
                peeled.array_len = -1;
            }
            return peeled;
        }
        return {inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, false, inner.elem_void, inner.elem_bool,
                inner.elem_bits, inner.elem_unsigned, inner.trait_id, inner.elem_float};
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        if (index->is_range) {
            error(index->loc, "not an lvalue");
            return {};
        }
        if (index->tuple_index >= 0) {
            // `t[0] = 1` stores into the tuple. The field was chosen while compiling.
            // `box.value[0]` is the same field inside a struct, not only a bare name.
            const auto *named = index->base == nullptr ? nullptr : std::get_if<Expr::Name>(&index->base->data);
            const Binding *binding = named == nullptr ? nullptr : lookup(named->text);
            llvm::Value *aggregate_ptr = nullptr;
            llvm::StructType *aggregate = nullptr;
            std::vector<ResultSlot> parts;
            if (binding != nullptr && binding->is_tuple && binding->value != nullptr) {
                aggregate_ptr = binding->value;
                aggregate = llvm::dyn_cast_or_null<llvm::StructType>(object_type(binding->value));
                parts = binding->tuple_parts;
            } else if (index->base != nullptr) {
                const TypedValue base = emit_addr(*index->base);
                if (base.value != nullptr && base.is_tuple && !base.tuple_parts.empty()) {
                    aggregate_ptr = base.value;
                    parts = base.tuple_parts;
                    std::vector<llvm::Type *> fields;
                    fields.reserve(parts.size());
                    for (const ResultSlot &slot : parts) {
                        fields.push_back(stored_type(lower_result(current_file_, slot)));
                    }
                    aggregate = llvm::StructType::get(context_, fields);
                }
            }
            const int part = index->tuple_index;
            if (aggregate_ptr == nullptr || aggregate == nullptr || parts.empty() || part < 0 ||
                static_cast<std::size_t>(part) >= parts.size()) {
                error(index->loc, "not an lvalue");
                return {};
            }
            llvm::Value *field = builder_.CreateInBoundsGEP(
                aggregate, aggregate_ptr, llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(part)});
            const ResultSlot &slot = parts[static_cast<std::size_t>(part)];
            const Lowered low = lower_result(current_file_, slot);
            TypedValue out{field,        low.depth,     low.elem_char, low.struct_id, low.is_array && !low.is_slice,
                           low.elem_void, low.elem_bool, low.elem_bits, low.elem_unsigned, -1, low.elem_float};
            out.enum_id = low.enum_id;
            out.is_slice = low.is_slice;
            out.array_len = low.array_len;
            out.inner_array_len = low.inner_array_len;
            out.elem_slice = low.elem_slice;
            out.shells = low.shells;
            // An array of tuples is not itself a tuple. `tuple_parts` is one element.
            if (!low.tuple_parts.empty()) {
                out.tuple_parts = low.tuple_parts;
            }
            if (low.is_tuple) {
                out.is_tuple = true;
            }
            if (low.type != nullptr && low.type->isArrayTy()) {
                out.array_type = low.type;
            }
            return out;
        }
        const TypedValue base = emit_rvalue(*index->base);
        const TypedValue subscript = emit_rvalue(*index->index);
        return index_address(base, subscript.value, index->loc);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (!selector->resolved_owner.empty()) {
            const Binding *found =
                visible(assoc_home(*selector), selector->resolved_owner + "." + selector->field, false);
            if (found == nullptr || found->value == nullptr || found->is_function || found->is_array) {
                error(selector->loc, "not an lvalue");
                return {};
            }
            TypedValue slot;
            slot.value = found->value;
            slot.depth = found->depth;
            slot.elem_char = found->elem_char;
            slot.struct_id = found->struct_id;
            slot.elem_void = found->elem_void;
            slot.elem_bool = found->elem_bool;
            slot.elem_bits = found->elem_bits;
            slot.elem_unsigned = found->elem_unsigned;
            slot.elem_float = found->elem_float;
            slot.trait_id = found->trait_id;
            slot.enum_id = found->enum_id;
            slot.fn_ty = found->fn_ty;
            return slot;
        }
        const auto *base_name = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const auto package = base_name == nullptr ? packages_.end() : packages_.find(base_name->text);
        if (base_name != nullptr && package != packages_.end()) {
            const Binding *found = visible(package->second, selector->field, false);
            if (found == nullptr || found->is_function || found->is_array) {
                error(selector->loc, found != nullptr && found->is_array ? "not supported yet" : "not an lvalue");
                return {};
            }
            return {found->value, found->depth, found->elem_char, found->struct_id, false, found->elem_void,
                    found->elem_bool, found->elem_bits, found->elem_unsigned, found->trait_id, found->elem_float};
        }
        if (selector->base == nullptr) {
            error(selector->loc, "not an lvalue");
            return {};
        }
        if (addressable(*selector->base)) {
            const TypedValue base = emit_addr(*selector->base);
            if (base.value == nullptr) {
                return {};
            }
            if (base.depth == 0 && base.struct_id >= 0) {
                return field_addr(base.struct_id, base.value, selector->field, selector->loc);
            }
            if (base.depth == 1 && base.struct_id >= 0) {
                llvm::Value *pointer = builder_.CreateLoad(ptr_type(), base.value);
                return field_addr(base.struct_id, pointer, selector->field, selector->loc);
            }
            error(selector->loc, "not an lvalue");
            return {};
        }
        const TypedValue base = emit_rvalue(*selector->base);
        if (base.value != nullptr && base.depth == 1 && base.struct_id >= 0) {
            return field_addr(base.struct_id, base.value, selector->field, selector->loc);
        }
        error(selector->loc, "not an lvalue");
        return {};
    }
    error(expr_loc(expr), "not an lvalue");
    return {};
}

llvm::Value *CodeGen::emit_short_circuit(const Expr::Binary &expr, bool is_and) {
    llvm::Value *lhs = emit_rvalue(*expr.lhs).value;
    llvm::BasicBlock *lhs_block = builder_.GetInsertBlock();
    llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(context_, is_and ? "land.rhs" : "lor.rhs", function_);
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, is_and ? "land.end" : "lor.end", function_);
    builder_.CreateCondBr(is_nonzero(lhs), is_and ? rhs_block : end_block, is_and ? end_block : rhs_block);
    builder_.SetInsertPoint(rhs_block);
    llvm::Value *rhs = emit_rvalue(*expr.rhs).value;
    llvm::Value *rhs_flag = is_nonzero(rhs);
    llvm::BasicBlock *rhs_end = builder_.GetInsertBlock();
    if (!terminated()) {
        builder_.CreateBr(end_block);
    }
    builder_.SetInsertPoint(end_block);
    auto *phi = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
    phi->addIncoming(is_and ? llvm::ConstantInt::getFalse(context_) : llvm::ConstantInt::getTrue(context_), lhs_block);
    phi->addIncoming(rhs_flag, rhs_end);
    return phi;
}

TypedValue CodeGen::index_address(const TypedValue &base, llvm::Value *subscript, SourceLoc loc) {
    if (!base.shells.empty() && subscript != nullptr) {
        return index_shells(base, subscript);
    }
    if (base.is_slice) {
        if (base.value == nullptr || subscript == nullptr) {
            error(loc, "not an lvalue");
            return {};
        }
        // A packed slice stores `{ ptr, len }` and `depth` is the element.
        // A string literal is still the array. Its depth is one above the byte.
        const bool packed = base.value->getType() == slice_type();
        llvm::Value *data = base.value;
        int elem_depth = base.depth;
        if (packed) {
            data = builder_.CreateExtractValue(base.value, 0);
        } else {
            if (llvm::Type *object = object_type(base.value); object != nullptr && object->isArrayTy()) {
                data = builder_.CreateInBoundsGEP(object, base.value,
                                                  llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(0)});
            }
            if (elem_depth > 0) {
                --elem_depth;
            }
        }
        llvm::Type *element = leaf_type(elem_depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits,
                                        base.elem_float);
        // `view[i]` on a slice of `[M]T` is that row, the same pointer `grid[i]` yields.
        if (base.elem_slice) {
            llvm::Value *slot =
                builder_.CreateInBoundsGEP(slice_type(), data, llvm::ArrayRef<llvm::Value *>{subscript});
            TypedValue out{slot,       elem_depth, base.elem_char, base.struct_id, false,          base.elem_void,
                           base.elem_bool, base.elem_bits, base.elem_unsigned, -1,         base.elem_float};
            out.is_slice = true;
            out.array_len = -1;
            out.enum_id = base.enum_id;
            return out;
        }
        if (base.inner_array_len > 0) {
            llvm::Type *row = llvm::ArrayType::get(element, static_cast<uint64_t>(base.inner_array_len));
            llvm::Value *slot = builder_.CreateInBoundsGEP(row, data, llvm::ArrayRef<llvm::Value *>{subscript});
            TypedValue out{slot, elem_depth + 1, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool,
                           base.elem_bits, base.elem_unsigned, -1, base.elem_float};
            out.row_decay = true;
            out.array_len = base.inner_array_len;
            out.array_type = row;
            out.enum_id = base.enum_id;
            return out;
        }
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(element, data, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out{slot,       elem_depth, base.elem_char, base.struct_id, false,          base.elem_void,
                       base.elem_bool, base.elem_bits, base.elem_unsigned, -1,         base.elem_float};
        out.enum_id = base.enum_id;
        return out;
    }
    // A decayed `[N]Duo` is a pointer to the tuple. The shells were the array and are gone.
    if (base.shells.empty() && !base.tuple_parts.empty() && base.value != nullptr && subscript != nullptr) {
        const Lowered elem = lower_tuple_parts(current_file_, base.tuple_parts);
        if (!elem.is_tuple || elem.type == nullptr) {
            error(loc, "not an lvalue");
            return {};
        }
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(elem.type, base.value, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out;
        out.value = slot;
        out.is_tuple = true;
        out.tuple_parts = base.tuple_parts;
        return out;
    }
    if (base.depth <= 0 || base.value == nullptr || subscript == nullptr) {
        error(loc, "not an lvalue");
        return {};
    }
    llvm::Value *offset = subscript;
    if (base.elem_slice) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(slice_type(), base.value, llvm::ArrayRef<llvm::Value *>{offset});
        TypedValue out{slot, base.depth > 0 ? base.depth - 1 : 0, base.elem_char, base.struct_id, false,
                       base.elem_void, base.elem_bool, base.elem_bits, base.elem_unsigned, -1, base.elem_float};
        out.is_slice = true;
        out.array_len = -1;
        out.enum_id = base.enum_id;
        return out;
    }
    if (base.inner_array_len > 0) {
        llvm::Type *leaf =
            leaf_type(base.depth - 1, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
        llvm::Type *row = llvm::ArrayType::get(leaf, static_cast<uint64_t>(base.inner_array_len));
        llvm::Value *slot = builder_.CreateInBoundsGEP(row, base.value, llvm::ArrayRef<llvm::Value *>{offset});
        TypedValue out{slot, base.depth, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool,
                       base.elem_bits, base.elem_unsigned, -1, base.elem_float};
        // The address is the row `[M]T`. The length stays so the row converts to `[]T`.
        out.row_decay = true;
        out.array_len = base.inner_array_len;
        out.array_type = row;
        return out;
    }
    if (base.ptr_to_array) {
        const int depth = base.depth - 1;
        llvm::Type *element =
            leaf_type(depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
        llvm::Type *array = llvm::ArrayType::get(element, static_cast<uint64_t>(base.ptr_array_len));
        llvm::Value *slot = builder_.CreateInBoundsGEP(
            array, base.value, llvm::ArrayRef<llvm::Value *>{const_i32(0), offset});
        return {slot, depth, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool, base.elem_bits,
                base.elem_unsigned, -1, base.elem_float};
    }
    const int depth = base.depth - 1;
    llvm::Value *slot = builder_.CreateInBoundsGEP(
        leaf_type(depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float), base.value,
        llvm::ArrayRef<llvm::Value *>{offset});
    TypedValue out{slot,
                   depth,
                   base.elem_char,
                   base.struct_id,
                   false,
                   base.elem_void,
                   base.elem_bool,
                   base.elem_bits,
                   base.elem_unsigned,
                   -1,
                   base.elem_float};
    out.fn_ty = base.fn_ty;
    out.fn_ret_depth = base.fn_ret_depth;
    out.fn_ret_float = base.fn_ret_float;
    out.fn_ret_bits = base.fn_ret_bits;
    out.fn_ret_unsigned = base.fn_ret_unsigned;
    out.fn_ret_char = base.fn_ret_char;
    out.fn_ret_bool = base.fn_ret_bool;
    out.fn_ret_struct = base.fn_ret_struct;
    out.fn_results = base.fn_results;
    out.enum_id = base.enum_id;
    return out;
}

// A local `[N]...T` decays by dropping the outer array. The pointer already addresses element 0.
TypedValue CodeGen::index_shells(const TypedValue &base, llvm::Value *subscript) {
    if (base.shells.empty() || base.value == nullptr || subscript == nullptr) {
        return {};
    }
    auto carry = [&](TypedValue &out) {
        out.depth = base.depth;
        out.elem_char = base.elem_char;
        out.struct_id = base.struct_id;
        out.elem_void = base.elem_void;
        out.elem_bool = base.elem_bool;
        out.elem_bits = base.elem_bits;
        out.elem_unsigned = base.elem_unsigned;
        out.elem_float = base.elem_float;
        out.enum_id = base.enum_id;
    };
    auto mark = [&](TypedValue &out, std::vector<TypeShell> shells) {
        out.shells = std::move(shells);
        if (out.shells.empty()) {
            return;
        }
        if (out.shells.front().kind == TypeShell::Kind::Slice) {
            out.is_slice = true;
            out.array_len = -1;
            out.elem_slice = out.shells.size() >= 2 && out.shells[1].kind == TypeShell::Kind::Slice;
            return;
        }
        if (out.shells.front().kind == TypeShell::Kind::Pointer && out.shells.size() >= 2 &&
            out.shells[1].kind == TypeShell::Kind::Array) {
            out.ptr_to_array = true;
            out.ptr_array_len = out.shells[1].length;
        }
        out.elem_slice = out.shells.front().kind == TypeShell::Kind::Pointer && out.shells.size() >= 2 &&
                         out.shells[1].kind == TypeShell::Kind::Slice;
    };
    const TypeShell &outer = base.shells.front();
    std::vector<TypeShell> rest(base.shells.begin() + 1, base.shells.end());
    auto element_of = [&](const std::vector<TypeShell> &shells) {
        if (!base.tuple_parts.empty()) {
            if (llvm::Type *wrapped = tuple_shell_type(base.tuple_parts, shells)) {
                return wrapped;
            }
        }
        return shell_type(shells, base.depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
    };
    // `tuple_parts` is the leaf. A row `[N]Duo` stays an array, so it is not `is_tuple`.
    auto note_tuple = [&](TypedValue &out) {
        if (base.tuple_parts.empty()) {
            return;
        }
        out.tuple_parts = base.tuple_parts;
        if (out.shells.empty() && !out.row_decay && !out.is_slice && !out.is_array) {
            out.is_tuple = true;
        }
    };
    if (outer.kind == TypeShell::Kind::Slice && base.value->getType() == slice_type()) {
        llvm::Value *data = builder_.CreateExtractValue(base.value, 0);
        llvm::Value *slot = builder_.CreateInBoundsGEP(element_of(rest), data, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out;
        out.value = slot;
        carry(out);
        // `view[i]` on `[][M]T` is that row. A further index reads one element.
        if (rest.size() == 1 && rest.front().kind == TypeShell::Kind::Array) {
            out.row_decay = true;
            out.array_len = rest.front().length;
            out.depth = base.depth + 1;
            out.array_type = element_of(rest);
            note_tuple(out);
            return out;
        }
        mark(out, std::move(rest));
        note_tuple(out);
        return out;
    }
    if (outer.kind == TypeShell::Kind::Slice || outer.kind == TypeShell::Kind::Array) {
        // A decayed array is a pointer that steps by one element. The result is that element.
        const int leaf_depth = base.depth > 0 ? base.depth - 1 : 0;
        llvm::Type *tuple_step = outer.kind == TypeShell::Kind::Array && !base.tuple_parts.empty()
                                     ? tuple_shell_type(base.tuple_parts, base.shells)
                                     : nullptr;
        llvm::Type *step = outer.kind == TypeShell::Kind::Slice
                               ? static_cast<llvm::Type *>(slice_type())
                               : tuple_step != nullptr
                                     ? tuple_step
                                     : shell_type(base.shells, leaf_depth, base.elem_char, base.struct_id, base.elem_bool,
                                                  base.elem_bits, base.elem_float);
        llvm::Value *slot = builder_.CreateInBoundsGEP(step, base.value, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out;
        out.value = slot;
        carry(out);
        mark(out, rest);
        if (outer.kind == TypeShell::Kind::Array) {
            out.row_decay = true;
            out.array_len = outer.length;
            out.is_slice = false;
            out.depth = base.depth;
            out.array_type = step;
        } else {
            // A decayed `[N][]T` steps by one header. That slot is the slice.
            // No shell is left, so the element is the leaf. Array decay added one to that depth.
            // A further slice shell, as in `[N][][]T`, keeps the depth the next index already uses.
            out.is_slice = true;
            out.array_len = -1;
            if (out.shells.empty() && out.depth > 0) {
                out.depth -= 1;
            }
        }
        note_tuple(out);
        return out;
    }
    if (outer.kind == TypeShell::Kind::Pointer && !rest.empty() && rest.front().kind == TypeShell::Kind::Array &&
        base.array_len > 0) {
        // A parameter `[N][M]T` points at the first row. Indexing steps by one row.
        // `*[M]T` has no decayed length and still indexes inside that one array.
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(element_of(rest), base.value, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out;
        out.value = slot;
        carry(out);
        out.row_decay = true;
        out.array_len = rest.front().length;
        out.depth = base.depth + 1;
        out.array_type = element_of(rest);
        mark(out, std::vector<TypeShell>(rest.begin() + 1, rest.end()));
        out.is_slice = false;
        note_tuple(out);
        return out;
    }
    if (outer.kind == TypeShell::Kind::Pointer && !rest.empty() && rest.front().kind == TypeShell::Kind::Array &&
        base.array_len == 0 && nested_array_element(base.shells, 1)) {
        // `p[i]` on `*[N][M]...T` is `(*p)[i]`. Decay the array, then index that value.
        // A further dimension is the same call. `*[N]T` has no inner shell and stays below.
        TypedValue array = base;
        array.shells = rest;
        array.ptr_to_array = false;
        array.ptr_array_len = 0;
        return index_shells(decay_outer_array(std::move(array)), subscript);
    }
    if (outer.kind == TypeShell::Kind::Pointer && !rest.empty() && rest.front().kind == TypeShell::Kind::Array) {
        std::vector<TypeShell> inner(rest.begin() + 1, rest.end());
        llvm::Type *array = llvm::ArrayType::get(element_of(inner), static_cast<uint64_t>(rest.front().length));
        llvm::Value *slot = builder_.CreateInBoundsGEP(array, base.value,
                                                       llvm::ArrayRef<llvm::Value *>{const_i32(0), subscript});
        TypedValue out;
        out.value = slot;
        carry(out);
        mark(out, std::move(inner));
        note_tuple(out);
        return out;
    }
    if (outer.kind == TypeShell::Kind::Pointer && !rest.empty() && rest.front().kind == TypeShell::Kind::Slice) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(slice_type(), base.value, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out;
        out.value = slot;
        carry(out);
        mark(out, std::move(rest));
        note_tuple(out);
        return out;
    }
    if (outer.kind == TypeShell::Kind::Pointer || outer.kind == TypeShell::Kind::Array) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(element_of(rest), base.value, llvm::ArrayRef<llvm::Value *>{subscript});
        TypedValue out;
        out.value = slot;
        carry(out);
        mark(out, std::move(rest));
        note_tuple(out);
        return out;
    }
    return {};
}

llvm::Type *CodeGen::shape_type(const Shape &shape) const {
    if (!shape.shells.empty()) {
        return shell_type(shape.shells, shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits,
                          shape.elem_float);
    }
    if (shape.is_slice) {
        return slice_type();
    }
    if (shape.is_function || (shape.depth == 0 && shape.elem_void && !shape.is_array)) {
        return nullptr;
    }
    if (shape.is_array) {
        if (shape.array_len < 0) {
            return nullptr;
        }
        llvm::Type *element =
            shape.elem_slice ? static_cast<llvm::Type *>(slice_type())
                             : leaf_type(shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits,
                                         shape.elem_float);
        return llvm::ArrayType::get(element, static_cast<uint64_t>(shape.array_len));
    }
    return leaf_type(shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits);
}

CodeGen::Shape CodeGen::decay_shape(Shape shape) const {
    if (shape.is_array) {
        shape.depth += 1;
        shape.is_array = false;
        shape.array_len = 0;
        shape.type = ptr_type();
        return shape;
    }
    if (shape.depth == 0 && shape.elem_char) {
        shape.elem_char = false;
        shape.type = i32();
    }
    if (shape.depth == 0 && shape.elem_bool) {
        shape.elem_bool = false;
        shape.type = i32();
    }
    return shape;
}

CodeGen::Shape CodeGen::shape_from_binding(const Binding &binding) const {
    Shape shape;
    shape.depth = binding.depth;
    shape.elem_char = binding.elem_char;
    shape.elem_void = binding.elem_void;
    shape.elem_bool = binding.elem_bool;
    shape.elem_bits = binding.elem_bits;
    shape.elem_unsigned = binding.elem_unsigned;
    shape.struct_id = binding.struct_id;
    shape.is_array = binding.is_array;
    shape.array_len = binding.array_len;
    shape.is_function = binding.is_function;
    shape.is_slice = binding.is_slice;
    shape.elem_slice = binding.elem_slice;
    shape.shells = binding.shells;
    shape.elem_float = binding.elem_float;
    shape.type = shape_type(shape);
    // A tuple value, or `[N]Duo`, is that aggregate. A decayed parameter stays a pointer.
    if (!binding.tuple_parts.empty() && binding.depth == 0 && !binding.is_slice) {
        if (llvm::Type *wrapped = tuple_shell_type(binding.tuple_parts, binding.shells)) {
            if (binding.shells.empty() && binding.is_array && binding.array_len > 0) {
                shape.type = llvm::ArrayType::get(wrapped, static_cast<uint64_t>(binding.array_len));
            } else if (!binding.shells.empty() || binding.is_tuple) {
                shape.type = wrapped;
            }
        }
    }
    return shape;
}

CodeGen::Shape CodeGen::raw_shape(const Expr &expr) const {
    return std::visit(
        [&](const auto &node) -> Shape {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const Binding *binding = lookup(node.text)) {
                    return shape_from_binding(*binding);
                }
                const Alias *alias = find_alias(current_file_, node.text);
                if (alias == nullptr) {
                    alias = selective_alias(current_file_, node.text);
                }
                if (alias != nullptr) {
                    // `sizeof(Duo)` names the alias. The aggregate is `tuple_ty`, not a scalar leaf.
                    if (alias->is_tuple && alias->tuple_ty != nullptr) {
                        Shape shape;
                        shape.type = alias->tuple_ty;
                        return shape;
                    }
                    Shape shape;
                    shape.depth = alias->depth;
                    shape.elem_char = alias->elem_char;
                    shape.elem_void = alias->elem_void;
                    shape.elem_bool = alias->elem_bool;
                    shape.elem_bits = alias->elem_bits;
                    shape.elem_unsigned = alias->elem_unsigned;
                    shape.struct_id = alias->struct_id;
                    shape.is_array = alias->is_array;
                    shape.array_len = alias->array_len;
                    shape.is_slice = alias->is_slice;
                    shape.elem_slice = alias->elem_slice;
                    shape.elem_float = alias->elem_float;
                    shape.type = shape_type(shape);
                    return shape;
                }
                int sid = tag_id(current_file_, node.text);
                if (sid < 0) {
                    sid = selective_tag(current_file_, node.text);
                }
                if (sid < 0) {
                    int eid = enum_named(current_file_, node.text);
                    if (eid < 0) {
                        eid = selective_enum(current_file_, node.text);
                    }
                    if (eid < 0 || eid >= static_cast<int>(enums_.size())) {
                        return {};
                    }
                    const EnumLayout &layout = enums_[static_cast<std::size_t>(eid)];
                    Shape shape;
                    shape.elem_bool = layout.is_bool;
                    shape.elem_char = layout.is_char;
                    shape.elem_unsigned = layout.is_unsigned;
                    shape.elem_bits = layout.is_bool ? 0 : layout.bits;
                    shape.type = shape_type(shape);
                    return shape;
                }
                Shape shape;
                shape.struct_id = sid;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                // `typeof` is `*const CompiledType`. The union stays in one constant.
                Shape shape;
                shape.depth = 1;
                shape.struct_id = type_struct_id_ >= 0 ? type_struct_id_ : synthetic_type_id;
                shape.type = ptr_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Sizeof>) {
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                Shape shape;
                shape.elem_bool = true;
                shape.type = i1();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                Shape shape;
                shape.depth = 1;
                shape.elem_void = true;
                shape.type = ptr_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                Shape shape;
                shape.is_slice = true;
                shape.array_len = -1;
                shape.elem_bits = 8;
                shape.elem_unsigned = true;
                shape.elem_char = true;
                shape.type = slice_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.postfix && node.op == TokenKind::Bang && node.expr) {
                    return raw_shape(*node.expr);
                }
                if (node.op == TokenKind::Star && node.expr) {
                    const Shape inner = decay_shape(raw_shape(*node.expr));
                    if (inner.depth <= 0) {
                        return {};
                    }
                    Shape pointee = inner;
                    pointee.depth -= 1;
                    pointee.is_array = false;
                    pointee.array_len = 0;
                    pointee.is_function = false;
                    pointee.type = shape_type(pointee);
                    return pointee;
                }
                if (node.op == TokenKind::Amp && node.expr) {
                    Shape inner = raw_shape(*node.expr);
                    if (inner.is_array || inner.is_function || inner.type == nullptr) {
                        return {};
                    }
                    inner.depth += 1;
                    inner.type = ptr_type();
                    return inner;
                }
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                return node.lhs ? raw_shape(*node.lhs) : Shape{};
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.is_range && node.base != nullptr) {
                    Shape base = raw_shape(*node.base);
                    Shape slice = base;
                    slice.is_slice = true;
                    slice.is_array = false;
                    slice.array_len = -1;
                    slice.elem_slice = base.elem_slice;
                    slice.type = slice_type();
                    if (!base.is_slice && base.depth > 0) {
                        slice.depth = base.depth - 1;
                    }
                    return slice;
                }
                if (node.tuple_index >= 0 && node.base != nullptr) {
                    const auto *named = std::get_if<Expr::Name>(&node.base->data);
                    const Binding *binding = named == nullptr ? nullptr : lookup(named->text);
                    if (binding != nullptr && binding->is_tuple &&
                        static_cast<std::size_t>(node.tuple_index) < binding->tuple_parts.size()) {
                        const ResultSlot &slot = binding->tuple_parts[static_cast<std::size_t>(node.tuple_index)];
                        const Lowered low = lower_result(current_file_, slot);
                        Shape shape;
                        shape.depth = low.depth;
                        shape.elem_char = low.elem_char;
                        shape.elem_void = low.elem_void;
                        shape.elem_bool = low.elem_bool;
                        shape.elem_bits = low.elem_bits;
                        shape.elem_unsigned = low.elem_unsigned;
                        shape.elem_float = low.elem_float;
                        shape.struct_id = low.struct_id;
                        shape.is_array = low.is_array;
                        shape.array_len = low.array_len;
                        shape.is_slice = low.is_slice;
                        shape.elem_slice = low.elem_slice;
                        shape.type = low.type;
                        return shape;
                    }
                    return {};
                }
                if (node.base == nullptr) {
                    return {};
                }
                const Shape base = raw_shape(*node.base);
                if (base.elem_slice) {
                    Shape element = base;
                    element.is_slice = true;
                    element.elem_slice = false;
                    element.is_array = false;
                    element.array_len = -1;
                    if (!base.is_slice && element.depth > 0) {
                        element.depth -= 1;
                    }
                    element.type = slice_type();
                    return element;
                }
                if (base.is_slice) {
                    Shape element = base;
                    element.is_slice = false;
                    element.array_len = 0;
                    element.type = shape_type(element);
                    return element;
                }
                const Shape inner = decay_shape(base);
                if (inner.depth <= 0) {
                    return {};
                }
                Shape pointee = inner;
                pointee.depth -= 1;
                pointee.is_array = false;
                pointee.array_len = 0;
                pointee.type = shape_type(pointee);
                return pointee;
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                const Lowered low =
                    node.is_fn_ptr ? lower_fnptr(current_file_, node.type_name, node.type_package, node.is_struct,
                                                 node.is_enum, node.ret_indirections, node.indirections, node.fn_params,
                                                 node.fn_variadic, node.fn_results)
                                   : lower(current_file_, node.type_name, node.type_package, node.is_struct,
                                           node.indirections, false, 0, false);
                Shape shape;
                shape.depth = low.depth;
                shape.elem_char = low.elem_char;
                shape.elem_void = low.elem_void;
                shape.elem_bool = low.elem_bool;
                shape.elem_bits = low.elem_bits;
                shape.elem_unsigned = low.elem_unsigned;
                shape.struct_id = low.struct_id;
                shape.is_slice = low.is_slice;
                shape.elem_slice = low.elem_slice;
                shape.array_len = low.is_slice ? -1 : low.array_len;
                shape.is_array = low.is_array && !low.is_slice;
                shape.type = low.depth == 0 && low.elem_void ? nullptr : low.type;
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.type_name) {
                    Shape shape;
                    shape.depth = 1;
                    shape.elem_bits = 8;
                    shape.elem_unsigned = true;
                    shape.type = ptr_type();
                    return shape;
                }
                if (node.type_fields) {
                    Shape shape;
                    shape.is_slice = true;
                    shape.array_len = -1;
                    shape.depth = 0;
                    shape.struct_id = field_struct_id_;
                    shape.type = slice_type();
                    return shape;
                }
                if (node.callee == nullptr) {
                    return {};
                }
                const Binding *binding = nullptr;
                if (node.selected != nullptr) {
                    binding = binding_for_decl(node.selected);
                } else if (const auto *name = std::get_if<Expr::Name>(&node.callee->data)) {
                    binding = lookup(name->text);
                } else if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                    const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
                    if (base != nullptr) {
                        const auto package = packages_.find(base->text);
                        if (package != packages_.end()) {
                            binding = visible(package->second, selector->field, true);
                        }
                    }
                }
                if (binding == nullptr || !binding->is_function) {
                    return {};
                }
                // Several results, and one tuple result, are one aggregate. `sizeof` of the call is that aggregate.
                const bool one_tuple = binding->decl != nullptr && binding->decl->results.size() == 1 &&
                                       binding->decl->results.front().is_tuple &&
                                       binding->decl->results.front().indirections == 0 &&
                                       (binding->decl->results.front().shells.empty() ||
                                        binding->decl->results.front().shells.front().kind != TypeShell::Kind::Pointer);
                if (binding->decl != nullptr && (binding->decl->results.size() > 1 || one_tuple)) {
                    llvm::Type *ret = nullptr;
                    if (const auto *function = llvm::dyn_cast_or_null<llvm::Function>(binding->value)) {
                        ret = function->getReturnType();
                    }
                    if (ret == nullptr || !ret->isSized() || ret->isVoidTy()) {
                        const Lowered low =
                            one_tuple ? lower_tuple_parts(current_file_, binding->decl->results.front().tuple_parts)
                                      : lower_tuple_parts(current_file_, binding->decl->results);
                        ret = low.is_tuple ? low.type : nullptr;
                    }
                    if (ret != nullptr && ret->isSized() && !ret->isVoidTy()) {
                        Shape shape;
                        shape.type = ret;
                        return shape;
                    }
                }
                Shape shape;
                shape.depth = binding->depth;
                shape.elem_char = binding->elem_char;
                shape.elem_void = binding->elem_void;
                shape.elem_bool = binding->elem_bool;
                shape.elem_bits = binding->elem_bits;
                shape.elem_unsigned = binding->elem_unsigned;
                shape.struct_id = binding->struct_id;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (!node.resolved_owner.empty()) {
                    const std::string key = node.resolved_owner + "." + node.field;
                    const Binding *global = visible(assoc_home(node), key, false);
                    if (global != nullptr) {
                        return shape_from_binding(*global);
                    }
                    const Binding *method = static_selector_binding(current_file_, node);
                    if (method != nullptr) {
                        return shape_from_binding(*method);
                    }
                    return {};
                }
                const auto *base_name = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base_name != nullptr) {
                    const auto package = packages_.find(base_name->text);
                    if (package != packages_.end()) {
                        if (const Binding *function = visible(package->second, node.field, true)) {
                            return shape_from_binding(*function);
                        }
                        if (const Binding *global = visible(package->second, node.field, false)) {
                            return shape_from_binding(*global);
                        }
                        return {};
                    }
                }
                if (node.base == nullptr) {
                    return {};
                }
                const Shape base = raw_shape(*node.base);
                if (base.depth > 1 || base.struct_id < 0 || base.is_array) {
                    return {};
                }
                int field_index = -1;
                const FieldLayout *field = find_field(base.struct_id, node.field, field_index);
                if (field == nullptr) {
                    return {};
                }
                Shape shape;
                shape.depth = field->depth;
                shape.elem_char = field->elem_char;
                shape.elem_void = field->elem_void;
                shape.elem_bool = field->elem_bool;
                shape.elem_bits = field->elem_bits;
                shape.elem_unsigned = field->elem_unsigned;
                shape.struct_id = field->struct_id;
                shape.is_array = field->is_array;
                shape.array_len = field->array_len;
                shape.is_slice = field->is_slice;
                shape.elem_slice = field->elem_slice;
                shape.type = field->type != nullptr ? field->type : shape_type(shape);
                return shape;
            } else {
                return {};
            }
        },
        expr.data);
}

TypedValue CodeGen::load_field(const TypedValue &addr) {
    if (addr.value == nullptr) {
        return {zero(), 0};
    }
    if (!addr.shells.empty() && addr.shells.front().kind == TypeShell::Kind::Pointer) {
        TypedValue loaded;
        loaded.value = builder_.CreateLoad(ptr_type(), addr.value);
        loaded.shells = addr.shells;
        loaded.depth = addr.depth;
        loaded.elem_char = addr.elem_char;
        loaded.struct_id = addr.struct_id;
        loaded.elem_void = addr.elem_void;
        loaded.elem_bool = addr.elem_bool;
        loaded.elem_bits = addr.elem_bits;
        loaded.elem_unsigned = addr.elem_unsigned;
        loaded.elem_float = addr.elem_float;
        loaded.enum_id = addr.enum_id;
        loaded.ptr_to_array = addr.ptr_to_array;
        loaded.ptr_array_len = addr.ptr_array_len;
        loaded.elem_slice = addr.elem_slice;
        loaded.inner_array_len = addr.inner_array_len;
        return loaded;
    }
    if (addr.is_tuple && !addr.tuple_parts.empty()) {
        std::vector<llvm::Type *> fields;
        fields.reserve(addr.tuple_parts.size());
        for (const ResultSlot &slot : addr.tuple_parts) {
            fields.push_back(stored_type(lower_result(current_file_, slot)));
        }
        llvm::StructType *type = llvm::StructType::get(context_, fields);
        TypedValue loaded;
        loaded.value = builder_.CreateLoad(type, addr.value);
        loaded.is_tuple = true;
        loaded.tuple_parts = addr.tuple_parts;
        return loaded;
    }
    // A slice field is the `{ ptr, i32 }` header, not the first element.
    if (addr.is_slice) {
        TypedValue loaded;
        loaded.value = builder_.CreateLoad(slice_type(), addr.value);
        loaded.is_slice = true;
        loaded.elem_slice = addr.elem_slice;
        loaded.shells = addr.shells;
        loaded.array_len = -1;
        loaded.depth = addr.depth;
        loaded.elem_char = addr.elem_char;
        loaded.struct_id = addr.struct_id;
        loaded.elem_void = addr.elem_void;
        loaded.elem_bool = addr.elem_bool;
        loaded.elem_bits = addr.elem_bits;
        loaded.elem_unsigned = addr.elem_unsigned;
        loaded.elem_float = addr.elem_float;
        return loaded;
    }
    // An array field decays to a pointer to its first element. The length stays so a range can use it.
    // `[N][M]...T` keeps the inner shells. The next index steps by the row, as a local array does.
    if (addr.is_array) {
        TypedValue value{addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void,
                         addr.elem_bool, addr.elem_bits, addr.elem_unsigned, -1, addr.elem_float};
        value.array_len = addr.array_len;
        value.elem_slice = addr.elem_slice;
        value.enum_id = addr.enum_id;
        if (!addr.shells.empty() && addr.shells.front().kind == TypeShell::Kind::Array) {
            if (addr.shells.size() >= 2 && addr.shells[1].kind == TypeShell::Kind::Array) {
                value.inner_array_len = addr.shells[1].length;
            }
            value.shells.assign(addr.shells.begin() + 1, addr.shells.end());
            if (addr.shells.size() >= 2 && addr.tuple_parts.empty()) {
                value.array_type = shell_type(addr.shells, addr.depth, addr.elem_char, addr.struct_id, addr.elem_bool,
                                              addr.elem_bits, addr.elem_float);
            }
        }
        // The leaf stays when an inner `[M]` remains. `[2][2]T` indexes that tuple.
        value.tuple_parts = addr.tuple_parts;
        return value;
    }
    TypedValue loaded = load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                    addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
    // `box.p` when the field is `*T` and `T` is a tuple. The pointer keeps those elements.
    loaded.tuple_parts = addr.tuple_parts;
    copy_fn_sig(loaded, addr);
    return loaded;
}

// `p[i:j]` on `*[N]...T` is still the pointer. A decayed `[N]T` already stores N in `array_len`.
// Turn the pointer into that decayed shape so the range packs `{ data, N }` and steps by the element.
void adopt_pointer_array(TypedValue &base) {
    if (base.array_len > 0 || !base.ptr_to_array || base.ptr_array_len <= 0 || base.value == nullptr ||
        !base.value->getType()->isPointerTy()) {
        return;
    }
    const bool described = base.shells.size() >= 2 && base.shells[0].kind == TypeShell::Kind::Pointer &&
                           base.shells[1].kind == TypeShell::Kind::Array;
    std::vector<TypeShell> element;
    if (described) {
        element.assign(base.shells.begin() + 2, base.shells.end());
        // The binding depth is the leaf. A decayed array is one above that leaf.
        base.depth += 1;
    }
    base.array_len = base.ptr_array_len;
    base.ptr_to_array = false;
    base.ptr_array_len = 0;
    if (!described) {
        // `&row` and `new [_]T` are already one above the element. `inner_array_len` is the row.
        base.shells.clear();
        return;
    }
    if (element.size() == 1 && element[0].kind == TypeShell::Kind::Array) {
        base.inner_array_len = element[0].length;
        base.elem_slice = false;
        base.shells.clear();
        return;
    }
    if (!element.empty() && element[0].kind == TypeShell::Kind::Slice) {
        // Same shape as a parameter `[N][]T`: a pointer to headers, with the length in `array_len`.
        TypeShell pointer;
        pointer.kind = TypeShell::Kind::Pointer;
        base.shells.clear();
        base.shells.push_back(pointer);
        base.shells.insert(base.shells.end(), element.begin(), element.end());
        base.inner_array_len = 0;
        base.elem_slice = false;
        return;
    }
    base.inner_array_len = 0;
    base.elem_slice = false;
    base.shells.clear();
}

TypedValue CodeGen::emit_range(const Expr::Index &node) {
    if (node.base == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    TypedValue base = emit_rvalue(*node.base);
    if (base.value == nullptr || terminated()) {
        return {zero(), 0};
    }
    adopt_pointer_array(base);
    // `[N]Duo` keeps the tuple slots on the element. That element is not a slice.
    if (!base.tuple_parts.empty() && base.shells.empty() && !base.elem_slice && base.inner_array_len <= 0) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    // An omitted low bound is 0. An omitted high bound is the length already in the slice.
    const bool low_omitted = node.index == nullptr;
    const bool high_omitted = node.end == nullptr;
    TypedValue low;
    TypedValue high;
    if (!low_omitted) {
        low = emit_rvalue(*node.index);
        if (low.value == nullptr || terminated()) {
            return {zero(), 0};
        }
    }
    if (!high_omitted) {
        high = emit_rvalue(*node.end);
        if (high.value == nullptr || terminated()) {
            return {zero(), 0};
        }
    }
    llvm::Value *packed = as_slice_value(base, node.loc);
    if (terminated() || packed == nullptr) {
        return {zero(), 0};
    }
    // A packed slice stores the element depth. A string literal and a decayed array
    // are one level above that element: the literal is still the array address.
    const bool packed_slice = base.is_slice && base.value != nullptr && base.value->getType() == slice_type();
    const int elem_depth = packed_slice ? base.depth : base.depth > 0 ? base.depth - 1 : base.depth;
    llvm::Value *out = packed;
    std::vector<TypeShell> range_shells;
    // A parameter `[N][]T` is a pointer to headers. The range is a slice of those headers.
    const bool decayed_headers = base.array_len > 0 && !base.is_slice && !base.shells.empty() &&
                                 base.shells.front().kind == TypeShell::Kind::Pointer && base.shells.size() >= 2 &&
                                 base.shells[1].kind == TypeShell::Kind::Slice;
    // `(*p)[i:j]` on `*[N][]T` decayed the array. The value is still that pointer, and the
    // shells are the element (a header), not a packed slice of a further element.
    const bool decayed_slice_elems = base.is_slice && !packed_slice && base.array_len > 0 && base.value != nullptr &&
                                     base.value->getType()->isPointerTy() && !base.shells.empty() &&
                                     base.shells.front().kind == TypeShell::Kind::Slice;
    if (decayed_slice_elems) {
        TypeShell header;
        header.kind = TypeShell::Kind::Slice;
        range_shells.push_back(header);
        range_shells.insert(range_shells.end(), base.shells.begin(), base.shells.end());
    } else if (!base.shells.empty() && base.shells.front().kind == TypeShell::Kind::Slice) {
        TypeShell header;
        header.kind = TypeShell::Kind::Slice;
        range_shells.push_back(header);
        range_shells.insert(range_shells.end(), base.shells.begin() + 1, base.shells.end());
    } else if (decayed_headers) {
        TypeShell header;
        header.kind = TypeShell::Kind::Slice;
        range_shells.push_back(header);
        range_shells.insert(range_shells.end(), base.shells.begin() + 1, base.shells.end());
    }
    // `a[:]` is that header. One omitted bound builds a new header from it.
    if (!low_omitted || !high_omitted) {
        llvm::Value *data = builder_.CreateExtractValue(packed, 0);
        llvm::Type *element =
            leaf_type(elem_depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
        auto as_bound = [&](const TypedValue &bound) -> llvm::Value * {
            if (bound.value == nullptr || !bound.value->getType()->isIntegerTy()) {
                return nullptr;
            }
            if (bound.value->getType() == i32()) {
                return bound.value;
            }
            return resize_int(bound.value, i32(), bound.elem_unsigned);
        };
        llvm::Value *start = data;
        llvm::Value *len = nullptr;
        // A header element steps by `tlc.slice`. A row `[M]T` steps by that row.
        // A parameter `[N][]T` is a pointer. The range steps by the element after that pointer.
        if (decayed_slice_elems) {
            element = shell_type(base.shells, elem_depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits,
                                 base.elem_float);
        } else if (!base.shells.empty() && base.shells.front().kind == TypeShell::Kind::Slice) {
            std::vector<TypeShell> rest(base.shells.begin() + 1, base.shells.end());
            element = shell_type(rest, elem_depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits,
                                 base.elem_float);
        } else if (decayed_headers) {
            std::vector<TypeShell> rest(base.shells.begin() + 1, base.shells.end());
            element = shell_type(rest, elem_depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits,
                                 base.elem_float);
        } else if (base.elem_slice) {
            element = slice_type();
        } else if (base.inner_array_len > 0) {
            element = llvm::ArrayType::get(element, static_cast<uint64_t>(base.inner_array_len));
        }
        if (low_omitted) {
            len = as_bound(high);
        } else {
            llvm::Value *start_index = as_bound(low);
            if (start_index == nullptr) {
                error(node.loc, "type mismatch");
                return {zero(), 0};
            }
            start = builder_.CreateInBoundsGEP(element, data, llvm::ArrayRef<llvm::Value *>{start_index});
            llvm::Value *end_index = high_omitted ? builder_.CreateExtractValue(packed, 1) : as_bound(high);
            if (end_index == nullptr) {
                error(node.loc, "type mismatch");
                return {zero(), 0};
            }
            len = builder_.CreateSub(end_index, start_index);
        }
        if (len == nullptr) {
            error(node.loc, "type mismatch");
            return {zero(), 0};
        }
        out = llvm::ConstantAggregateZero::get(slice_type());
        out = builder_.CreateInsertValue(out, start, 0);
        out = builder_.CreateInsertValue(out, len, 1);
    }
    TypedValue result;
    result.value = out;
    result.is_slice = true;
    result.elem_slice = base.elem_slice || decayed_headers || decayed_slice_elems;
    result.shells = std::move(range_shells);
    result.array_len = -1;
    result.depth = elem_depth;
    result.inner_array_len = result.elem_slice ? 0 : base.inner_array_len;
    result.elem_char = base.elem_char;
    result.struct_id = base.struct_id;
    result.elem_void = base.elem_void;
    result.elem_bool = base.elem_bool;
    result.elem_bits = base.elem_bits;
    result.elem_unsigned = base.elem_unsigned;
    result.elem_float = base.elem_float;
    result.enum_id = base.enum_id;
    result.fn_ty = base.fn_ty;
    result.fn_ret_depth = base.fn_ret_depth;
    result.fn_ret_float = base.fn_ret_float;
    result.fn_ret_bits = base.fn_ret_bits;
    result.fn_ret_unsigned = base.fn_ret_unsigned;
    result.fn_ret_char = base.fn_ret_char;
    result.fn_ret_bool = base.fn_ret_bool;
    result.fn_ret_struct = base.fn_ret_struct;
    result.fn_results = base.fn_results;
    return result;
}

} // namespace tlc
