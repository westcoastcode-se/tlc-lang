// Stack overflow is `panic: stack overflow` with the traced frames, in every build.
//
// `main` installs a SIGSEGV handler that runs on a 64 KiB signal stack. Each thread made with
// `pthread_create` starts in `tlc.guard.boot`, which gives the thread its own signal stack and
// records the thread's stack bounds. A fault near those bounds is an overflow. Any other fault goes
// to the handler that was installed before, or to the default action.
//
// Each function probes every page of a large frame, so a frame past the stack touches the guard
// page first instead of memory below it.
//
// Included once, from codegen_builtin.cpp.
#pragma once

#include "gen/gen.hpp"

#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace tlc {
namespace guard_abi {
// 64-bit Linux, glibc and musl alike.
constexpr int kSigsegv = 11;
constexpr int kSaSiginfo = 4;
constexpr int kSaOnstack = 0x08000000;
// `struct sigaction`: the handler, a 128-byte `sigset_t`, `int sa_flags`, then `sa_restorer`.
constexpr uint64_t kSigactionBytes = 152;
constexpr int kSaFlagsOffset = 136;
// `stack_t`: `ss_sp`, `int ss_flags`, `size_t ss_size`.
constexpr uint64_t kStackBytes = 24;
constexpr int kSsSizeOffset = 16;
constexpr int kSsDisable = 2;
// `si_addr` follows `si_signo`, `si_errno`, `si_code`, and padding.
constexpr int kSiCodeOffset = 8;
constexpr int kSiAddrOffset = 16;
// `pthread_attr_t` is 56 bytes on x86-64 and 64 on AArch64.
constexpr uint64_t kAttrBytes = 128;
constexpr uint64_t kSignalStackBytes = 65536;
// A probe lands within a page of the stack's low end. The main thread's limit can sit up to the
// kernel's 1 MiB guard gap away from where glibc reports it.
constexpr uint64_t kBelowLow = 1U << 20;
} // namespace guard_abi

bool CodeGen::stack_guard_enabled() const {
    if (eval_) {
        return false;
    }
    const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    return triple.isOSLinux() &&
           (triple.getArch() == llvm::Triple::x86_64 || triple.getArch() == llvm::Triple::aarch64);
}

llvm::Function *CodeGen::guard_function(const char *name) {
    using namespace guard_abi;
    if (guard_start_ == nullptr) {
        llvm::Type *void_ty = llvm::Type::getVoidTy(context_);
        auto make = [&](const char *symbol, llvm::Type *result, llvm::ArrayRef<llvm::Type *> args) {
            auto *type = llvm::FunctionType::get(result, args, false);
            auto *fn = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage, symbol, module_);
            fn->setCallingConv(llvm::CallingConv::C);
            return fn;
        };
        // A global past 2 GiB of `.bss` is out of reach of a PC-relative access. The runtime's own
        // globals go first, ahead of any large array the program declares.
        auto front = [&](llvm::GlobalVariable *global) {
            module_.removeGlobalVariable(global);
            module_.insertGlobalVariable(module_.global_begin(), global);
            return global;
        };
        auto bytes = [&](const char *symbol, uint64_t size, unsigned align) {
            auto *type = llvm::ArrayType::get(i8(), size);
            auto *global = new llvm::GlobalVariable(module_, type, false, llvm::GlobalValue::InternalLinkage,
                                                    llvm::ConstantAggregateZero::get(type), symbol);
            global->setAlignment(llvm::Align(align));
            return front(global);
        };
        auto tls_i64 = [&](const char *symbol) {
            auto *global = new llvm::GlobalVariable(module_, i64(), false, llvm::GlobalValue::InternalLinkage,
                                                    llvm::ConstantInt::get(i64(), 0), symbol);
            global->setThreadLocalMode(llvm::GlobalValue::LocalExecTLSModel);
            return global;
        };
        auto at = [&](llvm::Value *base, int offset) {
            return builder_.CreateConstInBoundsGEP1_64(i8(), base, static_cast<uint64_t>(offset));
        };
        auto libc = [&](const char *symbol, llvm::Type *result, llvm::ArrayRef<llvm::Type *> args) {
            return module_.getOrInsertFunction(symbol, llvm::FunctionType::get(result, args, false));
        };
        llvm::FunctionCallee sigaction_fn = libc("sigaction", i32(), {i32(), ptr_type(), ptr_type()});
        llvm::FunctionCallee sigaltstack_fn = libc("sigaltstack", i32(), {ptr_type(), ptr_type()});
        llvm::FunctionCallee self_fn = libc("pthread_self", i64(), {});
        llvm::FunctionCallee getattr_fn = libc("pthread_getattr_np", i32(), {i64(), ptr_type()});
        llvm::FunctionCallee getstack_fn = libc("pthread_attr_getstack", i32(), {ptr_type(), ptr_type(), ptr_type()});
        llvm::FunctionCallee destroy_fn = libc("pthread_attr_destroy", i32(), {ptr_type()});
        llvm::FunctionCallee create_fn =
            libc("pthread_create", i32(), {ptr_type(), ptr_type(), ptr_type(), ptr_type()});
        llvm::FunctionCallee malloc_fn = libc("malloc", ptr_type(), {i64()});
        llvm::FunctionCallee free_fn = libc("free", void_ty, {ptr_type()});
        llvm::FunctionCallee raise_fn = libc("raise", i32(), {i32()});

        llvm::GlobalVariable *old_action = bytes("tlc.guard.old", kSigactionBytes, 8);
        llvm::GlobalVariable *main_stack = bytes("tlc.guard.main_stack", kSignalStackBytes, 16);
        auto *installed = front(new llvm::GlobalVariable(module_, i1(), false, llvm::GlobalValue::InternalLinkage,
                                                         llvm::ConstantInt::getFalse(context_), "tlc.guard.installed"));
        guard_low_ = tls_i64("tlc.guard.low");
        llvm::GlobalVariable *high = tls_i64("tlc.guard.high");

        llvm::Function *panic_fn = make("tlc.guard.panic", void_ty, {ptr_type()});
        llvm::Function *handler = make("tlc.guard.handler", void_ty, {i32(), ptr_type(), ptr_type()});
        llvm::Function *thread_fn = make("tlc.guard.thread", void_ty, {ptr_type()});
        guard_start_ = make("tlc.guard.start", void_ty, {});
        // A thread that C started and that calls a tlc callback. The callback's C wrapper calls `adopt`.
        llvm::Function *adopt = make("tlc.guard.adopt", void_ty, {});
        llvm::Function *once_init = make("tlc.guard.once_init", void_ty, {});
        llvm::Function *release = make("tlc.guard.release", void_ty, {ptr_type()});
        auto *key = front(new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::InternalLinkage,
                                                   const_i32(0), "tlc.guard.key"));
        // `pthread_once_t` and `pthread_key_t` are an `int` and an `unsigned` on glibc and musl.
        auto *once = front(new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::InternalLinkage,
                                                    const_i32(0), "tlc.guard.once"));
        llvm::FunctionCallee once_fn = libc("pthread_once", i32(), {ptr_type(), ptr_type()});
        llvm::FunctionCallee key_create_fn = libc("pthread_key_create", i32(), {ptr_type(), ptr_type()});
        llvm::FunctionCallee set_specific_fn = libc("pthread_setspecific", i32(), {i32(), ptr_type()});
        llvm::Function *boot = make("tlc.guard.boot", ptr_type(), {ptr_type()});
        llvm::Function *spawn = make("tlc.guard.spawn", i32(), {ptr_type(), ptr_type(), ptr_type(), ptr_type()});
        panic_fn->addFnAttr(llvm::Attribute::NoReturn);
        fill_panic(panic_fn);

        const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
        const llvm::DebugLoc saved_loc = builder_.getCurrentDebugLocation();
        builder_.SetCurrentDebugLocation(llvm::DebugLoc());
        auto block = [&](llvm::Function *fn, const char *label) { return llvm::BasicBlock::Create(context_, label, fn); };

        // The faulting address, then this thread's bounds. A thread with no bounds chains.
        {
            builder_.SetInsertPoint(block(handler, "entry"));
            llvm::BasicBlock *check = block(handler, "check");
            llvm::BasicBlock *overflow = block(handler, "overflow");
            llvm::BasicBlock *chain = block(handler, "chain");
            llvm::BasicBlock *call = block(handler, "call");
            llvm::BasicBlock *restore = block(handler, "restore");
            llvm::BasicBlock *info_call = block(handler, "siginfo");
            llvm::BasicBlock *plain_call = block(handler, "plain");
            llvm::Value *info = handler->getArg(1);
            llvm::Value *address = builder_.CreatePtrToInt(builder_.CreateLoad(ptr_type(), at(info, kSiAddrOffset)), i64());
            llvm::Value *low = builder_.CreateLoad(i64(), guard_low_);
            llvm::Value *top = builder_.CreateLoad(i64(), high);
            // A positive `si_code` is a fault the kernel raised. `kill` and `raise` are not overflow.
            llvm::Value *code = builder_.CreateLoad(i32(), at(info, kSiCodeOffset));
            llvm::Value *faulted = builder_.CreateICmpSGT(code, const_i32(0));
            llvm::Value *bounded = builder_.CreateICmpUGT(low, llvm::ConstantInt::get(i64(), kBelowLow));
            builder_.CreateCondBr(builder_.CreateAnd(faulted, bounded), check, chain);
            builder_.SetInsertPoint(check);
            llvm::Value *floor = builder_.CreateSub(low, llvm::ConstantInt::get(i64(), kBelowLow));
            llvm::Value *inside =
                builder_.CreateAnd(builder_.CreateICmpUGE(address, floor), builder_.CreateICmpULT(address, top));
            builder_.CreateCondBr(inside, overflow, chain);
            builder_.SetInsertPoint(overflow);
            builder_.CreateCall(panic_fn, {builder_.CreateGlobalString("stack overflow")});
            builder_.CreateUnreachable();
            // SIG_DFL and SIG_IGN are 0 and 1. Restoring one and returning faults again under it.
            builder_.SetInsertPoint(chain);
            llvm::Value *previous = builder_.CreateLoad(ptr_type(), old_action);
            llvm::Value *is_default =
                builder_.CreateICmpULE(builder_.CreatePtrToInt(previous, i64()), llvm::ConstantInt::get(i64(), 1));
            builder_.CreateCondBr(is_default, restore, call);
            // A signal sent with `kill` or `raise` does not fault again. Raising it once more
            // delivers it under the restored action when the handler returns.
            builder_.SetInsertPoint(restore);
            builder_.CreateCall(sigaction_fn, {const_i32(kSigsegv), old_action, null_ptr()});
            builder_.CreateCall(raise_fn, {handler->getArg(0)});
            builder_.CreateRetVoid();
            builder_.SetInsertPoint(call);
            llvm::Value *flags = builder_.CreateLoad(i32(), at(old_action, kSaFlagsOffset));
            llvm::Value *wants_info = builder_.CreateICmpNE(builder_.CreateAnd(flags, const_i32(kSaSiginfo)), const_i32(0));
            builder_.CreateCondBr(wants_info, info_call, plain_call);
            builder_.SetInsertPoint(info_call);
            builder_.CreateCall(handler->getFunctionType(), previous,
                                {handler->getArg(0), handler->getArg(1), handler->getArg(2)});
            builder_.CreateRetVoid();
            builder_.SetInsertPoint(plain_call);
            builder_.CreateCall(llvm::FunctionType::get(void_ty, {i32()}, false), previous, {handler->getArg(0)});
            builder_.CreateRetVoid();
        }
        // The signal stack, then the bounds of the calling thread's stack.
        {
            builder_.SetInsertPoint(block(thread_fn, "entry"));
            llvm::BasicBlock *known = block(thread_fn, "known");
            llvm::BasicBlock *done = block(thread_fn, "done");
            auto *stack = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kStackBytes));
            stack->setAlignment(llvm::Align(8));
            auto *attr = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kAttrBytes));
            attr->setAlignment(llvm::Align(16));
            auto *base = builder_.CreateAlloca(ptr_type());
            auto *size = builder_.CreateAlloca(i64());
            builder_.CreateMemSet(stack, llvm::ConstantInt::get(i8(), 0), kStackBytes, llvm::Align(8));
            builder_.CreateStore(thread_fn->getArg(0), stack);
            builder_.CreateStore(llvm::ConstantInt::get(i64(), kSignalStackBytes), at(stack, kSsSizeOffset));
            builder_.CreateCall(sigaltstack_fn, {stack, null_ptr()});
            llvm::Value *self = builder_.CreateCall(self_fn, {});
            llvm::Value *rc = builder_.CreateCall(getattr_fn, {self, attr});
            builder_.CreateCondBr(builder_.CreateICmpEQ(rc, const_i32(0)), known, done);
            builder_.SetInsertPoint(known);
            builder_.CreateStore(null_ptr(), base);
            builder_.CreateStore(llvm::ConstantInt::get(i64(), 0), size);
            builder_.CreateCall(getstack_fn, {attr, base, size});
            builder_.CreateCall(destroy_fn, {attr});
            llvm::Value *low = builder_.CreatePtrToInt(builder_.CreateLoad(ptr_type(), base), i64());
            builder_.CreateStore(low, guard_low_);
            builder_.CreateStore(builder_.CreateAdd(low, builder_.CreateLoad(i64(), size)), high);
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateRetVoid();
        }
        // Once per process, from `main`. A second `main` call keeps the first handler.
        {
            builder_.SetInsertPoint(block(guard_start_, "entry"));
            llvm::BasicBlock *install = block(guard_start_, "install");
            llvm::BasicBlock *done = block(guard_start_, "done");
            builder_.CreateCondBr(builder_.CreateLoad(i1(), installed), done, install);
            builder_.SetInsertPoint(install);
            builder_.CreateStore(llvm::ConstantInt::getTrue(context_), installed);
            auto *action = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kSigactionBytes));
            action->setAlignment(llvm::Align(8));
            builder_.CreateMemSet(action, llvm::ConstantInt::get(i8(), 0), kSigactionBytes, llvm::Align(8));
            builder_.CreateStore(handler, action);
            builder_.CreateStore(const_i32(kSaSiginfo | kSaOnstack), at(action, kSaFlagsOffset));
            builder_.CreateCall(thread_fn, {main_stack});
            builder_.CreateCall(sigaction_fn, {const_i32(kSigsegv), action, old_action});
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateRetVoid();
        }
        // Once per process, from the first callback of a C thread. The key frees each thread's signal stack
        // when it exits. The handler is installed here when no `main` of tlc ran.
        {
            builder_.SetInsertPoint(block(once_init, "entry"));
            llvm::BasicBlock *install = block(once_init, "install");
            llvm::BasicBlock *done = block(once_init, "done");
            builder_.CreateCall(key_create_fn, {key, release});
            builder_.CreateCondBr(builder_.CreateLoad(i1(), installed), done, install);
            builder_.SetInsertPoint(install);
            builder_.CreateStore(llvm::ConstantInt::getTrue(context_), installed);
            auto *action = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kSigactionBytes));
            action->setAlignment(llvm::Align(8));
            builder_.CreateMemSet(action, llvm::ConstantInt::get(i8(), 0), kSigactionBytes, llvm::Align(8));
            builder_.CreateStore(handler, action);
            builder_.CreateStore(const_i32(kSaSiginfo | kSaOnstack), at(action, kSaFlagsOffset));
            builder_.CreateCall(sigaction_fn, {const_i32(kSigsegv), action, old_action});
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateRetVoid();
        }
        // A thread exits. Its signal stack is turned off and freed.
        {
            builder_.SetInsertPoint(block(release, "entry"));
            auto *off = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kStackBytes));
            off->setAlignment(llvm::Align(8));
            builder_.CreateMemSet(off, llvm::ConstantInt::get(i8(), 0), kStackBytes, llvm::Align(8));
            builder_.CreateStore(const_i32(kSsDisable), at(off, 8));
            builder_.CreateCall(sigaltstack_fn, {off, null_ptr()});
            builder_.CreateStore(llvm::ConstantInt::get(i64(), 0), guard_low_);
            builder_.CreateCall(free_fn, {release->getArg(0)});
            builder_.CreateRetVoid();
        }
        // A thread with a signal stack returns at once: `main`, a thread from `pthread_create`, or one adopted
        // before. Another gets one on the heap. Bounds that cannot be read are marked 1, so a fault chains.
        {
            builder_.SetInsertPoint(block(adopt, "entry"));
            llvm::BasicBlock *fresh = block(adopt, "fresh");
            llvm::BasicBlock *mapped = block(adopt, "mapped");
            llvm::BasicBlock *mark = block(adopt, "mark");
            llvm::BasicBlock *keep = block(adopt, "keep");
            llvm::BasicBlock *done = block(adopt, "done");
            llvm::Value *low = builder_.CreateLoad(i64(), guard_low_);
            builder_.CreateCondBr(builder_.CreateICmpNE(low, llvm::ConstantInt::get(i64(), 0)), done, fresh);
            builder_.SetInsertPoint(fresh);
            builder_.CreateCall(once_fn, {once, once_init});
            llvm::Value *memory = builder_.CreateCall(malloc_fn, {llvm::ConstantInt::get(i64(), kSignalStackBytes)});
            builder_.CreateCondBr(builder_.CreateICmpEQ(memory, null_ptr()), done, mapped);
            builder_.SetInsertPoint(mapped);
            builder_.CreateCall(thread_fn, {memory});
            llvm::Value *bounds = builder_.CreateLoad(i64(), guard_low_);
            builder_.CreateCondBr(builder_.CreateICmpEQ(bounds, llvm::ConstantInt::get(i64(), 0)), mark, keep);
            builder_.SetInsertPoint(mark);
            builder_.CreateStore(llvm::ConstantInt::get(i64(), 1), guard_low_);
            builder_.CreateBr(keep);
            builder_.SetInsertPoint(keep);
            builder_.CreateCall(set_specific_fn, {builder_.CreateLoad(i32(), key), memory});
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateRetVoid();
        }
        // The thread's first frame holds its signal stack. Leaving the frame turns that stack off.
        {
            builder_.SetInsertPoint(block(boot, "entry"));
            llvm::Value *pair = boot->getArg(0);
            llvm::Value *start = builder_.CreateLoad(ptr_type(), pair);
            llvm::Value *arg = builder_.CreateLoad(ptr_type(), at(pair, 8));
            builder_.CreateCall(free_fn, {pair});
            auto *signal_stack = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kSignalStackBytes));
            signal_stack->setAlignment(llvm::Align(16));
            builder_.CreateCall(thread_fn, {signal_stack});
            auto *start_ty = llvm::FunctionType::get(ptr_type(), {ptr_type()}, false);
            llvm::Value *result = builder_.CreateCall(start_ty, start, {arg});
            auto *off = builder_.CreateAlloca(llvm::ArrayType::get(i8(), kStackBytes));
            off->setAlignment(llvm::Align(8));
            builder_.CreateMemSet(off, llvm::ConstantInt::get(i8(), 0), kStackBytes, llvm::Align(8));
            builder_.CreateStore(const_i32(kSsDisable), at(off, 8));
            builder_.CreateCall(sigaltstack_fn, {off, null_ptr()});
            builder_.CreateStore(llvm::ConstantInt::get(i64(), 0), guard_low_);
            builder_.CreateRet(result);
        }
        // `pthread_create` with `tlc.guard.boot` in front of the start routine. With no memory for
        // the pair, the thread starts as asked and has no signal stack.
        {
            builder_.SetInsertPoint(block(spawn, "entry"));
            llvm::BasicBlock *wrap = block(spawn, "wrap");
            llvm::BasicBlock *plain = block(spawn, "plain");
            llvm::BasicBlock *failed = block(spawn, "failed");
            llvm::BasicBlock *done = block(spawn, "done");
            llvm::Value *pair = builder_.CreateCall(malloc_fn, {llvm::ConstantInt::get(i64(), 16)});
            builder_.CreateCondBr(builder_.CreateICmpEQ(pair, null_ptr()), plain, wrap);
            builder_.SetInsertPoint(plain);
            builder_.CreateRet(builder_.CreateCall(
                create_fn, {spawn->getArg(0), spawn->getArg(1), spawn->getArg(2), spawn->getArg(3)}));
            builder_.SetInsertPoint(wrap);
            builder_.CreateStore(spawn->getArg(2), pair);
            builder_.CreateStore(spawn->getArg(3), at(pair, 8));
            llvm::Value *rc = builder_.CreateCall(create_fn, {spawn->getArg(0), spawn->getArg(1), boot, pair});
            builder_.CreateCondBr(builder_.CreateICmpEQ(rc, const_i32(0)), done, failed);
            builder_.SetInsertPoint(failed);
            builder_.CreateCall(free_fn, {pair});
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateRet(rc);
        }
        builder_.restoreIP(saved);
        builder_.SetCurrentDebugLocation(saved_loc);
    }
    return module_.getFunction(name);
}

void CodeGen::emit_guard_start() {
    if (!stack_guard_enabled() || terminated()) {
        return;
    }
    llvm::Function *start = guard_function("tlc.guard.start");
    if (start != nullptr) {
        builder_.CreateCall(start, {});
    }
}

void CodeGen::apply_stack_probes() {
    if (!stack_guard_enabled()) {
        return;
    }
    // Each call to `pthread_create` goes through `tlc.guard.spawn`. That wrapper's own call stays.
    std::vector<llvm::CallInst *> creates;
    for (llvm::Function &fn : module_) {
        if (fn.isDeclaration() || fn.getName() == "tlc.guard.spawn") {
            continue;
        }
        for (llvm::BasicBlock &block : fn) {
            for (llvm::Instruction &inst : block) {
                if (auto *call = llvm::dyn_cast<llvm::CallInst>(&inst)) {
                    if (llvm::Function *callee = call->getCalledFunction();
                        callee != nullptr && callee->getName() == "pthread_create" && call->arg_size() == 4) {
                        creates.push_back(call);
                    }
                }
            }
        }
    }
    if (!creates.empty()) {
        llvm::Function *spawn = guard_function("tlc.guard.spawn");
        for (llvm::CallInst *call : creates) {
            if (spawn != nullptr && call->getFunctionType() == spawn->getFunctionType()) {
                call->setCalledFunction(spawn);
            }
        }
    }
    for (llvm::Function &fn : module_) {
        if (!fn.isDeclaration()) {
            fn.addFnAttr("probe-stack", "inline-asm");
        }
    }
}

} // namespace tlc
