// Large struct and array values go through memory before instruction selection.
//
// Codegen moves a struct or an array as one first-class value: `load %Big`, `store %Big`, a `%Big`
// parameter or result, `insertvalue`, `extractvalue`. Instruction selection splits such a value into one
// node per scalar leaf, and a node holds at most 65535 operands. At 65535 leaves the compiler crashed,
// and at 65536 the count wrapped and the copy was silently wrong. Far below that, the split is slow.
//
// This pass rewrites each value with more than `kAggregateLeafLimit` leaves:
//   - a parameter of that type becomes a pointer to a copy, and a result becomes a pointer the caller
//     passes first. Every call, direct or through a pointer, is rewritten to match.
//   - inside a body, each such value lives in its own stack slot. A load becomes a memcpy into the slot,
//     a store a memcpy out of it, and `insertvalue` and `extractvalue` address the slot's parts.
//
// A comptime entry point has a scalar signature, so the host's call into the JIT does not change.
#pragma once

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tlc {

inline constexpr uint64_t kAggregateLeafLimit = 1024;

namespace aggregate_detail {

// A load of the copy a caller made for a big parameter. Nothing else writes that memory, so the
// loaded value can stay there.
inline constexpr const char *kCallerCopy = "tlc.caller_copy";
// A slot a rewritten call reads as its argument. Only that call reads it, after the store that fills it.
inline constexpr const char *kArgSlot = "tlc.arg_slot";
// A slot a rewritten call writes as its result. Only that call writes it.
inline constexpr const char *kResultSlot = "tlc.result_slot";

inline bool marked(const llvm::Value *value, const char *kind) {
    const auto *inst = llvm::dyn_cast<llvm::Instruction>(value);
    return inst != nullptr && inst->getMetadata(kind) != nullptr;
}

// Scalar leaves of `type`, saturated just past the limit.
inline uint64_t leaves(llvm::Type *type) {
    constexpr uint64_t cap = kAggregateLeafLimit + 1;
    if (auto *st = llvm::dyn_cast<llvm::StructType>(type)) {
        uint64_t sum = 0;
        for (llvm::Type *field : st->elements()) {
            sum += leaves(field);
            if (sum >= cap) {
                return cap;
            }
        }
        return sum;
    }
    if (auto *at = llvm::dyn_cast<llvm::ArrayType>(type)) {
        const uint64_t each = leaves(at->getElementType());
        const uint64_t count = at->getNumElements();
        if (each == 0 || count == 0) {
            return 0;
        }
        return count >= cap / each + 1 ? cap : std::min(cap, count * each);
    }
    return 1;
}

inline bool big(llvm::Type *type) {
    return (type->isStructTy() || type->isArrayTy()) && type->isSized() && leaves(type) > kAggregateLeafLimit;
}

inline bool big_signature(llvm::FunctionType *type) {
    if (big(type->getReturnType())) {
        return true;
    }
    for (llvm::Type *param : type->params()) {
        if (big(param)) {
            return true;
        }
    }
    return false;
}

// A big result becomes a leading pointer. Each big parameter becomes a pointer.
inline llvm::FunctionType *lowered_signature(llvm::FunctionType *type) {
    llvm::LLVMContext &context = type->getContext();
    llvm::Type *ptr = llvm::PointerType::getUnqual(context);
    std::vector<llvm::Type *> params;
    const bool sret = big(type->getReturnType());
    if (sret) {
        params.push_back(ptr);
    }
    for (llvm::Type *param : type->params()) {
        params.push_back(big(param) ? ptr : param);
    }
    llvm::Type *result = sret ? llvm::Type::getVoidTy(context) : type->getReturnType();
    return llvm::FunctionType::get(result, params, type->isVarArg());
}

// The old attributes moved to the new parameter positions. A pointer that replaced a value has none.
inline llvm::AttributeList lowered_attributes(llvm::LLVMContext &context, const llvm::AttributeList &old,
                                              llvm::FunctionType *type, unsigned arg_count) {
    const bool sret = big(type->getReturnType());
    std::vector<llvm::AttributeSet> params;
    if (sret) {
        params.emplace_back();
    }
    for (unsigned index = 0; index < arg_count; ++index) {
        const bool replaced = index < type->getNumParams() && big(type->getParamType(index));
        params.push_back(replaced ? llvm::AttributeSet() : old.getParamAttrs(index));
    }
    return llvm::AttributeList::get(context, old.getFnAttrs(), sret ? llvm::AttributeSet() : old.getRetAttrs(),
                                    params);
}

inline llvm::AllocaInst *entry_slot(llvm::Function &fn, llvm::Type *type, const llvm::DataLayout &layout,
                                    const llvm::Twine &name = "") {
    llvm::BasicBlock &entry = fn.getEntryBlock();
    llvm::IRBuilder<> at(&entry, entry.getFirstInsertionPt());
    llvm::AllocaInst *slot = at.CreateAlloca(type, nullptr, name);
    slot->setAlignment(layout.getPrefTypeAlign(type));
    return slot;
}

inline void rewrite_call(llvm::CallInst *call, const llvm::DataLayout &layout) {
    llvm::FunctionType *old_type = call->getFunctionType();
    llvm::FunctionType *new_type = lowered_signature(old_type);
    llvm::Function &fn = *call->getFunction();
    llvm::IRBuilder<> builder(call);
    std::vector<llvm::Value *> args;
    llvm::AllocaInst *result = nullptr;
    if (big(old_type->getReturnType())) {
        result = entry_slot(fn, old_type->getReturnType(), layout, "big.result");
        result->setMetadata(kResultSlot, llvm::MDNode::get(call->getContext(), {}));
        args.push_back(result);
    }
    for (unsigned index = 0; index < call->arg_size(); ++index) {
        llvm::Value *arg = call->getArgOperand(index);
        if (index < old_type->getNumParams() && big(arg->getType())) {
            // The store is lowered with the body, into a memcpy.
            llvm::AllocaInst *copy = entry_slot(fn, arg->getType(), layout, "big.arg");
            copy->setMetadata(kArgSlot, llvm::MDNode::get(call->getContext(), {}));
            builder.CreateStore(arg, copy);
            args.push_back(copy);
        } else {
            args.push_back(arg);
        }
    }
    llvm::CallInst *lowered = builder.CreateCall(new_type, call->getCalledOperand(), args);
    lowered->setCallingConv(call->getCallingConv());
    lowered->setAttributes(
        lowered_attributes(call->getContext(), call->getAttributes(), old_type, call->arg_size()));
    lowered->setDebugLoc(call->getDebugLoc());
    if (result != nullptr) {
        lowered->addParamAttr(0, llvm::Attribute::getWithStructRetType(call->getContext(), old_type->getReturnType()));
    }
    if (result != nullptr) {
        llvm::LoadInst *value = builder.CreateLoad(old_type->getReturnType(), result);
        call->replaceAllUsesWith(value);
    } else if (!call->getType()->isVoidTy()) {
        call->replaceAllUsesWith(lowered);
    }
    call->eraseFromParent();
}

inline void rewrite_function(llvm::Function *old_fn) {
    llvm::Module &module = *old_fn->getParent();
    llvm::FunctionType *old_type = old_fn->getFunctionType();
    llvm::FunctionType *new_type = lowered_signature(old_type);
    llvm::Function *fn =
        llvm::Function::Create(new_type, old_fn->getLinkage(), old_fn->getAddressSpace(), "", &module);
    fn->copyAttributesFrom(old_fn);
    fn->setAttributes(
        lowered_attributes(old_fn->getContext(), old_fn->getAttributes(), old_type, old_type->getNumParams()));
    fn->takeName(old_fn);
    llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 4> metadata;
    old_fn->getAllMetadata(metadata);
    old_fn->clearMetadata();
    for (const auto &[kind, node] : metadata) {
        fn->setMetadata(kind, node);
    }
    const bool sret = big(old_type->getReturnType());
    if (sret) {
        // Every caller passes a slot of its own, so nothing else can reach this memory during the call.
        fn->addParamAttr(0, llvm::Attribute::getWithStructRetType(fn->getContext(), old_type->getReturnType()));
    }
    if (!old_fn->isDeclaration()) {
        fn->splice(fn->begin(), old_fn);
        llvm::BasicBlock &entry = fn->getEntryBlock();
        llvm::IRBuilder<> at(&entry, entry.getFirstInsertionPt());
        unsigned next = sret ? 1U : 0U;
        for (llvm::Argument &old_arg : old_fn->args()) {
            llvm::Argument *arg = fn->getArg(next++);
            if (big(old_arg.getType())) {
                // The caller's copy. The load is lowered with the body.
                arg->setName(old_arg.getName() + ".ptr");
                arg->addAttr(llvm::Attribute::get(fn->getContext(), kCallerCopy));
                llvm::LoadInst *value = at.CreateLoad(old_arg.getType(), arg, old_arg.getName());
                value->setMetadata(kCallerCopy, llvm::MDNode::get(fn->getContext(), {}));
                old_arg.replaceAllUsesWith(value);
            } else {
                arg->takeName(&old_arg);
                old_arg.replaceAllUsesWith(arg);
            }
        }
        if (sret) {
            llvm::Argument *out = fn->getArg(0);
            out->setName("big.out");
            std::vector<llvm::ReturnInst *> returns;
            for (llvm::BasicBlock &block : *fn) {
                if (auto *ret = llvm::dyn_cast<llvm::ReturnInst>(block.getTerminator())) {
                    returns.push_back(ret);
                }
            }
            for (llvm::ReturnInst *ret : returns) {
                llvm::IRBuilder<> here(ret);
                if (llvm::Value *value = ret->getReturnValue()) {
                    here.CreateStore(value, out);
                }
                here.CreateRetVoid()->setDebugLoc(ret->getDebugLoc());
                ret->eraseFromParent();
            }
        }
    }
    old_fn->replaceAllUsesWith(fn);
    old_fn->eraseFromParent();
}

// Each big value in `fn` moves to a stack slot, and every use reads that slot.
inline void lower_body(llvm::Function &fn, const llvm::DataLayout &layout) {
    llvm::Module &module = *fn.getParent();
    llvm::LLVMContext &context = fn.getContext();
    auto bytes = [&](llvm::Type *type) {
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), layout.getTypeAllocSize(type));
    };
    // `store (load p), q` is one copy from `p` to `q` when the value cannot change between the two:
    //   - `p` is a call's result slot. Only that call writes it, and a later run of the call runs the
    //     load again, so the store copies the newest value.
    //   - `q` is a call's argument slot. Only that call reads it, after the store, so the copy can be
    //     made where the load is.
    //   - nothing between the two in one block writes memory. `p` and `q` may overlap, as in `g = g`,
    //     so that copy is a memmove.
    {
        enum class At { Load, Store };
        struct Pair {
            llvm::LoadInst *load;
            llvm::StoreInst *store;
            At at;
        };
        std::vector<Pair> pairs;
        for (llvm::BasicBlock &block : fn) {
            for (llvm::Instruction &inst : block) {
                auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst);
                if (load == nullptr || !big(load->getType()) || !load->hasOneUse()) {
                    continue;
                }
                auto *store = llvm::dyn_cast<llvm::StoreInst>(load->user_back());
                if (store == nullptr || store->getValueOperand() != load) {
                    continue;
                }
                if (marked(load->getPointerOperand(), kResultSlot)) {
                    pairs.push_back(Pair{load, store, At::Store});
                    continue;
                }
                if (marked(store->getPointerOperand(), kArgSlot)) {
                    pairs.push_back(Pair{load, store, At::Load});
                    continue;
                }
                if (store->getParent() != &block) {
                    continue;
                }
                bool clear = true;
                for (llvm::Instruction *step = load->getNextNode(); step != store; step = step->getNextNode()) {
                    if (step == nullptr || step->mayWriteToMemory()) {
                        clear = false;
                        break;
                    }
                }
                if (clear) {
                    pairs.push_back(Pair{load, store, At::Store});
                }
            }
        }
        for (const auto &[load, store, at] : pairs) {
            // `return f(...)`: the call writes this function's own result memory directly.
            llvm::Value *source = load->getPointerOperand();
            auto *out = llvm::dyn_cast<llvm::Argument>(store->getPointerOperand());
            if (out != nullptr && out->hasStructRetAttr() && marked(source, kResultSlot) &&
                source->getNumUses() == 2) {
                store->eraseFromParent();
                load->eraseFromParent();
                source->replaceAllUsesWith(out);
                llvm::cast<llvm::Instruction>(source)->eraseFromParent();
                continue;
            }
            llvm::IRBuilder<> builder(at == At::Load ? static_cast<llvm::Instruction *>(load) : store);
            builder.CreateMemMove(store->getPointerOperand(), store->getAlign(), load->getPointerOperand(),
                                  load->getAlign(), bytes(load->getType()), load->isVolatile() || store->isVolatile());
            store->eraseFromParent();
            load->eraseFromParent();
        }
    }
    // The caller's copy of a parameter belongs to this call. When the body's first use of it copies it
    // into the parameter's local, the local can be that copy.
    {
        llvm::BasicBlock &entry = fn.getEntryBlock();
        std::vector<llvm::MemMoveInst *> moves;
        for (llvm::Instruction &inst : entry) {
            if (auto *move = llvm::dyn_cast<llvm::MemMoveInst>(&inst)) {
                moves.push_back(move);
            }
        }
        for (llvm::MemMoveInst *move : moves) {
            auto *arg = llvm::dyn_cast<llvm::Argument>(move->getRawSource());
            auto *local = llvm::dyn_cast<llvm::AllocaInst>(move->getRawDest());
            if (arg == nullptr || local == nullptr || !arg->hasOneUse() ||
                !arg->hasAttribute(kCallerCopy) || !big(local->getAllocatedType()) ||
                local->getParent() != &entry || local->isArrayAllocation()) {
                continue;
            }
            // The local must not be touched before the copy fills it.
            bool early = false;
            for (const llvm::User *user : local->users()) {
                const auto *inst = llvm::cast<llvm::Instruction>(user);
                if (inst != move && inst->getParent() == &entry && inst->comesBefore(move)) {
                    early = true;
                    break;
                }
            }
            if (early) {
                continue;
            }
            move->eraseFromParent();
            local->replaceAllUsesWith(arg);
            local->eraseFromParent();
        }
    }
    std::vector<llvm::Instruction *> defs;
    for (llvm::BasicBlock &block : fn) {
        for (llvm::Instruction &inst : block) {
            if (big(inst.getType())) {
                defs.push_back(&inst);
            }
        }
    }
    // A store of a big constant or argument has no def in this body. It is lowered below too.
    std::vector<llvm::StoreInst *> stores;
    for (llvm::BasicBlock &block : fn) {
        for (llvm::Instruction &inst : block) {
            if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst); store != nullptr &&
                                                                    big(store->getValueOperand()->getType())) {
                stores.push_back(store);
            }
        }
    }
    if (defs.empty() && stores.empty()) {
        return;
    }
    std::unordered_map<llvm::Value *, llvm::Value *> slots;
    for (llvm::Instruction *inst : defs) {
        if (inst->getMetadata(kCallerCopy) != nullptr) {
            slots[inst] = llvm::cast<llvm::LoadInst>(inst)->getPointerOperand();
            continue;
        }
        slots[inst] = entry_slot(fn, inst->getType(), layout, "big");
    }
    // The address that holds `value`. A constant other than zero is a private global.
    auto address_of = [&](llvm::Value *value) -> llvm::Value * {
        if (auto found = slots.find(value); found != slots.end()) {
            return found->second;
        }
        if (auto *constant = llvm::dyn_cast<llvm::Constant>(value)) {
            llvm::Constant *init = llvm::isa<llvm::UndefValue>(constant) ? llvm::Constant::getNullValue(value->getType())
                                                                         : constant;
            auto *global = new llvm::GlobalVariable(module, value->getType(), true, llvm::GlobalValue::PrivateLinkage,
                                                    init, "big.const");
            global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            global->setAlignment(layout.getPrefTypeAlign(value->getType()));
            slots[value] = global;
            return global;
        }
        return nullptr;
    };
    // `dest` gets a copy of `value`. Zero is a memset.
    auto copy_into = [&](llvm::IRBuilder<> &builder, llvm::Value *dest, llvm::Align dest_align, llvm::Value *value) {
        llvm::Type *type = value->getType();
        if (auto *constant = llvm::dyn_cast<llvm::Constant>(value);
            constant != nullptr && (constant->isNullValue() || llvm::isa<llvm::UndefValue>(constant))) {
            if (!llvm::isa<llvm::UndefValue>(constant)) {
                builder.CreateMemSet(dest, builder.getInt8(0), bytes(type), dest_align);
            }
            return;
        }
        llvm::Value *source = address_of(value);
        if (source == nullptr) {
            builder.CreateStore(value, dest);
            return;
        }
        builder.CreateMemCpy(dest, dest_align, source, layout.getPrefTypeAlign(type), bytes(type));
    };
    auto part = [&](llvm::IRBuilder<> &builder, llvm::Type *type, llvm::Value *base,
                    llvm::ArrayRef<unsigned> indices) {
        std::vector<llvm::Value *> steps{builder.getInt32(0)};
        for (const unsigned index : indices) {
            steps.push_back(builder.getInt32(index));
        }
        return builder.CreateInBoundsGEP(type, base, steps);
    };
    std::vector<llvm::Instruction *> dead;
    std::vector<llvm::StoreInst *> kept;
    for (llvm::Instruction *inst : defs) {
        if (inst->getMetadata(kCallerCopy) != nullptr) {
            dead.push_back(inst);
            continue;
        }
        auto *slot = llvm::cast<llvm::AllocaInst>(slots[inst]);
        const llvm::Align align = slot->getAlign();
        if (auto *load = llvm::dyn_cast<llvm::LoadInst>(inst)) {
            llvm::IRBuilder<> builder(load);
            builder.CreateMemCpy(slot, align, load->getPointerOperand(), load->getAlign(), bytes(load->getType()),
                                 load->isVolatile());
        } else if (auto *insert = llvm::dyn_cast<llvm::InsertValueInst>(inst)) {
            llvm::IRBuilder<> builder(insert);
            copy_into(builder, slot, align, insert->getAggregateOperand());
            llvm::Value *field = part(builder, insert->getType(), slot, insert->getIndices());
            llvm::Value *inserted = insert->getInsertedValueOperand();
            if (big(inserted->getType())) {
                copy_into(builder, field, llvm::Align(1), inserted);
            } else {
                builder.CreateStore(inserted, field);
            }
        } else if (auto *extract = llvm::dyn_cast<llvm::ExtractValueInst>(inst)) {
            llvm::IRBuilder<> builder(extract);
            llvm::Value *base = address_of(extract->getAggregateOperand());
            llvm::Value *field = part(builder, extract->getAggregateOperand()->getType(), base, extract->getIndices());
            builder.CreateMemCpy(slot, align, field, llvm::Align(1), bytes(extract->getType()));
        } else if (auto *phi = llvm::dyn_cast<llvm::PHINode>(inst)) {
            // A phi of the incoming slots, then one copy. Two phis that swap values each read first.
            llvm::IRBuilder<> builder(phi);
            llvm::PHINode *from = builder.CreatePHI(llvm::PointerType::getUnqual(context), phi->getNumIncomingValues());
            for (unsigned index = 0; index < phi->getNumIncomingValues(); ++index) {
                from->addIncoming(address_of(phi->getIncomingValue(index)), phi->getIncomingBlock(index));
            }
            llvm::IRBuilder<> after(phi->getParent(), phi->getParent()->getFirstInsertionPt());
            after.CreateMemCpy(slot, align, from, llvm::Align(1), bytes(phi->getType()));
        } else if (auto *select = llvm::dyn_cast<llvm::SelectInst>(inst)) {
            llvm::IRBuilder<> builder(select);
            llvm::Value *from = builder.CreateSelect(select->getCondition(), address_of(select->getTrueValue()),
                                                     address_of(select->getFalseValue()));
            builder.CreateMemCpy(slot, align, from, llvm::Align(1), bytes(select->getType()));
        } else if (auto *freeze = llvm::dyn_cast<llvm::FreezeInst>(inst)) {
            llvm::IRBuilder<> builder(freeze);
            copy_into(builder, slot, align, freeze->getOperand(0));
        } else {
            // An instruction this pass does not know keeps its value. A plain store fills its slot, so a
            // phi or an `extractvalue` of it still has an address.
            llvm::Instruction *next = inst->getNextNode();
            if (llvm::isa<llvm::PHINode>(inst)) {
                next = &*inst->getParent()->getFirstInsertionPt();
            }
            llvm::IRBuilder<> builder(next);
            kept.push_back(builder.CreateStore(inst, slot));
            continue;
        }
        dead.push_back(inst);
    }
    // Every use of a lowered def reads its slot. A lowered def that uses one read its address above.
    const std::vector<llvm::Instruction *> lowered_defs = dead;
    auto is_lowered = [&](llvm::Instruction *inst) {
        return std::find(lowered_defs.begin(), lowered_defs.end(), inst) != lowered_defs.end();
    };
    for (llvm::Instruction *inst : lowered_defs) {
        std::vector<llvm::Use *> uses;
        for (llvm::Use &use : inst->uses()) {
            uses.push_back(&use);
        }
        for (llvm::Use *use : uses) {
            auto *user = llvm::cast<llvm::Instruction>(use->getUser());
            if (llvm::isa<llvm::StoreInst>(user) || is_lowered(user)) {
                continue;
            }
            if (auto *extract = llvm::dyn_cast<llvm::ExtractValueInst>(user); extract != nullptr && !big(extract->getType())) {
                llvm::IRBuilder<> builder(extract);
                llvm::Value *field = part(builder, inst->getType(), slots[inst], extract->getIndices());
                extract->replaceAllUsesWith(builder.CreateLoad(extract->getType(), field));
                dead.push_back(extract);
                continue;
            }
            // Any other use reads the whole value back.
            llvm::Instruction *before = user;
            if (auto *phi = llvm::dyn_cast<llvm::PHINode>(user)) {
                before = phi->getIncomingBlock(*use)->getTerminator();
            }
            llvm::IRBuilder<> builder(before);
            use->set(builder.CreateLoad(inst->getType(), slots[inst]));
        }
    }
    for (llvm::BasicBlock &block : fn) {
        for (llvm::Instruction &inst : block) {
            if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst); store != nullptr &&
                                                                    big(store->getValueOperand()->getType())) {
                if (std::find(stores.begin(), stores.end(), store) == stores.end()) {
                    stores.push_back(store);
                }
            }
        }
    }
    for (llvm::StoreInst *store : stores) {
        llvm::Value *value = store->getValueOperand();
        if (std::find(kept.begin(), kept.end(), store) != kept.end()) {
            continue;
        }
        if (!llvm::isa<llvm::Constant>(value) && slots.count(value) == 0) {
            continue;
        }
        llvm::IRBuilder<> builder(store);
        llvm::Type *type = value->getType();
        if (auto *constant = llvm::dyn_cast<llvm::Constant>(value)) {
            if (llvm::isa<llvm::UndefValue>(constant)) {
                store->eraseFromParent();
                continue;
            }
            if (constant->isNullValue()) {
                builder.CreateMemSet(store->getPointerOperand(), builder.getInt8(0), bytes(type), store->getAlign(),
                                     store->isVolatile());
                store->eraseFromParent();
                continue;
            }
        }
        builder.CreateMemCpy(store->getPointerOperand(), store->getAlign(), address_of(value),
                             layout.getPrefTypeAlign(type), bytes(type), store->isVolatile());
        store->eraseFromParent();
    }
    // Users go before the values they use.
    for (auto it = dead.rbegin(); it != dead.rend(); ++it) {
        (*it)->replaceAllUsesWith(llvm::PoisonValue::get((*it)->getType()));
    }
    for (llvm::Instruction *inst : dead) {
        inst->eraseFromParent();
    }
}

} // namespace aggregate_detail

inline void lower_large_aggregates(llvm::Module &module) {
    using namespace aggregate_detail;
    const llvm::DataLayout &layout = module.getDataLayout();
    std::vector<llvm::CallInst *> calls;
    std::vector<llvm::Function *> functions;
    for (llvm::Function &fn : module) {
        if (!fn.isIntrinsic() && big_signature(fn.getFunctionType())) {
            functions.push_back(&fn);
        }
        for (llvm::BasicBlock &block : fn) {
            for (llvm::Instruction &inst : block) {
                auto *call = llvm::dyn_cast<llvm::CallInst>(&inst);
                if (call == nullptr || call->isInlineAsm()) {
                    continue;
                }
                if (const llvm::Function *callee = call->getCalledFunction(); callee != nullptr && callee->isIntrinsic()) {
                    continue;
                }
                if (big_signature(call->getFunctionType())) {
                    calls.push_back(call);
                }
            }
        }
    }
    for (llvm::CallInst *call : calls) {
        rewrite_call(call, layout);
    }
    for (llvm::Function *fn : functions) {
        rewrite_function(fn);
    }
    for (llvm::Function &fn : module) {
        if (!fn.isDeclaration()) {
            lower_body(fn, layout);
        }
    }
}

} // namespace tlc
