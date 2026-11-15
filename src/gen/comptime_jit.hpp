// The JIT that runs a comptime call.
//
// A comptime module holds every global the program declares. The JIT allocated each one and wrote
// zeros over it, so a 2 GiB array cost 2 GiB before the call ran. Code also reached data only within
// 2 GiB, and a larger array failed with `out of range of Delta32 fixup`.
//
// Now each zero global of at least `kLazyGlobalBytes` lives in its own anonymous mapping, which the
// kernel fills with zero pages only when the call touches them. Code uses the large code model, so
// that data may sit anywhere. The JIT's own error reports are dropped; the call fails as before.
//
// Included once, from codegen_eval.cpp.
#pragma once

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CodeGen.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sys/mman.h>

namespace tlc {

inline constexpr uint64_t kLazyGlobalBytes = uint64_t{1} << 20;

// The mappings of one comptime module. They stay until the JIT that uses them is gone, so this is
// declared before the JIT.
class LazyGlobals {
  public:
    LazyGlobals() = default;
    LazyGlobals(const LazyGlobals &) = delete;
    LazyGlobals &operator=(const LazyGlobals &) = delete;
    ~LazyGlobals() {
        for (const auto &[address, size] : maps_) {
            ::munmap(address, size);
        }
    }
    void *map(std::size_t size) {
        void *address = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (address == MAP_FAILED) {
            return nullptr;
        }
        maps_.emplace_back(address, size);
        return address;
    }

  private:
    std::vector<std::pair<void *, std::size_t>> maps_;
};

// Writes plain data, an integer, a float, or an array or struct of them, at `dest`. False for a pointer or
// any other constant that needs a relocation. A zero part is already zero in a fresh mapping.
inline bool write_constant(const llvm::Constant *value, char *dest, const llvm::DataLayout &layout) {
    if (value->isNullValue()) {
        return true;
    }
    if (const auto *number = llvm::dyn_cast<llvm::ConstantInt>(value)) {
        const uint64_t bytes = layout.getTypeStoreSize(number->getType());
        const uint64_t bits = number->getZExtValue();
        for (uint64_t i = 0; i < bytes && i < 8; ++i) {
            dest[i] = static_cast<char>((bits >> (8 * i)) & 0xffU);
        }
        return number->getBitWidth() <= 64 && layout.isLittleEndian();
    }
    if (const auto *real = llvm::dyn_cast<llvm::ConstantFP>(value)) {
        const llvm::APInt bits = real->getValueAPF().bitcastToAPInt();
        const uint64_t raw = bits.getZExtValue();
        const uint64_t bytes = layout.getTypeStoreSize(real->getType());
        for (uint64_t i = 0; i < bytes && i < 8; ++i) {
            dest[i] = static_cast<char>((raw >> (8 * i)) & 0xffU);
        }
        return bits.getBitWidth() <= 64 && layout.isLittleEndian();
    }
    if (const auto *data = llvm::dyn_cast<llvm::ConstantDataSequential>(value)) {
        const llvm::StringRef raw = data->getRawDataValues();
        std::copy(raw.begin(), raw.end(), dest);
        return layout.isLittleEndian();
    }
    if (const auto *array = llvm::dyn_cast<llvm::ConstantArray>(value)) {
        const uint64_t step = layout.getTypeAllocSize(array->getType()->getElementType());
        for (unsigned i = 0; i < array->getNumOperands(); ++i) {
            if (!write_constant(array->getOperand(i), dest + step * i, layout)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *record = llvm::dyn_cast<llvm::ConstantStruct>(value)) {
        const llvm::StructLayout *fields = layout.getStructLayout(record->getType());
        for (unsigned i = 0; i < record->getNumOperands(); ++i) {
            if (!write_constant(record->getOperand(i), dest + fields->getElementOffset(i), layout)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

// A constant `write_constant` can write: no pointer, no expression, nothing that needs a relocation.
inline bool write_constant_probe(const llvm::Constant *value) {
    if (value->isNullValue() || llvm::isa<llvm::ConstantInt>(value) || llvm::isa<llvm::ConstantFP>(value) ||
        llvm::isa<llvm::ConstantDataSequential>(value)) {
        return !value->getType()->isPointerTy();
    }
    if (llvm::isa<llvm::ConstantArray>(value) || llvm::isa<llvm::ConstantStruct>(value)) {
        for (const llvm::Use &operand : value->operands()) {
            if (!write_constant_probe(llvm::cast<llvm::Constant>(operand.get()))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

inline void prepare_comptime_machine(llvm::orc::JITTargetMachineBuilder &machine) {
    machine.setCodeModel(llvm::CodeModel::Large);
}

// Moves each large zero global of `module` into a mapping, then adds the module to `jit`.
inline llvm::Error add_comptime_module(llvm::orc::LLJIT &jit, std::unique_ptr<llvm::Module> module,
                                       std::unique_ptr<llvm::LLVMContext> context, LazyGlobals &lazy) {
    jit.getExecutionSession().setErrorReporter([](llvm::Error err) { llvm::consumeError(std::move(err)); });
    const llvm::DataLayout &layout = module->getDataLayout();
    llvm::orc::SymbolMap symbols;
    int index = 0;
    for (llvm::GlobalVariable &global : module->globals()) {
        if (global.isDeclaration() || global.isThreadLocal() || !global.hasInitializer() ||
            global.getAlign().valueOrOne().value() > 4096) {
            continue;
        }
        const uint64_t size = layout.getTypeAllocSize(global.getValueType());
        if (size < kLazyGlobalBytes) {
            continue;
        }
        // A zero global, or a short list and a zero tail. Only the list's bytes are written.
        const llvm::Constant *init = global.getInitializer();
        if (!init->isNullValue() && !write_constant_probe(init)) {
            continue;
        }
        void *address = lazy.map(static_cast<std::size_t>(size));
        if (address == nullptr) {
            continue;
        }
        if (!write_constant(init, static_cast<char *>(address), layout)) {
            continue;
        }
        // A name of its own, so no other definition or host symbol can answer for it.
        global.setName("tlc.lazy." + std::to_string(index++));
        global.setInitializer(nullptr);
        global.setLinkage(llvm::GlobalValue::ExternalLinkage);
        global.setDSOLocal(false);
        symbols[jit.mangleAndIntern(global.getName())] =
            llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(address), llvm::JITSymbolFlags::Exported);
    }
    if (!symbols.empty()) {
        if (llvm::Error err = jit.getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols)))) {
            return err;
        }
    }
    return jit.addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)));
}

} // namespace tlc
