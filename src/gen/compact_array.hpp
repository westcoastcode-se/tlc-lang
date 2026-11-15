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

llvm::Constant *CodeGen::finish_listed_array(llvm::ArrayType *array_ty, std::vector<llvm::Constant *> values,
                                             bool compact) {
    llvm::Type *element = array_ty->getElementType();
    const uint64_t total = array_ty->getNumElements();
    bool exact = true;
    for (llvm::Constant *value : values) {
        exact = exact && value->getType() == element;
    }
    // A long zero tail, or a compact row among the elements, makes this array compact too.
    if (compact && values.size() < total && (total - values.size() >= kCompactTail || !exact)) {
        return compact_array(array_ty, values);
    }
    if (!exact) {
        if (values.size() != total) {
            return compact_array(array_ty, values);
        }
        std::vector<llvm::Type *> types;
        for (llvm::Constant *value : values) {
            types.push_back(value->getType());
        }
        return llvm::ConstantStruct::get(llvm::StructType::get(context_, types), values);
    }
    while (values.size() < total) {
        values.push_back(llvm::Constant::getNullValue(element));
    }
    return llvm::ConstantArray::get(array_ty, values);
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

void CodeGen::struct_fills(int struct_id, uint64_t base, std::vector<PendingFill> &out, int depth,
                           const Expr::Init *overlay) {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size()) || depth > 64) {
        return;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.type == nullptr || layout.is_union || !layout.type->isSized()) {
        return;
    }
    const llvm::StructLayout *offsets = module_.getDataLayout().getStructLayout(layout.type);
    for (std::size_t index = 0; index < layout.fields.size() && index < layout.type->getNumElements(); ++index) {
        const FieldLayout &field = layout.fields[index];
        const uint64_t at = base + offsets->getElementOffset(static_cast<unsigned>(index));
        // A field the literal names takes that value. A struct literal there has omitted fields of its own.
        const Expr *named = nullptr;
        if (overlay != nullptr) {
            for (const Expr::FieldInit &item : overlay->fields) {
                if (item.value != nullptr && item.field == field.name) {
                    named = item.value.get();
                }
            }
        }
        if (named != nullptr) {
            const auto *nested = std::get_if<Expr::Init>(&named->data);
            if (nested != nullptr && !field.is_array && field.depth == 0 && field.struct_id >= 0 && !nested->is_tuple &&
                !nested->array_literal && nested->elements.empty()) {
                struct_fills(field.struct_id, at, out, depth + 1, nested);
            }
            continue;
        }
        // A field with its own value is that value, not the element defaults.
        if (field.init != nullptr || field.depth != 0 || field.is_slice || field.struct_id < 0) {
            continue;
        }
        if (field.is_array) {
            if (big_default_array(field.type, field.struct_id)) {
                const StructLayout &leaf = structs_[static_cast<std::size_t>(field.struct_id)];
                out.push_back(PendingFill{nullptr, field.struct_id, leaf_count(field.type, leaf.type), at});
            }
            continue;
        }
        struct_fills(field.struct_id, at, out, depth + 1);
    }
}

llvm::Function *CodeGen::emit_fill_function() {
    if (fill_fn_ != nullptr || pending_fills_.empty()) {
        return fill_fn_;
    }
    auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), false);
    fill_fn_ = llvm::Function::Create(type, eval_ ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage,
                                      "tlc.fill", module_);
    fill_fn_->setCallingConv(llvm::CallingConv::C);
    llvm::Function *saved_function = function_;
    llvm::Type *saved_return = return_type_;
    const llvm::IRBuilderBase::InsertPoint saved = builder_.saveIP();
    const llvm::DebugLoc saved_loc = builder_.getCurrentDebugLocation();
    function_ = fill_fn_;
    return_type_ = fill_fn_->getReturnType();
    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", fill_fn_));
    builder_.SetCurrentDebugLocation(llvm::DebugLoc());
    for (const PendingFill &fill : pending_fills_) {
        const StructLayout &layout = structs_[static_cast<std::size_t>(fill.struct_id)];
        // One element's defaults, copied into each element.
        llvm::Constant *image = default_struct(fill.struct_id);
        if (image == nullptr || image->getType() != layout.type) {
            continue;
        }
        llvm::Value *raw = fill.global;
        if (fill.offset != 0) {
            raw = builder_.CreateConstInBoundsGEP1_64(i8(), fill.global, fill.offset);
        }
        store_each_element(raw, const_len(static_cast<int64_t>(fill.count)), layout.type, image);
    }
    builder_.CreateRetVoid();
    builder_.restoreIP(saved);
    builder_.SetCurrentDebugLocation(saved_loc);
    function_ = saved_function;
    return_type_ = saved_return;
    return fill_fn_;
}

void CodeGen::finish_compact_globals() {
    // A comptime module has no `tlc.init`. Its fill is made here, before the swap below.
    if (eval_) {
        emit_fill_function();
    }
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
