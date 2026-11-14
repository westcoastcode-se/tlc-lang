// A test-time check that no store writes past the memory it targets.
#pragma once

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace tlc {

// Set by the tests. A `comptime func` runs in the compiler's own process, so a store wider than its slot
// overwrote the compiler's frame there, and an ordinary program overwrote the next variable. Each such bug
// was found one fuzz case at a time. With this set, the first one in any module stops the compiler.
inline bool store_check_enabled() {
    static const bool enabled = std::getenv("TLC_CHECK_STORES") != nullptr;
    return enabled;
}

// The type of the memory `pointer` names, when the IR says it: a local, a global, or a field or element step.
inline llvm::Type *known_slot_type(const llvm::Value *pointer) {
    if (const auto *local = llvm::dyn_cast<llvm::AllocaInst>(pointer)) {
        return local->getAllocatedType();
    }
    if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(pointer)) {
        return global->getValueType();
    }
    if (const auto *step = llvm::dyn_cast<llvm::GetElementPtrInst>(pointer)) {
        // `getelementptr i8, ptr %p, i64 12` is a byte offset. It says where, not how large the slot is.
        if (step->getSourceElementType()->isIntegerTy(8)) {
            return nullptr;
        }
        return step->getResultElementType();
    }
    return nullptr;
}

// A description of the first store whose value is larger than its slot, or empty. A smaller store is
// allowed: a union arm, or the first field of a struct, writes into part of the slot.
inline std::string wide_store(const llvm::Module &module) {
    const llvm::DataLayout &layout = module.getDataLayout();
    for (const llvm::Function &fn : module) {
        for (const llvm::BasicBlock &block : fn) {
            for (const llvm::Instruction &inst : block) {
                const auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst);
                if (store == nullptr) {
                    continue;
                }
                llvm::Type *slot = known_slot_type(store->getPointerOperand());
                llvm::Type *value = store->getValueOperand()->getType();
                if (slot == nullptr || !slot->isSized() || !value->isSized()) {
                    continue;
                }
                if (layout.getTypeStoreSize(value).getFixedValue() <= layout.getTypeAllocSize(slot).getFixedValue()) {
                    continue;
                }
                std::string text;
                llvm::raw_string_ostream out(text);
                out << "store of " << *value << " into " << *slot << " in " << fn.getName() << ": " << *store;
                return text;
            }
        }
    }
    return {};
}

inline void check_stores(const llvm::Module &module) {
    // A module the verifier rejects is never run, so its stores cannot overwrite anything. A rejected
    // program can leave one, and the verifier reports it.
    if (!store_check_enabled() || llvm::verifyModule(module, nullptr)) {
        return;
    }
    const std::string found = wide_store(module);
    if (found.empty()) {
        return;
    }
    std::fprintf(stderr, "tlc internal error: %s\n", found.c_str());
    if (std::getenv("TLC_CHECK_STORES_DUMP") != nullptr) {
        module.print(llvm::errs(), nullptr);
    }
    std::abort();
}

} // namespace tlc
