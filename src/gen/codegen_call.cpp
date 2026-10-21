// Calls, including generic calls and trait calls.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

std::vector<llvm::Value *> CodeGen::pack_extern_args(llvm::Function *function, const ExternLayout &layout,
                                                       const std::vector<TypedValue> &sources, PreparedCall &prepared) {
    std::vector<llvm::Value *> args;
    unsigned attr_index = 0;
    if (layout.sret && layout.ret_struct != nullptr) {
        llvm::AllocaInst *slot = aligned_slot(layout.ret_struct, std::max(layout.ret.natural_align, 1u), "sret");
        args.push_back(slot);
        prepared.abi_return = true;
        prepared.abi_sret = true;
        prepared.abi_slot = slot;
        prepared.abi_struct = layout.ret_struct;
        prepared.abi_struct_id = layout.ret_struct_id;
        prepared.abi_ret = layout.ret;
        AbiMark mark;
        mark.index = attr_index++;
        mark.sret = true;
        mark.type = layout.ret_struct;
        mark.align = layout.ret.natural_align == 0 ? 1 : layout.ret.natural_align;
        prepared.abi_attrs.push_back(mark);
    } else if (layout.ret_struct != nullptr) {
        llvm::AllocaInst *slot = aligned_slot(layout.ret_struct, std::max(layout.ret.natural_align, 8u), "cret");
        prepared.abi_return = true;
        prepared.abi_slot = slot;
        prepared.abi_struct = layout.ret_struct;
        prepared.abi_struct_id = layout.ret_struct_id;
        prepared.abi_ret = layout.ret;
    }
    std::size_t source = 0;
    for (const TypedValue &value : sources) {
        if (source < layout.args.size() && layout.args[source].is_struct) {
            const ExternLayout::Arg &info = layout.args[source];
            llvm::AllocaInst *slot = aligned_slot(info.struct_type, std::max(info.abi.natural_align, 8u), "carg");
            if (value.value != nullptr && value.value->getType() == info.struct_type) {
                builder_.CreateStore(value.value, slot);
            } else if (info.struct_type != nullptr) {
                builder_.CreateStore(llvm::ConstantAggregateZero::get(info.struct_type), slot);
            }
            if (info.abi.memory) {
                args.push_back(slot);
                AbiMark mark;
                mark.index = attr_index++;
                mark.type = info.struct_type;
                mark.align = info.abi.indirect_align == 0 ? 8 : info.abi.indirect_align;
                prepared.abi_attrs.push_back(mark);
            } else {
                for (std::size_t part = 0; part < info.abi.parts.size(); ++part) {
                    const uint64_t offset = part < info.abi.offsets.size() ? info.abi.offsets[part] : 0;
                    args.push_back(load_at(slot, info.abi.parts[part], offset));
                    ++attr_index;
                }
            }
        } else {
            llvm::Type *param = attr_index < function->getFunctionType()->getNumParams()
                                    ? function->getFunctionType()->getParamType(attr_index)
                                    : nullptr;
            args.push_back(adapt(value, param, prepared.loc));
            ++attr_index;
        }
        ++source;
    }
    return args;
}

CodeGen::PreparedCall CodeGen::prepare_call(const Expr::Call &node) {
    PreparedCall prepared;
    prepared.loc = node.loc;
    if (node.callee == nullptr) {
        error(node.loc, "type mismatch");
        return prepared;
    }
    prepared.trace = !is_std_builtin_call(*node.callee, "trace") && !is_std_builtin_call(*node.callee, "va");
    auto selected_binding = [&](const Binding *found) -> const Binding * {
        if (node.selected == nullptr) {
            return found;
        }
        return binding_for_decl(node.selected);
    };
    auto remember = [&](llvm::Function *function, std::vector<llvm::Value *> args, int depth, bool elem_char, int struct_id,
                        bool elem_void, bool elem_bool, int elem_bits, bool elem_unsigned) {
        prepared.function = function;
        prepared.args = std::move(args);
        prepared.depth = depth;
        prepared.elem_char = elem_char;
        prepared.struct_id = struct_id;
        prepared.elem_void = elem_void;
        prepared.elem_bool = elem_bool;
        prepared.elem_bits = elem_bits;
        prepared.elem_unsigned = elem_unsigned;
    };
    // A method call is a function call. `extern(abi="c")` packs the receiver with the other arguments.
    auto extern_method = [&](llvm::Function *function, llvm::Value *receiver, const Binding &method,
                             bool with_enum) -> bool {
        const auto abi = extern_abis_.find(function);
        if (abi == extern_abis_.end() || !abi->second.active) {
            return false;
        }
        std::vector<TypedValue> sources;
        sources.reserve((receiver != nullptr ? 1 : 0) + node.args.size());
        if (receiver != nullptr) {
            TypedValue typed;
            typed.value = receiver;
            sources.push_back(std::move(typed));
        }
        for (const auto &arg : node.args) {
            sources.push_back(emit_rvalue(*arg));
        }
        remember(function, pack_extern_args(function, abi->second, sources, prepared), method.depth, method.elem_char,
                 method.struct_id, method.elem_void, method.elem_bool, method.elem_bits, method.elem_unsigned);
        prepared.decl = method.decl;
        prepared.elem_float = method.elem_float;
        if (with_enum) {
            prepared.enum_id = method.enum_id;
        }
        return true;
    };
    // `extern(abi="c")` may pass a struct as an integer or a pointer. The receiver is still `self` or `self *this`.
    auto pointer_receiver = [&](llvm::Function *function, const Binding &method) {
        const auto abi = extern_abis_.find(function);
        if (abi != extern_abis_.end() && abi->second.active && method.decl != nullptr && !method.decl->params.empty()) {
            return method.decl->params.front().indirections > 0;
        }
        return function->getFunctionType()->getNumParams() > 0 &&
               function->getFunctionType()->getParamType(0)->isPointerTy();
    };
    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data);
        selector != nullptr && selector->base != nullptr) {
        const auto *base_name = std::get_if<Expr::Name>(&selector->base->data);
        const bool package_call = base_name != nullptr && packages_.contains(base_name->text);
        if (!package_call && !node.trait_method.empty()) {
            // `a.malloc[i32]()` on a trait parameter. The active specialization names the struct.
            int struct_id = -1;
            TypedValue addr;
            if (addressable(*selector->base)) {
                addr = emit_addr(*selector->base);
                struct_id = addr.struct_id;
            }
            if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size()) || addr.value == nullptr) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            const std::string key = structs_[static_cast<std::size_t>(struct_id)].name + "." + node.trait_method;
            const Binding *method = method_binding(current_file_, structs_[static_cast<std::size_t>(struct_id)].file_id, key);
            auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
            if (function == nullptr || method->decl == nullptr || method->decl->static_method) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            const bool receiver_pointer = pointer_receiver(function, *method);
            llvm::Value *receiver_arg = nullptr;
            if (receiver_pointer) {
                receiver_arg = addr.depth > 0 ? builder_.CreateLoad(ptr_type(), addr.value) : addr.value;
            } else if (addr.depth == 0 && addr.struct_id >= 0) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, addr.value);
            }
            if (receiver_arg == nullptr) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            if (extern_method(function, receiver_arg, *method, true)) {
                return prepared;
            }
            std::vector<llvm::Value *> args;
            args.reserve(node.args.size() + 1);
            args.push_back(receiver_arg);
            unsigned index = 1;
            for (const auto &arg : node.args) {
                const TypedValue value = emit_rvalue(*arg);
                llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                        ? function->getFunctionType()->getParamType(index)
                                        : nullptr;
                args.push_back(adapt(value, param, node.loc));
                ++index;
            }
            remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                     method->elem_bool, method->elem_bits, method->elem_unsigned);
            prepared.decl = method->decl;
            prepared.elem_float = method->elem_float;
            prepared.enum_id = method->enum_id;
            return prepared;
        }
        if (!package_call) {
            // `Box[i32].from(...)`. The owner is the specialization, and the call passes no receiver.
            if (!selector->resolved_owner.empty()) {
                const Binding *method = selected_binding(static_selector_binding(current_file_, *selector));
                if (method != nullptr && method->decl != nullptr && method->decl->static_method) {
                    auto *function = llvm::dyn_cast<llvm::Function>(method->value);
                    if (function == nullptr) {
                        error(node.loc, "type mismatch");
                        return prepared;
                    }
                    if (extern_method(function, nullptr, *method, true)) {
                        return prepared;
                    }
                    std::vector<llvm::Value *> args;
                    args.reserve(node.args.size());
                    unsigned index = 0;
                    for (const auto &arg : node.args) {
                        const TypedValue value = emit_rvalue(*arg);
                        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                                ? function->getFunctionType()->getParamType(index)
                                                : nullptr;
                        args.push_back(adapt(value, param, node.loc));
                        ++index;
                    }
                    remember(function, std::move(args), method->depth, method->elem_char, method->struct_id,
                             method->elem_void, method->elem_bool, method->elem_bits, method->elem_unsigned);
                    prepared.decl = method->decl;
                    prepared.elem_float = method->elem_float;
                    prepared.enum_id = method->enum_id;
                    return prepared;
                }
            }
            bool value_named = false;
            if (base_name != nullptr) {
                for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
                    const auto found = scope->find(base_name->text);
                    if (found == scope->end()) {
                        continue;
                    }
                    value_named = !found->second.is_function;
                    break;
                }
            }
            std::string static_type;
            if (base_name != nullptr && !value_named && !packages_.contains(base_name->text)) {
                for (const StructLayout &item : structs_) {
                    if (item.name == base_name->text) {
                        static_type = item.name;
                        break;
                    }
                }
                if (static_type.empty()) {
                    for (const EnumLayout &item : enums_) {
                        if (item.name == base_name->text) {
                            static_type = item.name;
                            break;
                        }
                    }
                }
                if (static_type.empty() && find_alias(current_file_, base_name->text) != nullptr) {
                    static_type = base_name->text;
                }
            }
            if (!static_type.empty()) {
                const Binding *method = selected_binding(static_method_binding(current_file_, static_type, selector->field));
                if (method != nullptr && method->is_generic && method->decl != nullptr) {
                    generic_method_call(*method, nullptr, node, prepared);
                    return prepared;
                }
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                if (extern_method(function, nullptr, *method, true)) {
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size());
                unsigned index = 0;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(adapt(value, param, node.loc));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            const int receiver_enum = enum_value_id(*selector->base);
            if (receiver_enum >= 0 && receiver_enum < static_cast<int>(enums_.size())) {
                const std::string key = enums_[static_cast<std::size_t>(receiver_enum)].name + "." + selector->field;
                const Binding *method = selected_binding(method_binding(current_file_, selector->resolved_file, key));
                if (method != nullptr && method->is_generic && method->decl != nullptr && !method->decl->static_method) {
                    const bool receiver_pointer =
                        !method->decl->params.empty() && method->decl->params.front().indirections > 0;
                    TypedValue receiver;
                    if (receiver_pointer) {
                        if (!addressable(*selector->base)) {
                            error(node.loc, "not an lvalue");
                            return prepared;
                        }
                        const TypedValue addr = emit_addr(*selector->base);
                        receiver.value = addr.value;
                        receiver.depth = 0;
                    } else {
                        receiver = emit_rvalue(*selector->base);
                    }
                    if (receiver.value == nullptr) {
                        error(node.loc, "type mismatch");
                        return prepared;
                    }
                    generic_method_call(*method, &receiver, node, prepared);
                    return prepared;
                }
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr || (method->decl != nullptr && method->decl->static_method)) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                const bool receiver_pointer = pointer_receiver(function, *method);
                llvm::Value *receiver_arg = nullptr;
                if (receiver_pointer) {
                    if (!addressable(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        return prepared;
                    }
                    const TypedValue addr = emit_addr(*selector->base);
                    receiver_arg = addr.value;
                } else {
                    const TypedValue value = emit_rvalue(*selector->base);
                    llvm::Type *param = function->getFunctionType()->getNumParams() > 0
                                            ? function->getFunctionType()->getParamType(0)
                                            : nullptr;
                    receiver_arg = as_stored(value.value, value.depth, param, value.elem_unsigned || value.elem_char);
                }
                if (receiver_arg == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                if (extern_method(function, receiver_arg, *method, true)) {
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size() + 1);
                args.push_back(receiver_arg);
                unsigned index = 1;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(adapt(value, param, node.loc));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            if (!selector->resolved_owner.empty()) {
                const std::string key = selector->resolved_owner + "." + selector->field;
                const Binding *method = selected_binding(method_binding(current_file_, selector->resolved_file, key));
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr || (method->decl != nullptr && method->decl->static_method)) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                const bool receiver_pointer = pointer_receiver(function, *method);
                llvm::Value *receiver_arg = nullptr;
                if (receiver_pointer) {
                    if (!addressable(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        return prepared;
                    }
                    // `var n Integer` is depth 0: the alloca is the integer.
                    // `var p *Integer` is depth 1: the alloca holds the pointer, so load it.
                    const TypedValue addr = emit_addr(*selector->base);
                    if (addr.value == nullptr) {
                        return prepared;
                    }
                    receiver_arg = addr.depth > 0 ? builder_.CreateLoad(ptr_type(), addr.value) : addr.value;
                } else {
                    llvm::Type *param = function->getFunctionType()->getNumParams() > 0
                                            ? function->getFunctionType()->getParamType(0)
                                            : nullptr;
                    if (addressable(*selector->base)) {
                        const TypedValue addr = emit_addr(*selector->base);
                        if (addr.value == nullptr) {
                            return prepared;
                        }
                        llvm::Value *slot = addr.value;
                        if (addr.depth > 0) {
                            slot = builder_.CreateLoad(ptr_type(), addr.value);
                        }
                        receiver_arg = builder_.CreateLoad(param, slot);
                    } else {
                        const TypedValue value = emit_rvalue(*selector->base);
                        receiver_arg = as_stored(value.value, value.depth, param, value.elem_unsigned || value.elem_char);
                    }
                }
                if (receiver_arg == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                if (extern_method(function, receiver_arg, *method, true)) {
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size() + 1);
                args.push_back(receiver_arg);
                unsigned index = 1;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(adapt(value, param, node.loc));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            int struct_id = -1;
            llvm::Value *struct_ptr = nullptr;
            llvm::Value *struct_value = nullptr;
            llvm::Value *pointer_value = nullptr;
            if (addressable(*selector->base)) {
                const TypedValue addr = emit_addr(*selector->base);
                if (addr.value == nullptr) {
                    return prepared;
                }
                if (addr.fn_ty != nullptr && addr.struct_id < 0) {
                    call_trait_fn(builder_.CreateLoad(ptr_type(), addr.value), addr, node, prepared);
                    return prepared;
                }
                struct_id = addr.struct_id;
                if (addr.depth == 0 && addr.struct_id >= 0) {
                    struct_ptr = addr.value;
                } else if (addr.depth == 1 && addr.struct_id >= 0) {
                    pointer_value = builder_.CreateLoad(ptr_type(), addr.value);
                }
            } else {
                const TypedValue base = emit_rvalue(*selector->base);
                if (base.value == nullptr) {
                    return prepared;
                }
                if (base.fn_ty != nullptr && base.struct_id < 0) {
                    call_trait_fn(base.value, base, node, prepared);
                    return prepared;
                }
                struct_id = base.struct_id;
                if (base.depth == 0 && base.struct_id >= 0) {
                    struct_value = base.value;
                } else if (base.depth == 1 && base.struct_id >= 0) {
                    pointer_value = base.value;
                }
            }
            const Binding *method = nullptr;
            if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size())) {
                const std::string key = structs_[static_cast<std::size_t>(struct_id)].name + "." + selector->field;
                method = selected_binding(method_binding(current_file_, selector->resolved_file, key));
                // Specialization knows the struct. The trait's list picks the overload. No table.
                if (node.selected == nullptr) {
                    if (const Binding *exact =
                            trait_spec_method(current_file_, selector->resolved_file, key, *selector, node)) {
                        method = exact;
                    }
                }
            }
            if (method != nullptr && method->is_generic && method->decl != nullptr && !method->decl->static_method) {
                const bool receiver_pointer =
                    !method->decl->params.empty() && method->decl->params.front().indirections > 0;
                TypedValue receiver;
                if (receiver_pointer) {
                    receiver.value = pointer_value != nullptr ? pointer_value : struct_ptr;
                    receiver.depth = 0;
                } else if (struct_value != nullptr) {
                    receiver.value = struct_value;
                    receiver.depth = 0;
                    receiver.struct_id = struct_id;
                } else if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size())) {
                    llvm::Value *ptr = pointer_value != nullptr ? pointer_value : struct_ptr;
                    if (ptr != nullptr) {
                        receiver.value = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, ptr);
                        receiver.depth = 0;
                        receiver.struct_id = struct_id;
                    }
                }
                if (receiver.value == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                generic_method_call(*method, &receiver, node, prepared);
                return prepared;
            }
            auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
            if (function == nullptr || (method->decl != nullptr && method->decl->static_method)) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            const bool receiver_pointer = pointer_receiver(function, *method);
            llvm::Value *receiver_arg = nullptr;
            if (receiver_pointer) {
                if (pointer_value != nullptr) {
                    receiver_arg = pointer_value;
                } else if (struct_ptr != nullptr) {
                    receiver_arg = struct_ptr;
                }
            } else if (struct_value != nullptr) {
                receiver_arg = struct_value;
            } else if (pointer_value != nullptr) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, pointer_value);
            } else if (struct_ptr != nullptr) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, struct_ptr);
            }
            if (receiver_arg == nullptr) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            if (extern_method(function, receiver_arg, *method, false)) {
                return prepared;
            }
            std::vector<llvm::Value *> args;
            args.reserve(node.args.size() + 1);
            args.push_back(receiver_arg);
            unsigned index = 1;
            for (const auto &arg : node.args) {
                const TypedValue value = emit_rvalue(*arg);
                llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                        ? function->getFunctionType()->getParamType(index)
                                        : nullptr;
                args.push_back(adapt(value, param, node.loc));
                ++index;
            }
            remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                     method->elem_bool, method->elem_bits, method->elem_unsigned);
            prepared.decl = method->decl;
            prepared.elem_float = method->elem_float;
            return prepared;
        }
    }
    TypedValue callee;
    if (node.selected != nullptr) {
        const Binding *chosen = binding_for_decl(node.selected);
        if (chosen == nullptr || chosen->value == nullptr) {
            error(node.loc, "type mismatch");
            return prepared;
        }
        prepared.decl = chosen->decl != nullptr ? chosen->decl : node.selected;
        callee = value_of_binding(*chosen);
    } else {
        prepared.decl = node.callee == nullptr ? nullptr : callee_decl(*node.callee);
        callee = emit_rvalue(*node.callee);
    }
    prepared.enum_id = callee.enum_id;
    if (callee.fn_ty != nullptr && callee.value != nullptr && !llvm::isa<llvm::Function>(callee.value)) {
        std::vector<llvm::Value *> args;
        args.reserve(node.args.size());
        unsigned index = 0;
        for (const auto &arg : node.args) {
            const TypedValue value = emit_rvalue(*arg);
            llvm::Type *param = index < callee.fn_ty->getNumParams() ? callee.fn_ty->getParamType(index) : nullptr;
            args.push_back(adapt(value, param, node.loc));
            ++index;
        }
        prepared.indirect = callee.value;
        prepared.fn_ty = callee.fn_ty;
        prepared.args = std::move(args);
        prepared.depth = callee.fn_ret_depth;
        prepared.elem_float = callee.fn_ret_float;
        prepared.elem_bits = callee.fn_ret_bits;
        prepared.elem_unsigned = callee.fn_ret_unsigned;
        prepared.elem_char = callee.fn_ret_char;
        prepared.elem_bool = callee.fn_ret_bool;
        prepared.struct_id = callee.fn_ret_struct;
        prepared.fn_results = callee.fn_results;
        return prepared;
    }
    auto *function = llvm::dyn_cast_or_null<llvm::Function>(callee.value);
    if (function == nullptr) {
        error(node.loc, "type mismatch");
        return prepared;
    }
    const auto abi = extern_abis_.find(function);
    if (abi != extern_abis_.end() && abi->second.active) {
        std::vector<TypedValue> sources;
        sources.reserve(node.args.size());
        for (const auto &arg : node.args) {
            sources.push_back(emit_rvalue(*arg));
        }
        remember(function, pack_extern_args(function, abi->second, sources, prepared), callee.depth, callee.elem_char,
                 callee.struct_id, callee.elem_void, callee.elem_bool, callee.elem_bits, callee.elem_unsigned);
        prepared.elem_float = callee.elem_float;
        return prepared;
    }
    std::vector<llvm::Value *> args;
    args.reserve(node.args.size());
    unsigned index = 0;
    for (const auto &arg : node.args) {
        const TypedValue value = emit_rvalue(*arg);
        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                ? function->getFunctionType()->getParamType(index)
                                : nullptr;
        llvm::Value *stored = adapt(value, param, node.loc);
        if (param == nullptr && function->isVarArg()) {
            // A slice in a C variadic slot passes the address, not the header.
            if (value.is_slice && stored != nullptr && stored->getType() == slice_type()) {
                stored = builder_.CreateExtractValue(stored, 0);
            }
            TypedValue widened = value;
            widened.value = stored;
            stored = variadic_arg(widened);
        }
        args.push_back(stored);
        ++index;
    }
    append_captures(prepared.decl, args, node.loc);
    if (prepared.decl != nullptr && !prepared.decl->captures.empty() && !function->isVarArg() &&
        args.size() != function->arg_size()) {
        error(node.loc, "not supported yet");
        return prepared;
    }
    remember(function, std::move(args), callee.depth, callee.elem_char, callee.struct_id, callee.elem_void, callee.elem_bool,
             callee.elem_bits, callee.elem_unsigned);
    prepared.elem_float = callee.elem_float;
    return prepared;
}

TypedValue CodeGen::emit_prepared(const PreparedCall &call) {
    if (terminated() || (call.function == nullptr && (call.indirect == nullptr || call.fn_ty == nullptr))) {
        return {zero(), 0};
    }
    llvm::Value *pushed = call.trace ? push_trace(call.loc) : nullptr;
    llvm::CallInst *inst = call.indirect != nullptr ? builder_.CreateCall(call.fn_ty, call.indirect, call.args)
                                                    : builder_.CreateCall(call.function, call.args);
    if (pushed != nullptr) {
        pop_trace(pushed);
    }
    inst->setCallingConv(llvm::CallingConv::C);
    if (!call.abi_attrs.empty()) {
        annotate_call(inst, call.abi_attrs);
    }
    if (call.abi_return && call.abi_slot != nullptr && call.abi_struct != nullptr) {
        if (!call.abi_sret) {
            unpack_return(inst, call.abi_slot, call.abi_ret);
        }
        return load_object(call.abi_slot, 0, false, call.abi_struct_id, false, false, 0, false, false);
    }
    if (inst->getType() == slice_type()) {
        TypedValue result{inst, call.depth, call.elem_char, call.struct_id, false, call.elem_void, call.elem_bool,
                          call.elem_bits, call.elem_unsigned};
        result.is_slice = true;
        result.array_len = -1;
        result.elem_float = call.elem_float;
        result.enum_id = call.enum_id;
        if (call.decl != nullptr && call.decl->results.size() == 1) {
            result.elem_slice = call.decl->results.front().elem_slice;
        } else if (call.fn_results.size() == 1) {
            result.elem_slice = call.fn_results.front().elem_slice;
        }
        return result;
    }
    if (call.elem_float) {
        return {inst, 0, false, -1, false, false, false, call.elem_bits >= 64 ? 64 : 32, false, -1, true};
    }
    if (inst->getType()->isVoidTy()) {
        return {zero(), 0};
    }
    auto tagged = [&](TypedValue value) {
        value.enum_id = call.enum_id;
        return value;
    };
    if (inst->getType()->isArrayTy() && function_ != nullptr) {
        auto *array_ty = llvm::cast<llvm::ArrayType>(inst->getType());
        llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
        stamp_builder(allocas);
        llvm::AllocaInst *slot = allocas.CreateAlloca(array_ty, nullptr, "arr.ret");
        builder_.CreateStore(inst, slot);
        llvm::Value *first = builder_.CreateInBoundsGEP(array_ty, slot, {const_i32(0), const_i32(0)});
        TypedValue result = tagged({first, call.depth + 1, call.elem_char, call.struct_id, false, call.elem_void,
                                    call.elem_bool, call.elem_bits, call.elem_unsigned});
        // The address is the first row. Shells after the outer array are that row, as on a local.
        if (call.decl != nullptr && call.decl->results.size() == 1) {
            const std::vector<TypeShell> &shells = call.decl->results.front().shells;
            if (!shells.empty() && shells.front().kind == TypeShell::Kind::Array) {
                result.shells.assign(shells.begin() + 1, shells.end());
            }
        }
        result.array_type = array_ty;
        return result;
    }
    if (call.depth == 0 && call.elem_bool) {
        return tagged({builder_.CreateZExt(inst, i32()), 0});
    }
    if (call.depth == 0 && (call.elem_char || call.elem_bits == 8 || call.elem_bits == 16)) {
        const bool zero_extend = call.elem_unsigned || call.elem_char;
        llvm::Value *wide = zero_extend ? builder_.CreateZExt(inst, i32()) : builder_.CreateSExt(inst, i32());
        return tagged({wide, 0});
    }
    TypedValue result = tagged({inst, call.depth, call.elem_char, call.struct_id, false, call.elem_void, call.elem_bool,
                                call.elem_bits, call.elem_unsigned});
    // `give()` that returns `*[N]T` is still that pointer. The length stays so a slice use can pack it.
    auto stamp_array_ptr = [&](const std::string &name, const std::string &package, bool is_struct, int indirections,
                               bool is_array, int array_len, bool elem_slice, const std::vector<TypeShell> &shells) {
        if (result.ptr_to_array || shells.empty()) {
            return;
        }
        const Lowered low = lower(current_file_, name, package, is_struct, indirections, is_array, array_len, false,
                                  elem_slice, shells);
        if (!low.ptr_to_array || low.ptr_array_len <= 0) {
            return;
        }
        result.ptr_to_array = true;
        result.ptr_array_len = low.ptr_array_len;
        result.shells = low.shells;
        result.inner_array_len = low.inner_array_len;
        result.elem_slice = low.elem_slice;
    };
    if (call.decl != nullptr && call.decl->results.size() == 1) {
        const ResultSlot &slot = call.decl->results.front();
        stamp_array_ptr(slot.type_name, slot.type_package, slot.is_struct, slot.indirections, slot.is_array,
                        slot.array_len, slot.elem_slice, slot.shells);
    } else if (call.fn_results.size() == 1) {
        const FnParam &slot = call.fn_results.front();
        stamp_array_ptr(slot.type_name, slot.type_package, slot.is_struct, slot.indirections, slot.is_array,
                        slot.array_len, slot.elem_slice, slot.shells);
    }
    if (inst->getType()->isStructTy() &&
        ((call.decl != nullptr && call.decl->results.size() > 1) || call.fn_results.size() > 1)) {
        result.is_tuple = true;
        if (call.decl != nullptr && call.decl->results.size() > 1) {
            result.tuple_parts = call.decl->results;
        } else {
            result.tuple_parts.reserve(call.fn_results.size());
            for (const FnParam &part : call.fn_results) {
                ResultSlot slot;
                slot.type_name = part.type_name;
                slot.type_package = part.type_package;
                slot.is_struct = part.is_struct;
                slot.is_enum = part.is_enum;
                slot.indirections = part.indirections;
                slot.optional = part.optional;
                slot.const_pointee = part.const_pointee;
                slot.is_array = part.is_array && part.array_len != 0;
                slot.array_len = slot.is_array ? part.array_len : 0;
                slot.elem_slice = part.elem_slice;
                result.tuple_parts.push_back(std::move(slot));
            }
        }
    }
    attach_returned_fn(result, call.decl);
    return result;
}

void CodeGen::attach_returned_fn(TypedValue &value, const FunctionDecl *decl) const {
    if (decl == nullptr || decl->results.size() != 1 || !decl->results.front().is_fn_ptr) {
        return;
    }
    const ResultSlot &slot = decl->results.front();
    const Lowered ret = lower_fnptr(current_file_, slot.type_name, slot.type_package, slot.is_struct, slot.is_enum,
                                     slot.ret_indirections, slot.indirections, slot.fn_params, slot.fn_variadic,
                                     slot.fn_results);
    value.depth = ret.depth;
    value.elem_char = false;
    value.elem_void = false;
    value.elem_bool = false;
    value.elem_bits = 0;
    value.elem_unsigned = false;
    value.elem_float = false;
    value.struct_id = -1;
    value.fn_ty = ret.fn_ty;
    value.fn_ret_depth = ret.fn_ret_depth;
    value.fn_ret_float = ret.fn_ret_float;
    value.fn_ret_bits = ret.fn_ret_bits;
    value.fn_ret_unsigned = ret.fn_ret_unsigned;
    value.fn_ret_char = ret.fn_ret_char;
    value.fn_ret_bool = ret.fn_ret_bool;
    value.fn_ret_struct = ret.fn_ret_struct;
    value.fn_results = ret.fn_results;
}

llvm::AllocaInst *CodeGen::defer_flag() {
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    stamp_builder(allocas);
    llvm::AllocaInst *flag = allocas.CreateAlloca(i1(), nullptr, "defer.on");
    allocas.CreateStore(llvm::ConstantInt::getFalse(context_), flag);
    return flag;
}

void CodeGen::emit_one_defer(const DeferredCall &call) {
    if (terminated() || call.function == nullptr || call.flag == nullptr) {
        return;
    }
    llvm::BasicBlock *call_block = llvm::BasicBlock::Create(context_, "defer.call", function_);
    llvm::BasicBlock *skip_block = llvm::BasicBlock::Create(context_, "defer.skip", function_);
    llvm::Value *flag = builder_.CreateLoad(i1(), call.flag);
    builder_.CreateCondBr(flag, call_block, skip_block);
    builder_.SetInsertPoint(call_block);
    std::vector<llvm::Value *> args;
    args.reserve(call.args.size());
    for (llvm::AllocaInst *slot : call.args) {
        llvm::Type *type = slot->getAllocatedType();
        args.push_back(builder_.CreateLoad(type, slot));
    }
    llvm::CallInst *inst = builder_.CreateCall(call.function, args);
    inst->setCallingConv(call.function->getCallingConv());
    builder_.CreateStore(llvm::ConstantInt::getFalse(context_), call.flag);
    builder_.CreateBr(skip_block);
    builder_.SetInsertPoint(skip_block);
}

void CodeGen::emit_defer_frame(DeferFrame &frame) {
    for (int index = static_cast<int>(frame.calls.size()) - 1; index >= 0; --index) {
        emit_one_defer(frame.calls[static_cast<std::size_t>(index)]);
        if (terminated()) {
            return;
        }
    }
}

void CodeGen::emit_defers_from(int min_loop_index) {
    for (int frame = static_cast<int>(defers_.size()) - 1; frame >= 0; --frame) {
        if (defers_[static_cast<std::size_t>(frame)].loop_index < min_loop_index) {
            break;
        }
        emit_defer_frame(defers_[static_cast<std::size_t>(frame)]);
        if (terminated()) {
            return;
        }
    }
}

llvm::Function *CodeGen::specialize(const FunctionDecl &fn, const Binding &meta,
                                      const std::vector<TypedValue> &arg_values, SourceLoc loc,
                                      std::vector<llvm::Value *> &out_args) {
    if (arg_values.size() != fn.params.size()) {
        error(loc, "argument count");
        return nullptr;
    }
    // Traits and the body belong to the function's package. The call is emitted in the caller.
    const uint32_t saved_file = current_file_;
    if (fn.loc.valid()) {
        current_file_ = fn.loc.file;
    }
    struct RestoreFile {
        CodeGen *self;
        uint32_t file;
        ~RestoreFile() { self->current_file_ = file; }
    } restore{this, saved_file};
    std::vector<int> spec(fn.params.size(), -1);
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (!param_is_trait(current_file_, fn.params[i])) {
            continue;
        }
        const TypedValue &arg = arg_values[i];
        if (trait_fn_argument(arg)) {
            if (trait_fn_lowered(fn.params[i].type_name).fn_ty == nullptr) {
                error(loc, "type mismatch");
                return nullptr;
            }
            spec[i] = kFnSpec;
            continue;
        }
        const int indirections = fn.params[i].indirections;
        const bool value_ok = indirections == 0 && arg.depth == 0 && arg.struct_id >= 0;
        const bool pointer_ok = indirections == 1 && arg.depth == 1 && arg.struct_id >= 0;
        if (!value_ok && !pointer_ok) {
            error(loc, "type mismatch");
            return nullptr;
        }
        spec[i] = arg.struct_id;
    }
    if (fn.body == nullptr) {
        error(loc, "not supported yet");
        return nullptr;
    }
    const PackageNode *owner = find_node(current_file_);
    if (owner == nullptr) {
        error(loc, "type mismatch");
        return nullptr;
    }
    std::string symbol = owner->qualifier + ".";
    if (!fn.method_struct.empty()) {
        symbol += fn.method_struct;
        symbol += '.';
    }
    symbol += fn.name;
    symbol += ".spec";
    for (int sid : spec) {
        if (sid == -1) {
            continue;
        }
        symbol += '.';
        if (sid == kFnSpec) {
            symbol += "fn";
            continue;
        }
        if (sid >= 0 && sid < static_cast<int>(structs_.size())) {
            const StructLayout &layout = structs_[static_cast<std::size_t>(sid)];
            // Same-package structs keep `main.use.spec.Rect`. A foreign struct adds its package.
            if (layout.file_id != 0) {
                if (const PackageNode *home = find_node(layout.file_id);
                    home != nullptr && !home->qualifier.empty() && home->qualifier != owner->qualifier) {
                    symbol += home->qualifier;
                    symbol += '.';
                }
            }
            symbol += layout.name;
        }
    }
    llvm::Function *function = nullptr;
    if (const auto found = specs_.find(symbol); found != specs_.end()) {
        function = found->second;
    } else {
        function = llvm::Function::Create(spec_type(fn, spec), llvm::GlobalValue::InternalLinkage, symbol, module_);
        function->setCallingConv(llvm::CallingConv::C);
        unsigned index = 0;
        for (auto &arg : function->args()) {
            if (index < fn.params.size() && !fn.params[index].name.empty()) {
                arg.setName(fn.params[index].name);
            }
            ++index;
        }
        specs_.emplace(symbol, function);
    }
    if (fn.body != nullptr && function->empty() && !emitting_specs_.contains(symbol)) {
        emitting_specs_.insert(symbol);
        auto saved_scopes = std::move(scopes_);
        auto saved_packages = std::move(packages_);
        llvm::Function *saved_function = function_;
        llvm::Type *saved_return = return_type_;
        const int saved_depth = return_depth_;
        auto saved_spec = active_spec_;
        auto saved_loops = std::move(loops_);
        auto saved_defers = std::move(defers_);
        auto saved_cases = std::move(case_blocks_);
        const auto saved_ip = builder_.saveIP();
        scopes_.clear();
        packages_.clear();
        loops_.clear();
        defers_.clear();
        case_blocks_.clear();
        active_spec_ = spec;
        push();
        bind_package_globals();
        if (const PackageNode *node_owner = find_node(current_file_)) {
            bind_imports(*node_owner);
        }
        emit_function_body(fn, function, meta.depth);
        pop();
        scopes_ = std::move(saved_scopes);
        packages_ = std::move(saved_packages);
        function_ = saved_function;
        return_type_ = saved_return;
        return_depth_ = saved_depth;
        active_spec_ = std::move(saved_spec);
        loops_ = std::move(saved_loops);
        defers_ = std::move(saved_defers);
        case_blocks_ = std::move(saved_cases);
        builder_.restoreIP(saved_ip);
        emitting_specs_.erase(symbol);
    }
    out_args.clear();
    out_args.reserve(arg_values.size());
    unsigned index = 0;
    for (const TypedValue &value : arg_values) {
        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                ? function->getFunctionType()->getParamType(index)
                                : nullptr;
        out_args.push_back(adapt(value, param, loc));
        ++index;
    }
    return function;
}

void CodeGen::generic_method_call(const Binding &method, const TypedValue *receiver, const Expr::Call &node,
                                  PreparedCall &prepared) {
    if (method.decl == nullptr) {
        error(node.loc, "type mismatch");
        return;
    }
    std::vector<TypedValue> values;
    values.reserve((receiver != nullptr ? 1 : 0) + node.args.size());
    if (receiver != nullptr) {
        values.push_back(*receiver);
    }
    for (const auto &arg : node.args) {
        values.push_back(arg == nullptr ? TypedValue{} : emit_rvalue(*arg));
    }
    std::vector<llvm::Value *> stored;
    llvm::Function *function = specialize(*method.decl, method, values, node.loc, stored);
    if (function == nullptr) {
        return;
    }
    prepared.function = function;
    prepared.args = std::move(stored);
    prepared.depth = method.depth;
    prepared.elem_char = method.elem_char;
    prepared.struct_id = method.struct_id;
    prepared.elem_void = method.elem_void;
    prepared.elem_bool = method.elem_bool;
    prepared.elem_bits = method.elem_bits;
    prepared.elem_unsigned = method.elem_unsigned;
    prepared.decl = method.decl;
    prepared.elem_float = method.elem_float;
    prepared.enum_id = method.enum_id;
}

void CodeGen::call_trait_fn(llvm::Value *fnptr, const TypedValue &meta, const Expr::Call &node, PreparedCall &prepared) {
    if (fnptr == nullptr || meta.fn_ty == nullptr) {
        error(node.loc, "type mismatch");
        return;
    }
    std::vector<llvm::Value *> args;
    args.reserve(node.args.size());
    unsigned index = 0;
    for (const auto &arg : node.args) {
        if (arg == nullptr) {
            ++index;
            continue;
        }
        const TypedValue value = emit_rvalue(*arg);
        llvm::Type *param = index < meta.fn_ty->getNumParams() ? meta.fn_ty->getParamType(index) : nullptr;
        args.push_back(adapt(value, param, node.loc));
        ++index;
    }
    prepared.indirect = fnptr;
    prepared.fn_ty = meta.fn_ty;
    prepared.args = std::move(args);
    prepared.depth = meta.fn_ret_depth;
    prepared.elem_char = meta.fn_ret_char;
    prepared.elem_bool = meta.fn_ret_bool;
    prepared.elem_bits = meta.fn_ret_bits;
    prepared.elem_unsigned = meta.fn_ret_unsigned;
    prepared.elem_float = meta.fn_ret_float;
    prepared.struct_id = meta.fn_ret_struct;
}

TypedValue CodeGen::emit_generic_call(const Binding &binding, const Expr::Call &node) {
    const Binding meta = binding;
    const FunctionDecl *fn = meta.decl;
    std::vector<TypedValue> arg_values;
    arg_values.reserve(node.args.size());
    for (const auto &arg : node.args) {
        arg_values.push_back(arg == nullptr ? TypedValue{} : emit_rvalue(*arg));
    }
    if (fn == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    std::vector<llvm::Value *> args;
    llvm::Function *function = specialize(*fn, meta, arg_values, node.loc, args);
    if (function == nullptr) {
        return {zero(), 0};
    }
    llvm::CallInst *call = builder_.CreateCall(function, args);
    call->setCallingConv(function->getCallingConv());
    if (call->getType()->isVoidTy()) {
        return {zero(), 0};
    }
    if (meta.depth == 0 && meta.elem_bool) {
        return {builder_.CreateZExt(call, i32()), 0};
    }
    if (meta.depth == 0 && (meta.elem_char || meta.elem_bits == 8 || meta.elem_bits == 16)) {
        const bool zero_extend = meta.elem_unsigned || meta.elem_char;
        llvm::Value *wide = zero_extend ? builder_.CreateZExt(call, i32()) : builder_.CreateSExt(call, i32());
        return {wide, 0};
    }
    TypedValue result{call, meta.depth, meta.elem_char, meta.struct_id, false, meta.elem_void, meta.elem_bool,
                      meta.elem_bits, meta.elem_unsigned, -1, meta.elem_float};
    result.enum_id = meta.enum_id;
    attach_returned_fn(result, fn);
    return result;
}

} // namespace tlc
