// Builtin trace, panic, and new and delete.
#include "gen/gen.hpp"

namespace tlc {

bool CodeGen::is_std_builtin_call(const Expr &callee, const std::string &field) const {
    if (graph_ == nullptr) {
        return false;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->field != field || selector->base == nullptr) {
            return false;
        }
        const auto *base = std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return false;
        }
        const auto package = packages_.find(base->text);
        if (package == packages_.end()) {
            return false;
        }
        return is_compiler_builtin(find_node(package->second));
    }
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        const Binding *binding = lookup(named->text);
        if (binding == nullptr || binding->decl == nullptr || binding->decl->name != field) {
            return false;
        }
        for (const PackageNode &file : graph_->nodes) {
            if (!is_compiler_builtin(&file)) {
                continue;
            }
            for (const TopDecl &decl : file.unit.decls) {
                if (const auto *fn = std::get_if<FunctionDecl>(&decl.data); fn == binding->decl) {
                    return true;
                }
            }
        }
    }
    return false;
}

void CodeGen::ensure_trace() {
    if (trace_frame_ != nullptr) {
        return;
    }
    trace_frame_ = llvm::StructType::create(context_, {ptr_type(), i32(), i32(), ptr_type()}, "tlc.frame");
    trace_stack_type_ = llvm::ArrayType::get(trace_frame_, 32);
    trace_depth_ = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::InternalLinkage, const_i32(0),
                                            "tlc.trace.depth");
    trace_stack_ = new llvm::GlobalVariable(module_, trace_stack_type_, false, llvm::GlobalValue::InternalLinkage,
                                            llvm::ConstantAggregateZero::get(trace_stack_type_), "tlc.trace.stack");
    // One tlc invocation is one executable. Local-exec is an %fs offset.
    // A shared library would need another model. The comptime runner is one process-local global.
    if (!eval_) {
        for (llvm::GlobalVariable *global : {trace_depth_, trace_stack_}) {
            global->setThreadLocalMode(llvm::GlobalValue::LocalExecTLSModel);
        }
    }
}

std::string CodeGen::display_path(SourceLoc loc) const {
    const SourceFile *file = diags_.source(loc.file);
    if (file == nullptr) {
        return "unknown";
    }
    const std::string &path = file->path();
    std::string dir;
    if (graph_ != nullptr && !graph_->nodes.empty()) {
        const std::string &entry = graph_->nodes.front().path;
        const auto slash = entry.find_last_of("/\\");
        if (slash != std::string::npos) {
            dir = entry.substr(0, slash + 1);
        }
    }
    if (!dir.empty() && path.starts_with(dir)) {
        return path.substr(dir.size());
    }
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

llvm::Value *CodeGen::push_trace(SourceLoc loc) {
    if (trace_name_.empty() || function_ == nullptr || terminated()) {
        return nullptr;
    }
    ensure_trace();
    int line = 1;
    int column = 1;
    if (const SourceFile *file = diags_.source(loc.file)) {
        const LineCol at = file->line_col(loc.offset);
        line = at.line;
        column = at.column;
    }
    llvm::BasicBlock *from = builder_.GetInsertBlock();
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    llvm::Value *can = builder_.CreateICmpULT(depth, const_i32(32));
    auto *push_block = llvm::BasicBlock::Create(context_, "trace.push", function_);
    auto *call_block = llvm::BasicBlock::Create(context_, "trace.call", function_);
    builder_.CreateCondBr(can, push_block, call_block);
    builder_.SetInsertPoint(push_block);
    llvm::Value *frame = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), depth});
    auto store_field = [&](unsigned index, llvm::Value *value) {
        llvm::Value *slot = builder_.CreateInBoundsGEP(trace_frame_, frame, {const_i32(0), const_i32(static_cast<int>(index))});
        builder_.CreateStore(value, slot);
    };
    store_field(0, builder_.CreateGlobalString(display_path(loc)));
    store_field(1, const_i32(line));
    store_field(2, const_i32(column));
    store_field(3, builder_.CreateGlobalString(trace_name_));
    builder_.CreateStore(builder_.CreateAdd(depth, const_i32(1)), trace_depth_);
    builder_.CreateBr(call_block);
    builder_.SetInsertPoint(call_block);
    auto *pushed = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
    pushed->addIncoming(llvm::ConstantInt::getTrue(context_), push_block);
    pushed->addIncoming(llvm::ConstantInt::getFalse(context_), from);
    return pushed;
}

void CodeGen::pop_trace(llvm::Value *pushed) {
    if (pushed == nullptr || function_ == nullptr || terminated()) {
        return;
    }
    auto *pop_block = llvm::BasicBlock::Create(context_, "trace.pop", function_);
    auto *done_block = llvm::BasicBlock::Create(context_, "trace.done", function_);
    builder_.CreateCondBr(pushed, pop_block, done_block);
    builder_.SetInsertPoint(pop_block);
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    builder_.CreateStore(builder_.CreateSub(depth, const_i32(1)), trace_depth_);
    builder_.CreateBr(done_block);
    builder_.SetInsertPoint(done_block);
}

llvm::Value *CodeGen::variadic_arg(const TypedValue &value) {
    llvm::Value *arg = value.value == nullptr ? zero() : value.value;
    if (arg->getType()->isFloatTy()) {
        return builder_.CreateFPExt(arg, f64_ty());
    }
    if (arg->getType()->isIntegerTy() && arg->getType()->getIntegerBitWidth() < 32) {
        const bool zero_extend = value.elem_unsigned || value.elem_char;
        return zero_extend ? builder_.CreateZExt(arg, i32()) : builder_.CreateSExt(arg, i32());
    }
    return arg;
}

TypedValue CodeGen::emit_builtin_va(SourceLoc loc) {
    if (function_ == nullptr || !function_->isVarArg() || terminated()) {
        error(loc, "not supported yet");
        return {null_ptr(), 1, true};
    }
    auto *tag = llvm::StructType::get(context_, {i32(), i32(), ptr_type(), ptr_type()});
    auto *list = llvm::ArrayType::get(tag, 1);
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    stamp_builder(allocas);
    llvm::AllocaInst *slot = allocas.CreateAlloca(list, nullptr, "va");
    slot->setAlignment(llvm::Align(16));
    llvm::Value *ap = builder_.CreateInBoundsGEP(list, slot, {const_i32(0), const_i32(0)});
    llvm::Function *start = llvm::Intrinsic::getOrInsertDeclaration(&module_, llvm::Intrinsic::vastart, {ptr_type()});
    builder_.CreateCall(start, {ap});
    return {ap, 1, true};
}

llvm::Function *CodeGen::builtin_function(const std::string &name) const {
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (!is_compiler_builtin(&node)) {
            continue;
        }
        const auto file = files_.find(node.file_id);
        if (file == files_.end()) {
            continue;
        }
        const auto found = file->second.functions.find(name);
        if (found == file->second.functions.end()) {
            continue;
        }
        return llvm::dyn_cast<llvm::Function>(found->second.value);
    }
    return nullptr;
}

void CodeGen::emit_stack_trace() {
    llvm::Function *get = builtin_function("StackTrace.get");
    llvm::Function *release = builtin_function("StackTrace.free");
    if ((get == nullptr || !get->empty()) && (release == nullptr || !release->empty())) {
        return;
    }
    const StructLayout *frame_layout = nullptr;
    const StructLayout *trace_layout = nullptr;
    for (const StructLayout &layout : structs_) {
        if (layout.type == nullptr || !is_compiler_builtin(find_node(layout.file_id))) {
            continue;
        }
        if (layout.name == "Frame") {
            frame_layout = &layout;
        } else if (layout.name == "StackTrace") {
            trace_layout = &layout;
        }
    }
    if (frame_layout == nullptr || trace_layout == nullptr || frame_layout->fields.size() < 4) {
        return;
    }
    ensure_trace();
    const uint64_t stride = module_.getDataLayout().getTypeAllocSize(frame_layout->type).getFixedValue();
    auto *malloc_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
    llvm::FunctionCallee malloc_fn = module_.getOrInsertFunction("malloc", malloc_type);
    auto *free_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
    llvm::FunctionCallee free_fn = module_.getOrInsertFunction("free", free_type);
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
    if (get != nullptr && get->empty() && get->getReturnType() == trace_layout->type) {
        auto *entry = llvm::BasicBlock::Create(context_, "entry", get);
        auto *none = llvm::BasicBlock::Create(context_, "none", get);
        auto *reserve = llvm::BasicBlock::Create(context_, "reserve", get);
        auto *fail = llvm::BasicBlock::Create(context_, "fail", get);
        auto *head = llvm::BasicBlock::Create(context_, "head", get);
        auto *body = llvm::BasicBlock::Create(context_, "body", get);
        auto *done = llvm::BasicBlock::Create(context_, "done", get);
        builder_.SetInsertPoint(entry);
        llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
        builder_.CreateCondBr(builder_.CreateICmpEQ(depth, const_i32(0)), none, reserve);
        builder_.SetInsertPoint(none);
        builder_.CreateRet(llvm::ConstantAggregateZero::get(trace_layout->type));
        builder_.SetInsertPoint(reserve);
        llvm::Value *bytes = builder_.CreateMul(builder_.CreateZExt(depth, i64()), llvm::ConstantInt::get(i64(), stride));
        llvm::Value *memory = builder_.CreateCall(malloc_type, malloc_fn.getCallee(), {bytes});
        builder_.CreateCondBr(builder_.CreateICmpEQ(memory, null_ptr()), fail, head);
        builder_.SetInsertPoint(fail);
        abort_panic(SourceLoc{}, "out of memory");
        builder_.SetInsertPoint(head);
        auto *index = builder_.CreatePHI(i32(), 2);
        index->addIncoming(const_i32(0), reserve);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, depth), body, done);
        builder_.SetInsertPoint(body);
        llvm::Value *source_index = builder_.CreateSub(builder_.CreateSub(depth, const_i32(1)), index);
        llvm::Value *source = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), source_index});
        llvm::Value *dest = builder_.CreateInBoundsGEP(frame_layout->type, memory, {index});
        for (unsigned field = 0; field < 4; ++field) {
            llvm::Type *field_type = field == 1 || field == 2 ? i32() : ptr_type();
            llvm::Value *from = builder_.CreateInBoundsGEP(trace_frame_, source, {const_i32(0), const_i32(field)});
            llvm::Value *to = builder_.CreateInBoundsGEP(frame_layout->type, dest, {const_i32(0), const_i32(field)});
            builder_.CreateStore(builder_.CreateLoad(field_type, from), to);
        }
        llvm::Value *next = builder_.CreateAdd(index, const_i32(1));
        index->addIncoming(next, body);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(done);
        llvm::Value *slice = llvm::ConstantAggregateZero::get(slice_type());
        slice = builder_.CreateInsertValue(slice, memory, 0);
        slice = builder_.CreateInsertValue(slice, depth, 1);
        llvm::Value *result = builder_.CreateInsertValue(llvm::ConstantAggregateZero::get(trace_layout->type), slice, 0);
        builder_.CreateRet(result);
    }
    if (release != nullptr && release->empty() && release->arg_size() >= 1) {
        auto *entry = llvm::BasicBlock::Create(context_, "entry", release);
        builder_.SetInsertPoint(entry);
        llvm::Value *header = builder_.CreateInBoundsGEP(trace_layout->type, release->getArg(0), {const_i32(0), const_i32(0)});
        llvm::Value *pointer_slot = builder_.CreateInBoundsGEP(slice_type(), header, {const_i32(0), const_i32(0)});
        llvm::Value *length_slot = builder_.CreateInBoundsGEP(slice_type(), header, {const_i32(0), const_i32(1)});
        builder_.CreateCall(free_type, free_fn.getCallee(), {builder_.CreateLoad(ptr_type(), pointer_slot)});
        builder_.CreateStore(null_ptr(), pointer_slot);
        builder_.CreateStore(const_i32(0), length_slot);
        builder_.CreateRetVoid();
    }
    builder_.restoreIP(saved);
}

bool CodeGen::bind_trait_allocator(const Expr &allocator, bool &builtin_alloc, uint32_t &alloc_file,
                                   std::string &alloc_name) {
    // `new(&name)` still names the parameter. The address is taken when the call is emitted.
    const Expr *cursor = &allocator;
    if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data)) {
        if (unary->op == TokenKind::Amp && !unary->postfix && unary->expr != nullptr) {
            cursor = unary->expr.get();
        }
    }
    const auto *named = std::get_if<Expr::Name>(&cursor->data);
    if (named == nullptr) {
        error(expr_loc(allocator), "type mismatch");
        return false;
    }
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(named->text);
        if (found == scope->end()) {
            continue;
        }
        const int sid = found->second.struct_id;
        if (sid < 0 || sid >= static_cast<int>(structs_.size())) {
            error(expr_loc(allocator), "type mismatch");
            return false;
        }
        const StructLayout &layout = structs_[static_cast<std::size_t>(sid)];
        alloc_file = layout.file_id;
        alloc_name = layout.name;
        const PackageNode *home = find_node(layout.file_id);
        builtin_alloc = layout.name == "Malloc" && home != nullptr && home->compiler_builtin;
        return true;
    }
    error(expr_loc(allocator), "undeclared identifier");
    return false;
}

llvm::Value *CodeGen::allocator_receiver(const Expr &allocator, bool is_pointer) {
    if (is_pointer) {
        const TypedValue value = emit_rvalue(allocator);
        return value.value;
    }
    if (!addressable(allocator)) {
        error(expr_loc(allocator), "not an lvalue");
        return nullptr;
    }
    const TypedValue addr = emit_addr(allocator);
    if (addr.value == nullptr) {
        return nullptr;
    }
    if (addr.depth > 0) {
        return builder_.CreateLoad(ptr_type(), addr.value);
    }
    return addr.value;
}

llvm::Function *CodeGen::allocator_method(uint32_t file_id, const std::string &type_name, const std::string &name) {
    const auto file = files_.find(file_id);
    if (file == files_.end() || type_name.empty()) {
        return nullptr;
    }
    const auto found = file->second.functions.find(type_name + "." + name);
    if (found == file->second.functions.end() || found->second.value == nullptr) {
        return nullptr;
    }
    return llvm::dyn_cast<llvm::Function>(found->second.value);
}

void CodeGen::abort_now() {
    auto *abort_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    llvm::FunctionCallee abort_fn = module_.getOrInsertFunction("abort", abort_type);
    builder_.CreateCall(abort_type, abort_fn.getCallee());
    builder_.CreateUnreachable();
}

llvm::Function *CodeGen::panic_function() {
    if (llvm::Function *existing = builtin_function("panic")) {
        return existing;
    }
    if (llvm::Function *internal = module_.getFunction("tlc.panic")) {
        return internal;
    }
    auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
    return llvm::Function::Create(type, llvm::Function::InternalLinkage, "tlc.panic", module_);
}

void CodeGen::abort_panic(SourceLoc loc, const char *message) {
    // Comptime evaluation has no `tlc.panic` body. Record the fault and leave the call.
    if (eval_) {
        eval_fail(loc);
        return;
    }
    llvm::Function *panic = panic_function();
    if (panic == nullptr || message == nullptr) {
        abort_now();
        return;
    }
    // The frame stays. panic does not return.
    push_trace(loc);
    llvm::CallInst *call = builder_.CreateCall(panic, {builder_.CreateGlobalString(message)});
    call->setCallingConv(llvm::CallingConv::C);
    builder_.CreateUnreachable();
}

void CodeGen::abort_out_of_memory(SourceLoc loc) {
    abort_panic(loc, "out of memory");
}

void CodeGen::install_eval_fault() {
    if (!eval_ || eval_fail_file_ != nullptr) {
        return;
    }
    eval_fail_file_ = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage,
                                               const_i32(0), "tlc.comptime.fail_file");
    eval_fail_off_ = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage, const_i32(0),
                                              "tlc.comptime.fail_off");
}

void CodeGen::eval_fail(SourceLoc loc) {
    if (terminated() || function_ == nullptr) {
        return;
    }
    if (eval_fail_file_ != nullptr && eval_fail_off_ != nullptr) {
        builder_.CreateStore(const_i32(static_cast<int>(loc.file)), eval_fail_file_);
        builder_.CreateStore(const_i32(static_cast<int>(loc.offset)), eval_fail_off_);
    }
    llvm::Type *ret = function_->getReturnType();
    if (ret->isVoidTy()) {
        builder_.CreateRetVoid();
        return;
    }
    builder_.CreateRet(llvm::Constant::getNullValue(ret));
}

void CodeGen::trap_index(llvm::Value *index, llvm::Value *length, SourceLoc loc) {
    // A comptime func that is emitted as a runtime call still has no bounds check here.
    // The evaluator sets `eval_` and records the source location instead of panicking.
    if (!eval_ && emitting_fn_ != nullptr && emitting_fn_->is_comptime) {
        return;
    }
    if (index == nullptr || length == nullptr || terminated() || function_ == nullptr) {
        return;
    }
    llvm::Value *low_bad = builder_.CreateICmpSLT(index, const_i32(0));
    llvm::Value *high_bad = builder_.CreateICmpSGE(index, length);
    llvm::Value *bad = builder_.CreateOr(low_bad, high_bad);
    auto *fail = llvm::BasicBlock::Create(context_, "index.fail", function_);
    auto *ok = llvm::BasicBlock::Create(context_, "index.ok", function_);
    builder_.CreateCondBr(bad, fail, ok);
    builder_.SetInsertPoint(fail);
    if (eval_) {
        eval_fail(loc);
    } else {
        abort_panic(loc, "index out of range");
    }
    builder_.SetInsertPoint(ok);
}

llvm::Value *CodeGen::call_traced(SourceLoc loc, llvm::FunctionType *type, llvm::Value *callee,
                                 llvm::ArrayRef<llvm::Value *> args) {
    llvm::Value *pushed = push_trace(loc);
    llvm::CallInst *call = builder_.CreateCall(type, callee, args);
    call->setCallingConv(llvm::CallingConv::C);
    pop_trace(pushed);
    return call;
}

llvm::Value *CodeGen::heap_alloc(const Expr::New &node, llvm::Value *receiver, llvm::Value *bytes, uint64_t align) {
    if (bytes == nullptr) {
        return nullptr;
    }
    // A bare `new` uses the type named by `--allocator`. `new(&arena)` keeps that pointer.
    const bool bare = node.builtin_alloc && node.allocator == nullptr;
    if (bare && default_alloc_ == DefaultAlloc::Named) {
        receiver = default_alloc_global_;
    } else if (node.builtin_alloc) {
        if (align > 16) {
            llvm::Value *step = llvm::ConstantInt::get(i64(), align);
            llvm::Value *padded = builder_.CreateAdd(bytes, llvm::ConstantInt::get(i64(), align - 1));
            bytes = builder_.CreateMul(builder_.CreateUDiv(padded, step), step);
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("aligned_alloc", fn_type);
            return call_traced(node.loc, fn_type, fn.getCallee(), {step, bytes});
        }
        auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
        llvm::FunctionCallee fn = module_.getOrInsertFunction("malloc", fn_type);
        return call_traced(node.loc, fn_type, fn.getCallee(), {bytes});
    }
    if (receiver == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    // The trait method takes an i32 byte count.
    llvm::BasicBlock *big = llvm::BasicBlock::Create(context_, "new.big", function_);
    llvm::BasicBlock *fit = llvm::BasicBlock::Create(context_, "new.fit", function_);
    builder_.CreateCondBr(builder_.CreateICmpUGT(bytes, llvm::ConstantInt::get(i64(), 2147483647)), big, fit);
    builder_.SetInsertPoint(big);
    abort_panic(node.loc, "size does not fit");
    builder_.SetInsertPoint(fit);
    const uint32_t method_file = bare ? default_alloc_file_ : node.alloc_file;
    const std::string &method_type = bare ? default_alloc_type_ : node.alloc_name;
    llvm::Function *method = allocator_method(method_file, method_type, "malloc");
    if (method == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    if (align > 2147483647ULL) {
        error(node.loc, "not supported yet");
        return nullptr;
    }
    return call_traced(node.loc, method->getFunctionType(), method,
                      {receiver, builder_.CreateTrunc(bytes, i32()), const_i32(static_cast<int>(align))});
}

llvm::Value *CodeGen::call_malloc(const Expr::New &node, uint64_t size, uint64_t align) {
    const bool bare = node.builtin_alloc && node.allocator == nullptr;
    if (bare && default_alloc_ == DefaultAlloc::Named) {
        if (default_alloc_global_ == nullptr) {
            error(node.loc, "undeclared identifier");
            return nullptr;
        }
        if (size > 2147483647ULL || align > 2147483647ULL) {
            error(node.loc, "not supported yet");
            return nullptr;
        }
        llvm::Function *method = allocator_method(default_alloc_file_, default_alloc_type_, "malloc");
        if (method == nullptr) {
            error(node.loc, "undeclared identifier");
            return nullptr;
        }
        const uint64_t bytes = size == 0 ? 1 : size;
        return call_traced(node.loc, method->getFunctionType(), method,
                          {default_alloc_global_, const_i32(static_cast<int32_t>(bytes)),
                           const_i32(static_cast<int32_t>(align))});
    }
    if (node.builtin_alloc) {
        uint64_t bytes = size == 0 ? 1 : size;
        llvm::Value *raw = nullptr;
        if (align > 16) {
            bytes = (bytes + align - 1) / align * align;
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("aligned_alloc", fn_type);
            raw = call_traced(node.loc, fn_type, fn.getCallee(),
                             {llvm::ConstantInt::get(i64(), align), llvm::ConstantInt::get(i64(), bytes)});
        } else {
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("malloc", fn_type);
            raw = call_traced(node.loc, fn_type, fn.getCallee(), {llvm::ConstantInt::get(i64(), bytes)});
        }
        return raw;
    }
    llvm::Function *method = allocator_method(node.alloc_file, node.alloc_name, "malloc");
    if (method == nullptr || node.allocator == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    llvm::Value *receiver = allocator_receiver(*node.allocator, node.alloc_is_pointer);
    if (receiver == nullptr) {
        return nullptr;
    }
    if (size > 2147483647ULL || align > 2147483647ULL) {
        error(node.loc, "not supported yet");
        return nullptr;
    }
    auto *fn_type = method->getFunctionType();
    return call_traced(node.loc, fn_type, method,
                      {receiver, const_i32(static_cast<int32_t>(size)), const_i32(static_cast<int32_t>(align))});
}

void CodeGen::call_free(const Expr::Delete &node, llvm::Value *pointer, llvm::Value *receiver) {
    if (pointer == nullptr) {
        return;
    }
    const bool bare = node.builtin_alloc && node.allocator == nullptr;
    if (bare && default_alloc_ == DefaultAlloc::Named) {
        llvm::Function *method = allocator_method(default_alloc_file_, default_alloc_type_, "free");
        if (method == nullptr || default_alloc_global_ == nullptr) {
            error(node.loc, "undeclared identifier");
            return;
        }
        builder_.CreateCall(method->getFunctionType(), method, {default_alloc_global_, pointer});
        return;
    }
    if (node.builtin_alloc) {
        auto *fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
        llvm::FunctionCallee fn = module_.getOrInsertFunction("free", fn_type);
        builder_.CreateCall(fn_type, fn.getCallee(), {pointer});
        return;
    }
    llvm::Function *method = allocator_method(node.alloc_file, node.alloc_name, "free");
    if (method == nullptr || receiver == nullptr) {
        error(node.loc, "undeclared identifier");
        return;
    }
    builder_.CreateCall(method->getFunctionType(), method, {receiver, pointer});
}

void CodeGen::store_new_element(llvm::Value *raw, llvm::Value *index, const Lowered &low, const TypedValue &value,
                                SourceLoc loc) {
    if (raw == nullptr || index == nullptr || value.value == nullptr || low.type == nullptr || terminated()) {
        return;
    }
    llvm::Value *slot = builder_.CreateInBoundsGEP(low.type, raw, llvm::ArrayRef<llvm::Value *>{index});
    if (low.is_slice) {
        builder_.CreateStore(as_slice_value(value, loc), slot);
        return;
    }
    builder_.CreateStore(as_stored(value.value, value.depth, low.type, value.elem_unsigned), slot);
}

void CodeGen::fill_new_buffer(llvm::Value *raw, llvm::Value *count, const Lowered &low, const Expr::Init &init,
                              bool repeat) {
    if (repeat) {
        if (init.elements.size() != 1 || init.elements.front() == nullptr || count == nullptr) {
            error(init.loc, "type mismatch");
            return;
        }
        const TypedValue value = emit_rvalue(*init.elements.front());
        if (terminated() || value.value == nullptr) {
            return;
        }
        llvm::AllocaInst *step = alloca_typed(i32(), "new.i");
        builder_.CreateStore(const_i32(0), step);
        llvm::BasicBlock *cond = llvm::BasicBlock::Create(context_, "new.rep", function_);
        llvm::BasicBlock *body = llvm::BasicBlock::Create(context_, "new.rep.body", function_);
        llvm::BasicBlock *done = llvm::BasicBlock::Create(context_, "new.rep.done", function_);
        builder_.CreateBr(cond);
        builder_.SetInsertPoint(cond);
        llvm::Value *index = builder_.CreateLoad(i32(), step);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, count), body, done);
        builder_.SetInsertPoint(body);
        store_new_element(raw, index, low, value, expr_loc(*init.elements.front()));
        builder_.CreateStore(builder_.CreateAdd(index, const_i32(1)), step);
        builder_.CreateBr(cond);
        builder_.SetInsertPoint(done);
        return;
    }
    int index = 0;
    for (const auto &element : init.elements) {
        if (element == nullptr || terminated()) {
            ++index;
            continue;
        }
        const TypedValue value = emit_rvalue(*element);
        if (terminated()) {
            return;
        }
        store_new_element(raw, const_i32(index), low, value, expr_loc(*element));
        ++index;
    }
}

TypedValue CodeGen::emit_new(const Expr::New &node) {
    set_loc(node.loc);
    if (function_ == nullptr) {
        error(node.loc, "global initializer is not a constant");
        return {zero(), 0};
    }
    // The same `new(alloc)` node is emitted once per struct passed for `*Allocator`.
    if (node.alloc_is_trait) {
        node.builtin_alloc = false;
        node.alloc_file = 0;
        node.alloc_name.clear();
        if (node.allocator == nullptr ||
            !bind_trait_allocator(*node.allocator, node.builtin_alloc, node.alloc_file, node.alloc_name)) {
            if (node.allocator == nullptr) {
                error(node.loc, "type mismatch");
            }
            return {zero(), 0};
        }
    }
    const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections, false,
                              0, false);
    if (low.type == nullptr || low.elem_void || low.type->isFunctionTy()) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    const uint64_t size = module_.getDataLayout().getTypeAllocSize(low.type).getFixedValue();
    uint64_t align = module_.getDataLayout().getABITypeAlign(low.type).value();
    if (align == 0) {
        align = 1;
    }
    llvm::Value *raw = nullptr;
    llvm::Value *buffer_bytes = nullptr;
    llvm::Value *element_count = nullptr;
    llvm::BasicBlock *have_slice = nullptr;
    llvm::BasicBlock *empty_block = nullptr;
    llvm::Value *empty_slice = nullptr;
    const bool buffer = node.count != nullptr || node.count_inferred;
    int list_len = 0;
    if (node.count_inferred && node.init != nullptr) {
        if (const auto *list = std::get_if<Expr::Init>(&node.init->data)) {
            list_len = static_cast<int>(list->elements.size());
        }
    }
    if (node.count_inferred) {
        if (list_len <= 0) {
            error(node.loc, "type mismatch");
            return {zero(), 0};
        }
        uint64_t total = 1;
        if (size != 0) {
            if (static_cast<uint64_t>(list_len) > UINT64_MAX / size) {
                error(node.loc, "not supported yet");
                return {zero(), 0};
            }
            total = size * static_cast<uint64_t>(list_len);
        }
        raw = call_malloc(node, total, align);
        element_count = const_i32(list_len);
    } else if (node.count != nullptr) {
        // The allocator is written first, so its side effects run before the count.
        llvm::Value *receiver = nullptr;
        if (!node.builtin_alloc) {
            if (node.allocator == nullptr) {
                error(node.loc, "undeclared identifier");
                return {zero(), 0};
            }
            receiver = allocator_receiver(*node.allocator, node.alloc_is_pointer);
            if (receiver == nullptr || terminated()) {
                return {zero(), 0};
            }
        }
        const TypedValue count_value = emit_rvalue(*node.count);
        if (count_value.value == nullptr || terminated()) {
            return {zero(), 0};
        }
        llvm::BasicBlock *bad = llvm::BasicBlock::Create(context_, "new.count", function_);
        llvm::BasicBlock *check = llvm::BasicBlock::Create(context_, "new.zero", function_);
        llvm::BasicBlock *empty = llvm::BasicBlock::Create(context_, "new.empty", function_);
        llvm::BasicBlock *ready = llvm::BasicBlock::Create(context_, "new.ready", function_);
        element_count = count_value.value;
        auto *count_int = llvm::dyn_cast<llvm::IntegerType>(count_value.value->getType());
        if (count_int == nullptr) {
            error(node.loc, "type mismatch");
            return {zero(), 0};
        }
        llvm::Value *count_zero = llvm::ConstantInt::get(count_int, 0);
        // `new [0]T` is an empty slice and does not call malloc. A negative count panics.
        // An `i64` count is compared at its own width. The slice length stays `i32`.
        builder_.CreateCondBr(builder_.CreateICmpSLT(count_value.value, count_zero), bad, check);
        builder_.SetInsertPoint(bad);
        abort_panic(node.loc, "count must be positive");
        builder_.SetInsertPoint(check);
        builder_.CreateCondBr(builder_.CreateICmpEQ(count_value.value, count_zero), empty, ready);
        have_slice = llvm::BasicBlock::Create(context_, "new.have", function_);
        empty_block = empty;
        builder_.SetInsertPoint(empty);
        empty_slice = llvm::ConstantAggregateZero::get(slice_type());
        empty_slice = builder_.CreateInsertValue(empty_slice, null_ptr(), 0);
        empty_slice = builder_.CreateInsertValue(empty_slice, const_i32(0), 1);
        builder_.CreateBr(have_slice);
        builder_.SetInsertPoint(ready);
        if (count_int->getBitWidth() > 32) {
            llvm::BasicBlock *big = llvm::BasicBlock::Create(context_, "new.bigcount", function_);
            llvm::BasicBlock *fits = llvm::BasicBlock::Create(context_, "new.fitcount", function_);
            llvm::Value *limit = llvm::ConstantInt::get(count_int, 2147483647);
            builder_.CreateCondBr(builder_.CreateICmpSGT(count_value.value, limit), big, fits);
            builder_.SetInsertPoint(big);
            abort_panic(node.loc, "size does not fit");
            builder_.SetInsertPoint(fits);
            element_count = builder_.CreateTrunc(count_value.value, i32());
        } else if (count_int->getBitWidth() < 32) {
            element_count = builder_.CreateSExt(count_value.value, i32());
        }
        llvm::Value *wide = builder_.CreateZExt(element_count, i64());
        if (size == 0) {
            buffer_bytes = llvm::ConstantInt::get(i64(), 1);
        } else {
            const uint64_t limit = UINT64_MAX / size;
            llvm::BasicBlock *overflow = llvm::BasicBlock::Create(context_, "new.overflow", function_);
            llvm::BasicBlock *sized = llvm::BasicBlock::Create(context_, "new.sized", function_);
            llvm::Value *cap = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i64()), limit, false);
            builder_.CreateCondBr(builder_.CreateICmpUGT(wide, cap), overflow, sized);
            builder_.SetInsertPoint(overflow);
            abort_panic(node.loc, "size does not fit");
            builder_.SetInsertPoint(sized);
            buffer_bytes = builder_.CreateMul(wide, llvm::ConstantInt::get(i64(), size));
        }
        raw = heap_alloc(node, receiver, buffer_bytes, align);
    } else {
        raw = call_malloc(node, size, align);
    }
    if (raw == nullptr || terminated()) {
        return {zero(), 0};
    }
    llvm::BasicBlock *oom = llvm::BasicBlock::Create(context_, "new.oom", function_);
    llvm::BasicBlock *ok = llvm::BasicBlock::Create(context_, "new.ok", function_);
    builder_.CreateCondBr(builder_.CreateICmpEQ(raw, null_ptr()), oom, ok);
    builder_.SetInsertPoint(oom);
    abort_out_of_memory(node.loc);
    builder_.SetInsertPoint(ok);
    // A struct stores named fields, `0...`, and fields that have a value. Other fields stay unset.
    // `new i32` stores nothing.
    bool union_default = false;
    if (low.struct_id >= 0 && low.struct_id < static_cast<int>(structs_.size())) {
        const StructLayout &object = structs_[static_cast<std::size_t>(low.struct_id)];
        if (object.is_union && object.tag_enum < 0) {
            std::unordered_set<int> seen;
            union_default = struct_has_default(low.struct_id, seen);
        }
    }
    const bool struct_object = low.struct_id >= 0 && low.depth == 0 && !low.is_array && !low.is_slice &&
                               low.struct_id < static_cast<int>(structs_.size()) &&
                               (!structs_[static_cast<std::size_t>(low.struct_id)].is_union || union_default);
    if (buffer && node.init != nullptr) {
        if (const auto *list = std::get_if<Expr::Init>(&node.init->data)) {
            if (list->zero_rest && size != 0) {
                store_each_element(raw, element_count, low.type, llvm::ConstantAggregateZero::get(low.type));
            }
            fill_new_buffer(raw, element_count, low, *list, node.repeat_init);
        }
    } else if (node.init != nullptr) {
        const auto *init = std::get_if<Expr::Init>(&node.init->data);
        if (init == nullptr || init->struct_id < 0) {
            error(node.loc, "type mismatch");
        } else if (size != 0) {
            if (init->zero_rest || !struct_object) {
                builder_.CreateStore(llvm::ConstantAggregateZero::get(low.type), raw);
            } else {
                std::unordered_set<int> seen;
                store_struct_defaults(raw, low.struct_id, seen, init);
            }
            store_init_fields(init->struct_id, raw, *init);
        }
    } else if (struct_object && size != 0) {
        if (buffer) {
            std::unordered_set<int> probe;
            if (struct_has_default(low.struct_id, probe)) {
                llvm::Constant *image = default_struct(low.struct_id);
                if (image != nullptr && image->getType() == low.type) {
                    store_each_element(raw, element_count, low.type, image);
                }
            }
        } else {
            std::unordered_set<int> seen;
            store_struct_defaults(raw, low.struct_id, seen);
        }
    }
    if (terminated()) {
        return {zero(), 0};
    }
    TypedValue result;
    result.elem_char = low.elem_char;
    result.struct_id = low.struct_id;
    result.elem_void = low.elem_void;
    result.elem_bool = low.elem_bool;
    result.elem_bits = low.elem_bits;
    result.elem_unsigned = low.elem_unsigned;
    result.elem_float = low.elem_float;
    result.enum_id = low.enum_id;
    if (node.count != nullptr && !node.count_inferred) {
        // `new [n]T` is `[]T`. The length is the element count.
        llvm::Value *packed = llvm::ConstantAggregateZero::get(slice_type());
        packed = builder_.CreateInsertValue(packed, raw, 0);
        packed = builder_.CreateInsertValue(packed, element_count, 1);
        result.value = packed;
        result.is_slice = true;
        result.depth = low.depth;
        result.array_len = -1;
        result.elem_slice = low.is_slice;
        result.inner_array_len = low.is_array ? low.array_len : low.inner_array_len;
        if (have_slice != nullptr && empty_slice != nullptr && empty_block != nullptr && !terminated()) {
            llvm::BasicBlock *filled = builder_.GetInsertBlock();
            builder_.CreateBr(have_slice);
            builder_.SetInsertPoint(have_slice);
            llvm::PHINode *phi = builder_.CreatePHI(slice_type(), 2, "new.slice");
            phi->addIncoming(empty_slice, empty_block);
            phi->addIncoming(packed, filled);
            result.value = phi;
        }
        return result;
    }
    result.value = raw;
    result.depth = low.depth + 1;
    if (node.count_inferred) {
        result.ptr_to_array = true;
        result.ptr_array_len = list_len;
        // `buf[i:j]` on `new [_][M]T` steps by that row. A flat element leaves this at 0.
        result.inner_array_len = low.inner_array_len;
        result.elem_slice = low.elem_slice;
    }
    return result;
}

void CodeGen::emit_delete(const Expr::Delete &node) {
    set_loc(node.loc);
    if (node.expr == nullptr || function_ == nullptr) {
        error(node.loc, "type mismatch");
        return;
    }
    // The same `delete(alloc)` node is emitted once per struct passed for `*Allocator`.
    if (node.alloc_is_trait) {
        node.builtin_alloc = false;
        node.alloc_file = 0;
        node.alloc_name.clear();
        if (node.allocator == nullptr ||
            !bind_trait_allocator(*node.allocator, node.builtin_alloc, node.alloc_file, node.alloc_name)) {
            if (node.allocator == nullptr) {
                error(node.loc, "type mismatch");
            }
            return;
        }
    }
    // The allocator is written first, so its side effects run before the pointer.
    llvm::Value *receiver = nullptr;
    if (node.allocator != nullptr) {
        receiver = allocator_receiver(*node.allocator, node.alloc_is_pointer);
        if (receiver == nullptr || terminated()) {
            return;
        }
    } else if (!node.builtin_alloc) {
        error(node.loc, "undeclared identifier");
        return;
    }
    const TypedValue value = emit_rvalue(*node.expr);
    if (value.value == nullptr || terminated()) {
        return;
    }
    llvm::Value *pointer = value.value;
    if (value.is_slice || value.value->getType() == slice_type()) {
        pointer = builder_.CreateExtractValue(value.value, 0);
    }
    call_free(node, pointer, receiver);
}

void CodeGen::emit_builtin_panic() {
    llvm::Function *function = builtin_function("panic");
    if (function == nullptr) {
        function = module_.getFunction("tlc.panic");
    }
    if (function == nullptr || !function->empty() || function->arg_empty()) {
        return;
    }
    ensure_trace();
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
    auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
    auto *head = llvm::BasicBlock::Create(context_, "head", function);
    auto *body = llvm::BasicBlock::Create(context_, "frame", function);
    auto *die = llvm::BasicBlock::Create(context_, "abort", function);
    builder_.SetInsertPoint(entry);
    llvm::Value *message = function->getArg(0);
    llvm::Value *missing = builder_.CreateICmpEQ(message, null_ptr());
    llvm::Value *text = builder_.CreateSelect(missing, builder_.CreateGlobalString("(null)"), message);
    auto *print_type = llvm::FunctionType::get(i32(), {i32(), ptr_type()}, true);
    llvm::FunctionCallee print = module_.getOrInsertFunction("dprintf", print_type);
    builder_.CreateCall(print_type, print.getCallee(),
                        {const_i32(2), builder_.CreateGlobalString("panic: %s\n"), text});
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    builder_.CreateBr(head);
    builder_.SetInsertPoint(head);
    auto *index = builder_.CreatePHI(i32(), 2);
    index->addIncoming(depth, entry);
    builder_.CreateCondBr(builder_.CreateICmpNE(index, const_i32(0)), body, die);
    builder_.SetInsertPoint(body);
    // The newest frame is the last one pushed.
    llvm::Value *frame_index = builder_.CreateSub(index, const_i32(1));
    llvm::Value *frame = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), frame_index});
    auto load_field = [&](unsigned field_index, llvm::Type *type) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(trace_frame_, frame, {const_i32(0), const_i32(static_cast<int>(field_index))});
        return builder_.CreateLoad(type, slot);
    };
    builder_.CreateCall(print_type, print.getCallee(),
                        {const_i32(2), builder_.CreateGlobalString("%s:%d:%d: %s\n"), load_field(0, ptr_type()),
                         load_field(1, i32()), load_field(2, i32()), load_field(3, ptr_type())});
    builder_.CreateBr(head);
    index->addIncoming(frame_index, body);
    builder_.SetInsertPoint(die);
    auto *abort_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    llvm::FunctionCallee abort_fn = module_.getOrInsertFunction("abort", abort_type);
    builder_.CreateCall(abort_type, abort_fn.getCallee());
    builder_.CreateUnreachable();
    builder_.restoreIP(saved);
}


void CodeGen::prepare_default_allocator() {
    if (allocator_name_.empty()) {
        return;
    }
    const AllocatorChoice choice = parse_allocator_choice(allocator_name_);
    if (choice.kind == AllocatorChoice::Kind::Malloc) {
        default_alloc_ = DefaultAlloc::Malloc;
        return;
    }
    default_alloc_ = DefaultAlloc::Named;
    const StructLayout *found = nullptr;
    int matches = 0;
    for (const StructLayout &layout : structs_) {
        if (layout.name != choice.type) {
            continue;
        }
        const PackageNode *node = find_node(layout.file_id);
        if (node == nullptr) {
            continue;
        }
        if (!choice.package.empty() && node->qualifier != choice.package) {
            continue;
        }
        found = &layout;
        ++matches;
    }
    if (matches != 1 || found == nullptr || found->type == nullptr || !found->type->isSized()) {
        error(SourceLoc{}, matches == 0 ? "undeclared identifier" : "type mismatch");
        return;
    }
    default_alloc_file_ = found->file_id;
    default_alloc_type_ = found->name;
    // An exported variable of this type is that allocator. Its initializer runs before `main`.
    const PackageNode *owner = find_node(found->file_id);
    const VarDecl *instance = nullptr;
    uint32_t instance_file = 0;
    bool many = false;
    if (owner != nullptr && graph_ != nullptr) {
        for (const PackageNode &node : graph_->nodes) {
            if (node.package_key != owner->package_key) {
                continue;
            }
            for (const TopDecl &decl : node.unit.decls) {
                const auto *var = std::get_if<VarDecl>(&decl.data);
                if (var == nullptr || !var->exported || !var->owner.empty() || var->is_extern || var->indirections != 0 ||
                    var->is_array || var->type_name != found->name || !var->type_package.empty()) {
                    continue;
                }
                if (instance != nullptr) {
                    many = true;
                }
                instance = var;
                instance_file = node.file_id;
            }
        }
    }
    if (many) {
        error(SourceLoc{}, "type mismatch");
        return;
    }
    if (instance != nullptr) {
        const auto file = files_.find(instance_file);
        if (file != files_.end()) {
            const auto global = file->second.globals.find(instance->name);
            if (global != file->second.globals.end() && global->second.value != nullptr) {
                default_alloc_global_ = llvm::dyn_cast<llvm::GlobalVariable>(global->second.value);
            }
        }
    }
    if (default_alloc_global_ != nullptr) {
        return;
    }
    default_alloc_global_ =
        new llvm::GlobalVariable(module_, found->type, false, llvm::GlobalValue::InternalLinkage,
                                 llvm::ConstantAggregateZero::get(found->type), "tlc.allocator");
}

void CodeGen::emit_builtin_heap() {
    auto allocate = [&](llvm::Function *function, llvm::Value *bytes, llvm::Value *align) {
        llvm::Value *over = builder_.CreateICmpSGT(align, const_i32(16));
        auto *plain = llvm::BasicBlock::Create(context_, "plain", function);
        auto *aligned = llvm::BasicBlock::Create(context_, "aligned", function);
        auto *done = llvm::BasicBlock::Create(context_, "done", function);
        builder_.CreateCondBr(over, aligned, plain);
        builder_.SetInsertPoint(plain);
        auto *malloc_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
        llvm::FunctionCallee malloc_fn = module_.getOrInsertFunction("malloc", malloc_type);
        llvm::Value *simple = builder_.CreateCall(malloc_type, malloc_fn.getCallee(), {bytes});
        builder_.CreateBr(done);
        builder_.SetInsertPoint(aligned);
        llvm::Value *step = builder_.CreateZExt(align, i64());
        llvm::Value *padded = builder_.CreateAdd(bytes, builder_.CreateSub(step, llvm::ConstantInt::get(i64(), 1)));
        llvm::Value *rounded = builder_.CreateMul(builder_.CreateUDiv(padded, step), step);
        auto *aligned_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
        llvm::FunctionCallee aligned_fn = module_.getOrInsertFunction("aligned_alloc", aligned_type);
        llvm::Value *block = builder_.CreateCall(aligned_type, aligned_fn.getCallee(), {step, rounded});
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
        auto *phi = builder_.CreatePHI(ptr_type(), 2);
        phi->addIncoming(simple, plain);
        phi->addIncoming(block, aligned);
        return phi;
    };
    if (llvm::Function *function = builtin_function("Malloc.malloc");
        function != nullptr && function->empty() && function->arg_size() >= 3) {
        const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
        auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
        builder_.SetInsertPoint(entry);
        llvm::Value *bytes = builder_.CreateZExt(function->getArg(1), i64());
        builder_.CreateRet(allocate(function, bytes, function->getArg(2)));
        builder_.restoreIP(saved);
    }
    if (llvm::Function *function = builtin_function("Malloc.free");
        function != nullptr && function->empty() && function->arg_size() >= 2) {
        const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
        auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
        builder_.SetInsertPoint(entry);
        auto *free_type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
        llvm::FunctionCallee free_fn = module_.getOrInsertFunction("free", free_type);
        builder_.CreateCall(free_type, free_fn.getCallee(), {function->getArg(1)});
        builder_.CreateRetVoid();
        builder_.restoreIP(saved);
    }
    if (llvm::Function *function = builtin_function("Malloc.realloc");
        function != nullptr && function->empty() && function->arg_size() >= 4) {
        const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
        auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
        auto *fresh = llvm::BasicBlock::Create(context_, "fresh", function);
        auto *grow = llvm::BasicBlock::Create(context_, "grow", function);
        builder_.SetInsertPoint(entry);
        llvm::Value *pointer = function->getArg(1);
        llvm::Value *bytes = builder_.CreateZExt(function->getArg(2), i64());
        builder_.CreateCondBr(builder_.CreateICmpEQ(pointer, null_ptr()), fresh, grow);
        builder_.SetInsertPoint(fresh);
        llvm::Value *reserved = allocate(function, bytes, function->getArg(3));
        builder_.CreateRet(reserved);
        builder_.SetInsertPoint(grow);
        auto *realloc_type = llvm::FunctionType::get(ptr_type(), {ptr_type(), i64()}, false);
        llvm::FunctionCallee realloc_fn = module_.getOrInsertFunction("realloc", realloc_type);
        builder_.CreateRet(builder_.CreateCall(realloc_type, realloc_fn.getCallee(), {pointer, bytes}));
        builder_.restoreIP(saved);
    }
}

namespace {

enum class AtomicBuiltin { None, Load, Store, Rmw, Cmpxchg, Fence };

AtomicBuiltin atomic_builtin(const std::string &name) {
    const auto is = [&](const char *stem) {
        const std::string base(stem);
        return name == base || name.starts_with(base + ".spec");
    };
    if (is("atomic_load")) {
        return AtomicBuiltin::Load;
    }
    if (is("atomic_store")) {
        return AtomicBuiltin::Store;
    }
    if (is("atomic_rmw")) {
        return AtomicBuiltin::Rmw;
    }
    if (is("atomic_cmpxchg")) {
        return AtomicBuiltin::Cmpxchg;
    }
    if (is("fence")) {
        return AtomicBuiltin::Fence;
    }
    return AtomicBuiltin::None;
}

bool atomic_order_ok(int order, AtomicBuiltin kind) {
    if (order < 0 || order > 4) {
        return false;
    }
    if (kind == AtomicBuiltin::Load) {
        return order != 2 && order != 3;
    }
    if (kind == AtomicBuiltin::Store) {
        return order != 1 && order != 3;
    }
    return true;
}

bool cmpxchg_orders(int success, int failure) {
    if (failure == 2 || failure == 3) {
        return false;
    }
    if (success == failure || success == 4) {
        return true;
    }
    if (success == 3) {
        return failure != 4;
    }
    if (success == 1) {
        return failure == 0 || failure == 1;
    }
    if (success == 2) {
        return failure == 0 || failure == 2;
    }
    return failure == 0;
}

} // namespace

void CodeGen::yield_atomic(const Expr::Call &node, PreparedCall &prepared) {
    if (terminated() || prepared.decl == nullptr || graph_ == nullptr) {
        return;
    }
    const AtomicBuiltin kind = atomic_builtin(prepared.decl->name);
    if (kind == AtomicBuiltin::None) {
        return;
    }
    bool builtin = false;
    for (const PackageNode &file : graph_->nodes) {
        if (!is_compiler_builtin(&file)) {
            continue;
        }
        for (const TopDecl &item : file.unit.decls) {
            if (const auto *fn = std::get_if<FunctionDecl>(&item.data); fn == prepared.decl) {
                builtin = true;
                break;
            }
        }
        if (builtin) {
            break;
        }
    }
    if (!builtin) {
        return;
    }
    const auto fail = [&](const char *message) {
        error(node.loc, message);
        prepared.yielded = true;
        prepared.yield = {zero(), 0};
    };
    const auto fold_order = [&](const Expr &expr) -> std::optional<int> {
        if (const std::optional<FoldedNumber> number = fold_number(expr)) {
            if (number->is_float || number->value < -2147483648.0 || number->value > 2147483647.0) {
                return std::nullopt;
            }
            return static_cast<int>(number->value);
        }
        const auto *selector = std::get_if<Expr::Selector>(&expr.data);
        if (selector == nullptr || selector->base == nullptr) {
            return std::nullopt;
        }
        const auto *base = std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return std::nullopt;
        }
        const auto package = packages_.find(base->text);
        if (package == packages_.end()) {
            return std::nullopt;
        }
        if (const Binding *member = visible(package->second, selector->field, false); member != nullptr && member->immediate) {
            return member->const_value;
        }
        const PackageNode *owner = find_node(package->second);
        if (owner == nullptr) {
            return std::nullopt;
        }
        for (const TopDecl &item : owner->unit.decls) {
            const auto *var = std::get_if<VarDecl>(&item.data);
            if (var == nullptr || var->name != selector->field || !var->is_comptime || var->init == nullptr) {
                continue;
            }
            if (const std::optional<FoldedNumber> number = fold_number(*var->init);
                number && !number->is_float && number->value >= -2147483648.0 && number->value <= 2147483647.0) {
                return static_cast<int>(number->value);
            }
        }
        return std::nullopt;
    };
    const auto order_at = [&](std::size_t index) -> std::optional<int> {
        if (index >= node.args.size() || node.args[index] == nullptr) {
            return std::nullopt;
        }
        return fold_order(*node.args[index]);
    };
    const auto llvm_order = [](int order) -> std::optional<llvm::AtomicOrdering> {
        switch (order) {
        case 0: return llvm::AtomicOrdering::Monotonic;
        case 1: return llvm::AtomicOrdering::Acquire;
        case 2: return llvm::AtomicOrdering::Release;
        case 3: return llvm::AtomicOrdering::AcquireRelease;
        case 4: return llvm::AtomicOrdering::SequentiallyConsistent;
        default: return std::nullopt;
        }
    };
    if (kind == AtomicBuiltin::Fence) {
        std::optional<int> order = order_at(0);
        // `fence.spec.4` has already dropped the comptime argument.
        if (!order && prepared.decl != nullptr && prepared.decl->name.starts_with("fence.spec.")) {
            const std::string tail = prepared.decl->name.substr(std::string("fence.spec.").size());
            if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
                order = std::stoi(tail);
            }
        }
        const std::optional<llvm::AtomicOrdering> mapped = order ? llvm_order(*order) : std::nullopt;
        if (!order || !atomic_order_ok(*order, kind) || !mapped) {
            fail("type mismatch");
            return;
        }
        prepared.yielded = true;
        prepared.yield = {zero(), 0};
        // `defer builtin.fence(order)` runs when the block is left.
        if (recording_defer_) {
            prepared.fence_order = *order;
            prepared.function = nullptr;
            return;
        }
        // A relaxed fence has no effect. The other orders are one fence.
        if (*order != 0) {
            builder_.CreateFence(*mapped);
        }
        return;
    }
    llvm::Type *ty = nullptr;
    if ((kind == AtomicBuiltin::Load || kind == AtomicBuiltin::Rmw) && prepared.function != nullptr) {
        ty = prepared.function->getReturnType();
    } else if (prepared.args.size() > 1 && prepared.args[1] != nullptr) {
        ty = prepared.args[1]->getType();
    }
    const auto supported = [](llvm::Type *type) {
        if (type == nullptr) {
            return false;
        }
        if (type->isPointerTy()) {
            return true;
        }
        if (type->isFloatingPointTy()) {
            const unsigned bits = type->getPrimitiveSizeInBits();
            return bits == 32 || bits == 64;
        }
        if (!type->isIntegerTy()) {
            return false;
        }
        const unsigned bits = type->getIntegerBitWidth();
        // `bool` is `i1` in a register and one byte in memory.
        return bits == 1 || bits == 8 || bits == 16 || bits == 32 || bits == 64;
    };
    // A struct of 1, 2, 4, or 8 bytes, aligned to that size, is that integer.
    llvm::Type *value_struct = nullptr;
    if (ty != nullptr && ty->isStructTy() && ty->isSized()) {
        const llvm::DataLayout &layout = module_.getDataLayout();
        const uint64_t bytes = layout.getTypeStoreSize(ty).getFixedValue();
        const uint64_t aligned = layout.getABITypeAlign(ty).value();
        if ((bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8) && aligned >= bytes) {
            value_struct = ty;
            ty = llvm::IntegerType::get(context_, static_cast<unsigned>(bytes * 8));
            if (prepared.struct_id < 0) {
                for (int index = 0; index < static_cast<int>(structs_.size()); ++index) {
                    if (structs_[static_cast<std::size_t>(index)].type == value_struct) {
                        prepared.struct_id = index;
                        break;
                    }
                }
            }
        }
    }
    if (!supported(ty) || prepared.args.empty() || prepared.args[0] == nullptr) {
        fail(ty != nullptr && ty->isStructTy() ? "not supported yet" : "type mismatch");
        return;
    }
    const auto to_struct_bits = [&](llvm::Value *value) -> llvm::Value * {
        if (value_struct == nullptr || value == nullptr || value->getType() == ty) {
            return value;
        }
        llvm::AllocaInst *slot = builder_.CreateAlloca(value_struct);
        slot->setAlignment(llvm::Align(module_.getDataLayout().getTypeStoreSize(value_struct).getFixedValue()));
        if (value->getType() == value_struct) {
            builder_.CreateStore(value, slot);
        } else {
            builder_.CreateStore(llvm::ConstantAggregateZero::get(value_struct), slot);
        }
        return builder_.CreateLoad(ty, slot);
    };
    const auto from_struct_bits = [&](llvm::Value *value) -> llvm::Value * {
        if (value_struct == nullptr || value == nullptr) {
            return value;
        }
        llvm::AllocaInst *slot = builder_.CreateAlloca(ty);
        slot->setAlignment(llvm::Align(module_.getDataLayout().getTypeStoreSize(ty).getFixedValue()));
        builder_.CreateStore(value, slot);
        return builder_.CreateLoad(value_struct, slot);
    };
    // An atomic `i1` is not a byte. `bool` occupies one byte, so the access is `i8`.
    const bool bool_byte = ty->isIntegerTy(1);
    llvm::Type *access = bool_byte ? i8() : ty;
    llvm::Type *bit_ty = nullptr;
    if (ty->isFloatingPointTy()) {
        bit_ty = ty->getPrimitiveSizeInBits() == 64 ? static_cast<llvm::Type *>(i64()) : i32();
    }
    const auto as_byte = [&](llvm::Value *value) {
        if (!bool_byte) {
            return value;
        }
        if (!value->getType()->isIntegerTy(1)) {
            value = builder_.CreateTrunc(value, i1());
        }
        return builder_.CreateZExt(value, i8());
    };
    const auto from_byte = [&](llvm::Value *value) {
        if (!bool_byte || value->getType()->isIntegerTy(1)) {
            return value;
        }
        return builder_.CreateTrunc(value, i1());
    };
    const auto as_bits = [&](llvm::Value *value) {
        return builder_.CreateBitCast(value, bit_ty);
    };
    const auto from_bits = [&](llvm::Value *value) {
        return builder_.CreateBitCast(value, ty);
    };
    const uint64_t bytes = module_.getDataLayout().getTypeStoreSize(access).getFixedValue();
    const llvm::Align align(bytes == 0 ? 1 : bytes);
    llvm::Value *pointer = prepared.args[0];
    const auto scalar = [&](llvm::Value *inst) {
        if (prepared.depth == 0 && prepared.elem_bool) {
            prepared.yield = {builder_.CreateZExt(inst, i32()), 0, false, -1, false, false, true};
            return;
        }
        if (prepared.depth == 0 && (prepared.elem_char || prepared.elem_bits == 8 || prepared.elem_bits == 16)) {
            const bool zero_extend = prepared.elem_unsigned || prepared.elem_char;
            llvm::Value *wide = zero_extend ? builder_.CreateZExt(inst, i32()) : builder_.CreateSExt(inst, i32());
            prepared.yield = {wide, 0};
            return;
        }
        TypedValue value{inst, prepared.depth, prepared.elem_char, prepared.struct_id, false, prepared.elem_void,
                         prepared.elem_bool, prepared.elem_bits, prepared.elem_unsigned};
        value.elem_float = prepared.elem_float;
        value.enum_id = prepared.enum_id;
        prepared.yield = std::move(value);
    };
    if (kind == AtomicBuiltin::Load) {
        const std::optional<int> order = order_at(1);
        const std::optional<llvm::AtomicOrdering> mapped = order ? llvm_order(*order) : std::nullopt;
        if (!order || !atomic_order_ok(*order, kind) || !mapped) {
            fail("type mismatch");
            return;
        }
        llvm::LoadInst *load = builder_.CreateAlignedLoad(access, pointer, align);
        load->setAtomic(*mapped);
        llvm::Value *result = load;
        if (bool_byte) {
            result = builder_.CreateTrunc(load, i1());
        } else if (value_struct != nullptr) {
            result = from_struct_bits(load);
        }
        prepared.yielded = true;
        scalar(result);
        return;
    }
    if (kind == AtomicBuiltin::Store) {
        const std::optional<int> order = order_at(2);
        const std::optional<llvm::AtomicOrdering> mapped = order ? llvm_order(*order) : std::nullopt;
        if (!order || !atomic_order_ok(*order, kind) || !mapped || prepared.args.size() < 2 || prepared.args[1] == nullptr) {
            fail("type mismatch");
            return;
        }
        llvm::Value *stored = prepared.args[1];
        if (value_struct != nullptr) {
            stored = to_struct_bits(stored);
        }
        if (bool_byte && stored->getType() != i8()) {
            if (!stored->getType()->isIntegerTy(1)) {
                stored = builder_.CreateTrunc(stored, i1());
            }
            stored = builder_.CreateZExt(stored, i8());
        }
        llvm::StoreInst *store = builder_.CreateAlignedStore(stored, pointer, align);
        store->setAtomic(*mapped);
        prepared.yielded = true;
        prepared.yield = {zero(), 0};
        return;
    }
    if (kind == AtomicBuiltin::Rmw) {
        const std::optional<int> op = order_at(1);
        const std::optional<int> order = order_at(3);
        const std::optional<llvm::AtomicOrdering> mapped = order ? llvm_order(*order) : std::nullopt;
        if (!op || *op < 0 || *op > 5 || !order || !atomic_order_ok(*order, kind) || !mapped || prepared.args.size() < 3 ||
            prepared.args[2] == nullptr) {
            fail("type mismatch");
            return;
        }
        if ((ty->isPointerTy() || value_struct != nullptr) && *op != 0) {
            fail("not supported yet");
            return;
        }
        llvm::AtomicRMWInst::BinOp bin = llvm::AtomicRMWInst::Xchg;
        switch (*op) {
        case 0: bin = llvm::AtomicRMWInst::Xchg; break;
        case 1: bin = ty->isFloatingPointTy() ? llvm::AtomicRMWInst::FAdd : llvm::AtomicRMWInst::Add; break;
        case 2: bin = ty->isFloatingPointTy() ? llvm::AtomicRMWInst::FSub : llvm::AtomicRMWInst::Sub; break;
        case 3: bin = llvm::AtomicRMWInst::And; break;
        case 4: bin = llvm::AtomicRMWInst::Or; break;
        case 5: bin = llvm::AtomicRMWInst::Xor; break;
        default: break;
        }
        llvm::Value *operand = prepared.args[2];
        // `and`, `or`, and `xor` on a float use the bits. Add and sub are floating operations.
        const bool float_bits = ty->isFloatingPointTy() && *op >= 3;
        if (bool_byte) {
            operand = as_byte(operand);
        } else if (float_bits) {
            operand = as_bits(operand);
        } else if (value_struct != nullptr) {
            operand = to_struct_bits(operand);
        }
        llvm::Value *previous = builder_.CreateAtomicRMW(bin, pointer, operand, align, *mapped);
        if (bool_byte) {
            previous = from_byte(previous);
        } else if (float_bits) {
            previous = from_bits(previous);
        } else if (value_struct != nullptr) {
            previous = from_struct_bits(previous);
        }
        prepared.yielded = true;
        scalar(previous);
        return;
    }
    const std::optional<int> success = order_at(3);
    const std::optional<int> failure = order_at(4);
    const std::optional<llvm::AtomicOrdering> success_order = success ? llvm_order(*success) : std::nullopt;
    const std::optional<llvm::AtomicOrdering> failure_order = failure ? llvm_order(*failure) : std::nullopt;
    if (!success || !failure || !success_order || !failure_order || !atomic_order_ok(*success, AtomicBuiltin::Rmw) ||
        !atomic_order_ok(*failure, AtomicBuiltin::Rmw) || !cmpxchg_orders(*success, *failure) || prepared.args.size() < 3 ||
        prepared.args[1] == nullptr || prepared.args[2] == nullptr || prepared.decl->results.size() != 2) {
        fail("type mismatch");
        return;
    }
    llvm::Value *expected = prepared.args[1];
    llvm::Value *desired = prepared.args[2];
    if (bool_byte) {
        expected = as_byte(expected);
        desired = as_byte(desired);
    } else if (ty->isFloatingPointTy()) {
        // Compare-exchange matches the bits, not a floating equality.
        expected = as_bits(expected);
        desired = as_bits(desired);
    } else if (value_struct != nullptr) {
        expected = to_struct_bits(expected);
        desired = to_struct_bits(desired);
    }
    llvm::Value *pair = builder_.CreateAtomicCmpXchg(pointer, expected, desired, align, *success_order, *failure_order);
    llvm::Type *ret = aggregate_return(*prepared.decl, current_file_);
    auto *pair_ty = llvm::dyn_cast<llvm::StructType>(ret);
    llvm::Value *previous = builder_.CreateExtractValue(pair, 0);
    llvm::Value *flag = builder_.CreateExtractValue(pair, 1);
    if (bool_byte) {
        previous = from_byte(previous);
    } else if (ty->isFloatingPointTy()) {
        previous = from_bits(previous);
    } else if (value_struct != nullptr) {
        previous = from_struct_bits(previous);
    }
    if (pair_ty == nullptr || pair_ty->getNumElements() != 2 || previous->getType() != pair_ty->getElementType(0) ||
        flag->getType() != pair_ty->getElementType(1)) {
        fail("type mismatch");
        return;
    }
    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(pair_ty);
    aggregate = builder_.CreateInsertValue(aggregate, previous, 0);
    aggregate = builder_.CreateInsertValue(aggregate, flag, 1);
    prepared.yielded = true;
    prepared.yield = {aggregate, 0};
    prepared.yield.is_tuple = true;
    prepared.yield.tuple_parts = prepared.decl->results;
}

} // namespace tlc
