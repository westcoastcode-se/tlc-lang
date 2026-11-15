// A file-scope array with a short list is the listed elements and one zero tail.
//
// `var arr [2147483647]i32 = {1, 2, 3}` was built as one LLVM constant per element, zeros included, which
// took more than 8 GiB in the compiler. Past `kCompactTail` zero elements the constant is a struct: the
// listed elements, then `[rest x T] zeroinitializer`. The struct has the same bytes, size, and alignment as
// `[N x T]`. The binary still holds the whole array, as a C compiler's does.
//
// The global keeps its `[N x T]` type while code is emitted, so every binding stays valid. Its compact
// initializer waits in `compact_inits_`, and `finish_compact_globals` swaps in a global of that type once
// emission is done.
//
// Included once, from codegen_const.cpp.
#pragma once

#include "gen/gen.hpp"

namespace tlc {

bool CodeGen::same_layout(llvm::Type *got, llvm::Type *want) const {
    if (got == want) {
        return true;
    }
    if (got == nullptr || want == nullptr || !got->isAggregateType() || !want->isAggregateType() ||
        !got->isSized() || !want->isSized()) {
        return false;
    }
    const llvm::DataLayout &layout = module_.getDataLayout();
    return layout.getTypeAllocSize(got) == layout.getTypeAllocSize(want) &&
           layout.getABITypeAlign(got) == layout.getABITypeAlign(want);
}

llvm::Constant *CodeGen::compact_array(llvm::ArrayType *array_ty, const std::vector<llvm::Constant *> &listed) {
    llvm::Type *element = array_ty->getElementType();
    const uint64_t total = array_ty->getNumElements();
    std::vector<llvm::Constant *> fields;
    std::vector<llvm::Type *> types;
    bool exact = true;
    for (llvm::Constant *value : listed) {
        exact = exact && value->getType() == element;
    }
    if (exact) {
        // One run of the listed elements.
        auto *head = llvm::ArrayType::get(element, listed.size());
        fields.push_back(llvm::ConstantArray::get(head, listed));
    } else {
        // A listed element that is itself compact keeps its own type, so each one is a field.
        fields = listed;
    }
    for (llvm::Constant *field : fields) {
        types.push_back(field->getType());
    }
    auto *tail = llvm::ArrayType::get(element, total - listed.size());
    fields.push_back(llvm::ConstantAggregateZero::get(tail));
    types.push_back(tail);
    return llvm::ConstantStruct::get(llvm::StructType::get(context_, types), fields);
}

llvm::Constant *CodeGen::array_element(llvm::Constant *array, uint64_t index) {
    if (array == nullptr) {
        return nullptr;
    }
    if (array->getType()->isArrayTy()) {
        if (index >= llvm::cast<llvm::ArrayType>(array->getType())->getNumElements()) {
            return nullptr;
        }
        return array->getAggregateElement(static_cast<unsigned>(index));
    }
    auto *shape = llvm::dyn_cast<llvm::StructType>(array->getType());
    if (shape == nullptr || shape->getNumElements() < 2) {
        return nullptr;
    }
    auto *tail = llvm::dyn_cast<llvm::ArrayType>(shape->getElementType(shape->getNumElements() - 1));
    if (tail == nullptr) {
        return nullptr;
    }
    llvm::Type *element = tail->getElementType();
    // A run of listed elements is an array of the element type. A field per element is not, since an
    // element never contains its own type.
    auto *head = llvm::dyn_cast<llvm::ArrayType>(shape->getElementType(0));
    const bool run = shape->getNumElements() == 2 && head != nullptr && head->getElementType() == element;
    const uint64_t listed = run ? head->getNumElements() : shape->getNumElements() - 1;
    if (index >= listed + tail->getNumElements()) {
        return nullptr;
    }
    if (index >= listed) {
        return llvm::Constant::getNullValue(element);
    }
    llvm::Constant *first = array->getAggregateElement(0U);
    if (run) {
        return first == nullptr ? nullptr : first->getAggregateElement(static_cast<unsigned>(index));
    }
    return array->getAggregateElement(static_cast<unsigned>(index));
}

bool CodeGen::set_global_initializer(llvm::GlobalVariable *global, llvm::Constant *value) {
    if (global == nullptr || value == nullptr) {
        return false;
    }
    if (value->getType() == global->getValueType()) {
        compact_inits_.erase(global);
        global->setInitializer(value);
        return true;
    }
    if (!same_layout(value->getType(), global->getValueType())) {
        return false;
    }
    // Zero until the global of the compact type replaces this one.
    compact_inits_[global] = value;
    global->setInitializer(llvm::Constant::getNullValue(global->getValueType()));
    return true;
}

void CodeGen::finish_compact_globals() {
    const auto pending = std::move(compact_inits_);
    compact_inits_.clear();
    for (const auto &[old_global, value] : pending) {
        auto *global = new llvm::GlobalVariable(module_, value->getType(), old_global->isConstant(),
                                                old_global->getLinkage(), value, "", old_global,
                                                old_global->getThreadLocalMode(), old_global->getAddressSpace());
        global->copyAttributesFrom(old_global);
        global->setAlignment(old_global->getAlign().value_or(module_.getDataLayout().getABITypeAlign(
            old_global->getValueType())));
        llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 4> metadata;
        old_global->getAllMetadata(metadata);
        for (const auto &[kind, node] : metadata) {
            global->setMetadata(kind, node);
        }
        global->takeName(old_global);
        old_global->replaceAllUsesWith(global);
        old_global->eraseFromParent();
    }
}

} // namespace tlc
