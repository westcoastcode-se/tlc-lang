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

#include <algorithm>

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
    // A compact row among the elements. Each element is a field, then the tail, empty when every element is
    // listed, so `array_element` reads one shape.
    if (!exact) {
        return compact_array(array_ty, values);
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
                           const Expr::Init *overlay, std::vector<StartWrite> *copies) {
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
                struct_fills(field.struct_id, at, out, depth + 1, nested, copies);
            } else if (copies != nullptr && !field.is_slice) {
                // `.inner = w` copies that global's start value at start.
                start_copy_part(*named, layout.type->getElementType(static_cast<unsigned>(index)), at, *copies);
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

void CodeGen::list_fills(const Expr::Init &list, const Binding &binding, std::vector<PendingFill> &out,
                         std::vector<StartWrite> &copies) {
    auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(binding.value);
    if (global == nullptr || binding.struct_id < 0 || binding.struct_id >= static_cast<int>(structs_.size()) ||
        list.is_tuple) {
        return;
    }
    auto *array = llvm::dyn_cast<llvm::ArrayType>(global->getValueType());
    llvm::StructType *elem = structs_[static_cast<std::size_t>(binding.struct_id)].type;
    if (array == nullptr || elem == nullptr || leaf_count(array, elem) == 0) {
        return;
    }
    std::vector<PendingFill> found;
    std::vector<StartWrite> moved;
    if (!collect_list_fills(list, array, 0, binding.struct_id, found, moved)) {
        return;
    }
    out.insert(out.end(), found.begin(), found.end());
    copies.insert(copies.end(), moved.begin(), moved.end());
}

bool CodeGen::collect_list_fills(const Expr::Init &list, llvm::ArrayType *array, uint64_t base, int struct_id,
                                 std::vector<PendingFill> &fills, std::vector<StartWrite> &copies) {
    if (array == nullptr || list.is_tuple || !list.fields.empty() || list.elements.size() > array->getNumElements()) {
        return false;
    }
    llvm::Type *elem = array->getElementType();
    if (!elem->isSized()) {
        return false;
    }
    const uint64_t stride = module_.getDataLayout().getTypeAllocSize(elem).getFixedValue();
    for (std::size_t index = 0; index < list.elements.size(); ++index) {
        const Expr *element = list.elements[index].get();
        if (element == nullptr) {
            return false;
        }
        const uint64_t at = base + index * stride;
        // `{w, w}` copies that global at start.
        if (start_copy_part(*element, elem, at, copies)) {
            continue;
        }
        const auto *literal = std::get_if<Expr::Init>(&element->data);
        if (literal == nullptr) {
            return false;
        }
        if (auto *inner = llvm::dyn_cast<llvm::ArrayType>(elem)) {
            if (!collect_list_fills(*literal, inner, at, struct_id, fills, copies)) {
                return false;
            }
            continue;
        }
        if (elem != structs_[static_cast<std::size_t>(struct_id)].type || literal->is_tuple || literal->array_literal ||
            !literal->elements.empty()) {
            return false;
        }
        struct_fills(struct_id, at, fills, 0, literal, &copies);
    }
    return true;
}

llvm::GlobalVariable *CodeGen::heavy_root(const Expr &place) {
    const Expr *cursor = &place;
    const Binding *binding = nullptr;
    for (int depth = 0; depth < 256; ++depth) {
        if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data)) {
            // `conf.w` is the root, a global of that package.
            if ((binding = package_global(current_file_, *selector)) != nullptr) {
                break;
            }
            if (selector->base == nullptr) {
                return nullptr;
            }
            cursor = selector->base.get();
            continue;
        }
        if (const auto *index = std::get_if<Expr::Index>(&cursor->data)) {
            if (index->is_range || index->base == nullptr || index->index == nullptr) {
                return nullptr;
            }
            const std::optional<int32_t> at = fold(*index->index);
            if (!at || *at < 0) {
                return nullptr;
            }
            cursor = index->base.get();
            continue;
        }
        break;
    }
    if (binding == nullptr) {
        const auto *named = std::get_if<Expr::Name>(&cursor->data);
        binding = named == nullptr ? nullptr : global_named(current_file_, named->text);
    }
    if (binding == nullptr || binding->is_slice || binding->depth != 0) {
        return nullptr;
    }
    auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(binding->value);
    if (global == nullptr) {
        return nullptr;
    }
    if (start_written_.contains(global) || heavy_value(binding->struct_id, binding->is_array, global->getValueType())) {
        return global;
    }
    return nullptr;
}

bool CodeGen::start_copy_part(const Expr &source, llvm::Type *type, uint64_t offset, std::vector<StartWrite> &out) {
    llvm::GlobalVariable *root = type == nullptr ? nullptr : heavy_root(source);
    if (root == nullptr) {
        return false;
    }
    StartWrite write;
    write.offset = offset;
    write.type = type;
    write.source = &source;
    write.root = root;
    out.push_back(write);
    return true;
}

void CodeGen::commit_start_writes(std::vector<StartWrite> &writes, llvm::GlobalVariable *global, const VarDecl &var) {
    for (StartWrite &write : writes) {
        write.global = global;
        write.file_id = current_file_;
        write.loc = var.loc;
        write.name = var.name;
        if (write.source != nullptr) {
            start_parts_[write.source] = global;
        }
        start_writes_.push_back(write);
    }
    if (!writes.empty()) {
        start_written_.insert(global);
    }
    writes.clear();
}

void CodeGen::store_image(llvm::Value *base, uint64_t offset, llvm::Constant *value) {
    if (value == nullptr || value->isNullValue() || terminated()) {
        return;
    }
    const llvm::DataLayout &layout = module_.getDataLayout();
    llvm::Type *type = value->getType();
    auto at = [&](uint64_t bytes) -> llvm::Value * {
        return bytes == 0 ? base : builder_.CreateConstInBoundsGEP1_64(i8(), base, bytes);
    };
    // A small part, or a scalar, is one store.
    if (!type->isAggregateType() || layout.getTypeAllocSize(type).getFixedValue() <= 64) {
        const llvm::Align align = layout.getABITypeAlign(type);
        builder_.CreateAlignedStore(value, at(offset), offset % align.value() == 0 ? align : llvm::Align(1));
        return;
    }
    if (auto *record = llvm::dyn_cast<llvm::StructType>(type)) {
        const llvm::StructLayout *fields = layout.getStructLayout(record);
        for (unsigned index = 0; index < record->getNumElements(); ++index) {
            store_image(base, offset + fields->getElementOffset(index), value->getAggregateElement(index));
        }
        return;
    }
    auto *array = llvm::cast<llvm::ArrayType>(type);
    const uint64_t count = array->getNumElements();
    llvm::Type *elem = array->getElementType();
    const uint64_t stride = layout.getTypeAllocSize(elem).getFixedValue();
    // The same element throughout is one loop.
    llvm::Constant *first = count == 0 ? nullptr : value->getAggregateElement(0U);
    bool same = first != nullptr;
    for (uint64_t index = 1; same && index < count; ++index) {
        same = value->getAggregateElement(static_cast<unsigned>(index)) == first;
    }
    if (same && count > 1) {
        store_each_element(at(offset), const_len(static_cast<int64_t>(count)), elem, first);
        return;
    }
    for (uint64_t index = 0; index < count; ++index) {
        store_image(base, offset + index * stride, value->getAggregateElement(static_cast<unsigned>(index)));
    }
}

void CodeGen::store_defaults_each(llvm::Value *raw, uint64_t count, int struct_id, std::unordered_set<int> &seen) {
    if (raw == nullptr || count == 0 || terminated() || function_ == nullptr || struct_id < 0 ||
        struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    llvm::StructType *elem = structs_[static_cast<std::size_t>(struct_id)].type;
    if (elem == nullptr) {
        return;
    }
    if (count == 1) {
        store_struct_defaults(raw, struct_id, seen);
        return;
    }
    llvm::AllocaInst *step = alloca_typed(len_ty(), "fill.i");
    builder_.CreateStore(const_len(0), step);
    llvm::BasicBlock *cond = llvm::BasicBlock::Create(context_, "fill.each", function_);
    llvm::BasicBlock *body = llvm::BasicBlock::Create(context_, "fill.each.body", function_);
    llvm::BasicBlock *done = llvm::BasicBlock::Create(context_, "fill.each.done", function_);
    builder_.CreateBr(cond);
    builder_.SetInsertPoint(cond);
    llvm::Value *index = builder_.CreateLoad(len_ty(), step);
    builder_.CreateCondBr(builder_.CreateICmpSLT(index, const_len(static_cast<int64_t>(count))), body, done);
    builder_.SetInsertPoint(body);
    llvm::Value *at = builder_.CreateInBoundsGEP(elem, raw, llvm::ArrayRef<llvm::Value *>{index});
    store_struct_defaults(at, struct_id, seen);
    // The body may end in its own loop. The step continues from wherever that left the builder.
    llvm::Value *again = builder_.CreateLoad(len_ty(), step);
    builder_.CreateStore(builder_.CreateAdd(again, const_len(1)), step);
    builder_.CreateBr(cond);
    builder_.SetInsertPoint(done);
}

llvm::Function *CodeGen::emit_fill_function() {
    if (fill_fn_ != nullptr || (pending_fills_.empty() && start_writes_.empty())) {
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
    // A literal's image first. Its large default arrays are zero there and filled below.
    for (const StartWrite &write : start_writes_) {
        if (write.image != nullptr) {
            store_image(write.global, write.offset, write.image);
        }
    }
    for (const PendingFill &fill : pending_fills_) {
        const StructLayout &layout = structs_[static_cast<std::size_t>(fill.struct_id)];
        if (heavy_defaults(fill.struct_id)) {
            llvm::Value *raw = fill.global;
            if (fill.offset != 0) {
                raw = builder_.CreateConstInBoundsGEP1_64(i8(), fill.global, fill.offset);
            }
            std::unordered_set<int> seen;
            store_defaults_each(raw, fill.count, fill.struct_id, seen);
            continue;
        }
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
    emit_start_copies();
    builder_.CreateRetVoid();
    builder_.restoreIP(saved);
    builder_.SetCurrentDebugLocation(saved_loc);
    function_ = saved_function;
    return_type_ = saved_return;
    return fill_fn_;
}

void CodeGen::emit_start_copies() {
    std::vector<const StartWrite *> waiting;
    for (const StartWrite &write : start_writes_) {
        if (write.source != nullptr) {
            waiting.push_back(&write);
        }
    }
    if (waiting.empty() || graph_ == nullptr) {
        return;
    }
    const uint32_t saved_file = current_file_;
    const std::string saved_trace = trace_name_;
    const llvm::DataLayout &layout = module_.getDataLayout();
    // A copy reads its root after every copy into that root. The checker rejects a cycle; if one is left,
    // the first waiting copy goes next.
    std::vector<char> done(waiting.size(), 0);
    auto ready = [&](std::size_t index) {
        for (std::size_t other = 0; other < waiting.size(); ++other) {
            if (other == index || done[other] != 0) {
                continue;
            }
            const StartWrite &write = *waiting[index];
            if (waiting[other]->global == write.root ||
                std::find(write.roots.begin(), write.roots.end(), waiting[other]->global) != write.roots.end()) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t left = waiting.size(); left > 0 && !terminated(); --left) {
        std::size_t index = 0;
        while (index < waiting.size() && (done[index] != 0 || !ready(index))) {
            ++index;
        }
        if (index == waiting.size()) {
            index = 0;
            while (done[index] != 0) {
                ++index;
            }
        }
        done[index] = 1;
        const StartWrite &write = *waiting[index];
        const PackageNode *node = find_node(write.file_id);
        if (node == nullptr) {
            continue;
        }
        current_file_ = write.file_id;
        packages_.clear();
        push();
        bind_package_globals();
        bind_imports(*node);
        trace_name_ = write.name;
        set_loc(write.loc);
        llvm::Value *dest = write.offset == 0 ? static_cast<llvm::Value *>(write.global)
                                              : builder_.CreateConstInBoundsGEP1_64(i8(), write.global, write.offset);
        if (write.type->isAggregateType()) {
            const TypedValue place = emit_addr(*write.source);
            if (place.value != nullptr && !terminated()) {
                builder_.CreateMemCpy(dest, llvm::MaybeAlign(1), place.value, llvm::MaybeAlign(1),
                                      layout.getTypeAllocSize(write.type).getFixedValue());
            }
        } else {
            const TypedValue value = emit_rvalue(*write.source);
            llvm::Value *stored = value.value == nullptr || terminated()
                                      ? nullptr
                                      : as_stored(value.value, value.depth, write.type, value.elem_unsigned);
            if (stored == nullptr || stored->getType() != write.type) {
                error(write.loc, "type mismatch");
            } else {
                builder_.CreateStore(stored, dest);
            }
        }
        pop();
    }
    current_file_ = saved_file;
    trace_name_ = saved_trace;
    packages_.clear();
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
