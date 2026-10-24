// Declaring functions and globals, and emitting a package.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

void CodeGen::declare_functions(const PackageNode &node) {
    current_file_ = node.file_id;
    struct Acc {
        const FunctionDecl *fn = nullptr;
        std::vector<const FunctionDecl *> decls;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
        bool extern_c = false;
        std::string link_name;
        std::string plain;
    };
    std::unordered_map<std::string, std::unordered_set<std::string>> kinds;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->name.empty() || fn->is_comptime || fn->is_macro || !fn->type_params.empty()) {
            continue;
        }
        kinds[function_key(*fn)].insert(param_list_key(*fn));
    }
    std::unordered_map<std::string, Acc> functions;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->name.empty() || fn->is_comptime || fn->is_macro) {
            continue;
        }
        const std::string plain = function_key(*fn);
        std::string map_key = plain;
        const auto kind = kinds.find(plain);
        if (kind != kinds.end() && kind->second.size() > 1) {
            map_key.push_back('.');
            map_key += param_list_key(*fn);
        }
        Acc &acc = functions[map_key];
        acc.plain = plain;
        acc.decls.push_back(fn);
        if (acc.fn == nullptr || fn->body != nullptr) {
            acc.fn = fn;
        }
        acc.exported = acc.exported || fn->exported;
        acc.is_static = acc.is_static || fn->is_static;
        acc.is_extern = acc.is_extern || fn->is_extern;
        acc.extern_c = acc.extern_c || fn->extern_c;
        if (acc.link_name.empty()) {
            acc.link_name = fn->link_name;
        }
    }
    FileSymbols &file = files_[node.file_id];
    const bool entry = node.file_id == entry_file_;
    std::unordered_map<std::string, int> exported_lists;
    for (const auto &[name, acc] : functions) {
        (void)name;
        if (acc.fn != nullptr && acc.fn->type_params.empty() && acc.exported && !acc.is_static) {
            exported_lists[acc.plain] += 1;
        }
    }
    for (const auto &[name, acc] : functions) {
        // `var f = id` names this template. `T` is not a type yet, so the binding has no value.
        if (acc.fn != nullptr && !acc.fn->type_params.empty() && acc.fn->method_struct.empty()) {
            Binding binding;
            binding.is_function = true;
            binding.is_generic = true;
            binding.is_static = acc.is_static;
            binding.is_extern = acc.is_extern;
            binding.decl = acc.fn;
            file.functions.emplace(name, binding);
            continue;
        }
        if (!acc.fn->type_params.empty()) {
            continue;
        }
        if (!acc.fn->method_struct.empty()) {
            bool method_template = false;
            for (const TopDecl &decl : node.unit.decls) {
                const auto *item = std::get_if<StructDecl>(&decl.data);
                if (item != nullptr && item->name == acc.fn->method_struct && !item->type_params.empty()) {
                    method_template = true;
                    break;
                }
            }
            if (method_template) {
                continue;
            }
        }
        const bool many = acc.fn->results.size() > 1;
        const bool returns_void = !many && acc.fn->return_type == "void" && acc.fn->indirections == 0 && !acc.fn->is_struct &&
                                  acc.fn->type_package.empty();
        const bool ret_fnptr = !many && acc.fn->results.size() == 1 && acc.fn->results.front().is_fn_ptr;
        const ResultSlot *ret_slot = ret_fnptr ? &acc.fn->results.front() : nullptr;
        const Lowered ret = returns_void || many
                                ? Lowered{}
                                : (ret_fnptr ? lower_fnptr(node.file_id, ret_slot->type_name, ret_slot->type_package,
                                                          ret_slot->is_struct, ret_slot->is_enum, ret_slot->ret_indirections,
                                                          ret_slot->indirections, ret_slot->fn_params, ret_slot->fn_variadic,
                                                          ret_slot->fn_results)
                                             : lower(node.file_id, acc.fn->return_type, acc.fn->type_package, acc.fn->is_struct,
                                                     acc.fn->indirections, false, 0, false));
        if (function_is_generic(node.file_id, *acc.fn)) {
            Binding binding;
            binding.is_function = true;
            binding.is_generic = true;
            binding.is_static = acc.is_static;
            // The body is the decl. A prototype may be the declaration that says `extern`.
            binding.is_extern = acc.is_extern;
            binding.decl = acc.fn;
            apply_scalar_result(binding, ret, returns_void);
            file.functions.emplace(name, binding);
            // Overloads are not stored under the plain name. The chosen list is found from its decl.
            for (const FunctionDecl *decl : acc.decls) {
                if (decl != nullptr && decl->type_params.empty()) {
                    decl_keys_[decl] = DeclKey{node.file_id, name};
                }
            }
            continue;
        }
        const std::string object = acc.extern_c ? acc.plain : name;
        const SymbolForm form = symbol_form(node, object, acc.is_extern, acc.extern_c, acc.is_static, acc.exported,
                                            entry && !testing_ && acc.plain == "main" && name == acc.plain, acc.link_name);
        const ExternLayout layout = acc.extern_c ? extern_layout(*acc.fn, node.file_id) : ExternLayout{};
        llvm::FunctionType *ftype = layout.active ? layout.type : function_type(*acc.fn, node.file_id);
        llvm::Function *function = nullptr;
        if (acc.is_extern) {
            if (llvm::GlobalValue *named = module_.getNamedValue(form.name)) {
                function = llvm::dyn_cast<llvm::Function>(named);
                if (function == nullptr || ftype == nullptr || function->getFunctionType() != ftype) {
                    error(acc.fn->loc, "redefinition");
                    continue;
                }
            }
        }
        if (function == nullptr) {
            function = llvm::Function::Create(ftype, form.linkage, form.name, module_);
            function->setCallingConv(llvm::CallingConv::C);
            if (layout.active) {
                apply_extern_attrs(function, layout);
                extern_abis_.emplace(function, layout);
            }
            if (!layout.active) {
                unsigned index = 0;
                for (auto &arg : function->args()) {
                    if (index < acc.fn->params.size() && !acc.fn->params[index].name.empty()) {
                        arg.setName(acc.fn->params[index].name);
                    }
                    ++index;
                }
            }
        } else if (layout.active && !extern_abis_.contains(function)) {
            apply_extern_attrs(function, layout);
            extern_abis_.emplace(function, layout);
        }
        Binding binding;
        binding.value = function;
        binding.is_function = true;
        apply_scalar_result(binding, ret, returns_void);
        binding.is_static = acc.is_static;
        binding.decl = acc.fn;
        file.functions.emplace(name, binding);
        for (const FunctionDecl *decl : acc.decls) {
            if (decl != nullptr && decl->type_params.empty()) {
                decl_keys_[decl] = DeclKey{node.file_id, name};
            }
        }
        const auto exported = exported_lists.find(acc.plain);
        if (exported != exported_lists.end() && exported->second == 1 && acc.exported && !acc.is_static &&
            name != acc.plain) {
            file.functions.emplace(acc.plain, binding);
        }
    }
}

void CodeGen::declare_globals(const PackageNode &node) {
    current_file_ = node.file_id;
    FileSymbols &file = files_[node.file_id];
    for (const TopDecl &decl : node.unit.decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        // A generic template mentions its parameters. Each specialization is appended later.
        if (var == nullptr || var->name.empty() || !var->owner_params.empty()) {
            continue;
        }
        const std::string key = global_key(*var);
        if (file.globals.contains(key)) {
            continue;
        }
        if (var->is_const_decl && var->has_const) {
            Binding binding;
            binding.immediate = true;
            binding.const_value = var->const_value;
            binding.elem_bits = 32;
            binding.is_static = var->is_static;
            file.globals.emplace(key, binding);
            // A use stays the immediate. `extern` still defines the object symbol.
            if (var->is_extern) {
                llvm::Type *ty = i32();
                if (!var->inferred && !var->type_name.empty()) {
                    const Lowered low = lower_declared(node.file_id, *var);
                    if (llvm::Type *stored = stored_type(low); stored != nullptr && stored->isIntegerTy()) {
                        ty = stored;
                    }
                }
                const SymbolForm form = symbol_form(node, key, true, var->extern_c, var->is_static, var->exported, false,
                                                    var->link_name);
                if (module_.getNamedValue(form.name) != nullptr) {
                    error(var->loc, "redefinition");
                } else {
                    llvm::Constant *init =
                        llvm::ConstantInt::get(ty, static_cast<uint64_t>(static_cast<int64_t>(var->const_value)), true);
                    new llvm::GlobalVariable(module_, ty, true, form.linkage, init, form.name);
                }
            }
            continue;
        }
        const SymbolForm form = symbol_form(node, key, var->is_extern, var->extern_c, var->is_static, var->exported,
                                            false, var->link_name);
        Lowered low;
        if (var->inferred && var->init != nullptr && std::holds_alternative<Expr::String>(var->init->data)) {
            // `var text = "hi"` and `const label = "hi"` are `[]const u8`.
            low.is_slice = true;
            low.array_len = -1;
            low.elem_bits = 8;
            low.elem_unsigned = true;
            low.elem_char = true;
            low.type = slice_type();
        } else if (var->inferred) {
            if (var->init == nullptr || !infer_global(node.file_id, *var->init, low)) {
                error(var->loc, "type mismatch");
                low = lower(node.file_id, "i32", "", false, 0, false, 0, false);
            }
        } else {
            low = lower_declared(node.file_id, *var);
        }
        llvm::Type *ty = stored_type(low);
        llvm::GlobalVariable *existing = nullptr;
        if (var->is_extern) {
            if (llvm::GlobalValue *named = module_.getNamedValue(form.name)) {
                existing = llvm::dyn_cast<llvm::GlobalVariable>(named);
                if (existing == nullptr || existing->getValueType() != ty) {
                    error(var->loc, "redefinition");
                    continue;
                }
            }
        }
        llvm::GlobalVariable *global = existing;
        if (global == nullptr) {
            llvm::Constant *init = nullptr;
            if (!var->is_extern) {
                if (low.is_slice) {
                    init = llvm::UndefValue::get(ty);
                } else if (low.trait_id >= 0 && low.struct_id < 0 && low.depth == 0 && !low.is_array) {
                    init = llvm::ConstantAggregateZero::get(ty);
                } else if (low.is_array || (low.depth == 0 && low.struct_id >= 0)) {
                    // Unset fields stay undef. A field value or `0...` replaces that hole.
                    init = bare_initializer(low, ty);
                } else {
                    init = llvm::UndefValue::get(ty);
                }
            }
            global = new llvm::GlobalVariable(module_, ty, var->is_const_decl, form.linkage, init, form.name);
            debug_global(key, global, var->loc, low, var->is_static);
        }
        Binding binding;
        binding.value = global;
        binding.depth = low.depth;
        binding.elem_char = low.elem_char;
        binding.is_array = low.is_array;
        binding.array_len = low.array_len;
        binding.ptr_to_array = low.ptr_to_array;
        binding.ptr_array_len = low.ptr_array_len;
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
        binding.is_slice = low.is_slice;
        binding.elem_slice = low.elem_slice;
        binding.alias_owner = low.alias_owner;
        binding.alias_file = low.alias_file;
        binding.is_static = var->is_static;
        // `[N][M]T` keeps every shell so an index steps by the row, then by the element.
        if (low.is_array) {
            binding.shells = low.shells;
        }
        file.globals.emplace(key, binding);
    }
}

void CodeGen::bind_imports(const PackageNode &node) {
    if (graph_ == nullptr) {
        return;
    }
    for (const ImportDecl &import : node.unit.imports) {
        const PackageNode *target = import_target(graph_, node.file_id, import);
        if (target == nullptr) {
            continue;
        }
        if (import.form == ImportDecl::Form::Selective) {
            for (const std::string &name : import.names) {
                if (const Binding *function = visible(target->file_id, name, true)) {
                    scopes_.back()[name] = *function;
                } else if (const Binding *global = visible(target->file_id, name, false)) {
                    scopes_.back()[name] = *global;
                }
            }
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (!binding.empty()) {
            packages_[binding] = target->file_id;
        }
    }
}

void CodeGen::emit_bodies(const PackageNode &node) {
    current_file_ = node.file_id;
    packages_.clear();
    push();
    bind_package_globals();
    bind_imports(node);
    std::unordered_set<std::string> emitted;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->body == nullptr || fn->is_comptime || fn->is_macro ||
            function_is_generic(node.file_id, *fn)) {
            continue;
        }
        std::string key = function_key(*fn);
        const auto recorded = decl_keys_.find(fn);
        if (recorded != decl_keys_.end()) {
            key = recorded->second.key;
        }
        if (!emitted.insert(key).second) {
            continue;
        }
        emit_function(*fn);
    }
    pop();
}
void CodeGen::emit_dynamic_inits() {
    if (graph_ == nullptr) {
        return;
    }
    struct FileOrder {
        const PackageNode *node = nullptr;
        int depth = 0;
    };
    // A cycle stores 0 and stops. The walk then numbers the chain from the importer.
    std::unordered_map<uint32_t, int> memo;
    auto depth_of = [&](uint32_t id) -> int {
        const uint32_t start = id;
        std::vector<uint32_t> chain;
        while (!memo.contains(id)) {
            const PackageNode *node = find_node(id);
            if (node == nullptr || node->imported_from == 0) {
                memo.emplace(id, 0);
                break;
            }
            memo.emplace(id, 0);
            chain.push_back(id);
            id = node->imported_from;
        }
        int depth = memo[id];
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            depth += 1;
            memo[*it] = depth;
        }
        return memo[start];
    };
    std::vector<FileOrder> order;
    for (const PackageNode &node : graph_->nodes) {
        bool runtime = false;
        for (const TopDecl &decl : node.unit.decls) {
            const auto *var = std::get_if<VarDecl>(&decl.data);
            if (var != nullptr && var->runtime_init && var->init != nullptr && var->owner_params.empty()) {
                runtime = true;
                break;
            }
        }
        if (runtime) {
            order.push_back(FileOrder{&node, depth_of(node.file_id)});
        }
    }
    if (order.empty()) {
        return;
    }
    // An imported package runs before the file that imports it. One package runs in path order.
    std::stable_sort(order.begin(), order.end(), [](const FileOrder &left, const FileOrder &right) {
        if (left.depth != right.depth) {
            return left.depth > right.depth;
        }
        return left.node->path < right.node->path;
    });

    const bool saved_many = return_many_;
    const bool saved_share = share_function_scope_;
    const bool saved_extern = extern_struct_ret_;
    const bool saved_sret = extern_sret_;
    llvm::Value *saved_sret_ptr = extern_sret_ptr_;
    const CAbi saved_abi = extern_ret_abi_;
    llvm::Type *saved_struct = extern_ret_struct_;
    const std::string saved_trace = trace_name_;
    const uint32_t saved_file = current_file_;
    llvm::Function *saved_function = function_;
    llvm::Type *saved_return = return_type_;
    const int saved_depth = return_depth_;

    auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    auto *init = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, "tlc.init", module_);
    init->setCallingConv(llvm::CallingConv::C);
    function_ = init;
    return_type_ = init->getReturnType();
    return_depth_ = 0;
    return_many_ = false;
    share_function_scope_ = true;
    extern_struct_ret_ = false;
    extern_sret_ = false;
    extern_sret_ptr_ = nullptr;
    extern_ret_abi_ = {};
    extern_ret_struct_ = nullptr;
    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", init);
    builder_.SetInsertPoint(entry);

    for (const FileOrder &item : order) {
        if (terminated()) {
            break;
        }
        current_file_ = item.node->file_id;
        packages_.clear();
        push();
        bind_package_globals();
        bind_imports(*item.node);
        for (const TopDecl &decl : item.node->unit.decls) {
            if (terminated()) {
                break;
            }
            const auto *var = std::get_if<VarDecl>(&decl.data);
            if (var == nullptr || !var->runtime_init || var->init == nullptr || var->name.empty() ||
                !var->owner_params.empty()) {
                continue;
            }
            const auto file = files_.find(current_file_);
            if (file == files_.end()) {
                continue;
            }
            const auto found = file->second.globals.find(global_key(*var));
            if (found == file->second.globals.end() || found->second.value == nullptr) {
                continue;
            }
            trace_name_ = var->name;
            set_loc(var->loc);
            llvm::Value *dest = found->second.value;
            // Store the list in the global. A later call reads elements already stored.
            if (found->second.is_array) {
                llvm::Type *ty = object_type(dest);
                auto *array_ty = ty != nullptr && ty->isArrayTy() ? llvm::cast<llvm::ArrayType>(ty) : nullptr;
                if (array_ty == nullptr) {
                    error(var->loc, "type mismatch");
                } else {
                    store_array(dest, array_ty, *var->init, var->loc);
                }
                continue;
            }
            const TypedValue value = emit_rvalue(*var->init);
            if (terminated() || value.value == nullptr) {
                continue;
            }
            if (found->second.is_slice) {
                llvm::Value *stored = as_slice_value(value, var->loc);
                if (!terminated() && stored != nullptr) {
                    builder_.CreateStore(stored, dest);
                }
            } else {
                llvm::Type *slot = object_type(dest);
                llvm::Value *stored = slot == nullptr ? nullptr : as_stored(value.value, value.depth, slot, value.elem_unsigned);
                if (stored == nullptr || slot == nullptr || stored->getType() != slot) {
                    error(var->loc, "type mismatch");
                } else if (!terminated()) {
                    builder_.CreateStore(stored, dest);
                }
            }
        }
        pop();
    }
    if (!terminated()) {
        builder_.CreateRetVoid();
    }

    auto *ctor_ty = llvm::StructType::get(context_, {i32(), ptr_type(), ptr_type()});
    auto *array_ty = llvm::ArrayType::get(ctor_ty, 1);
    llvm::Constant *fields[3] = {const_i32(65535), init, llvm::ConstantPointerNull::get(ptr_type())};
    llvm::Constant *ctor = llvm::ConstantStruct::get(ctor_ty, fields);
    new llvm::GlobalVariable(module_, array_ty, false, llvm::GlobalValue::AppendingLinkage,
                             llvm::ConstantArray::get(array_ty, {ctor}), "llvm.global_ctors");

    function_ = saved_function;
    return_type_ = saved_return;
    return_depth_ = saved_depth;
    return_many_ = saved_many;
    share_function_scope_ = saved_share;
    extern_struct_ret_ = saved_extern;
    extern_sret_ = saved_sret;
    extern_sret_ptr_ = saved_sret_ptr;
    extern_ret_abi_ = saved_abi;
    extern_ret_struct_ = saved_struct;
    trace_name_ = saved_trace;
    current_file_ = saved_file;
    packages_.clear();
}

void CodeGen::emit(const PackageGraph &graph) {
    if (graph.nodes.empty()) {
        return;
    }
    graph_ = &graph;
    entry_file_ = graph.nodes.front().file_id;
    if (debug_) {
        open_debug();
    }
    prepare_enums();
    prepare_structs();
    prepare_traits();
    for (const PackageNode &node : graph.nodes) {
        declare_functions(node);
    }
    for (const PackageNode &node : graph.nodes) {
        declare_globals(node);
    }
    for (const PackageNode &node : graph.nodes) {
        current_file_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                apply_initializer(*var);
            }
        }
    }
    for (const PackageNode &node : graph.nodes) {
        emit_bodies(node);
    }
    emit_dynamic_inits();
    emit_builtin_trace();
    emit_builtin_panic();
    emit_builtin_heap();
    if (testing_) {
        emit_tests();
    }
    if (di_builder_ != nullptr) {
        finish_debug();
    }
    graph_ = nullptr;
}
void CodeGen::emit_tests() {
    struct TestFn {
        std::string name;
        llvm::Function *function = nullptr;
    };
    std::vector<TestFn> tests;
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        int index = 0;
        current_file_ = node.file_id;
        packages_.clear();
        push();
        bind_package_globals();
        bind_imports(node);
        for (const TopDecl &decl : node.unit.decls) {
            const auto *test = std::get_if<TestDecl>(&decl.data);
            if (test == nullptr || test->body == nullptr) {
                continue;
            }
            const std::string symbol = "tlc.test." + std::to_string(node.file_id) + "." + std::to_string(index);
            ++index;
            auto *type = llvm::FunctionType::get(i32(), false);
            auto *function =
                llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, symbol, module_);
            function->setCallingConv(llvm::CallingConv::C);
            const bool saved_many = return_many_;
            const bool saved_share = share_function_scope_;
            const bool saved_extern = extern_struct_ret_;
            const bool saved_sret = extern_sret_;
            llvm::Value *saved_sret_ptr = extern_sret_ptr_;
            const CAbi saved_abi = extern_ret_abi_;
            llvm::Type *saved_struct = extern_ret_struct_;
            function_ = function;
            return_type_ = function->getReturnType();
            return_depth_ = 0;
            return_many_ = false;
            share_function_scope_ = true;
            const std::string saved_trace = trace_name_;
            trace_name_ = test->name;
            extern_struct_ret_ = false;
            extern_sret_ = false;
            extern_sret_ptr_ = nullptr;
            extern_ret_abi_ = {};
            extern_ret_struct_ = nullptr;
            llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", function);
            builder_.SetInsertPoint(entry);
            push();
            emit_stmt(*test->body);
            finish_function();
            pop();
            trace_name_ = saved_trace;
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
            tests.push_back(TestFn{test->name, function});
        }
        pop();
    }
    if (tests.empty()) {
        error(SourceLoc{}, "no tests");
        return;
    }
    auto *main_type = llvm::FunctionType::get(i32(), false);
    auto *main = llvm::Function::Create(main_type, llvm::GlobalValue::ExternalLinkage, "main", module_);
    main->setCallingConv(llvm::CallingConv::C);
    auto *printf_type = llvm::FunctionType::get(i32(), {ptr_type()}, true);
    llvm::FunctionCallee printf_fn = module_.getOrInsertFunction("printf", printf_type);
    llvm::BasicBlock *block = llvm::BasicBlock::Create(context_, "entry", main);
    builder_.SetInsertPoint(block);
    llvm::Value *failed = const_i32(0);
    for (const TestFn &test : tests) {
        llvm::BasicBlock *from = builder_.GetInsertBlock();
        llvm::Value *result = builder_.CreateCall(test.function);
        llvm::Value *bad = builder_.CreateICmpNE(result, const_i32(0));
        auto *fail_block = llvm::BasicBlock::Create(context_, "fail", main);
        auto *next_block = llvm::BasicBlock::Create(context_, "next", main);
        builder_.CreateCondBr(bad, fail_block, next_block);
        builder_.SetInsertPoint(fail_block);
        llvm::Value *format = builder_.CreateGlobalString("fail: %s\n");
        llvm::Value *label = builder_.CreateGlobalString(test.name);
        builder_.CreateCall(printf_type, printf_fn.getCallee(), {format, label});
        llvm::Value *added = builder_.CreateAdd(failed, const_i32(1));
        builder_.CreateBr(next_block);
        builder_.SetInsertPoint(next_block);
        auto *phi = builder_.CreatePHI(i32(), 2);
        phi->addIncoming(failed, from);
        phi->addIncoming(added, fail_block);
        failed = phi;
    }
    builder_.CreateRet(failed);
}

} // namespace tlc
