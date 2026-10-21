// Function bodies, nested functions, and captured names.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

namespace {

// Stable name for one captured function pointer. The id is the function that owns the name.
std::string capture_key(int function_id, const std::string &name) {
    return ".cap." + std::to_string(function_id) + "." + name;
}


} // namespace

void CodeGen::finish_function() {
    const bool returns_void = return_type_ != nullptr && return_type_->isVoidTy();
    for (llvm::BasicBlock &block : *function_) {
        if (block.hasTerminator()) {
            continue;
        }
        llvm::IRBuilder<> fix(&block);
        stamp_builder(fix);
        if (extern_sret_ && extern_sret_ptr_ != nullptr && extern_ret_struct_ != nullptr) {
            fix.CreateStore(llvm::ConstantAggregateZero::get(extern_ret_struct_), extern_sret_ptr_);
            fix.CreateRetVoid();
        } else if (returns_void) {
            fix.CreateRetVoid();
        } else if (return_type_ == slice_type()) {
            fix.CreateRet(llvm::ConstantAggregateZero::get(slice_type()));
        } else if (return_depth_ > 0) {
            fix.CreateRet(null_ptr());
        } else if (return_type_ != nullptr && return_type_->isIntegerTy(32)) {
            fix.CreateRet(const_i32(0));
        } else if (return_type_ != nullptr) {
            fix.CreateRet(llvm::Constant::getNullValue(return_type_));
        } else {
            fix.CreateRet(const_i32(0));
        }
    }
}

bool CodeGen::param_is_trait(uint32_t file, const Param &param) const {
    // `Allocator` is supplied by the compiler. It is not a trait declaration in the unit.
    const bool allocator = param.type_name == "Allocator" && param.type_package.empty();
    if (param.is_enum || param.is_array || !param.type_package.empty() || param.indirections > 1) {
        return false;
    }
    if (allocator) {
        return true;
    }
    if (param.is_struct) {
        return false;
    }
    if (has_typedef(file, param.type_name) || tag_id(file, param.type_name) >= 0 ||
        selective_tag(file, param.type_name) >= 0) {
        return false;
    }
    return trait_named(file, param.type_name) >= 0;
}

bool CodeGen::function_is_generic(uint32_t file, const FunctionDecl &fn) const {
    if (!fn.type_params.empty()) {
        return true;
    }
    // A trait default is copied onto each struct. The trait receiver is not emitted.
    if (!fn.method_struct.empty() && trait_named(file, fn.method_struct) >= 0) {
        return true;
    }
    for (const Param &param : fn.params) {
        if (param_is_trait(file, param)) {
            return true;
        }
    }
    return false;
}


CodeGen::Lowered CodeGen::trait_fn_lowered(const std::string &trait_name) const {
    const int id = trait_named(current_file_, trait_name);
    if (id < 0 || id >= static_cast<int>(trait_decls_.size()) || trait_decls_[static_cast<std::size_t>(id)] == nullptr) {
        return {};
    }
    const TraitDecl *trait = trait_decls_[static_cast<std::size_t>(id)];
    const FunctionDecl *only = nullptr;
    int methods = 0;
    int fields = 0;
    for (int guard = 0; trait != nullptr && guard < static_cast<int>(trait_decls_.size()); ++guard) {
        fields += static_cast<int>(trait->fields.size());
        for (const FunctionDecl &method : trait->methods) {
            ++methods;
            only = &method;
        }
        trait = trait_parent(current_file_, trait);
    }
    if (methods != 1 || fields != 0 || only == nullptr) {
        return {};
    }
    const FunctionDecl &method = *only;
    Lowered out;
    out.depth = 1;
    out.type = ptr_type();
    out.fn_ty = function_type(method, current_file_);
    const bool many = method.results.size() > 1;
    const bool returns_void = !many && method.return_type == "void" && method.indirections == 0 && !method.is_struct &&
                              method.type_package.empty();
    if (!returns_void && !many) {
        const bool result_array = method.results.size() == 1 && method.results.front().is_array &&
                                  method.results.front().array_len > 0 && !method.results.front().is_fn_ptr;
        const Lowered ret =
            method.results.size() == 1 && method.results.front().is_fn_ptr
                ? lower_fnptr(current_file_, method.results.front().type_name, method.results.front().type_package,
                              method.results.front().is_struct, method.results.front().is_enum,
                              method.results.front().ret_indirections, method.results.front().indirections,
                              method.results.front().fn_params, method.results.front().fn_variadic,
                              method.results.front().fn_results)
                : lower(current_file_, method.return_type, method.type_package, method.is_struct, method.indirections,
                        result_array, result_array ? method.results.front().array_len : 0, false);
        out.fn_ret_depth = ret.depth;
        out.fn_ret_float = ret.elem_float;
        out.fn_ret_bits = ret.elem_bits;
        out.fn_ret_unsigned = ret.elem_unsigned;
        out.fn_ret_char = ret.elem_char;
        out.fn_ret_bool = ret.elem_bool;
        out.fn_ret_struct = ret.struct_id;
    }
    return out;
}

CodeGen::Lowered CodeGen::spec_param(const FunctionDecl &fn, std::size_t index, const std::vector<int> &spec) const {
    const Param &param = fn.params[index];
    if (index < spec.size() && spec[index] == kFnSpec) {
        // `Shape` and `*Shape` are both this one function pointer. The call is `s.method(...)`.
        return trait_fn_lowered(param.type_name);
    }
    if (index < spec.size() && spec[index] >= 0 && spec[index] < static_cast<int>(structs_.size())) {
        // The id is the struct that was passed. Its name may also exist in the function's package.
        const StructLayout &layout = structs_[static_cast<std::size_t>(spec[index])];
        const uint32_t home = layout.file_id != 0 ? layout.file_id : current_file_;
        return lower(home, layout.name, "", true, param.indirections, false, 0, true);
    }
    return lower_param_type(current_file_, param);
}

CodeGen::Lowered CodeGen::lower_param(const FunctionDecl &fn, std::size_t index) const {
    return spec_param(fn, index, active_spec_);
}

llvm::FunctionType *CodeGen::spec_type(const FunctionDecl &fn, const std::vector<int> &spec) const {
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const bool result_slice = !many && fn.results.size() == 1 && fn.results.front().is_array &&
                             fn.results.front().array_len < 0 && !fn.results.front().is_fn_ptr;
    llvm::Type *ret = many ? aggregate_return(fn, current_file_)
                           : (returns_void ? llvm::Type::getVoidTy(context_)
                                           : stored_type(lower(current_file_, fn.return_type, fn.type_package, fn.is_struct,
                                                               fn.indirections, result_slice,
                                                               result_slice ? fn.results.front().array_len : 0, false,
                                                               result_slice && fn.results.front().elem_slice)));
    std::vector<llvm::Type *> params;
    params.reserve(fn.params.size());
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
        params.push_back(stored_type(spec_param(fn, index, spec)));
    }
    return llvm::FunctionType::get(ret, params, false);
}

void CodeGen::emit_function_body(const FunctionDecl &fn, llvm::Function *function, int return_depth,
                                 const std::vector<Binding> *captures) {
    if (function == nullptr || !fn.body || !function->empty()) {
        return;
    }
    const FunctionDecl *saved_emitting = emitting_fn_;
    emitting_fn_ = &fn;
    std::vector<llvm::DIScope *> saved_scopes = std::move(di_scopes_);
    di_scopes_.clear();
    llvm::DISubprogram *saved_subprogram = di_subprogram_;
    const llvm::DebugLoc saved_loc = builder_.getCurrentDebugLocation();
    builder_.SetCurrentDebugLocation(llvm::DebugLoc());
    const std::string saved_trace = trace_name_;
    trace_name_ = fn.name.empty() ? std::string("anon") : fn.name;
    const bool saved_many = return_many_;
    const bool saved_share = share_function_scope_;
    const bool saved_extern = extern_struct_ret_;
    const bool saved_sret = extern_sret_;
    llvm::Value *saved_sret_ptr = extern_sret_ptr_;
    const CAbi saved_abi = extern_ret_abi_;
    llvm::Type *saved_struct = extern_ret_struct_;
    function_ = function;
    return_type_ = function->getReturnType();
    return_depth_ = return_depth;
    return_many_ = fn.results.size() > 1;
    share_function_scope_ = true;
    extern_struct_ret_ = false;
    extern_sret_ = false;
    extern_sret_ptr_ = nullptr;
    extern_ret_abi_ = {};
    extern_ret_struct_ = nullptr;
    const auto abi_found = extern_abis_.find(function);
    const bool abi_body = abi_found != extern_abis_.end() && abi_found->second.active;
    const ExternLayout abi_layout = abi_body ? abi_found->second : ExternLayout{};
    if (abi_body && abi_layout.ret_struct != nullptr) {
        extern_struct_ret_ = true;
        extern_sret_ = abi_layout.sret;
        extern_ret_abi_ = abi_layout.ret;
        extern_ret_struct_ = abi_layout.ret_struct;
    }
    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", function);
    builder_.SetInsertPoint(entry);
    debug_begin_function(fn, function);
    push();
    if (abi_body) {
        const ExternLayout &layout = abi_layout;
        unsigned abi = 0;
        if (layout.sret && abi < function->arg_size()) {
            extern_sret_ptr_ = function->getArg(abi++);
        }
        for (std::size_t index = 0; index < fn.params.size(); ++index) {
            const std::string name = fn.params[index].name;
            const ExternLayout::Arg &info = layout.args[index];
            const Lowered low = lower_param(fn, index);
            if (info.is_struct && info.abi.memory) {
                llvm::Argument *arg = function->getArg(abi++);
                if (!name.empty()) {
                    Binding binding;
                    binding.value = arg;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                    alias_capture(name, binding);
                }
            } else if (info.is_struct) {
                const unsigned align = std::max(info.abi.natural_align, 8u);
                llvm::AllocaInst *slot = aligned_slot(info.struct_type, align, name.empty() ? "arg" : name);
                for (std::size_t part = 0; part < info.abi.parts.size() && abi < function->arg_size(); ++part) {
                    const uint64_t offset = part < info.abi.offsets.size() ? info.abi.offsets[part] : 0;
                    store_at(slot, function->getArg(abi++), offset);
                }
                if (!name.empty()) {
                    Binding binding;
                    binding.value = slot;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                    alias_capture(name, binding);
                    debug_declare(name, fn.params[index].loc, binding, static_cast<unsigned>(index + 1));
                }
            } else if (abi < function->arg_size()) {
                llvm::Argument *arg = function->getArg(abi++);
                if (!name.empty()) {
                    llvm::AllocaInst *slot = alloca_typed(stored_type(low), name);
                    builder_.CreateStore(arg, slot);
                    Binding binding;
                    binding.value = slot;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                    alias_capture(name, binding);
                    debug_declare(name, fn.params[index].loc, binding, static_cast<unsigned>(index + 1));
                }
            }
        }
    } else {
        unsigned index = 0;
        for (auto &arg : function->args()) {
            const std::string name = index < fn.params.size() ? fn.params[index].name : "";
            if (!name.empty()) {
                const Lowered low = lower_param(fn, index);
                llvm::AllocaInst *slot = alloca_typed(stored_type(low), name);
                builder_.CreateStore(&arg, slot);
                Binding binding;
                binding.value = slot;
                apply_low(binding, low);
                scopes_.back()[name] = binding;
                alias_capture(name, binding);
                debug_declare(name, fn.params[index].loc, binding, static_cast<unsigned>(index + 1));
            }
            ++index;
        }
    }
    if (captures != nullptr) {
        const unsigned base = static_cast<unsigned>(fn.params.size());
        for (std::size_t index = 0; index < fn.captures.size() && index < captures->size(); ++index) {
            const unsigned arg_index = base + static_cast<unsigned>(index);
            if (arg_index >= function->arg_size()) {
                break;
            }
            const FnCapture &item = fn.captures[index];
            llvm::Argument *arg = function->getArg(arg_index);
            if (!item.name.empty()) {
                arg->setName(item.name);
            }
            llvm::AllocaInst *slot = alloca_typed(ptr_type(), item.name.empty() ? "cap" : item.name);
            builder_.CreateStore(arg, slot);
            Binding binding = (*captures)[index];
            binding.value = slot;
            binding.is_function = false;
            binding.immediate = false;
            binding.is_array = false;
            binding.is_tuple = false;
            binding.decl = nullptr;
            if (binding.depth < 1) {
                binding.depth = 1;
            }
            if (!item.name.empty()) {
                scopes_.back()[item.name] = binding;
                scopes_.back()[capture_key(item.function_id, item.name)] = binding;
            }
        }
    }
    emit_stmt(*fn.body);
    finish_function();
    pop();
    debug_end_function();
    di_scopes_ = std::move(saved_scopes);
    di_subprogram_ = saved_subprogram;
    builder_.SetCurrentDebugLocation(saved_loc);
    function_ = nullptr;
    return_type_ = nullptr;
    return_depth_ = 0;
    return_many_ = saved_many;
    share_function_scope_ = saved_share;
    extern_struct_ret_ = saved_extern;
    extern_sret_ = saved_sret;
    extern_sret_ptr_ = saved_sret_ptr;
    extern_ret_abi_ = saved_abi;
    extern_ret_struct_ = saved_struct;
    trace_name_ = saved_trace;
    emitting_fn_ = saved_emitting;
}

void CodeGen::emit_function(const FunctionDecl &fn) {
    std::string key = function_key(fn);
    const auto recorded = decl_keys_.find(&fn);
    if (recorded != decl_keys_.end()) {
        key = recorded->second.key;
    }
    const auto found = files_.at(current_file_).functions.find(key);
    if (found == files_.at(current_file_).functions.end() || found->second.is_generic || found->second.value == nullptr) {
        return;
    }
    auto *function = llvm::cast<llvm::Function>(found->second.value);
    // Two TLC functions can share one object name. The second body would overwrite the first.
    if (!fn.link_name.empty() && !function->isDeclaration()) {
        error(fn.loc, "redefinition");
        return;
    }
    emit_function_body(fn, function, found->second.depth);
}

llvm::FunctionType *CodeGen::function_type_with_captures(const FunctionDecl &fn) const {
    llvm::FunctionType *base = function_type(fn, current_file_);
    if (fn.captures.empty() || base == nullptr) {
        return base;
    }
    std::vector<llvm::Type *> params;
    params.reserve(base->getNumParams() + fn.captures.size());
    for (unsigned index = 0; index < base->getNumParams(); ++index) {
        params.push_back(base->getParamType(index));
    }
    for (std::size_t index = 0; index < fn.captures.size(); ++index) {
        params.push_back(ptr_type());
    }
    return llvm::FunctionType::get(base->getReturnType(), params, base->isVarArg());
}

std::vector<Binding> CodeGen::capture_bindings(const FunctionDecl &fn) const {
    std::vector<Binding> out;
    out.reserve(fn.captures.size());
    for (const FnCapture &item : fn.captures) {
        const Binding *found = lookup(capture_key(item.function_id, item.name));
        if (found == nullptr) {
            found = lookup(item.name);
        }
        out.push_back(found == nullptr ? Binding{} : *found);
    }
    return out;
}

void CodeGen::alias_capture(const std::string &name, const Binding &binding) {
    if (emitting_fn_ == nullptr || scopes_.empty() || name.empty() || emitting_fn_->function_id == 0) {
        return;
    }
    bool owned = false;
    for (const std::string &kept : emitting_fn_->captured_here) {
        if (kept == name) {
            owned = true;
            break;
        }
    }
    if (!owned) {
        return;
    }
    const std::string slot = capture_key(emitting_fn_->function_id, name);
    if (lookup(slot) != nullptr) {
        return;
    }
    scopes_.back()[slot] = binding;
}

void CodeGen::append_captures(const FunctionDecl *decl, std::vector<llvm::Value *> &args, SourceLoc loc) {
    if (decl == nullptr) {
        return;
    }
    for (const FnCapture &item : decl->captures) {
        const Binding *binding = lookup(capture_key(item.function_id, item.name));
        if (binding == nullptr) {
            binding = lookup(item.name);
        }
        if (binding == nullptr || binding->value == nullptr) {
            error(loc, "not supported yet");
            args.push_back(null_ptr());
            continue;
        }
        const TypedValue value = value_of_binding(*binding);
        if (value.value == nullptr || !value.value->getType()->isPointerTy()) {
            error(loc, "not supported yet");
            args.push_back(null_ptr());
            continue;
        }
        args.push_back(value.value);
    }
}

void CodeGen::emit_local_function(const FunctionDecl &fn) {
    if (fn.name.empty() || !fn.method_struct.empty() || scopes_.empty()) {
        return;
    }
    if (function_is_generic(current_file_, fn)) {
        error(fn.loc, "not supported yet");
        return;
    }
    llvm::Function *function = nullptr;
    const std::string list = param_list_key(fn);
    const auto found = scopes_.back().find(fn.name);
    const bool same_list = found != scopes_.back().end() && found->second.is_function && found->second.decl != nullptr &&
                           param_list_key(*found->second.decl) == list;
    if (found != scopes_.back().end() && !found->second.is_function) {
        return;
    }
    if (same_list) {
        function = llvm::dyn_cast_or_null<llvm::Function>(found->second.value);
        found->second.decl = &fn;
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const Lowered ret = returns_void || many
                            ? Lowered{}
                            : lower(current_file_, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0,
                                    false);
    if (function == nullptr) {
        const PackageNode *owner = find_node(current_file_);
        const std::string qualifier = owner != nullptr && !owner->qualifier.empty() ? owner->qualifier : "local";
        const std::string symbol = qualifier + ".local." + fn.name + "." + std::to_string(++local_seq_);
        function = llvm::Function::Create(function_type_with_captures(fn), llvm::GlobalValue::InternalLinkage, symbol,
                                           module_);
        function->setCallingConv(llvm::CallingConv::C);
        unsigned index = 0;
        for (auto &arg : function->args()) {
            if (index < fn.params.size() && !fn.params[index].name.empty()) {
                arg.setName(fn.params[index].name);
            }
            ++index;
        }
        Binding binding;
        binding.value = function;
        binding.depth = returns_void ? 0 : ret.depth;
        binding.is_function = true;
        binding.elem_char = !returns_void && ret.elem_char;
        binding.struct_id = returns_void ? -1 : ret.struct_id;
        binding.elem_void = !returns_void && ret.elem_void;
        binding.elem_bool = !returns_void && ret.elem_bool;
        binding.elem_bits = returns_void ? 0 : ret.elem_bits;
        binding.elem_unsigned = !returns_void && ret.elem_unsigned;
        binding.elem_float = !returns_void && ret.elem_float;
        binding.trait_id = returns_void ? -1 : ret.trait_id;
        binding.enum_id = returns_void ? -1 : ret.enum_id;
        binding.decl = &fn;
        if (found == scopes_.back().end()) {
            scopes_.back()[fn.name] = binding;
        }
        local_fns_.insert_or_assign(&fn, binding);
    } else if (same_list) {
        local_fns_.insert_or_assign(&fn, found->second);
    }
    if (function != nullptr && !fn.captures.empty() &&
        function->arg_size() != fn.params.size() + fn.captures.size()) {
        error(fn.loc, "not supported yet");
        return;
    }
    emit_nested_body(fn, function, returns_void ? 0 : ret.depth, capture_bindings(fn));
}

void CodeGen::emit_nested_body(const FunctionDecl &fn, llvm::Function *function, int depth,
                               std::vector<Binding> captures) {
    if (fn.body == nullptr || function == nullptr || !function->empty()) {
        return;
    }
    std::vector<std::unordered_map<std::string, Binding>> kept;
    for (const auto &scope : scopes_) {
        std::unordered_map<std::string, Binding> filtered;
        for (const auto &[name, binding] : scope) {
            if (binding.is_function || llvm::isa_and_nonnull<llvm::GlobalVariable>(binding.value)) {
                filtered.emplace(name, binding);
            }
        }
        if (!filtered.empty()) {
            kept.push_back(std::move(filtered));
        }
    }
    auto saved_scopes = std::move(scopes_);
    llvm::Function *saved_function = function_;
    llvm::Type *saved_return = return_type_;
    const int saved_depth = return_depth_;
    auto saved_loops = std::move(loops_);
    auto saved_defers = std::move(defers_);
    auto saved_cases = std::move(case_blocks_);
    const llvm::IRBuilderBase::InsertPoint saved_ip = builder_.saveIP();
    scopes_ = std::move(kept);
    loops_.clear();
    defers_.clear();
    case_blocks_.clear();
    auto saved_spec = active_spec_;
    active_spec_.clear();
    emit_function_body(fn, function, depth, captures.empty() ? nullptr : &captures);
    active_spec_ = std::move(saved_spec);
    scopes_ = std::move(saved_scopes);
    function_ = saved_function;
    return_type_ = saved_return;
    return_depth_ = saved_depth;
    loops_ = std::move(saved_loops);
    defers_ = std::move(saved_defers);
    case_blocks_ = std::move(saved_cases);
    builder_.restoreIP(saved_ip);
}

TypedValue CodeGen::emit_anon_function(const FunctionDecl &fn) {
    if (!fn.method_struct.empty() || fn.variadic || function_is_generic(current_file_, fn) || fn.body == nullptr) {
        error(fn.loc, "not supported yet");
        return {zero(), 0};
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const Lowered ret = returns_void || many
                            ? Lowered{}
                            : lower(current_file_, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0,
                                    false);
    const PackageNode *owner = find_node(current_file_);
    const std::string qualifier = owner != nullptr && !owner->qualifier.empty() ? owner->qualifier : "anon";
    const std::string symbol = qualifier + ".anon." + std::to_string(++local_seq_);
    llvm::Function *function = llvm::Function::Create(function_type_with_captures(fn), llvm::GlobalValue::InternalLinkage,
                                                       symbol, module_);
    function->setCallingConv(llvm::CallingConv::C);
    unsigned index = 0;
    for (auto &arg : function->args()) {
        if (index < fn.params.size() && !fn.params[index].name.empty()) {
            arg.setName(fn.params[index].name);
        }
        ++index;
    }
    emit_nested_body(fn, function, returns_void ? 0 : ret.depth, capture_bindings(fn));
    return {function,
            returns_void ? 0 : ret.depth,
            !returns_void && ret.elem_char,
            returns_void ? -1 : ret.struct_id,
            false,
            !returns_void && ret.elem_void,
            !returns_void && ret.elem_bool,
            returns_void ? 0 : ret.elem_bits,
            !returns_void && ret.elem_unsigned};
}

} // namespace tlc
