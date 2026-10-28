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
    auto *buffer_type = llvm::ArrayType::get(i8(), 4096);
    trace_buffer_ = new llvm::GlobalVariable(module_, buffer_type, false, llvm::GlobalValue::InternalLinkage,
                                             llvm::ConstantAggregateZero::get(buffer_type), "tlc.trace.buf");
    // One tlc invocation is one executable. Local-exec is an %fs offset.
    // A shared library would need another model. The comptime runner is one process-local global.
    if (!eval_) {
        for (llvm::GlobalVariable *global : {trace_depth_, trace_stack_, trace_buffer_}) {
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

void CodeGen::emit_builtin_trace() {
    llvm::Function *function = builtin_function("trace");
    if (function == nullptr || !function->empty()) {
        return;
    }
    ensure_trace();
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
    auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
    auto *head = llvm::BasicBlock::Create(context_, "head", function);
    auto *body = llvm::BasicBlock::Create(context_, "body", function);
    auto *write = llvm::BasicBlock::Create(context_, "write", function);
    auto *next = llvm::BasicBlock::Create(context_, "next", function);
    auto *done = llvm::BasicBlock::Create(context_, "done", function);
    auto *buffer_type = llvm::cast<llvm::ArrayType>(trace_buffer_->getValueType());
    builder_.SetInsertPoint(entry);
    llvm::Value *first = builder_.CreateInBoundsGEP(buffer_type, trace_buffer_, {const_i32(0), const_i32(0)});
    builder_.CreateStore(llvm::ConstantInt::get(i8(), 0), first);
    llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
    builder_.CreateBr(head);
    builder_.SetInsertPoint(head);
    auto *index = builder_.CreatePHI(i32(), 2);
    auto *length = builder_.CreatePHI(i32(), 2);
    // `index` is how many frames are not written yet. The newest frame is first.
    index->addIncoming(depth, entry);
    length->addIncoming(const_i32(0), entry);
    builder_.CreateCondBr(builder_.CreateICmpNE(index, const_i32(0)), body, done);
    builder_.SetInsertPoint(body);
    llvm::Value *remain = builder_.CreateSub(const_i32(4096), length);
    builder_.CreateCondBr(builder_.CreateICmpSGT(remain, const_i32(1)), write, done);
    builder_.SetInsertPoint(write);
    llvm::Value *frame_index = builder_.CreateSub(index, const_i32(1));
    llvm::Value *frame = builder_.CreateInBoundsGEP(trace_stack_type_, trace_stack_, {const_i32(0), frame_index});
    auto load_field = [&](unsigned field_index, llvm::Type *type) {
        llvm::Value *slot =
            builder_.CreateInBoundsGEP(trace_frame_, frame, {const_i32(0), const_i32(static_cast<int>(field_index))});
        return builder_.CreateLoad(type, slot);
    };
    llvm::Value *file = load_field(0, ptr_type());
    llvm::Value *line = load_field(1, i32());
    llvm::Value *column = load_field(2, i32());
    llvm::Value *name = load_field(3, ptr_type());
    llvm::Value *last = builder_.CreateICmpEQ(frame_index, const_i32(0));
    llvm::Value *format = builder_.CreateSelect(last, builder_.CreateGlobalString("%s:%d:%d: %s"),
                                                builder_.CreateGlobalString("%s:%d:%d: %s\n"));
    llvm::Value *dest = builder_.CreateInBoundsGEP(buffer_type, trace_buffer_, {const_i32(0), length});
    auto *snprintf_type = llvm::FunctionType::get(i32(), {ptr_type(), i64(), ptr_type()}, true);
    llvm::FunctionCallee snprintf_fn = module_.getOrInsertFunction("snprintf", snprintf_type);
    llvm::Value *wrote = builder_.CreateCall(snprintf_type, snprintf_fn.getCallee(),
                                             {dest, builder_.CreateZExt(remain, i64()), format, file, line, column, name});
    llvm::Value *count = builder_.CreateSelect(builder_.CreateICmpSGT(wrote, const_i32(0)), wrote, const_i32(0));
    llvm::Value *limit = builder_.CreateSub(remain, const_i32(1));
    llvm::Value *fitted = builder_.CreateSelect(builder_.CreateICmpSLT(count, limit), count, limit);
    llvm::Value *sum = builder_.CreateAdd(length, fitted);
    llvm::Value *next_index = frame_index;
    builder_.CreateBr(next);
    builder_.SetInsertPoint(next);
    index->addIncoming(next_index, next);
    length->addIncoming(sum, next);
    builder_.CreateBr(head);
    builder_.SetInsertPoint(done);
    builder_.CreateRet(trace_buffer_);
    builder_.restoreIP(saved);
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

llvm::Value *CodeGen::heap_alloc(const Expr::New &node, llvm::Value *receiver, llvm::Value *bytes, uint64_t align) {
    if (bytes == nullptr) {
        return nullptr;
    }
    if (node.builtin_alloc) {
        if (align > 16) {
            llvm::Value *step = llvm::ConstantInt::get(i64(), align);
            llvm::Value *padded = builder_.CreateAdd(bytes, llvm::ConstantInt::get(i64(), align - 1));
            bytes = builder_.CreateMul(builder_.CreateUDiv(padded, step), step);
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("aligned_alloc", fn_type);
            return builder_.CreateCall(fn_type, fn.getCallee(), {step, bytes});
        }
        auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
        llvm::FunctionCallee fn = module_.getOrInsertFunction("malloc", fn_type);
        return builder_.CreateCall(fn_type, fn.getCallee(), {bytes});
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
    abort_now();
    builder_.SetInsertPoint(fit);
    llvm::Function *method = allocator_method(node.alloc_file, node.alloc_name, "malloc");
    if (method == nullptr) {
        error(node.loc, "undeclared identifier");
        return nullptr;
    }
    if (align > 2147483647ULL) {
        error(node.loc, "not supported yet");
        return nullptr;
    }
    return builder_.CreateCall(method->getFunctionType(), method,
                              {receiver, builder_.CreateTrunc(bytes, i32()), const_i32(static_cast<int>(align))});
}

llvm::Value *CodeGen::call_malloc(const Expr::New &node, uint64_t size, uint64_t align) {
    if (node.builtin_alloc) {
        uint64_t bytes = size == 0 ? 1 : size;
        llvm::Value *raw = nullptr;
        if (align > 16) {
            bytes = (bytes + align - 1) / align * align;
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64(), i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("aligned_alloc", fn_type);
            raw = builder_.CreateCall(fn_type, fn.getCallee(),
                                     {llvm::ConstantInt::get(i64(), align), llvm::ConstantInt::get(i64(), bytes)});
        } else {
            auto *fn_type = llvm::FunctionType::get(ptr_type(), {i64()}, false);
            llvm::FunctionCallee fn = module_.getOrInsertFunction("malloc", fn_type);
            raw = builder_.CreateCall(fn_type, fn.getCallee(), {llvm::ConstantInt::get(i64(), bytes)});
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
    return builder_.CreateCall(fn_type, method,
                              {receiver, const_i32(static_cast<int32_t>(size)), const_i32(static_cast<int32_t>(align))});
}

void CodeGen::call_free(const Expr::Delete &node, llvm::Value *pointer, llvm::Value *receiver) {
    if (pointer == nullptr) {
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
        llvm::BasicBlock *ready = llvm::BasicBlock::Create(context_, "new.ready", function_);
        element_count = count_value.value;
        builder_.CreateCondBr(builder_.CreateICmpSLE(count_value.value, const_i32(0)), bad, ready);
        builder_.SetInsertPoint(bad);
        abort_now();
        builder_.SetInsertPoint(ready);
        llvm::Value *wide = builder_.CreateZExt(count_value.value, i64());
        if (size == 0) {
            buffer_bytes = llvm::ConstantInt::get(i64(), 1);
        } else {
            const uint64_t limit = UINT64_MAX / size;
            llvm::BasicBlock *overflow = llvm::BasicBlock::Create(context_, "new.overflow", function_);
            llvm::BasicBlock *sized = llvm::BasicBlock::Create(context_, "new.sized", function_);
            llvm::Value *cap = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(i64()), limit, false);
            builder_.CreateCondBr(builder_.CreateICmpUGT(wide, cap), overflow, sized);
            builder_.SetInsertPoint(overflow);
            abort_now();
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
    const bool struct_object = low.struct_id >= 0 && low.depth == 0 && !low.is_array && !low.is_slice &&
                               low.struct_id < static_cast<int>(structs_.size()) &&
                               !structs_[static_cast<std::size_t>(low.struct_id)].is_union;
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
    result.value = raw;
    result.depth = low.depth + 1;
    result.elem_char = low.elem_char;
    result.struct_id = low.struct_id;
    result.elem_void = low.elem_void;
    result.elem_bool = low.elem_bool;
    result.elem_bits = low.elem_bits;
    result.elem_unsigned = low.elem_unsigned;
    result.elem_float = low.elem_float;
    result.enum_id = low.enum_id;
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
    call_free(node, value.value, receiver);
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
        const std::optional<int> order = order_at(0);
        const std::optional<llvm::AtomicOrdering> mapped = order ? llvm_order(*order) : std::nullopt;
        if (!order || !atomic_order_ok(*order, kind) || !mapped) {
            fail("type mismatch");
            return;
        }
        prepared.yielded = true;
        prepared.yield = {zero(), 0};
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
        if (!type->isIntegerTy()) {
            return false;
        }
        const unsigned bits = type->getIntegerBitWidth();
        return bits == 8 || bits == 16 || bits == 32 || bits == 64;
    };
    if (!supported(ty) || prepared.args.empty() || prepared.args[0] == nullptr) {
        fail(ty != nullptr && (ty->isFloatingPointTy() || ty->isStructTy()) ? "not supported yet" : "type mismatch");
        return;
    }
    const uint64_t bytes = module_.getDataLayout().getTypeStoreSize(ty).getFixedValue();
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
        llvm::LoadInst *load = builder_.CreateAlignedLoad(ty, pointer, align);
        load->setAtomic(*mapped);
        prepared.yielded = true;
        scalar(load);
        return;
    }
    if (kind == AtomicBuiltin::Store) {
        const std::optional<int> order = order_at(2);
        const std::optional<llvm::AtomicOrdering> mapped = order ? llvm_order(*order) : std::nullopt;
        if (!order || !atomic_order_ok(*order, kind) || !mapped || prepared.args.size() < 2 || prepared.args[1] == nullptr) {
            fail("type mismatch");
            return;
        }
        llvm::StoreInst *store = builder_.CreateAlignedStore(prepared.args[1], pointer, align);
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
        if (ty->isPointerTy() && *op != 0) {
            fail("not supported yet");
            return;
        }
        llvm::AtomicRMWInst::BinOp bin = llvm::AtomicRMWInst::Xchg;
        switch (*op) {
        case 0: bin = llvm::AtomicRMWInst::Xchg; break;
        case 1: bin = llvm::AtomicRMWInst::Add; break;
        case 2: bin = llvm::AtomicRMWInst::Sub; break;
        case 3: bin = llvm::AtomicRMWInst::And; break;
        case 4: bin = llvm::AtomicRMWInst::Or; break;
        case 5: bin = llvm::AtomicRMWInst::Xor; break;
        default: break;
        }
        llvm::Value *previous = builder_.CreateAtomicRMW(bin, pointer, prepared.args[2], align, *mapped);
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
    llvm::Value *pair =
        builder_.CreateAtomicCmpXchg(pointer, prepared.args[1], prepared.args[2], align, *success_order, *failure_order);
    llvm::Type *ret = aggregate_return(*prepared.decl, current_file_);
    auto *struct_ty = llvm::dyn_cast<llvm::StructType>(ret);
    llvm::Value *previous = builder_.CreateExtractValue(pair, 0);
    llvm::Value *flag = builder_.CreateExtractValue(pair, 1);
    if (struct_ty == nullptr || struct_ty->getNumElements() != 2 || previous->getType() != struct_ty->getElementType(0) ||
        flag->getType() != struct_ty->getElementType(1)) {
        fail("type mismatch");
        return;
    }
    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(struct_ty);
    aggregate = builder_.CreateInsertValue(aggregate, previous, 0);
    aggregate = builder_.CreateInsertValue(aggregate, flag, 1);
    prepared.yielded = true;
    prepared.yield = {aggregate, 0};
    prepared.yield.is_tuple = true;
    prepared.yield.tuple_parts = prepared.decl->results;
}

} // namespace tlc
