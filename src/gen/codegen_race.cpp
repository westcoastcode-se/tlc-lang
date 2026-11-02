// A debug build reports a race on a shared global.
// spawn and join order the accesses around them. A mutex does not.
#include "gen/gen.hpp"

#include "llvm/IR/Instructions.h"

#include <vector>

namespace tlc {
namespace {

constexpr int kSlots = 32;
constexpr int kCells = 64;
constexpr int kFrames = 8;

bool runtime_fn(llvm::StringRef name) { return name.starts_with("tlc.race"); }

bool watched(const llvm::GlobalVariable *global) {
    if (global == nullptr || global->isConstant() || global->isThreadLocal()) {
        return false;
    }
    const llvm::StringRef name = global->getName();
    return !name.starts_with("tlc.") && !name.starts_with("debug.") && !name.starts_with("thread.") &&
           !name.starts_with("builtin.") && !name.starts_with("llvm.");
}

const llvm::GlobalVariable *global_base(const llvm::Value *value) {
    while (value != nullptr) {
        if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(value)) {
            return global;
        }
        if (const auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(value)) {
            value = gep->getPointerOperand();
            continue;
        }
        if (const auto *cast = llvm::dyn_cast<llvm::BitCastInst>(value)) {
            value = cast->getOperand(0);
            continue;
        }
        if (const auto *cast = llvm::dyn_cast<llvm::AddrSpaceCastInst>(value)) {
            value = cast->getOperand(0);
            continue;
        }
        const auto *expr = llvm::dyn_cast<llvm::ConstantExpr>(value);
        if (expr != nullptr && (expr->isCast() || expr->getOpcode() == llvm::Instruction::GetElementPtr)) {
            value = expr->getOperand(0);
            continue;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

void CodeGen::emit_races() {
    if (!checks_ || eval_) {
        return;
    }
    ensure_trace();
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();

    auto *vc_ty = llvm::ArrayType::get(i32(), kSlots);
    auto *frame_array = llvm::ArrayType::get(trace_frame_, kFrames);
    auto *addr_array = llvm::ArrayType::get(ptr_type(), kCells);
    auto *i32_cells = llvm::ArrayType::get(i32(), kCells);
    auto *i8_cells = llvm::ArrayType::get(i8(), kCells);
    auto *vc_table = llvm::ArrayType::get(vc_ty, kCells);
    auto *frame_table = llvm::ArrayType::get(frame_array, kCells);
    auto *done_table = llvm::ArrayType::get(vc_ty, kSlots);
    auto *tid_array = llvm::ArrayType::get(i64(), kSlots);
    auto *exit_array = llvm::ArrayType::get(i8(), kSlots);
    auto zero_of = [](llvm::Type *type) { return llvm::ConstantAggregateZero::get(type); };
    auto make_global = [&](llvm::Type *type, const char *name, bool tls) {
        auto *global = new llvm::GlobalVariable(module_, type, false, llvm::GlobalValue::InternalLinkage, zero_of(type),
                                                 name);
        if (tls) {
            global->setThreadLocalMode(llvm::GlobalValue::LocalExecTLSModel);
        }
        return global;
    };
    auto *slot_tls = make_global(i32(), "tlc.race.slot", true);
    auto *vc_tls = make_global(vc_ty, "tlc.race.vc", true);
    auto *mu = make_global(ptr_type(), "tlc.race.mu", false);
    auto *next_slot = make_global(i32(), "tlc.race.next", false);
    auto *ncells = make_global(i32(), "tlc.race.ncells", false);
    auto *addrs = make_global(addr_array, "tlc.race.addr", false);
    auto *write_slot = make_global(i32_cells, "tlc.race.write_slot", false);
    auto *read_slot = make_global(i32_cells, "tlc.race.read_slot", false);
    auto *write_n = make_global(i32_cells, "tlc.race.write_n", false);
    auto *read_n = make_global(i32_cells, "tlc.race.read_n", false);
    auto *write_vc = make_global(vc_table, "tlc.race.write_vc", false);
    auto *read_vc = make_global(vc_table, "tlc.race.read_vc", false);
    auto *write_fr = make_global(frame_table, "tlc.race.write_fr", false);
    auto *read_fr = make_global(frame_table, "tlc.race.read_fr", false);
    auto *reported = make_global(i8_cells, "tlc.race.reported", false);
    auto *tids = make_global(tid_array, "tlc.race.tid", false);
    auto *done_vc = make_global(done_table, "tlc.race.done", false);
    auto *exited = make_global(exit_array, "tlc.race.exited", false);
    auto *boot_ty = llvm::StructType::create(context_, {ptr_type(), ptr_type(), vc_ty}, "tlc.race.boot");

    auto *print_ty = llvm::FunctionType::get(i32(), {i32(), ptr_type()}, true);
    llvm::FunctionCallee dprintf_fn = module_.getOrInsertFunction("dprintf", print_ty);
    auto *mutex_init_ty = llvm::FunctionType::get(i32(), {ptr_type(), ptr_type()}, false);
    auto *mutex_one_ty = llvm::FunctionType::get(i32(), {ptr_type()}, false);
    llvm::FunctionCallee mutex_init = module_.getOrInsertFunction("pthread_mutex_init", mutex_init_ty);
    llvm::FunctionCallee mutex_lock = module_.getOrInsertFunction("pthread_mutex_lock", mutex_one_ty);
    llvm::FunctionCallee mutex_unlock = module_.getOrInsertFunction("pthread_mutex_unlock", mutex_one_ty);
    auto *malloc_ty = llvm::FunctionType::get(ptr_type(), {i64()}, false);
    auto *free_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
    llvm::FunctionCallee malloc_fn = module_.getOrInsertFunction("malloc", malloc_ty);
    llvm::FunctionCallee free_fn = module_.getOrInsertFunction("free", free_ty);
    auto *self_ty = llvm::FunctionType::get(i64(), false);
    llvm::FunctionCallee self_fn = module_.getOrInsertFunction("pthread_self", self_ty);
    auto *create_ty = llvm::FunctionType::get(i32(), {ptr_type(), ptr_type(), ptr_type(), ptr_type()}, false);
    llvm::FunctionCallee create_fn = module_.getOrInsertFunction("pthread_create", create_ty);
    auto *join_ty = llvm::FunctionType::get(i32(), {i64(), ptr_type()}, false);
    llvm::FunctionCallee join_fn = module_.getOrInsertFunction("pthread_join", join_ty);

    auto declare = [&](const char *name, llvm::ArrayRef<llvm::Type *> args, llvm::Type *result) {
        auto *type = llvm::FunctionType::get(result == nullptr ? llvm::Type::getVoidTy(context_) : result, args, false);
        auto *fn = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, name, module_);
        fn->setCallingConv(llvm::CallingConv::C);
        return fn;
    };
    llvm::Function *init_fn = declare("tlc.race.init", {}, nullptr);
    llvm::Function *lock_fn = declare("tlc.race.lock", {}, nullptr);
    llvm::Function *unlock_fn = declare("tlc.race.unlock", {}, nullptr);
    llvm::Function *ensure_fn = declare("tlc.race.ensure", {}, i32());
    llvm::Function *hb_fn = declare("tlc.race.hb", {ptr_type(), ptr_type()}, i1());
    llvm::Function *trace_fn = declare("tlc.race.trace", {ptr_type(), ptr_type()}, nullptr);
    llvm::Function *frames_fn = declare("tlc.race.frames", {ptr_type(), i32()}, nullptr);
    llvm::Function *show_fn = declare("tlc.race.show", {i32(), ptr_type(), i32(), i32(), ptr_type(), i32()}, nullptr);
    llvm::Function *tramp_fn = declare("tlc.race.tramp", {ptr_type()}, ptr_type());
    llvm::Function *spawn_fn = declare("tlc.race.spawn", {ptr_type(), ptr_type(), ptr_type(), ptr_type()}, i32());
    llvm::Function *join_wrap = declare("tlc.race.join", {i64(), ptr_type()}, i32());
    llvm::Function *access_fn = declare("tlc.race.access", {ptr_type(), i64(), i32()}, nullptr);

    auto call0 = [&](llvm::Function *fn) { builder_.CreateCall(fn->getFunctionType(), fn, {}); };
    auto gep_at = [&](llvm::Type *type, llvm::Value *base, llvm::ArrayRef<llvm::Value *> index) {
        return builder_.CreateInBoundsGEP(type, base, index);
    };
    const uint64_t frame_bytes = module_.getDataLayout().getTypeAllocSize(trace_frame_);
    const uint64_t vc_bytes = module_.getDataLayout().getTypeAllocSize(vc_ty);
    const uint64_t boot_bytes = module_.getDataLayout().getTypeAllocSize(boot_ty);
    auto copy_bytes = [&](llvm::Value *dest, llvm::Value *src, uint64_t bytes) {
        builder_.CreateMemCpy(dest, llvm::Align(8), src, llvm::Align(8), llvm::ConstantInt::get(i64(), bytes));
    };

    {
        auto *entry = llvm::BasicBlock::Create(context_, "entry", init_fn);
        builder_.SetInsertPoint(entry);
        llvm::Value *block =
            builder_.CreateCall(malloc_ty, malloc_fn.getCallee(), {llvm::ConstantInt::get(i64(), 128)});
        builder_.CreateStore(block, mu);
        builder_.CreateCall(mutex_init_ty, mutex_init.getCallee(), {block, null_ptr()});
        builder_.CreateRetVoid();
        auto *ctor_ty = llvm::StructType::get(context_, {i32(), ptr_type(), ptr_type()});
        auto *array_ty = llvm::ArrayType::get(ctor_ty, 1);
        llvm::Constant *fields[3] = {const_i32(0), init_fn, llvm::ConstantPointerNull::get(ptr_type())};
        llvm::Constant *ctor = llvm::ConstantStruct::get(ctor_ty, fields);
        std::vector<llvm::Constant *> ctors{ctor};
        if (auto *existing = module_.getNamedGlobal("llvm.global_ctors")) {
            if (const auto *array = llvm::dyn_cast<llvm::ConstantArray>(existing->getInitializer())) {
                for (unsigned i = 0; i < array->getNumOperands(); ++i) {
                    ctors.push_back(array->getOperand(i));
                }
            }
            existing->eraseFromParent();
        }
        auto *merged = llvm::ArrayType::get(ctor_ty, ctors.size());
        new llvm::GlobalVariable(module_, merged, false, llvm::GlobalValue::AppendingLinkage,
                                 llvm::ConstantArray::get(merged, ctors), "llvm.global_ctors");
        (void)array_ty;
    }
    {
        auto *entry = llvm::BasicBlock::Create(context_, "entry", lock_fn);
        builder_.SetInsertPoint(entry);
        builder_.CreateCall(mutex_one_ty, mutex_lock.getCallee(), {builder_.CreateLoad(ptr_type(), mu)});
        builder_.CreateRetVoid();
    }
    {
        auto *entry = llvm::BasicBlock::Create(context_, "entry", unlock_fn);
        builder_.SetInsertPoint(entry);
        builder_.CreateCall(mutex_one_ty, mutex_unlock.getCallee(), {builder_.CreateLoad(ptr_type(), mu)});
        builder_.CreateRetVoid();
    }
    {
        auto *entry = llvm::BasicBlock::Create(context_, "entry", ensure_fn);
        auto *fresh = llvm::BasicBlock::Create(context_, "fresh", ensure_fn);
        auto *full = llvm::BasicBlock::Create(context_, "full", ensure_fn);
        auto *keep = llvm::BasicBlock::Create(context_, "keep", ensure_fn);
        auto *stamp = llvm::BasicBlock::Create(context_, "stamp", ensure_fn);
        auto *done = llvm::BasicBlock::Create(context_, "done", ensure_fn);
        builder_.SetInsertPoint(entry);
        llvm::Value *existing = builder_.CreateLoad(i32(), slot_tls);
        builder_.CreateCondBr(builder_.CreateICmpEQ(existing, const_i32(0)), fresh, keep);
        builder_.SetInsertPoint(fresh);
        llvm::Value *assigned = builder_.CreateAdd(builder_.CreateLoad(i32(), next_slot), const_i32(1));
        builder_.CreateStore(assigned, next_slot);
        builder_.CreateCondBr(builder_.CreateICmpSGE(assigned, const_i32(kSlots)), full, keep);
        builder_.SetInsertPoint(full);
        builder_.CreateStore(const_i32(kSlots - 1), next_slot);
        builder_.CreateBr(keep);
        builder_.SetInsertPoint(keep);
        auto *slot = builder_.CreatePHI(i32(), 3);
        slot->addIncoming(existing, entry);
        slot->addIncoming(assigned, fresh);
        slot->addIncoming(const_i32(kSlots - 1), full);
        auto *is_new = builder_.CreatePHI(i1(), 3);
        is_new->addIncoming(llvm::ConstantInt::getFalse(context_), entry);
        is_new->addIncoming(llvm::ConstantInt::getTrue(context_), fresh);
        is_new->addIncoming(llvm::ConstantInt::getTrue(context_), full);
        builder_.CreateStore(slot, slot_tls);
        builder_.CreateCondBr(is_new, stamp, done);
        builder_.SetInsertPoint(stamp);
        builder_.CreateStore(const_i32(1), gep_at(vc_ty, vc_tls, {const_i32(0), slot}));
        builder_.CreateStore(builder_.CreateCall(self_ty, self_fn.getCallee(), {}),
                             gep_at(tid_array, tids, {const_i32(0), slot}));
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
        builder_.CreateRet(slot);
    }
    {
        llvm::Argument *earlier = hb_fn->getArg(0);
        llvm::Argument *later = hb_fn->getArg(1);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", hb_fn);
        auto *head = llvm::BasicBlock::Create(context_, "head", hb_fn);
        auto *body = llvm::BasicBlock::Create(context_, "body", hb_fn);
        auto *step = llvm::BasicBlock::Create(context_, "step", hb_fn);
        auto *yes = llvm::BasicBlock::Create(context_, "yes", hb_fn);
        auto *no = llvm::BasicBlock::Create(context_, "no", hb_fn);
        builder_.SetInsertPoint(entry);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        auto *index = builder_.CreatePHI(i32(), 2);
        index->addIncoming(const_i32(0), entry);
        builder_.CreateCondBr(builder_.CreateICmpEQ(index, const_i32(kSlots)), yes, body);
        builder_.SetInsertPoint(body);
        llvm::Value *left = builder_.CreateLoad(i32(), gep_at(vc_ty, earlier, {const_i32(0), index}));
        llvm::Value *right = builder_.CreateLoad(i32(), gep_at(vc_ty, later, {const_i32(0), index}));
        builder_.CreateCondBr(builder_.CreateICmpUGT(left, right), no, step);
        builder_.SetInsertPoint(step);
        llvm::Value *next = builder_.CreateAdd(index, const_i32(1));
        builder_.CreateBr(head);
        index->addIncoming(next, step);
        builder_.SetInsertPoint(yes);
        builder_.CreateRet(llvm::ConstantInt::getTrue(context_));
        builder_.SetInsertPoint(no);
        builder_.CreateRet(llvm::ConstantInt::getFalse(context_));
    }
    {
        llvm::Argument *dest = trace_fn->getArg(0);
        llvm::Argument *count_slot = trace_fn->getArg(1);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", trace_fn);
        auto *head = llvm::BasicBlock::Create(context_, "head", trace_fn);
        auto *body = llvm::BasicBlock::Create(context_, "body", trace_fn);
        auto *done = llvm::BasicBlock::Create(context_, "done", trace_fn);
        builder_.SetInsertPoint(entry);
        llvm::Value *depth = builder_.CreateLoad(i32(), trace_depth_);
        llvm::Value *count =
            builder_.CreateSelect(builder_.CreateICmpSLT(depth, const_i32(kFrames)), depth, const_i32(kFrames));
        llvm::Value *nonneg = builder_.CreateSelect(builder_.CreateICmpSLT(count, const_i32(0)), const_i32(0), count);
        builder_.CreateStore(nonneg, count_slot);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        auto *index = builder_.CreatePHI(i32(), 2);
        index->addIncoming(const_i32(0), entry);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, nonneg), body, done);
        builder_.SetInsertPoint(body);
        llvm::Value *source_index = builder_.CreateSub(builder_.CreateSub(depth, const_i32(1)), index);
        llvm::Value *source = gep_at(trace_stack_type_, trace_stack_, {const_i32(0), source_index});
        llvm::Value *sink = gep_at(trace_frame_, dest, {index});
        copy_bytes(sink, source, frame_bytes);
        llvm::Value *next = builder_.CreateAdd(index, const_i32(1));
        builder_.CreateBr(head);
        index->addIncoming(next, body);
        builder_.SetInsertPoint(done);
        builder_.CreateRetVoid();
    }
    {
        llvm::Argument *base = frames_fn->getArg(0);
        llvm::Argument *count = frames_fn->getArg(1);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", frames_fn);
        auto *head = llvm::BasicBlock::Create(context_, "head", frames_fn);
        auto *body = llvm::BasicBlock::Create(context_, "body", frames_fn);
        auto *done = llvm::BasicBlock::Create(context_, "done", frames_fn);
        builder_.SetInsertPoint(entry);
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        auto *index = builder_.CreatePHI(i32(), 2);
        index->addIncoming(const_i32(0), entry);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, count), body, done);
        builder_.SetInsertPoint(body);
        llvm::Value *frame = gep_at(trace_frame_, base, {index});
        llvm::Value *file = builder_.CreateLoad(ptr_type(), gep_at(trace_frame_, frame, {const_i32(0), const_i32(0)}));
        llvm::Value *line = builder_.CreateLoad(i32(), gep_at(trace_frame_, frame, {const_i32(0), const_i32(1)}));
        llvm::Value *column = builder_.CreateLoad(i32(), gep_at(trace_frame_, frame, {const_i32(0), const_i32(2)}));
        llvm::Value *name = builder_.CreateLoad(ptr_type(), gep_at(trace_frame_, frame, {const_i32(0), const_i32(3)}));
        builder_.CreateCall(print_ty, dprintf_fn.getCallee(),
                            {const_i32(2), builder_.CreateGlobalString("%s:%d:%d: %s\n"), file, line, column, name});
        llvm::Value *next = builder_.CreateAdd(index, const_i32(1));
        builder_.CreateBr(head);
        index->addIncoming(next, body);
        builder_.SetInsertPoint(done);
        builder_.CreateRetVoid();
    }
    {
        auto *slot_a = show_fn->getArg(0);
        auto *frames_a = show_fn->getArg(1);
        auto *count_a = show_fn->getArg(2);
        auto *slot_b = show_fn->getArg(3);
        auto *frames_b = show_fn->getArg(4);
        auto *count_b = show_fn->getArg(5);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", show_fn);
        auto *forward = llvm::BasicBlock::Create(context_, "forward", show_fn);
        auto *backward = llvm::BasicBlock::Create(context_, "backward", show_fn);
        auto *done = llvm::BasicBlock::Create(context_, "done", show_fn);
        builder_.SetInsertPoint(entry);
        builder_.CreateCall(print_ty, dprintf_fn.getCallee(), {const_i32(2), builder_.CreateGlobalString("race\n")});
        builder_.CreateCondBr(builder_.CreateICmpSLE(slot_a, slot_b), forward, backward);
        builder_.SetInsertPoint(forward);
        builder_.CreateCall(frames_fn->getFunctionType(), frames_fn, {frames_a, count_a});
        builder_.CreateCall(frames_fn->getFunctionType(), frames_fn, {frames_b, count_b});
        builder_.CreateBr(done);
        builder_.SetInsertPoint(backward);
        builder_.CreateCall(frames_fn->getFunctionType(), frames_fn, {frames_b, count_b});
        builder_.CreateCall(frames_fn->getFunctionType(), frames_fn, {frames_a, count_a});
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
        builder_.CreateRetVoid();
    }

    auto bump = [&](llvm::Value *slot) {
        llvm::Value *elem = gep_at(vc_ty, vc_tls, {const_i32(0), slot});
        builder_.CreateStore(builder_.CreateAdd(builder_.CreateLoad(i32(), elem), const_i32(1)), elem);
    };
    auto user_fn_ty = llvm::FunctionType::get(ptr_type(), {ptr_type()}, false);

    {
        auto *thread = spawn_fn->getArg(0);
        auto *attr = spawn_fn->getArg(1);
        auto *start = spawn_fn->getArg(2);
        auto *arg = spawn_fn->getArg(3);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", spawn_fn);
        auto *ok = llvm::BasicBlock::Create(context_, "ok", spawn_fn);
        auto *fail = llvm::BasicBlock::Create(context_, "fail", spawn_fn);
        builder_.SetInsertPoint(entry);
        call0(lock_fn);
        llvm::Value *slot = builder_.CreateCall(ensure_fn->getFunctionType(), ensure_fn, {});
        bump(slot);
        llvm::Value *boot = builder_.CreateCall(malloc_ty, malloc_fn.getCallee(), {llvm::ConstantInt::get(i64(), boot_bytes)});
        builder_.CreateCondBr(builder_.CreateICmpEQ(boot, null_ptr()), fail, ok);
        builder_.SetInsertPoint(ok);
        builder_.CreateStore(start, gep_at(boot_ty, boot, {const_i32(0), const_i32(0)}));
        builder_.CreateStore(arg, gep_at(boot_ty, boot, {const_i32(0), const_i32(1)}));
        copy_bytes(gep_at(boot_ty, boot, {const_i32(0), const_i32(2)}), vc_tls, vc_bytes);
        bump(slot);
        call0(unlock_fn);
        llvm::Value *rc = builder_.CreateCall(create_ty, create_fn.getCallee(), {thread, attr, tramp_fn, boot});
        builder_.CreateRet(rc);
        builder_.SetInsertPoint(fail);
        call0(unlock_fn);
        builder_.CreateRet(const_i32(12));
    }
    {
        auto *boot = tramp_fn->getArg(0);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", tramp_fn);
        builder_.SetInsertPoint(entry);
        call0(lock_fn);
        copy_bytes(vc_tls, gep_at(boot_ty, boot, {const_i32(0), const_i32(2)}), vc_bytes);
        llvm::Value *slot = builder_.CreateCall(ensure_fn->getFunctionType(), ensure_fn, {});
        bump(slot);
        builder_.CreateStore(builder_.CreateCall(self_ty, self_fn.getCallee(), {}), gep_at(tid_array, tids, {const_i32(0), slot}));
        call0(unlock_fn);
        llvm::Value *start = builder_.CreateLoad(ptr_type(), gep_at(boot_ty, boot, {const_i32(0), const_i32(0)}));
        llvm::Value *arg = builder_.CreateLoad(ptr_type(), gep_at(boot_ty, boot, {const_i32(0), const_i32(1)}));
        llvm::Value *result = builder_.CreateCall(user_fn_ty, start, {arg});
        call0(lock_fn);
        copy_bytes(gep_at(done_table, done_vc, {const_i32(0), slot}), vc_tls, vc_bytes);
        builder_.CreateStore(llvm::ConstantInt::get(i8(), 1), gep_at(exit_array, exited, {const_i32(0), slot}));
        call0(unlock_fn);
        builder_.CreateCall(free_ty, free_fn.getCallee(), {boot});
        builder_.CreateRet(result);
    }
    {
        auto *tid = join_wrap->getArg(0);
        auto *out = join_wrap->getArg(1);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", join_wrap);
        auto *head = llvm::BasicBlock::Create(context_, "head", join_wrap);
        auto *body = llvm::BasicBlock::Create(context_, "body", join_wrap);
        auto *hit = llvm::BasicBlock::Create(context_, "hit", join_wrap);
        auto *step = llvm::BasicBlock::Create(context_, "step", join_wrap);
        auto *miss = llvm::BasicBlock::Create(context_, "miss", join_wrap);
        auto *done = llvm::BasicBlock::Create(context_, "done", join_wrap);
        builder_.SetInsertPoint(entry);
        llvm::Value *rc = builder_.CreateCall(join_ty, join_fn.getCallee(), {tid, out});
        call0(lock_fn);
        llvm::Value *mine = builder_.CreateCall(ensure_fn->getFunctionType(), ensure_fn, {});
        builder_.CreateBr(head);
        builder_.SetInsertPoint(head);
        auto *index = builder_.CreatePHI(i32(), 2);
        index->addIncoming(const_i32(1), entry);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, const_i32(kSlots)), body, miss);
        builder_.SetInsertPoint(body);
        llvm::Value *same = builder_.CreateICmpEQ(builder_.CreateLoad(i64(), gep_at(tid_array, tids, {const_i32(0), index})), tid);
        llvm::Value *gone = builder_.CreateICmpNE(builder_.CreateLoad(i8(), gep_at(exit_array, exited, {const_i32(0), index})),
                                                  llvm::ConstantInt::get(i8(), 0));
        builder_.CreateCondBr(builder_.CreateAnd(same, gone), hit, step);
        builder_.SetInsertPoint(step);
        llvm::Value *next = builder_.CreateAdd(index, const_i32(1));
        builder_.CreateBr(head);
        index->addIncoming(next, step);
        builder_.SetInsertPoint(hit);
        auto *merge = llvm::BasicBlock::Create(context_, "merge", join_wrap);
        auto *merge_step = llvm::BasicBlock::Create(context_, "merge.step", join_wrap);
        auto *merged = llvm::BasicBlock::Create(context_, "merged", join_wrap);
        builder_.CreateBr(merge);
        builder_.SetInsertPoint(merge);
        auto *part = builder_.CreatePHI(i32(), 2);
        part->addIncoming(const_i32(0), hit);
        llvm::Value *child_vc = gep_at(done_table, done_vc, {const_i32(0), index});
        llvm::Value *mine_word = builder_.CreateLoad(i32(), gep_at(vc_ty, vc_tls, {const_i32(0), part}));
        llvm::Value *child_word = builder_.CreateLoad(i32(), gep_at(vc_ty, child_vc, {const_i32(0), part}));
        llvm::Value *wider = builder_.CreateSelect(builder_.CreateICmpUGT(mine_word, child_word), mine_word, child_word);
        builder_.CreateStore(wider, gep_at(vc_ty, vc_tls, {const_i32(0), part}));
        builder_.CreateCondBr(builder_.CreateICmpEQ(part, const_i32(kSlots - 1)), merged, merge_step);
        builder_.SetInsertPoint(merge_step);
        llvm::Value *part_next = builder_.CreateAdd(part, const_i32(1));
        builder_.CreateBr(merge);
        part->addIncoming(part_next, merge_step);
        builder_.SetInsertPoint(merged);
        bump(mine);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(miss);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
        call0(unlock_fn);
        builder_.CreateRet(rc);
    }
    {
        auto *addr = access_fn->getArg(0);
        auto *writing = access_fn->getArg(2);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", access_fn);
        auto *search = llvm::BasicBlock::Create(context_, "search", access_fn);
        auto *look = llvm::BasicBlock::Create(context_, "look", access_fn);
        auto *next_cell = llvm::BasicBlock::Create(context_, "next", access_fn);
        auto *miss = llvm::BasicBlock::Create(context_, "miss", access_fn);
        auto *full = llvm::BasicBlock::Create(context_, "full", access_fn);
        auto *publish = llvm::BasicBlock::Create(context_, "publish", access_fn);
        auto *use = llvm::BasicBlock::Create(context_, "use", access_fn);
        auto *leave = llvm::BasicBlock::Create(context_, "leave", access_fn);
        builder_.SetInsertPoint(entry);
        llvm::IRBuilder<> allocas(&access_fn->getEntryBlock(), access_fn->getEntryBlock().begin());
        auto *cur_fr = allocas.CreateAlloca(frame_array);
        auto *cur_n = allocas.CreateAlloca(i32());
        auto *cell_slot = allocas.CreateAlloca(i32());
        call0(lock_fn);
        llvm::Value *slot = builder_.CreateCall(ensure_fn->getFunctionType(), ensure_fn, {});
        builder_.CreateCall(trace_fn->getFunctionType(), trace_fn,
                            {gep_at(frame_array, cur_fr, {const_i32(0), const_i32(0)}), cur_n});
        builder_.CreateBr(search);
        builder_.SetInsertPoint(search);
        auto *index = builder_.CreatePHI(i32(), 2);
        index->addIncoming(const_i32(0), entry);
        llvm::Value *limit = builder_.CreateLoad(i32(), ncells);
        builder_.CreateCondBr(builder_.CreateICmpSLT(index, limit), look, miss);
        builder_.SetInsertPoint(look);
        llvm::Value *known = builder_.CreateLoad(ptr_type(), gep_at(addr_array, addrs, {const_i32(0), index}));
        builder_.CreateCondBr(builder_.CreateICmpEQ(known, addr), use, next_cell);
        builder_.SetInsertPoint(next_cell);
        llvm::Value *follow = builder_.CreateAdd(index, const_i32(1));
        builder_.CreateBr(search);
        index->addIncoming(follow, next_cell);
        auto *check = llvm::BasicBlock::Create(context_, "check", access_fn);
        builder_.SetInsertPoint(use);
        builder_.CreateStore(index, cell_slot);
        builder_.CreateBr(check);
        builder_.SetInsertPoint(miss);
        llvm::Value *filled = builder_.CreateLoad(i32(), ncells);
        builder_.CreateCondBr(builder_.CreateICmpSGE(filled, const_i32(kCells)), full, publish);
        builder_.SetInsertPoint(full);
        builder_.CreateBr(leave);
        builder_.SetInsertPoint(publish);
        builder_.CreateStore(filled, cell_slot);
        builder_.CreateStore(builder_.CreateAdd(filled, const_i32(1)), ncells);
        builder_.CreateStore(addr, gep_at(addr_array, addrs, {const_i32(0), filled}));
        builder_.CreateBr(check);
        builder_.SetInsertPoint(check);
        llvm::Value *cell = builder_.CreateLoad(i32(), cell_slot);
        llvm::Value *cur_count = builder_.CreateLoad(i32(), cur_n);
        llvm::Value *cur_frames = gep_at(frame_array, cur_fr, {const_i32(0), const_i32(0)});
        llvm::Value *wslot = builder_.CreateLoad(i32(), gep_at(i32_cells, write_slot, {const_i32(0), cell}));
        llvm::Value *rslot = builder_.CreateLoad(i32(), gep_at(i32_cells, read_slot, {const_i32(0), cell}));
        llvm::Value *seen = builder_.CreateLoad(i8(), gep_at(i8_cells, reported, {const_i32(0), cell}));
        llvm::Value *is_write = builder_.CreateICmpNE(writing, const_i32(0));
        auto conflicts = [&](llvm::Value *other_slot, llvm::GlobalVariable *other_vc, llvm::GlobalVariable *other_fr,
                             llvm::GlobalVariable *other_n, const char *name) {
            auto *test = llvm::BasicBlock::Create(context_, name, access_fn);
            auto *report = llvm::BasicBlock::Create(context_, (std::string(name) + ".report").c_str(), access_fn);
            auto *next = llvm::BasicBlock::Create(context_, (std::string(name) + ".next").c_str(), access_fn);
            llvm::BasicBlock *from = builder_.GetInsertBlock();
            builder_.CreateBr(test);
            builder_.SetInsertPoint(test);
            llvm::Value *live = builder_.CreateICmpNE(other_slot, const_i32(0));
            llvm::Value *other = builder_.CreateICmpNE(other_slot, slot);
            llvm::Value *ordered = builder_.CreateCall(
                hb_fn->getFunctionType(), hb_fn, {gep_at(vc_table, other_vc, {const_i32(0), cell}), vc_tls});
            llvm::Value *quiet = builder_.CreateICmpNE(seen, llvm::ConstantInt::get(i8(), 0));
            llvm::Value *hit = builder_.CreateAnd(builder_.CreateAnd(live, other), builder_.CreateNot(ordered));
            builder_.CreateCondBr(builder_.CreateAnd(hit, builder_.CreateNot(quiet)), report, next);
            builder_.SetInsertPoint(report);
            builder_.CreateCall(show_fn->getFunctionType(), show_fn,
                                {slot, cur_frames, cur_count, other_slot,
                                 gep_at(frame_table, other_fr, {const_i32(0), cell, const_i32(0)}),
                                 builder_.CreateLoad(i32(), gep_at(i32_cells, other_n, {const_i32(0), cell}))});
            builder_.CreateStore(llvm::ConstantInt::get(i8(), 1), gep_at(i8_cells, reported, {const_i32(0), cell}));
            builder_.CreateBr(next);
            builder_.SetInsertPoint(next);
            (void)from;
        };
        auto *after_write = llvm::BasicBlock::Create(context_, "after.write", access_fn);
        auto *after_read = llvm::BasicBlock::Create(context_, "after.read", access_fn);
        auto *record = llvm::BasicBlock::Create(context_, "record", access_fn);
        builder_.CreateCondBr(is_write, after_write, after_read);
        builder_.SetInsertPoint(after_write);
        conflicts(wslot, write_vc, write_fr, write_n, "write");
        conflicts(rslot, read_vc, read_fr, read_n, "read");
        builder_.CreateBr(record);
        builder_.SetInsertPoint(after_read);
        conflicts(wslot, write_vc, write_fr, write_n, "writer");
        builder_.CreateBr(record);
        builder_.SetInsertPoint(record);
        auto *save_write = llvm::BasicBlock::Create(context_, "save.write", access_fn);
        auto *save_read = llvm::BasicBlock::Create(context_, "save.read", access_fn);
        builder_.CreateCondBr(is_write, save_write, save_read);
        builder_.SetInsertPoint(save_write);
        copy_bytes(gep_at(vc_table, write_vc, {const_i32(0), cell}), vc_tls, vc_bytes);
        builder_.CreateStore(slot, gep_at(i32_cells, write_slot, {const_i32(0), cell}));
        copy_bytes(gep_at(frame_table, write_fr, {const_i32(0), cell}), cur_fr, frame_bytes * kFrames);
        builder_.CreateStore(cur_count, gep_at(i32_cells, write_n, {const_i32(0), cell}));
        builder_.CreateBr(leave);
        builder_.SetInsertPoint(save_read);
        copy_bytes(gep_at(vc_table, read_vc, {const_i32(0), cell}), vc_tls, vc_bytes);
        builder_.CreateStore(slot, gep_at(i32_cells, read_slot, {const_i32(0), cell}));
        copy_bytes(gep_at(frame_table, read_fr, {const_i32(0), cell}), cur_fr, frame_bytes * kFrames);
        builder_.CreateStore(cur_count, gep_at(i32_cells, read_n, {const_i32(0), cell}));
        builder_.CreateBr(leave);
        builder_.SetInsertPoint(leave);
        call0(unlock_fn);
        builder_.CreateRetVoid();
    }

    auto *access_ty = access_fn->getFunctionType();
    std::vector<llvm::Instruction *> loads_and_stores;
    std::vector<llvm::CallInst *> creates;
    std::vector<llvm::CallInst *> joins;
    for (llvm::Function &fn : module_) {
        if (runtime_fn(fn.getName()) || fn.empty()) {
            continue;
        }
        for (llvm::BasicBlock &block : fn) {
            for (llvm::Instruction &inst : block) {
                if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
                    if (!load->isAtomic() && watched(global_base(load->getPointerOperand()))) {
                        loads_and_stores.push_back(load);
                    }
                } else if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                    if (!store->isAtomic() && watched(global_base(store->getPointerOperand()))) {
                        loads_and_stores.push_back(store);
                    }
                } else if (auto *call = llvm::dyn_cast<llvm::CallInst>(&inst)) {
                    llvm::Function *callee = call->getCalledFunction();
                    if (callee == nullptr) {
                        continue;
                    }
                    if (callee->getName() == "pthread_create") {
                        creates.push_back(call);
                    } else if (callee->getName() == "pthread_join") {
                        joins.push_back(call);
                    }
                }
            }
        }
    }
    for (llvm::Instruction *inst : loads_and_stores) {
        builder_.SetInsertPoint(inst);
        llvm::Value *address = nullptr;
        llvm::Type *stored = nullptr;
        llvm::Value *flag = const_i32(0);
        if (auto *load = llvm::dyn_cast<llvm::LoadInst>(inst)) {
            address = load->getPointerOperand();
            stored = load->getType();
        } else {
            auto *store = llvm::cast<llvm::StoreInst>(inst);
            address = store->getPointerOperand();
            stored = store->getValueOperand()->getType();
            flag = const_i32(1);
        }
        llvm::Value *size = llvm::ConstantInt::get(i64(), module_.getDataLayout().getTypeStoreSize(stored));
        builder_.CreateCall(access_ty, access_fn, {address, size, flag});
    }
    auto replace_call = [&](llvm::CallInst *call, llvm::Function *wrap) {
        builder_.SetInsertPoint(call);
        llvm::SmallVector<llvm::Value *, 4> args(call->args());
        auto *neu = builder_.CreateCall(wrap->getFunctionType(), wrap, args);
        neu->setCallingConv(llvm::CallingConv::C);
        call->replaceAllUsesWith(neu);
        call->eraseFromParent();
    };
    for (llvm::CallInst *call : creates) {
        replace_call(call, spawn_fn);
    }
    for (llvm::CallInst *call : joins) {
        replace_call(call, join_wrap);
    }

    builder_.restoreIP(saved);
}

} // namespace tlc
