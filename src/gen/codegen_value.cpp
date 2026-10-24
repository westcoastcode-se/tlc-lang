// Expression values.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

TypedValue CodeGen::emit_rvalue(const Expr &expr) {
    if (terminated()) {
        return {zero(), 0};
    }
    set_loc(std::visit([](const auto &node) -> SourceLoc { return node.loc; }, expr.data));
    return std::visit(
        [&](const auto &node) -> TypedValue {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                const Binding *binding = lookup(node.text);
                if (binding == nullptr) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (binding->is_generic) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                if (binding->immediate) {
                    return value_of_binding(*binding);
                }
                if (binding->value == nullptr) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                return value_of_binding(*binding);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                const std::string number = node.text.empty() ? "0" : node.text;
                const double parsed = std::strtod(number.c_str(), nullptr);
                llvm::Type *ty = node.is_f32 ? f32_ty() : f64_ty();
                return {llvm::ConstantFP::get(ty, parsed), 0, false, -1, false, false, false, node.is_f32 ? 32 : 64, false,
                        -1, true};
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                const int bits = node.suffix.bits;
                const bool uns = node.suffix.is_unsigned;
                if (bits >= 64) {
                    return {llvm::ConstantInt::get(i64(), node.value, false), 0, false, -1, false, false, false, 64, uns};
                }
                if (uns && bits == 32) {
                    return {const_i32(static_cast<int32_t>(node.value)), 0, false, -1, false, false, false, 32, true};
                }
                // A narrower suffix promotes to int when it is used as a value.
                return {const_i32(static_cast<int32_t>(node.value)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                return {const_i32(static_cast<int32_t>(node.value & 0xFFULL)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::KwUnsafe) {
                    if (node.expr == nullptr) {
                        return {zero(), 0};
                    }
                    return emit_rvalue(*node.expr);
                }
                if (node.op == TokenKind::Star) {
                    const TypedValue inner = emit_rvalue(*node.expr);
                    const bool pointer_shell =
                        !inner.shells.empty() && inner.shells.front().kind == TypeShell::Kind::Pointer;
                    if ((!pointer_shell && inner.depth <= 0) || inner.value == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    // `*p` on `*[N][M]...T` is that array, decayed like a local. The next index peels one shell.
                    if (pointer_shell && inner.shells.size() >= 2 && inner.shells[1].kind == TypeShell::Kind::Array &&
                        nested_array_element(inner.shells, 1)) {
                        TypedValue array = inner;
                        array.shells.erase(array.shells.begin());
                        array.ptr_to_array = false;
                        array.ptr_array_len = 0;
                        array.array_type = shell_type(array.shells, array.depth, array.elem_char, array.struct_id,
                                                      array.elem_bool, array.elem_bits, array.elem_float);
                        return decay_outer_array(std::move(array));
                    }
                    // `*arr` on `*[N]T` is that array, decayed to a pointer to its element.
                    // `array_len` keeps N so `(*p)[i:j]` can build the slice. Depth is one above the element.
                    if (inner.ptr_to_array ||
                        (pointer_shell && inner.shells.size() >= 2 && inner.shells[1].kind == TypeShell::Kind::Array &&
                         inner.shells.size() == 2)) {
                        int length = inner.ptr_array_len;
                        if (length <= 0 && inner.shells.size() >= 2 &&
                            inner.shells[1].kind == TypeShell::Kind::Array) {
                            length = inner.shells[1].length;
                        }
                        TypedValue decayed = inner;
                        decayed.ptr_to_array = false;
                        decayed.ptr_array_len = 0;
                        decayed.inner_array_len = 0;
                        decayed.elem_slice = false;
                        decayed.shells.clear();
                        decayed.array_len = length;
                        decayed.depth = inner.depth + 1;
                        if (length > 0) {
                            const int elem_depth = inner.depth > 0 ? inner.depth - 1 : 0;
                            llvm::Type *elem = leaf_type(elem_depth, inner.elem_char, inner.struct_id, inner.elem_bool,
                                                         inner.elem_bits, inner.elem_float);
                            decayed.array_type = llvm::ArrayType::get(elem, static_cast<uint64_t>(length));
                        }
                        return decayed;
                    }
                    if (pointer_shell && inner.shells.size() >= 2 && inner.shells[1].kind == TypeShell::Kind::Slice) {
                        TypedValue loaded;
                        loaded.value = builder_.CreateLoad(slice_type(), inner.value);
                        loaded.is_slice = true;
                        loaded.array_len = -1;
                        loaded.depth = inner.depth;
                        loaded.shells.assign(inner.shells.begin() + 1, inner.shells.end());
                        loaded.elem_slice = loaded.shells.size() >= 2 && loaded.shells[1].kind == TypeShell::Kind::Slice;
                        loaded.elem_char = inner.elem_char;
                        loaded.struct_id = inner.struct_id;
                        loaded.elem_void = inner.elem_void;
                        loaded.elem_bool = inner.elem_bool;
                        loaded.elem_bits = inner.elem_bits;
                        loaded.elem_unsigned = inner.elem_unsigned;
                        loaded.elem_float = inner.elem_float;
                        loaded.enum_id = inner.enum_id;
                        return loaded;
                    }
                    if (pointer_shell) {
                        TypedValue peeled = inner;
                        peeled.shells.erase(peeled.shells.begin());
                        // `*p` on `**[N]T` loads the next pointer. `*[N]T` itself decays below.
                        if (!peeled.shells.empty() && peeled.shells.front().kind == TypeShell::Kind::Pointer) {
                            peeled.value = builder_.CreateLoad(ptr_type(), inner.value);
                            peeled.ptr_to_array = peeled.shells.size() >= 2 &&
                                                  peeled.shells[1].kind == TypeShell::Kind::Array;
                            peeled.ptr_array_len = peeled.ptr_to_array ? peeled.shells[1].length : 0;
                            return peeled;
                        }
                        // `*p` on `*[N]T` decays to a pointer to the first element.
                        // The length stays so a range of that array can build the slice.
                        if (peeled.shells.size() == 1 && peeled.shells.front().kind == TypeShell::Kind::Array) {
                            const int length = peeled.shells.front().length;
                            peeled.array_len = length;
                            peeled.shells.clear();
                            peeled.ptr_to_array = false;
                            peeled.ptr_array_len = 0;
                            peeled.inner_array_len = 0;
                            peeled.depth = inner.depth + 1;
                            if (length > 0) {
                                const int elem_depth = inner.depth > 0 ? inner.depth - 1 : 0;
                                llvm::Type *elem =
                                    leaf_type(elem_depth, inner.elem_char, inner.struct_id, inner.elem_bool,
                                              inner.elem_bits, inner.elem_float);
                                peeled.array_type = llvm::ArrayType::get(elem, static_cast<uint64_t>(length));
                            }
                            return peeled;
                        }
                        return peeled;
                    }
                    // `*Adder` passed a function is that function pointer. `*a` does not load through it.
                    if (trait_fn_argument(inner)) {
                        TypedValue fn = inner;
                        fn.depth = 0;
                        return fn;
                    }
                    return load_object(inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, inner.elem_void,
                                       inner.elem_bool, inner.elem_bits, inner.elem_unsigned, inner.elem_float);
                }
                if (node.op == TokenKind::Amp) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Binding *binding = lookup(named->text); binding != nullptr && binding->is_array) {
                            TypedValue pointer;
                            pointer.value = binding->value;
                            pointer.depth = binding->depth + 1;
                            pointer.elem_char = binding->elem_char;
                            pointer.struct_id = binding->struct_id;
                            pointer.elem_void = binding->elem_void;
                            pointer.elem_bool = binding->elem_bool;
                            pointer.elem_bits = binding->elem_bits;
                            pointer.elem_unsigned = binding->elem_unsigned;
                            pointer.elem_float = binding->elem_float;
                            pointer.ptr_to_array = true;
                            pointer.ptr_array_len = binding->array_len;
                            // `(&grid)[i:j]` steps by the row. A flat array leaves this at 0.
                            pointer.inner_array_len = binding->inner_array_len;
                            pointer.elem_slice = binding->elem_slice;
                            return pointer;
                        }
                    }
                    const TypedValue addr = emit_addr(*node.expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    if (addr.is_slice || (!addr.shells.empty() && addr.shells.front().kind == TypeShell::Kind::Slice)) {
                        TypedValue pointer = addr;
                        pointer.is_slice = false;
                        pointer.array_len = 0;
                        pointer.elem_slice = false;
                        TypeShell shell;
                        shell.kind = TypeShell::Kind::Pointer;
                        pointer.shells.insert(pointer.shells.begin(), shell);
                        return pointer;
                    }
                    return {addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void,
                            addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.trait_id, addr.elem_float};
                }
                const TypedValue inner_value = emit_rvalue(*node.expr);
                if (node.op == TokenKind::Bang && node.postfix) {
                    llvm::Value *pointer = inner_value.value;
                    // `p!` is `*T` for this use. A null pointer panics before that use.
                    if (pointer == nullptr || terminated() || function_ == nullptr || !pointer->getType()->isPointerTy()) {
                        return inner_value;
                    }
                    llvm::BasicBlock *bad = llvm::BasicBlock::Create(context_, "bang.null", function_);
                    llvm::BasicBlock *ok = llvm::BasicBlock::Create(context_, "bang.ok", function_);
                    builder_.CreateCondBr(builder_.CreateICmpEQ(pointer, null_ptr()), bad, ok);
                    builder_.SetInsertPoint(bad);
                    abort_panic(node.loc, "null pointer");
                    builder_.SetInsertPoint(ok);
                    return inner_value;
                }
                llvm::Value *inner = inner_value.value;
                switch (node.op) {
                case TokenKind::Plus: return inner_value;
                case TokenKind::Minus:
                    if (inner_value.elem_float || (inner != nullptr && inner->getType()->isFloatingPointTy())) {
                        return {builder_.CreateFNeg(inner), 0, false, -1, false, false, false, inner_value.elem_bits, false,
                                -1, true};
                    }
                    return {builder_.CreateNeg(inner), 0, false, -1, false, false, false, inner_value.elem_bits,
                            inner_value.elem_unsigned};
                case TokenKind::Tilde:
                    return {builder_.CreateNot(inner), 0, false, -1, false, false, false, inner_value.elem_bits,
                            inner_value.elem_unsigned};
                case TokenKind::Bang:
                    return {builder_.CreateNot(as_bool(inner)), 0, false, -1, false, false, true};
                default: return {zero(), 0};
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.op == TokenKind::AmpAmp) {
                    return {emit_short_circuit(node, true), 0, false, -1, false, false, true};
                }
                if (node.op == TokenKind::PipePipe) {
                    return {emit_short_circuit(node, false), 0, false, -1, false, false, true};
                }
                const TypedValue left = emit_rvalue(*node.lhs);
                const TypedValue right = emit_rvalue(*node.rhs);
                llvm::Value *lhs = left.value;
                llvm::Value *rhs = right.value;
                const auto pointer_bits = [](const TypedValue &side) {
                    // `*[N]T` keeps leaf depth 0. The value is still a pointer, including `?*[N]T` and `**[N]T`.
                    return side.depth > 0 || side.ptr_to_array ||
                           (!side.shells.empty() && side.shells.front().kind == TypeShell::Kind::Pointer);
                };
                if ((node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) &&
                    (pointer_bits(left) || pointer_bits(right))) {
                    if (!pointer_bits(left)) {
                        lhs = null_ptr();
                    }
                    if (!pointer_bits(right)) {
                        rhs = null_ptr();
                    }
                    llvm::Value *cmp = node.op == TokenKind::EqEq ? builder_.CreateICmpEQ(lhs, rhs)
                                                                  : builder_.CreateICmpNE(lhs, rhs);
                    return {cmp, 0, false, -1, false, false, true};
                }
                if (left.elem_float || right.elem_float ||
                    (lhs != nullptr && lhs->getType()->isFloatingPointTy()) ||
                    (rhs != nullptr && rhs->getType()->isFloatingPointTy())) {
                    const bool wide = (left.elem_float && left.elem_bits >= 64) || (right.elem_float && right.elem_bits >= 64) ||
                                      (lhs != nullptr && lhs->getType()->isDoubleTy()) ||
                                      (rhs != nullptr && rhs->getType()->isDoubleTy());
                    llvm::Type *dest = wide ? f64_ty() : f32_ty();
                    auto to_fp = [&](llvm::Value *value, const TypedValue &meta) -> llvm::Value * {
                        if (value == nullptr) {
                            return llvm::ConstantFP::get(dest, 0.0);
                        }
                        return as_stored(value, meta.depth, dest, meta.elem_unsigned);
                    };
                    lhs = to_fp(lhs, left);
                    rhs = to_fp(rhs, right);
                    auto widened_fp = [&](llvm::Value *value) -> TypedValue {
                        return {value, 0, false, -1, false, false, false, wide ? 64 : 32, false, -1, true};
                    };
                    auto compared_fp = [&](llvm::CmpInst::Predicate pred) -> TypedValue {
                        return {builder_.CreateFCmp(pred, lhs, rhs), 0, false, -1, false, false, true};
                    };
                    switch (node.op) {
                    case TokenKind::Plus: return widened_fp(builder_.CreateFAdd(lhs, rhs));
                    case TokenKind::Minus: return widened_fp(builder_.CreateFSub(lhs, rhs));
                    case TokenKind::Star: return widened_fp(builder_.CreateFMul(lhs, rhs));
                    case TokenKind::Slash: return widened_fp(builder_.CreateFDiv(lhs, rhs));
                    case TokenKind::EqEq: return compared_fp(llvm::CmpInst::FCMP_OEQ);
                    case TokenKind::BangEq: return compared_fp(llvm::CmpInst::FCMP_ONE);
                    case TokenKind::Lt: return compared_fp(llvm::CmpInst::FCMP_OLT);
                    case TokenKind::Gt: return compared_fp(llvm::CmpInst::FCMP_OGT);
                    case TokenKind::Le: return compared_fp(llvm::CmpInst::FCMP_OLE);
                    case TokenKind::Ge: return compared_fp(llvm::CmpInst::FCMP_OGE);
                    default:
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                }
                const int left_bits = value_bits(left);
                const int right_bits = value_bits(right);
                const int common_bits = left_bits > right_bits ? left_bits : right_bits;
                bool common_unsigned = false;
                if (left.elem_unsigned == right.elem_unsigned) {
                    common_unsigned = left.elem_unsigned;
                } else if (left_bits == right_bits) {
                    common_unsigned = true;
                } else if (left.elem_unsigned && left_bits > right_bits) {
                    common_unsigned = true;
                } else if (right.elem_unsigned && right_bits > left_bits) {
                    common_unsigned = true;
                }
                lhs = resize_int(lhs, bits_type(common_bits), left.elem_unsigned);
                rhs = resize_int(rhs, bits_type(common_bits), right.elem_unsigned);
                auto widened = [&](llvm::Value *value) -> TypedValue {
                    return {value, 0, false, -1, false, false, false, common_bits, common_unsigned};
                };
                auto compared = [&](llvm::CmpInst::Predicate pred) -> TypedValue {
                    return {builder_.CreateICmp(pred, lhs, rhs), 0, false, -1, false, false, true};
                };
                const bool uns = common_unsigned;
                switch (node.op) {
                case TokenKind::Plus: return widened(builder_.CreateAdd(lhs, rhs));
                case TokenKind::Minus: return widened(builder_.CreateSub(lhs, rhs));
                case TokenKind::Star: return widened(builder_.CreateMul(lhs, rhs));
                case TokenKind::Slash: return widened(uns ? builder_.CreateUDiv(lhs, rhs) : builder_.CreateSDiv(lhs, rhs));
                case TokenKind::Percent: return widened(uns ? builder_.CreateURem(lhs, rhs) : builder_.CreateSRem(lhs, rhs));
                case TokenKind::Amp: return widened(builder_.CreateAnd(lhs, rhs));
                case TokenKind::Pipe: return widened(builder_.CreateOr(lhs, rhs));
                case TokenKind::Caret: return widened(builder_.CreateXor(lhs, rhs));
                case TokenKind::LtLt: return widened(builder_.CreateShl(lhs, rhs));
                case TokenKind::GtGt:
                    return widened(uns ? builder_.CreateLShr(lhs, rhs) : builder_.CreateAShr(lhs, rhs));
                case TokenKind::EqEq: return compared(llvm::CmpInst::ICMP_EQ);
                case TokenKind::BangEq: return compared(llvm::CmpInst::ICMP_NE);
                case TokenKind::Lt: return compared(uns ? llvm::CmpInst::ICMP_ULT : llvm::CmpInst::ICMP_SLT);
                case TokenKind::Gt: return compared(uns ? llvm::CmpInst::ICMP_UGT : llvm::CmpInst::ICMP_SGT);
                case TokenKind::Le: return compared(uns ? llvm::CmpInst::ICMP_ULE : llvm::CmpInst::ICMP_SLE);
                case TokenKind::Ge: return compared(uns ? llvm::CmpInst::ICMP_UGE : llvm::CmpInst::ICMP_SGE);
                default: return {zero(), 0};
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                arm_union_tag_ = true;
                TypedValue slot = emit_addr(*node.lhs);
                arm_union_tag_ = false;
                // `grid[i] = line` stores into this row. The address is the row, not a decayed pointer.
                if (slot.row_decay && slot.array_type != nullptr && slot.array_type->isArrayTy()) {
                    slot.is_array = true;
                }
                // A list and an array value use the same store. The result is this slot.
                if (slot.is_array && node.rhs != nullptr) {
                    if (slot.array_type == nullptr || !slot.array_type->isArrayTy() || slot.value == nullptr) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    store_array(slot.value, llvm::cast<llvm::ArrayType>(slot.array_type), *node.rhs, node.loc);
                    return slot;
                }
                const TypedValue rhs = emit_rvalue(*node.rhs);
                if (slot.is_slice) {
                    if (slot.value != nullptr && rhs.value != nullptr && !terminated()) {
                        builder_.CreateStore(as_slice_value(rhs, node.loc), slot.value);
                    }
                    return rhs;
                }
                llvm::Value *stored = nullptr;
                if (slot.value != nullptr && rhs.value != nullptr) {
                    stored = stored_value(rhs, slot);
                    if (!terminated()) {
                        builder_.CreateStore(stored, slot.value);
                    }
                }
                if (slot.depth == 0 && slot.elem_bool && stored != nullptr) {
                    return {builder_.CreateZExt(stored, i32()), 0};
                }
                // A function stored in a function-pointer field is that pointer. The function
                // value itself has depth 0, which is the result, not a null data pointer.
                if (slot.fn_ty != nullptr && stored != nullptr) {
                    TypedValue out{stored, slot.depth};
                    copy_fn_sig(out, slot);
                    return out;
                }
                if (rhs.depth == 0 && slot.depth > 0) {
                    return {null_ptr(), slot.depth, slot.elem_char, slot.struct_id, false, slot.elem_void};
                }
                return rhs;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (const auto folded = fold(expr)) {
                    return {const_i32(*folded), 0};
                }
                if (node.trait_cast && node.args.size() == 1 && node.args.front() != nullptr) {
                    return emit_rvalue(*node.args.front());
                }
                if (node.type_name || node.type_fields) {
                    const auto *selector = node.callee == nullptr ? nullptr : std::get_if<Expr::Selector>(&node.callee->data);
                    if (selector == nullptr || selector->base == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    llvm::Value *desc = type_descriptor(*selector->base);
                    if (desc == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    const int slot_index = node.type_fields ? 1 : 0;
                    llvm::Value *slot = builder_.CreateInBoundsGEP(
                        type_desc_type(), desc,
                        llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(slot_index)});
                    if (node.type_fields) {
                        if (field_struct_id_ < 0) {
                            error(node.loc, "type mismatch");
                            return {zero(), 0};
                        }
                        TypedValue out;
                        out.value = builder_.CreateLoad(slice_type(), slot);
                        out.is_slice = true;
                        out.array_len = -1;
                        out.depth = 0;
                        out.struct_id = field_struct_id_;
                        return out;
                    }
                    return {builder_.CreateLoad(ptr_type(), slot), 1, true};
                }
                if (node.callee != nullptr && is_std_builtin_call(*node.callee, "va")) {
                    return emit_builtin_va(node.loc);
                }
                if (node.callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        // An overloaded trait parameter is stored under a suffixed name.
                        // `selected` is the list sema chose. Its binding has no concrete function yet.
                        if (node.selected != nullptr && function_is_generic(current_file_, *node.selected)) {
                            Binding meta;
                            if (const Binding *chosen = binding_for_decl(node.selected)) {
                                meta = *chosen;
                            }
                            meta.decl = node.selected;
                            meta.is_generic = true;
                            return emit_generic_call(meta, node);
                        }
                        if (const Binding *binding = lookup(named->text);
                            binding != nullptr && binding->is_generic && binding->decl != nullptr) {
                            return emit_generic_call(*binding, node);
                        }
                        if (lookup(named->text) == nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                            int enum_id = enum_named(current_file_, named->text);
                            if (enum_id < 0) {
                                enum_id = selective_enum(current_file_, named->text);
                            }
                            if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                                const TypedValue source = emit_rvalue(*node.args.front());
                                if (source.value != nullptr && source.depth == 0 && source.struct_id >= 0 &&
                                    source.struct_id < static_cast<int>(structs_.size()) &&
                                    source.value->getType()->isStructTy() &&
                                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                    llvm::Value *tag = builder_.CreateExtractValue(source.value, 2);
                                    const EnumLayout &enumeration = enums_[static_cast<std::size_t>(enum_id)];
                                    TypedValue value;
                                    value.value = tag;
                                    value.enum_id = enum_id;
                                    value.elem_bool = enumeration.is_bool;
                                    value.elem_char = enumeration.is_char;
                                    value.elem_unsigned = enumeration.is_unsigned;
                                    value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                                    return value;
                                }
                                error(node.loc, "type mismatch");
                                return {zero(), 0};
                            }
                        }
                    }
                    // `geom.Kind(shape)` reads the tag. The enum is not a function.
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                        if (selector->base != nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                                if (packages_.contains(base->text)) {
                                    const uint32_t target = package_file(current_file_, base->text);
                                    const int enum_id = target == 0 ? -1 : enum_id_of(target, selector->field);
                                    if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                                        const TypedValue source = emit_rvalue(*node.args.front());
                                        if (source.value != nullptr && source.depth == 0 && source.struct_id >= 0 &&
                                            source.struct_id < static_cast<int>(structs_.size()) &&
                                            source.value->getType()->isStructTy() &&
                                            structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                            llvm::Value *tag = builder_.CreateExtractValue(source.value, 2);
                                            const EnumLayout &enumeration = enums_[static_cast<std::size_t>(enum_id)];
                                            TypedValue value;
                                            value.value = tag;
                                            value.enum_id = enum_id;
                                            value.elem_bool = enumeration.is_bool;
                                            value.elem_char = enumeration.is_char;
                                            value.elem_unsigned = enumeration.is_unsigned;
                                            value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                                            return value;
                                        }
                                        error(node.loc, "type mismatch");
                                        return {zero(), 0};
                                    }
                                }
                            }
                        }
                    }
                    // `geom.use(r)` specializes an exported function whose parameter is a trait.
                    // A template such as `measure[T Shape]` is not in this table.
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                        if (selector->base != nullptr) {
                            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                                const auto package = packages_.find(base->text);
                                if (package != packages_.end()) {
                                    if (const Binding *binding = visible(package->second, selector->field, true);
                                        binding != nullptr && binding->is_generic && binding->decl != nullptr &&
                                        binding->decl->type_params.empty()) {
                                        return emit_generic_call(*binding, node);
                                    }
                                }
                            }
                        }
                    }
                }
                return emit_prepared(prepare_call(node));
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.slice_len) {
                    if (node.len_value >= 0) {
                        return {const_i32(node.len_value), 0};
                    }
                    const TypedValue base = node.base != nullptr ? emit_rvalue(*node.base) : TypedValue{};
                    if (base.value == nullptr || !base.is_slice) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    return {builder_.CreateExtractValue(base.value, 1), 0};
                }
                if (!node.resolved_owner.empty()) {
                    const std::string key = node.resolved_owner + "." + node.field;
                    const Binding *global = visible(assoc_home(node), key, false);
                    if (global != nullptr && (global->value != nullptr || global->immediate)) {
                        return value_of_binding(*global);
                    }
                    const Binding *method = static_selector_binding(current_file_, node);
                    if (method != nullptr && method->value != nullptr) {
                        return value_of_binding(*method);
                    }
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (node.base != nullptr) {
                    const int enum_id = enum_type_designator(*node.base);
                    if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                        const EnumLayout &layout = enums_[static_cast<std::size_t>(enum_id)];
                        const auto member = layout.values.find(node.field);
                        if (member != layout.values.end()) {
                            TypedValue value;
                            value.value = enum_constant(enum_id, member->second);
                            value.enum_id = enum_id;
                            value.elem_bool = layout.is_bool;
                            value.elem_char = layout.is_char;
                            value.elem_unsigned = layout.is_unsigned;
                            value.elem_bits = layout.is_bool ? 0 : layout.bits;
                            return value;
                        }
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                        if (unshadowed_type_name(named->text)) {
                            const Binding *method = static_method_binding(current_file_, named->text, node.field);
                            auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                            if (function == nullptr) {
                                error(node.loc, "type mismatch");
                                return {zero(), 0};
                            }
                            return value_of_binding(*method);
                        }
                    }
                }
                const auto *base_name = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                const auto package = base_name == nullptr ? packages_.end() : packages_.find(base_name->text);
                if (base_name != nullptr && package != packages_.end()) {
                    if (const Binding *function = visible(package->second, node.field, true)) {
                        return value_of_binding(*function);
                    }
                    if (const Binding *global = visible(package->second, node.field, false)) {
                        return value_of_binding(*global);
                    }
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (node.base != nullptr && addressable(*node.base)) {
                    const TypedValue addr = emit_addr(expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return load_field(addr);
                }
                const TypedValue base = node.base != nullptr ? emit_rvalue(*node.base) : TypedValue{};
                if (base.value != nullptr && base.depth == 1 && base.struct_id >= 0) {
                    const TypedValue addr = field_addr(base.struct_id, base.value, node.field, node.loc);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return load_field(addr);
                }
                if (base.value == nullptr || base.depth != 0 || base.struct_id < 0) {
                    error(node.loc, "type mismatch");
                    return {zero(), 0};
                }
                int field_index = -1;
                const FieldLayout *field = find_field(base.struct_id, node.field, field_index);
                if (field == nullptr || field_index < 0) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (field->is_array) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                if (structs_[static_cast<std::size_t>(base.struct_id)].is_union) {
                    llvm::AllocaInst *slot =
                        alloca_typed(structs_[static_cast<std::size_t>(base.struct_id)].type, "uval");
                    if (base.value != nullptr) {
                        builder_.CreateStore(base.value, slot);
                    }
                    if (structs_[static_cast<std::size_t>(base.struct_id)].tag_enum >= 0) {
                        const TypedValue addr = field_addr(base.struct_id, slot, node.field, node.loc);
                        if (addr.value == nullptr) {
                            return {zero(), 0};
                        }
                        return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                           addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                    }
                    return load_object(slot, field->depth, field->elem_char, field->struct_id, field->elem_void,
                                       field->elem_bool, field->elem_bits, field->elem_unsigned, field->elem_float);
                }
                llvm::Value *extracted = builder_.CreateExtractValue(base.value, static_cast<unsigned>(field_index));
                if (field->is_slice) {
                    TypedValue loaded;
                    loaded.value = extracted;
                    loaded.is_slice = true;
                    loaded.elem_slice = field->elem_slice;
                    loaded.shells = field->shells;
                    loaded.array_len = -1;
                    loaded.depth = field->depth;
                    loaded.elem_char = field->elem_char;
                    loaded.struct_id = field->struct_id;
                    loaded.elem_void = field->elem_void;
                    loaded.elem_bool = field->elem_bool;
                    loaded.elem_bits = field->elem_bits;
                    loaded.elem_unsigned = field->elem_unsigned;
                    loaded.elem_float = field->elem_float;
                    return loaded;
                }
                if (field->depth == 0 && field->elem_bool) {
                    return {builder_.CreateZExt(extracted, i32()), 0, false, -1, false, field->elem_void, true};
                }
                if (field->depth == 0 && (field->elem_char || field->elem_bits == 8 || field->elem_bits == 16)) {
                    const bool zero_extend = field->elem_unsigned || field->elem_char;
                    llvm::Value *wide =
                        zero_extend ? builder_.CreateZExt(extracted, i32()) : builder_.CreateSExt(extracted, i32());
                    const int bits = field->elem_bits == 0 ? 8 : field->elem_bits;
                    return {wide, 0, field->elem_char, -1, false, field->elem_void, false, bits, field->elem_unsigned};
                }
                if (field->depth == 0 && field->elem_float) {
                    return {extracted, 0, false, -1, false, false, false, field->elem_bits, false, -1, true};
                }
                if (field->depth == 0 && field->elem_bits == 64) {
                    return {extracted, 0, false, -1, false, false, false, 64, field->elem_unsigned};
                }
                if (field->depth == 0 && field->elem_unsigned) {
                    return {extracted, 0, false, -1, false, false, false, 32, true};
                }
                TypedValue loaded{extracted, field->depth, field->elem_char, field->struct_id, false, field->elem_void,
                                  field->elem_bool, field->elem_bits, field->elem_unsigned, -1, field->elem_float};
                copy_fn_sig(loaded, *field);
                return loaded;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                llvm::Constant *data = llvm::ConstantDataArray::getString(context_, node.text, true);
                auto *global = new llvm::GlobalVariable(module_, data->getType(), true,
                                                        llvm::GlobalValue::PrivateLinkage, data, ".str");
                // The value stays the address, so `*const u8` is still one `leaq`.
                // `is_slice` tells a slice store to pack that address with the byte count.
                TypedValue out;
                out.value = global;
                out.depth = 1;
                out.elem_bits = 8;
                out.elem_unsigned = true;
                out.elem_char = true;
                out.is_slice = true;
                out.array_len = static_cast<int>(node.text.size());
                return out;
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (const auto folded = fold(expr)) {
                    return {const_i32(*folded), 0};
                }
                if (node.is_range) {
                    return emit_range(node);
                }
                if (node.tuple_index >= 0) {
                    if (node.base == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    const TypedValue base = emit_rvalue(*node.base);
                    if (base.value == nullptr || !base.value->getType()->isStructTy()) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    auto *struct_ty = llvm::cast<llvm::StructType>(base.value->getType());
                    const auto index = static_cast<unsigned>(node.tuple_index);
                    if (index >= struct_ty->getNumElements()) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    llvm::Value *extracted = builder_.CreateExtractValue(base.value, index);
                    const ResultSlot *slot = nullptr;
                    if (base.is_tuple && index < base.tuple_parts.size()) {
                        slot = &base.tuple_parts[index];
                    } else if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                        const Binding *binding = lookup(named->text);
                        if (binding != nullptr && index < binding->tuple_parts.size()) {
                            slot = &binding->tuple_parts[index];
                        }
                    }
                    if (slot != nullptr) {
                        const Lowered part = lower(current_file_, slot->type_name, slot->type_package, slot->is_struct,
                                                   slot->indirections, slot->is_array, slot->array_len, false,
                                                   slot->elem_slice);
                        if (part.is_slice) {
                            TypedValue loaded;
                            loaded.value = extracted;
                            loaded.is_slice = true;
                            loaded.elem_slice = part.elem_slice;
                            loaded.array_len = -1;
                            loaded.depth = part.depth;
                            loaded.elem_char = part.elem_char;
                            loaded.struct_id = part.struct_id;
                            loaded.elem_void = part.elem_void;
                            loaded.elem_bool = part.elem_bool;
                            loaded.elem_bits = part.elem_bits;
                            loaded.elem_unsigned = part.elem_unsigned;
                            loaded.elem_float = part.elem_float;
                            loaded.enum_id = part.enum_id;
                            return loaded;
                        }
                        if (part.depth == 0 && part.elem_bool) {
                            return {builder_.CreateZExt(extracted, i32()), 0, false, -1, false, part.elem_void, true};
                        }
                        if (part.depth == 0 && (part.elem_char || part.elem_bits == 8 || part.elem_bits == 16)) {
                            const bool zero_extend = part.elem_unsigned || part.elem_char;
                            llvm::Value *wide = zero_extend ? builder_.CreateZExt(extracted, i32())
                                                            : builder_.CreateSExt(extracted, i32());
                            const int bits = part.elem_bits == 0 ? 8 : part.elem_bits;
                            return {wide, 0, part.elem_char, -1, false, part.elem_void, false, bits, part.elem_unsigned};
                        }
                        if (part.depth == 0 && part.elem_float) {
                            return {extracted, 0, false, -1, false, false, false, part.elem_bits, false, -1, true};
                        }
                        if (part.depth == 0 && part.elem_bits == 64) {
                            return {extracted, 0, false, -1, false, false, false, 64, part.elem_unsigned};
                        }
                        if (part.depth == 0 && part.elem_unsigned) {
                            return {extracted, 0, false, -1, false, false, false, 32, true};
                        }
                        return {extracted, part.depth, part.elem_char, part.struct_id, false, part.elem_void,
                                part.elem_bool, part.elem_bits, part.elem_unsigned, -1, part.elem_float};
                    }
                    llvm::Type *field_ty = extracted->getType();
                    if (field_ty == slice_type()) {
                        TypedValue loaded;
                        loaded.value = extracted;
                        loaded.is_slice = true;
                        loaded.array_len = -1;
                        return loaded;
                    }
                    if (field_ty->isIntegerTy(1) || field_ty->isIntegerTy(8) || field_ty->isIntegerTy(16)) {
                        return {builder_.CreateZExt(extracted, i32()), 0};
                    }
                    if (field_ty->isFloatingPointTy()) {
                        const int bits = field_ty->getPrimitiveSizeInBits() >= 64 ? 64 : 32;
                        return {extracted, 0, false, -1, false, false, false, bits, false, -1, true};
                    }
                    if (field_ty->isIntegerTy(64)) {
                        return {extracted, 0, false, -1, false, false, false, 64, false};
                    }
                    if (field_ty->isPointerTy()) {
                        return {extracted, 1};
                    }
                    return {extracted, 0};
                }
                const TypedValue addr = emit_addr(expr);
                if (addr.value == nullptr) {
                    return {zero(), 0};
                }
                if (addr.row_decay) {
                    TypedValue row = addr;
                    row.row_decay = false;
                    return row;
                }
                if (addr.is_slice) {
                    return load_field(addr);
                }
                TypedValue loaded = load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                                addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                loaded.fn_ty = addr.fn_ty;
                loaded.fn_ret_depth = addr.fn_ret_depth;
                loaded.fn_ret_float = addr.fn_ret_float;
                loaded.fn_ret_bits = addr.fn_ret_bits;
                loaded.fn_ret_unsigned = addr.fn_ret_unsigned;
                loaded.fn_ret_char = addr.fn_ret_char;
                loaded.fn_ret_bool = addr.fn_ret_bool;
                loaded.fn_ret_struct = addr.fn_ret_struct;
                loaded.fn_results = addr.fn_results;
                if (loaded.enum_id < 0) {
                    loaded.enum_id = addr.enum_id;
                }
                return loaded;
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                llvm::Type *measured = nullptr;
                if (node.on_type) {
                    const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections,
                                              node.is_array, node.is_array ? node.array_len : 0, false, node.elem_slice,
                                              node.shells);
                    if (low.depth == 0 && low.elem_void) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    measured = low.type;
                } else if (node.expr) {
                    const Shape shape = raw_shape(*node.expr);
                    if (shape.is_function || (shape.depth == 0 && shape.elem_void)) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    measured = shape.type;
                }
                if (measured == nullptr || measured->isVoidTy() || measured->isFunctionTy()) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                const uint64_t bytes = node.align ? module_.getDataLayout().getABITypeAlign(measured).value()
                                                  : module_.getDataLayout().getTypeAllocSize(measured).getFixedValue();
                if (bytes > 2147483647ULL) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                return {const_i32(static_cast<int32_t>(bytes)), 0};
            } else if constexpr (std::is_same_v<T, Expr::New>) {
                return emit_new(node);
            } else if constexpr (std::is_same_v<T, Expr::Delete>) {
                emit_delete(node);
                return {zero(), 0, false, -1, false, true};
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                if (node.canonical.empty()) {
                    error(node.loc, "type mismatch");
                    return {zero(), 0};
                }
                return {intern_type_desc(node.canonical), 1};
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return {const_i32(node.value ? 1 : 0), 0};
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return {null_ptr(), 1, false, -1, false, true};
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                const TypedValue source = node.expr ? emit_rvalue(*node.expr) : TypedValue{};
                // `Drawable(value)` is the value. The trait has no representation.
                if (node.trait_value) {
                    return source;
                }
                const Lowered target =
                    node.is_fn_ptr ? lower_fnptr(current_file_, node.type_name, node.type_package, node.is_struct,
                                                 node.is_enum, node.ret_indirections, node.indirections, node.fn_params,
                                                 node.fn_variadic, node.fn_results)
                                   : lower(current_file_, node.type_name, node.type_package, node.is_struct,
                                           node.indirections, false, 0, false);
                if (source.value == nullptr) {
                    return {zero(), 0};
                }
                // The call uses this signature. A data pointer and another function pointer are both an address.
                auto stamp_fn = [&](llvm::Value *bits) {
                    TypedValue value;
                    value.value = bits;
                    value.depth = target.depth > 0 ? target.depth : 1;
                    value.fn_ty = target.fn_ty;
                    value.fn_ret_depth = target.fn_ret_depth;
                    value.fn_ret_float = target.fn_ret_float;
                    value.fn_ret_bits = target.fn_ret_bits;
                    value.fn_ret_unsigned = target.fn_ret_unsigned;
                    value.fn_ret_char = target.fn_ret_char;
                    value.fn_ret_bool = target.fn_ret_bool;
                    value.fn_ret_struct = target.fn_ret_struct;
                    value.fn_results = target.fn_results;
                    return value;
                };
                if (target.fn_ty != nullptr &&
                    (source.depth > 0 || source.value->getType()->isPointerTy())) {
                    return stamp_fn(source.value);
                }
                if (source.depth == 0 && source.struct_id >= 0 && source.struct_id < static_cast<int>(structs_.size()) &&
                    source.value->getType()->isStructTy()) {
                    const StructLayout &layout = structs_[static_cast<std::size_t>(source.struct_id)];
                    if (layout.tag_enum >= 0 && target.enum_id == layout.tag_enum &&
                        layout.tag_enum < static_cast<int>(enums_.size()) && layout.type != nullptr &&
                        layout.type->getNumElements() > 2) {
                        llvm::Value *tag = builder_.CreateExtractValue(source.value, 2);
                        const EnumLayout &enumeration = enums_[static_cast<std::size_t>(layout.tag_enum)];
                        TypedValue value;
                        value.value = tag;
                        value.enum_id = layout.tag_enum;
                        value.elem_bool = enumeration.is_bool;
                        value.elem_char = enumeration.is_char;
                        value.elem_unsigned = enumeration.is_unsigned;
                        value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                        return value;
                    }
                }
                // `string_view(text)` keeps the `{ ptr, len }` value. The alias is not an integer.
                if (target.is_slice) {
                    TypedValue value;
                    value.value = as_slice_value(source, node.loc);
                    value.is_slice = true;
                    value.elem_slice = target.elem_slice;
                    value.array_len = -1;
                    value.depth = target.depth;
                    value.elem_char = target.elem_char;
                    value.elem_void = target.elem_void;
                    value.elem_bool = target.elem_bool;
                    value.elem_bits = target.elem_bits;
                    value.elem_unsigned = target.elem_unsigned;
                    value.elem_float = target.elem_float;
                    value.struct_id = target.struct_id;
                    value.alias_owner = target.alias_owner;
                    value.alias_file = target.alias_file;
                    return value;
                }
                if (target.depth > 0 && source.depth > 0) {
                    return {source.value, target.depth, target.elem_char, target.struct_id, false, target.elem_void,
                            target.elem_bool, target.elem_bits, target.elem_unsigned, target.trait_id, target.elem_float};
                }
                if (target.depth > 0 && source.depth == 0) {
                    llvm::IntegerType *intptr = module_.getDataLayout().getIntPtrType(context_, 0);
                    llvm::Value *wide = source.value;
                    if (wide->getType()->isIntegerTy() && wide->getType() != intptr) {
                        if (wide->getType()->getIntegerBitWidth() < intptr->getIntegerBitWidth()) {
                            wide = source.elem_unsigned ? builder_.CreateZExt(wide, intptr)
                                                        : builder_.CreateSExt(wide, intptr);
                        } else {
                            wide = builder_.CreateTrunc(wide, intptr);
                        }
                    }
                    llvm::Value *pointer = builder_.CreateIntToPtr(wide, ptr_type());
                    if (target.fn_ty != nullptr) {
                        return stamp_fn(pointer);
                    }
                    return {pointer, target.depth, target.elem_char, target.struct_id, false, target.elem_void,
                            target.elem_bool, target.elem_bits, target.elem_unsigned, target.trait_id, target.elem_float};
                }
                if (source.depth > 0 && target.depth == 0 && !target.elem_char && !target.elem_void && target.struct_id < 0 &&
                    !target.elem_bool && target.elem_bits != 8 && target.elem_bits != 16 && target.elem_bits != 64) {
                    llvm::IntegerType *intptr = module_.getDataLayout().getIntPtrType(context_, 0);
                    llvm::Value *wide = builder_.CreatePtrToInt(source.value, intptr);
                    llvm::Value *narrow = wide->getType() == i32() ? wide : builder_.CreateTrunc(wide, i32());
                    return {narrow, 0};
                }
                // An alias and its representation are the same bits. The cast only changes the type.
                auto scalar_bits = [](int bits, bool is_bool, bool is_char) {
                    if (is_bool) {
                        return 1;
                    }
                    if (bits == 0 && is_char) {
                        return 8;
                    }
                    if (bits == 0) {
                        return 32;
                    }
                    return bits;
                };
                const bool scalar_source = source.depth == 0 && source.struct_id < 0 && !source.elem_float &&
                                           source.value->getType()->isIntegerTy();
                const bool scalar_target = target.depth == 0 && target.struct_id < 0 && !target.elem_float &&
                                           !target.elem_void && target.fn_ty == nullptr;
                if (scalar_source && scalar_target && source.elem_unsigned == target.elem_unsigned &&
                    source.elem_bool == target.elem_bool &&
                    scalar_bits(source.elem_bits, source.elem_bool, source.elem_char) ==
                        scalar_bits(target.elem_bits, target.elem_bool, target.elem_char)) {
                    TypedValue value = source;
                    value.elem_bits = target.elem_bits;
                    value.elem_bool = target.elem_bool;
                    value.elem_char = target.elem_char;
                    value.elem_unsigned = target.elem_unsigned;
                    value.enum_id = target.enum_id;
                    return value;
                }
                if (target.depth == 0 && source.depth == 0 && target.struct_id >= 0 &&
                    source.struct_id == target.struct_id && source.value->getType()->isStructTy()) {
                    return source;
                }
                if (target.depth == 0 && source.depth == 0 && target.elem_float && source.elem_float &&
                    target.elem_bits == source.elem_bits) {
                    TypedValue value = source;
                    value.elem_float = true;
                    value.elem_bits = target.elem_bits;
                    return value;
                }
                if (target.depth == 0 && source.depth == 0 && (target.elem_float || source.elem_float ||
                                                              source.value->getType()->isFloatingPointTy())) {
                    llvm::Type *dest = target.elem_float ? (target.elem_bits >= 64 ? f64_ty() : f32_ty())
                                                         : bits_type(target.elem_bits == 0 ? 32 : target.elem_bits);
                    llvm::Value *converted = as_stored(source.value, 0, dest, source.elem_unsigned);
                    if (target.elem_float) {
                        return {converted, 0, false, -1, false, false, false, target.elem_bits, false, -1, true};
                    }
                    return {converted, 0, target.elem_char, -1, false, false, target.elem_bool, target.elem_bits,
                            target.elem_unsigned};
                }
                if (target.depth == 0 && source.depth == 0 && target.struct_id < 0 && !target.elem_void) {
                    int bits = target.elem_bits;
                    if (target.elem_bool) {
                        bits = 1;
                    } else if (bits == 0 && target.elem_char) {
                        bits = 8;
                    } else if (bits == 0) {
                        bits = 32;
                    }
                    llvm::Value *converted = resize_int(source.value, bits_type(bits), source.elem_unsigned);
                    if (bits < 32) {
                        const bool zero_extend = target.elem_unsigned || target.elem_char;
                        converted = zero_extend ? builder_.CreateZExt(converted, i32()) : builder_.CreateSExt(converted, i32());
                        return {converted, 0};
                    }
                    return {converted, 0, false, -1, false, false, false, bits, target.elem_unsigned};
                }
                return {source.value, 0};
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (node.decl == nullptr) {
                    return {zero(), 0};
                }
                return emit_anon_function(*node.decl);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                if (node.array_literal) {
                    Lowered low;
                    llvm::AllocaInst *slot = emit_array_literal(node, low);
                    if (slot == nullptr) {
                        return {zero(), 0};
                    }
                    // Same shape as a loaded array name: the address, one level above the element.
                    TypedValue value{slot,          low.depth + 1, low.elem_char,     low.struct_id, false,
                                     low.elem_void, low.elem_bool, low.elem_bits,     low.elem_unsigned, -1,
                                     low.elem_float};
                    value.array_len = low.array_len;
                    value.inner_array_len = low.inner_array_len;
                    value.fn_ty = low.fn_ty;
                    value.fn_ret_depth = low.fn_ret_depth;
                    value.fn_ret_float = low.fn_ret_float;
                    value.fn_ret_bits = low.fn_ret_bits;
                    value.fn_ret_unsigned = low.fn_ret_unsigned;
                    value.fn_ret_char = low.fn_ret_char;
                    value.fn_ret_bool = low.fn_ret_bool;
                    value.fn_ret_struct = low.fn_ret_struct;
                    value.fn_results = low.fn_results;
                    value.enum_id = low.enum_id;
                    return value;
                }
                if (node.is_tuple) {
                    std::vector<llvm::Value *> fields;
                    std::vector<llvm::Type *> types;
                    std::vector<TypedValue> element_values;
                    fields.reserve(node.elements.size());
                    types.reserve(node.elements.size());
                    element_values.reserve(node.elements.size());
                    for (const auto &element : node.elements) {
                        TypedValue value = element ? emit_rvalue(*element) : TypedValue{};
                        if (terminated()) {
                            return {zero(), 0};
                        }
                        if (value.value == nullptr) {
                            value.value = zero();
                        }
                        // An rvalue promotes char, bool, and a narrow integer to int. The tuple keeps
                        // the type sema gave the element, the same width a function result uses.
                        auto restore = [&](llvm::Type *ty, bool elem_char, bool elem_bool, int elem_bits,
                                           bool elem_unsigned) {
                            if (ty == nullptr || value.value == nullptr) {
                                return;
                            }
                            if (value.value->getType() == ty) {
                                value.elem_char = elem_char;
                                value.elem_bool = elem_bool;
                                value.elem_bits = elem_bits;
                                value.elem_unsigned = elem_unsigned;
                                return;
                            }
                            if (!value.value->getType()->isIntegerTy() || !ty->isIntegerTy() ||
                                ty->getIntegerBitWidth() >= value.value->getType()->getIntegerBitWidth()) {
                                return;
                            }
                            value.value = as_stored(value.value, 0, ty, elem_unsigned || elem_char);
                            value.elem_char = elem_char;
                            value.elem_bool = elem_bool;
                            value.elem_bits = elem_bits;
                            value.elem_unsigned = elem_unsigned;
                            value.elem_float = false;
                            value.depth = 0;
                            value.struct_id = -1;
                        };
                        if (element != nullptr && std::holds_alternative<Expr::Char>(element->data)) {
                            restore(i8(), false, false, 8, true);
                        } else if (element != nullptr && std::holds_alternative<Expr::Bool>(element->data)) {
                            restore(i1(), false, true, 0, false);
                        } else if (element != nullptr) {
                            if (const auto *number = std::get_if<Expr::Int>(&element->data);
                                number != nullptr && (number->suffix.bits == 8 || number->suffix.bits == 16)) {
                                llvm::Type *ty = number->suffix.bits == 8 ? i8() : i16();
                                restore(ty, false, false, number->suffix.bits, number->suffix.is_unsigned);
                            } else if (const auto *named = std::get_if<Expr::Name>(&element->data)) {
                                const Binding *binding = lookup(named->text);
                                if (binding != nullptr && !binding->is_tuple && !binding->is_function &&
                                    !binding->is_array && binding->depth == 0 && binding->struct_id < 0 &&
                                    binding->value != nullptr) {
                                    restore(object_type(binding->value), binding->elem_char, binding->elem_bool,
                                            binding->elem_bits, binding->elem_unsigned);
                                    if (binding->enum_id >= 0) {
                                        value.enum_id = binding->enum_id;
                                    }
                                }
                            }
                        }
                        // A string is still a pointer here. The tuple element is the slice.
                        if (value.is_slice && value.value->getType() != slice_type()) {
                            if (value.depth > 0) {
                                value.depth -= 1;
                            }
                            value.value = as_slice_value(value, node.loc);
                        }
                        fields.push_back(value.value);
                        types.push_back(value.value->getType());
                        element_values.push_back(std::move(value));
                    }
                    llvm::StructType *aggregate_type = llvm::StructType::get(context_, types);
                    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(aggregate_type);
                    for (unsigned index = 0; index < fields.size(); ++index) {
                        aggregate = builder_.CreateInsertValue(aggregate, fields[index], index);
                    }
                    auto slot_of = [&](const TypedValue &element) {
                        ResultSlot slot;
                        auto scalar = [&]() -> std::string {
                            if (element.elem_bool) {
                                return "bool";
                            }
                            if (element.elem_char) {
                                return "u8";
                            }
                            if (element.elem_float) {
                                return element.elem_bits >= 64 ? "f64" : "f32";
                            }
                            if (element.elem_bits == 64) {
                                return element.elem_unsigned ? "u64" : "i64";
                            }
                            if (element.elem_bits == 16) {
                                return element.elem_unsigned ? "u16" : "i16";
                            }
                            if (element.elem_bits == 8) {
                                return element.elem_unsigned ? "u8" : "i8";
                            }
                            if (element.elem_unsigned) {
                                return "u32";
                            }
                            return "i32";
                        };
                        if (element.is_slice) {
                            slot.is_array = true;
                            slot.array_len = -1;
                            slot.indirections = element.depth;
                            if (element.struct_id >= 0 && element.struct_id < static_cast<int>(structs_.size())) {
                                slot.is_struct = true;
                                slot.type_name = structs_[static_cast<std::size_t>(element.struct_id)].name;
                            } else if (element.elem_void) {
                                slot.type_name = "void";
                            } else {
                                slot.type_name = scalar();
                            }
                            return slot;
                        }
                        if (element.depth > 0) {
                            slot.indirections = element.depth;
                            if (element.struct_id >= 0 && element.struct_id < static_cast<int>(structs_.size())) {
                                slot.is_struct = true;
                                slot.type_name = structs_[static_cast<std::size_t>(element.struct_id)].name;
                            } else if (element.elem_void) {
                                slot.type_name = "void";
                            } else {
                                slot.type_name = scalar();
                            }
                            return slot;
                        }
                        if (element.struct_id >= 0 && element.struct_id < static_cast<int>(structs_.size())) {
                            slot.is_struct = true;
                            slot.type_name = structs_[static_cast<std::size_t>(element.struct_id)].name;
                            return slot;
                        }
                        if (element.enum_id >= 0 && element.enum_id < static_cast<int>(enums_.size())) {
                            slot.is_enum = true;
                            slot.type_name = enums_[static_cast<std::size_t>(element.enum_id)].name;
                            return slot;
                        }
                        slot.type_name = scalar();
                        return slot;
                    };
                    TypedValue out;
                    out.value = aggregate;
                    out.is_tuple = true;
                    out.tuple_parts.reserve(element_values.size());
                    for (const TypedValue &element : element_values) {
                        out.tuple_parts.push_back(slot_of(element));
                    }
                    return out;
                }
                if (node.elements.size() == 1 && node.elements.front()) {
                    return emit_rvalue(*node.elements.front());
                }
                return emit_init(node);
            } else {
                return {zero(), 0};
            }
        },
        expr.data);
}

llvm::Value *CodeGen::type_descriptor(const Expr &expr) {
    // `var t builtin.Type` is one pointer. `var p *builtin.Type` is a pointer to that pointer.
    auto narrow = [&](llvm::Value *value, int depth) -> llvm::Value * {
        if (value == nullptr || depth < 1 || depth > 2) {
            return nullptr;
        }
        if (depth == 2) {
            value = builder_.CreateLoad(ptr_type(), value);
        }
        return value;
    };
    if (addressable(expr)) {
        const TypedValue addr = emit_addr(expr);
        if (addr.value == nullptr) {
            return nullptr;
        }
        return narrow(builder_.CreateLoad(ptr_type(), addr.value), addr.depth);
    }
    const TypedValue base = emit_rvalue(expr);
    return narrow(base.value, base.depth);
}

} // namespace tlc
