// Constant initializers and stores into arrays.
#include "gen/gen.hpp"

#include "gen/detail.hpp"

namespace tlc {

llvm::Constant *CodeGen::const_default_array(const Expr::Init &init, llvm::ArrayType *array_ty, const Binding &flags) {
    if (array_ty == nullptr || init.is_tuple || !init.fields.empty() || init.elements.size() > array_ty->getNumElements()) {
        return nullptr;
    }
    llvm::Type *element = array_ty->getElementType();
    std::vector<llvm::Constant *> values(array_ty->getNumElements(), llvm::Constant::getNullValue(element));
    for (std::size_t index = 0; index < init.elements.size(); ++index) {
        const auto &item = init.elements[index];
        if (!item) {
            return nullptr;
        }
        llvm::Constant *value = nullptr;
        if (element == slice_type()) {
            std::unordered_set<std::string> seen;
            value = constant_slice(*item, seen);
        } else if (auto *inner = llvm::dyn_cast<llvm::ArrayType>(element)) {
            const auto *nested = std::get_if<Expr::Init>(&item->data);
            if (nested != nullptr && array_brace(*nested)) {
                Binding row = flags;
                row.is_array = true;
                row.array_len = static_cast<int>(inner->getNumElements());
                row.inner_array_len = 0;
                row.value = nullptr;
                value = const_default_array(*nested, inner, row);
            } else {
                value = const_array_base(*item);
            }
        } else {
            Binding one = flags;
            one.is_array = false;
            one.array_len = 0;
            one.inner_array_len = 0;
            one.value = nullptr;
            value = const_initializer(*item, one);
        }
        if (value == nullptr || value->getType() != element) {
            return nullptr;
        }
        values[index] = value;
    }
    return llvm::ConstantArray::get(array_ty, values);
}

llvm::Constant *CodeGen::const_field(const Expr &expr, const FieldLayout &slot) {
    if (slot.is_slice) {
        std::unordered_set<std::string> seen;
        return constant_slice(expr, seen);
    }
    // `Holder{.raw = {4, 6}}` at file scope is that constant array. A short list and
    // `0...` are not a constant, the same way a file-scope array list is not.
    if (slot.is_array) {
        auto *array_ty = llvm::dyn_cast_or_null<llvm::ArrayType>(slot.type);
        if (array_ty == nullptr) {
            return nullptr;
        }
        Binding flags;
        flags.elem_char = slot.elem_char;
        flags.struct_id = slot.struct_id;
        flags.elem_void = slot.elem_void;
        flags.elem_bool = slot.elem_bool;
        flags.elem_bits = slot.elem_bits;
        flags.elem_unsigned = slot.elem_unsigned;
        flags.elem_float = slot.elem_float;
        flags.elem_slice = slot.elem_slice;
        flags.is_array = true;
        flags.array_len = slot.array_len;
        if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
            if (init->is_tuple || !init->fields.empty()) {
                return nullptr;
            }
            return fill_array(*init, array_ty, flags);
        }
        llvm::Constant *copied = const_array_base(expr);
        if (copied == nullptr || copied->getType() != array_ty) {
            return nullptr;
        }
        return copied;
    }
    if (slot.type == nullptr) {
        return nullptr;
    }
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        if (slot.depth != 0 || slot.struct_id < 0) {
            return nullptr;
        }
        return const_struct(*init, slot.struct_id);
    }
    if (slot.depth > 0) {
        if (std::holds_alternative<Expr::Nullptr>(expr.data)) {
            return null_ptr();
        }
        // `var box Box = {.op = inc}` at file scope. A data pointer stays non-constant.
        if (slot.fn_ty != nullptr) {
            if (const Binding *designator = function_designator(current_file_, expr)) {
                return llvm::dyn_cast_or_null<llvm::Function>(designator->value);
            }
        }
        return nullptr;
    }
    if (slot.elem_float) {
        if (const auto *literal = std::get_if<Expr::Float>(&expr.data)) {
            const std::string number = literal->text.empty() ? "0" : literal->text;
            return llvm::ConstantFP::get(slot.type, std::strtod(number.c_str(), nullptr));
        }
        const std::optional<int32_t> number = fold(expr);
        if (!number) {
            return nullptr;
        }
        return llvm::ConstantFP::get(slot.type, static_cast<double>(*number));
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data);
        selector != nullptr && selector->base != nullptr) {
        const int enum_id = enum_type_designator(*selector->base);
        if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
            const auto member = enums_[static_cast<std::size_t>(enum_id)].values.find(selector->field);
            if (member == enums_[static_cast<std::size_t>(enum_id)].values.end() || !slot.type->isIntegerTy()) {
                return nullptr;
            }
            return llvm::ConstantInt::get(slot.type, static_cast<uint64_t>(member->second), true);
        }
    }
    if (!slot.type->isIntegerTy()) {
        return nullptr;
    }
    const std::optional<int32_t> number = fold(expr);
    if (!number) {
        return nullptr;
    }
    return llvm::ConstantInt::get(slot.type, static_cast<uint64_t>(static_cast<int64_t>(*number)), true);
}

void CodeGen::store_union_tag(int struct_id, llvm::Value *pointer, const std::string &field) {
    if (pointer == nullptr || struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.tag_enum < 0 || layout.tag_enum >= static_cast<int>(enums_.size()) || layout.type == nullptr ||
        layout.type->getNumElements() == 0) {
        return;
    }
    const auto member = enums_[static_cast<std::size_t>(layout.tag_enum)].values.find(field);
    if (member == enums_[static_cast<std::size_t>(layout.tag_enum)].values.end()) {
        return;
    }
    llvm::Constant *tag = enum_constant(layout.tag_enum, member->second);
    if (tag == nullptr || layout.type->getNumElements() < 3 || tag->getType() != layout.type->getElementType(2)) {
        return;
    }
    llvm::Value *addr = builder_.CreateInBoundsGEP(layout.type, pointer,
                                                    llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(2)});
    builder_.CreateStore(tag, addr);
}

bool CodeGen::write_constant_bytes(llvm::Constant *value, uint64_t offset, std::vector<uint8_t> &bytes) const {
    if (value == nullptr) {
        return false;
    }
    const llvm::DataLayout &data_layout = module_.getDataLayout();
    llvm::Type *type = value->getType();
    if (type == nullptr || !type->isSized()) {
        return false;
    }
    const uint64_t size = data_layout.getTypeAllocSize(type).getFixedValue();
    if (offset + size > bytes.size()) {
        return false;
    }
    if (llvm::isa<llvm::ConstantAggregateZero>(value) || llvm::isa<llvm::UndefValue>(value) ||
        llvm::isa<llvm::PoisonValue>(value) || llvm::isa<llvm::ConstantPointerNull>(value)) {
        return true;
    }
    auto put_int = [&](const llvm::APInt &bits) {
        if (bits.getBitWidth() > 64) {
            return false;
        }
        const uint64_t raw = bits.getZExtValue();
        const unsigned nbytes = static_cast<unsigned>(size);
        const bool little = data_layout.isLittleEndian();
        for (unsigned i = 0; i < nbytes; ++i) {
            const unsigned shift = little ? i * 8 : (nbytes - 1U - i) * 8U;
            bytes[offset + i] = shift >= 64 ? 0 : static_cast<uint8_t>((raw >> shift) & 0xFFU);
        }
        return true;
    };
    if (const auto *integer = llvm::dyn_cast<llvm::ConstantInt>(value)) {
        return put_int(integer->getValue());
    }
    if (const auto *fp = llvm::dyn_cast<llvm::ConstantFP>(value)) {
        return put_int(fp->getValueAPF().bitcastToAPInt());
    }
    if (const auto *structure = llvm::dyn_cast<llvm::ConstantStruct>(value)) {
        const llvm::StructLayout *slots = data_layout.getStructLayout(llvm::cast<llvm::StructType>(type));
        for (unsigned index = 0; index < structure->getNumOperands(); ++index) {
            if (!write_constant_bytes(structure->getOperand(index), offset + slots->getElementOffset(index), bytes)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *array = llvm::dyn_cast<llvm::ConstantArray>(value)) {
        const uint64_t stride = data_layout.getTypeAllocSize(array->getType()->getElementType()).getFixedValue();
        for (unsigned index = 0; index < array->getNumOperands(); ++index) {
            if (!write_constant_bytes(array->getOperand(index), offset + stride * index, bytes)) {
                return false;
            }
        }
        return true;
    }
    if (const auto *data = llvm::dyn_cast<llvm::ConstantDataSequential>(value)) {
        const uint64_t stride = data_layout.getTypeAllocSize(data->getElementType()).getFixedValue();
        for (unsigned index = 0; index < data->getNumElements(); ++index) {
            if (!write_constant_bytes(data->getElementAsConstant(index), offset + stride * index, bytes)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

llvm::Constant *CodeGen::const_struct(const Expr::Init &init, int struct_id) {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.type == nullptr || !layout.type->isSized() || (layout.is_union && layout.tag_enum < 0)) {
        return nullptr;
    }
    if (layout.tag_enum >= 0) {
        if (layout.type->getNumElements() < 3 || layout.tag_enum >= static_cast<int>(enums_.size())) {
            return nullptr;
        }
        const auto *bytes_type = llvm::dyn_cast<llvm::ArrayType>(layout.type->getElementType(1));
        if (bytes_type == nullptr) {
            return nullptr;
        }
        std::vector<uint8_t> raw(bytes_type->getNumElements(), 0);
        int64_t tag_value = 0;
        for (const Expr::FieldInit &field : init.fields) {
            if (field.value == nullptr || field.field.empty()) {
                return nullptr;
            }
            int index = -1;
            const FieldLayout *slot = find_field(struct_id, field.field, index);
            if (slot == nullptr) {
                return nullptr;
            }
            llvm::Constant *value = const_field(*field.value, *slot);
            if (value == nullptr) {
                return nullptr;
            }
            std::fill(raw.begin(), raw.end(), 0);
            if (!write_constant_bytes(value, 0, raw)) {
                return nullptr;
            }
            const auto member = enums_[static_cast<std::size_t>(layout.tag_enum)].values.find(field.field);
            if (member == enums_[static_cast<std::size_t>(layout.tag_enum)].values.end()) {
                return nullptr;
            }
            tag_value = member->second;
        }
        llvm::Constant *tag = enum_constant(layout.tag_enum, tag_value);
        if (tag == nullptr || tag->getType() != layout.type->getElementType(2)) {
            return nullptr;
        }
        llvm::Constant *bytes = llvm::ConstantDataArray::get(context_, raw);
        if (bytes->getType() != bytes_type) {
            return nullptr;
        }
        return llvm::ConstantStruct::get(layout.type,
                                         {llvm::ConstantAggregateZero::get(layout.type->getElementType(0)), bytes, tag});
    }
    std::unordered_set<int> stack;
    return const_struct_into(&init, struct_id, true, stack);
}

llvm::Constant *CodeGen::default_struct_into(int struct_id, std::unordered_set<int> &stack) {
    return const_struct_into(nullptr, struct_id, false, stack);
}

llvm::Constant *CodeGen::default_struct(int struct_id) {
    std::unordered_set<int> stack;
    return default_struct_into(struct_id, stack);
}

llvm::Constant *CodeGen::init_image(int struct_id, const Expr::Init *named) {
    std::unordered_set<int> stack;
    return const_struct_into(named, struct_id, false, stack);
}

llvm::Constant *CodeGen::omitted_field(const FieldLayout &slot, std::unordered_set<int> &stack) {
    if (slot.init != nullptr) {
        // `{1}` on `[2]i32` is the constant `{1, 0}` here. A local stores only the named element.
        if (slot.is_array && !slot.is_slice) {
            auto *array_ty = llvm::dyn_cast_or_null<llvm::ArrayType>(slot.type);
            const auto *list = std::get_if<Expr::Init>(&slot.init->data);
            if (array_ty != nullptr && list != nullptr && array_brace(*list)) {
                Binding flags;
                flags.elem_char = slot.elem_char;
                flags.struct_id = slot.struct_id;
                flags.elem_void = slot.elem_void;
                flags.elem_bool = slot.elem_bool;
                flags.elem_bits = slot.elem_bits;
                flags.elem_unsigned = slot.elem_unsigned;
                flags.elem_float = slot.elem_float;
                flags.elem_slice = slot.elem_slice;
                flags.is_array = true;
                flags.array_len = slot.array_len;
                return const_default_array(*list, array_ty, flags);
            }
        }
        return const_field(*slot.init, slot);
    }
    if (slot.depth != 0 || slot.is_slice || slot.struct_id < 0) {
        return nullptr;
    }
    if (slot.is_array) {
        return default_array(slot.type, slot.struct_id, stack);
    }
    return default_struct_into(slot.struct_id, stack);
}

llvm::Constant *CodeGen::default_array(llvm::Type *type, int struct_id, std::unordered_set<int> &stack) {
    auto *array = llvm::dyn_cast_or_null<llvm::ArrayType>(type);
    if (array == nullptr || struct_id < 0) {
        return nullptr;
    }
    llvm::Type *elem = array->getElementType();
    llvm::Constant *one = nullptr;
    if (elem->isArrayTy()) {
        one = default_array(elem, struct_id, stack);
    } else {
        one = default_struct_into(struct_id, stack);
    }
    if (one == nullptr || one->getType() != elem) {
        return nullptr;
    }
    if (one->isNullValue()) {
        return llvm::ConstantAggregateZero::get(array);
    }
    std::vector<llvm::Constant *> values(array->getNumElements(), one);
    return llvm::ConstantArray::get(array, values);
}

llvm::Constant *CodeGen::const_struct_into(const Expr::Init *overlay, int struct_id, bool strict,
                                           std::unordered_set<int> &stack) {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.type == nullptr || !layout.type->isSized()) {
        return nullptr;
    }
    if (layout.is_union) {
        return strict ? nullptr : llvm::ConstantAggregateZero::get(layout.type);
    }
    // A by-value cycle is already rejected. The guard keeps a nested field of this struct at zero.
    if (!stack.insert(struct_id).second) {
        return llvm::ConstantAggregateZero::get(layout.type);
    }
    const uint32_t saved_file = current_file_;
    auto leave = [&](llvm::Constant *value) {
        stack.erase(struct_id);
        current_file_ = saved_file;
        return value;
    };
    const unsigned count = layout.type->getNumElements();
    std::vector<llvm::Constant *> elements(count, nullptr);
    std::vector<char> named(count, 0);
    // A named field is written at the use. Its names are looked up in that file.
    if (overlay != nullptr) {
        for (const Expr::FieldInit &field : overlay->fields) {
            if (field.value == nullptr || field.field.empty()) {
                if (strict) {
                    return leave(nullptr);
                }
                continue;
            }
            int index = -1;
            const FieldLayout *slot = find_field(struct_id, field.field, index);
            if (slot == nullptr || index < 0 || static_cast<unsigned>(index) >= count) {
                if (strict) {
                    return leave(nullptr);
                }
                continue;
            }
            named[static_cast<std::size_t>(index)] = 1;
            if (!strict) {
                continue;
            }
            llvm::Constant *value = const_field(*field.value, *slot);
            llvm::Type *expected = layout.type->getElementType(static_cast<unsigned>(index));
            if (value == nullptr || value->getType() != expected) {
                return leave(nullptr);
            }
            elements[static_cast<std::size_t>(index)] = value;
        }
    }
    // A value written on the field is looked up in the struct's file.
    current_file_ = layout.file_id;
    for (unsigned index = 0; index < count; ++index) {
        llvm::Type *expected = layout.type->getElementType(index);
        if (named[index] != 0) {
            if (elements[index] == nullptr) {
                elements[index] = llvm::Constant::getNullValue(expected);
            }
            continue;
        }
        llvm::Constant *value = nullptr;
        const bool have_slot = index < layout.fields.size();
        if (have_slot) {
            value = omitted_field(layout.fields[index], stack);
        }
        if (have_slot && layout.fields[index].init != nullptr &&
            (value == nullptr || value->getType() != expected)) {
            return leave(nullptr);
        }
        if (value == nullptr) {
            // `0...` writes a zero. A hole that was not requested stays unset.
            if (overlay != nullptr && overlay->zero_rest) {
                elements[index] = llvm::Constant::getNullValue(expected);
            } else {
                elements[index] = llvm::UndefValue::get(expected);
            }
            continue;
        }
        if (value->getType() != expected) {
            return leave(nullptr);
        }
        elements[index] = value;
    }
    bool zero = true;
    for (llvm::Constant *element : elements) {
        if (element == nullptr || !element->isNullValue()) {
            zero = false;
            break;
        }
    }
    if (zero) {
        return leave(llvm::ConstantAggregateZero::get(layout.type));
    }
    return leave(llvm::ConstantStruct::get(layout.type, elements));
}

bool CodeGen::struct_has_default(int struct_id, std::unordered_set<int> &seen) const {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return false;
    }
    if (!seen.insert(struct_id).second) {
        return false;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.is_union) {
        return false;
    }
    for (const FieldLayout &field : layout.fields) {
        if (field.init != nullptr) {
            return true;
        }
        if (field.depth != 0 || field.is_slice || field.struct_id < 0) {
            continue;
        }
        if (struct_has_default(field.struct_id, seen)) {
            return true;
        }
    }
    return false;
}

llvm::Constant *CodeGen::bare_initializer(const Lowered &low, llvm::Type *ty) {
    if (ty == nullptr) {
        return nullptr;
    }
    const bool plain = low.struct_id >= 0 && low.depth == 0 && !low.is_slice && !low.elem_slice &&
                       low.struct_id < static_cast<int>(structs_.size()) &&
                       !structs_[static_cast<std::size_t>(low.struct_id)].is_union;
    if (plain) {
        std::unordered_set<int> seen;
        if (struct_has_default(low.struct_id, seen)) {
            llvm::Constant *value = nullptr;
            if (low.is_array) {
                std::unordered_set<int> stack;
                value = default_array(ty, low.struct_id, stack);
            } else {
                value = default_struct(low.struct_id);
            }
            if (value != nullptr && value->getType() == ty) {
                return value;
            }
        }
    }
    return llvm::ConstantAggregateZero::get(ty);
}

void CodeGen::store_struct_defaults(llvm::Value *slot, int struct_id, std::unordered_set<int> &seen,
                                    const Expr::Init *overlay) {
    if (slot == nullptr || terminated() || struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return;
    }
    if (!seen.insert(struct_id).second) {
        return;
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(struct_id)];
    if (layout.is_union || layout.type == nullptr) {
        seen.erase(struct_id);
        return;
    }
    const uint32_t saved_file = current_file_;
    current_file_ = layout.file_id;
    for (int index = 0; index < static_cast<int>(layout.fields.size()); ++index) {
        if (terminated()) {
            break;
        }
        const FieldLayout &field = layout.fields[static_cast<std::size_t>(index)];
        bool named = false;
        if (overlay != nullptr) {
            for (const Expr::FieldInit &item : overlay->fields) {
                if (item.value != nullptr && item.field == field.name) {
                    named = true;
                    break;
                }
            }
        }
        // Naming the field replaces its value. A short list does not keep the omitted tail.
        if (named) {
            continue;
        }
        const bool nested = field.init == nullptr && field.depth == 0 && !field.is_slice && field.struct_id >= 0;
        bool dive = field.init != nullptr;
        if (!dive && nested) {
            std::unordered_set<int> probe;
            dive = struct_has_default(field.struct_id, probe);
        }
        if (!dive) {
            continue;
        }
        llvm::Value *addr = builder_.CreateInBoundsGEP(
            layout.type, slot, llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(index)});
        if (field.init != nullptr) {
            auto *array_ty = llvm::dyn_cast_or_null<llvm::ArrayType>(field.type);
            const auto *list = std::get_if<Expr::Init>(&field.init->data);
            // A brace list stores the elements it names. `0...` zeros the rest first.
            if (field.is_array && !field.is_slice && array_ty != nullptr && list != nullptr && array_brace(*list)) {
                store_array(addr, array_ty, *field.init, expr_loc(*field.init));
                continue;
            }
            llvm::Constant *value = const_field(*field.init, field);
            if (value != nullptr && field.type != nullptr && value->getType() == field.type) {
                builder_.CreateStore(value, addr);
            }
            continue;
        }
        if (field.is_array) {
            paint_omitted_elements(addr, llvm::dyn_cast<llvm::ArrayType>(field.type), field.struct_id, nullptr, seen);
            continue;
        }
        store_struct_defaults(addr, field.struct_id, seen);
    }
    current_file_ = saved_file;
    seen.erase(struct_id);
}

void CodeGen::paint_omitted_elements(llvm::Value *slot, llvm::ArrayType *array_ty, int struct_id, const Expr::Init *list,
                                     std::unordered_set<int> &seen) {
    if (slot == nullptr || array_ty == nullptr || terminated() || struct_id < 0) {
        return;
    }
    std::unordered_set<int> probe;
    if (!struct_has_default(struct_id, probe)) {
        return;
    }
    const unsigned count = array_ty->getNumElements();
    const unsigned written = list == nullptr ? 0 : static_cast<unsigned>(list->elements.size());
    llvm::Type *elem = array_ty->getElementType();
    auto *inner = llvm::dyn_cast<llvm::ArrayType>(elem);
    for (unsigned index = 0; index < count; ++index) {
        if (terminated()) {
            return;
        }
        llvm::Value *at = builder_.CreateInBoundsGEP(array_ty, slot,
                                                      {const_i32(0), const_i32(static_cast<int>(index))});
        if (index >= written || list == nullptr) {
            if (inner != nullptr) {
                paint_omitted_elements(at, inner, struct_id, nullptr, seen);
            } else {
                store_struct_defaults(at, struct_id, seen);
            }
            continue;
        }
        if (inner == nullptr || list->elements[index] == nullptr) {
            continue;
        }
        const auto *nested = std::get_if<Expr::Init>(&list->elements[index]->data);
        if (nested != nullptr) {
            paint_omitted_elements(at, inner, struct_id, nested, seen);
        }
    }
}

void CodeGen::store_each_element(llvm::Value *raw, llvm::Value *count, llvm::Type *elem, llvm::Constant *image) {
    if (raw == nullptr || count == nullptr || elem == nullptr || image == nullptr || terminated() || function_ == nullptr) {
        return;
    }
    if (image->getType() != elem || module_.getDataLayout().getTypeAllocSize(elem).getFixedValue() == 0) {
        return;
    }
    llvm::AllocaInst *step = alloca_typed(i32(), "new.i");
    builder_.CreateStore(const_i32(0), step);
    llvm::BasicBlock *cond = llvm::BasicBlock::Create(context_, "new.zero", function_);
    llvm::BasicBlock *body = llvm::BasicBlock::Create(context_, "new.zero.body", function_);
    llvm::BasicBlock *done = llvm::BasicBlock::Create(context_, "new.zero.done", function_);
    builder_.CreateBr(cond);
    builder_.SetInsertPoint(cond);
    llvm::Value *index = builder_.CreateLoad(i32(), step);
    builder_.CreateCondBr(builder_.CreateICmpSLT(index, count), body, done);
    builder_.SetInsertPoint(body);
    llvm::Value *at = builder_.CreateInBoundsGEP(elem, raw, llvm::ArrayRef<llvm::Value *>{index});
    builder_.CreateStore(image, at);
    builder_.CreateStore(builder_.CreateAdd(index, const_i32(1)), step);
    builder_.CreateBr(cond);
    builder_.SetInsertPoint(done);
}

std::optional<int32_t> CodeGen::folded_sizeof(const Expr::Sizeof &node) const {
    llvm::Type *measured = nullptr;
    if (node.on_type) {
        const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections,
                                  node.is_array, node.is_array ? node.array_len : 0, false, node.elem_slice, node.shells);
        if (low.depth == 0 && low.elem_void) {
            return std::nullopt;
        }
        measured = low.type;
    } else if (node.expr != nullptr) {
        const Shape shape = raw_shape(*node.expr);
        if (shape.is_function || (shape.depth == 0 && shape.elem_void)) {
            return std::nullopt;
        }
        measured = shape.type;
    }
    if (measured == nullptr || !measured->isSized() || measured->isFunctionTy()) {
        return std::nullopt;
    }
    const uint64_t bytes = node.align ? module_.getDataLayout().getABITypeAlign(measured).value()
                                      : module_.getDataLayout().getTypeAllocSize(measured).getFixedValue();
    if (bytes > 2147483647ULL) {
        return std::nullopt;
    }
    return static_cast<int32_t>(bytes);
}

const VarDecl *CodeGen::global_decl(const std::string &name) const {
    const PackageNode *node = find_node(current_file_);
    if (node == nullptr) {
        return nullptr;
    }
    for (const TopDecl &decl : node->unit.decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        if (var != nullptr && var->owner.empty() && var->name == name) {
            return var;
        }
    }
    return nullptr;
}

llvm::Constant *CodeGen::fill_array(const Expr::Init &init, llvm::ArrayType *array_ty, const Binding &flags) {
    if (array_ty == nullptr || init.elements.size() != array_ty->getNumElements() || !init.fields.empty()) {
        return nullptr;
    }
    llvm::Type *element = array_ty->getElementType();
    std::vector<llvm::Constant *> values;
    values.reserve(init.elements.size());
    for (const auto &item : init.elements) {
        if (!item) {
            return nullptr;
        }
        llvm::Constant *value = nullptr;
        if (element == slice_type()) {
            // `[_][]i32{a[:], b[:]}` stores one header per element.
            std::unordered_set<std::string> seen;
            value = constant_slice(*item, seen);
        } else if (auto *inner = llvm::dyn_cast<llvm::ArrayType>(element)) {
            const auto *nested = std::get_if<Expr::Init>(&item->data);
            if (nested != nullptr) {
                Binding row = flags;
                row.is_array = true;
                row.array_len = static_cast<int>(inner->getNumElements());
                row.inner_array_len = 0;
                row.value = nullptr;
                value = fill_array(*nested, inner, row);
            } else {
                // `[_][2]i32{inner}` and `[_][2]i32{grid[1]}` copy that constant row.
                value = const_array_base(*item);
            }
        } else {
            Binding one = flags;
            one.is_array = false;
            one.array_len = 0;
            one.inner_array_len = 0;
            one.value = nullptr;
            value = const_initializer(*item, one);
        }
        if (value == nullptr || value->getType() != element) {
            return nullptr;
        }
        values.push_back(value);
    }
    return llvm::ConstantArray::get(array_ty, values);
}

llvm::Constant *CodeGen::const_array(const Expr::Init &init, const Binding &dest) {
    auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(dest.value);
    if (global == nullptr || !dest.is_array) {
        return nullptr;
    }
    auto *array_ty = llvm::dyn_cast<llvm::ArrayType>(global->getValueType());
    if (array_ty == nullptr) {
        return nullptr;
    }
    return fill_array(init, array_ty, dest);
}

llvm::Constant *CodeGen::const_array_base(const Expr &expr) {
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        // `grid[1]` is one row of that constant. A range is a slice header.
        if (index->is_range || index->base == nullptr || index->index == nullptr) {
            return nullptr;
        }
        const std::optional<int32_t> at = fold(*index->index);
        if (!at || *at < 0) {
            return nullptr;
        }
        llvm::Constant *base = const_array_base(*index->base);
        if (base == nullptr || !base->getType()->isArrayTy()) {
            return nullptr;
        }
        auto *array_ty = llvm::cast<llvm::ArrayType>(base->getType());
        if (static_cast<uint64_t>(*at) >= array_ty->getNumElements()) {
            return nullptr;
        }
        return base->getAggregateElement(static_cast<unsigned>(*at));
    }
    const auto *named = std::get_if<Expr::Name>(&expr.data);
    if (named == nullptr) {
        return nullptr;
    }
    const auto file = files_.find(current_file_);
    if (file == files_.end()) {
        return nullptr;
    }
    const auto found = file->second.globals.find(named->text);
    if (found == file->second.globals.end() || !found->second.is_array) {
        return nullptr;
    }
    if (!const_names_.insert(named->text).second) {
        return nullptr;
    }
    const VarDecl *written = global_decl(named->text);
    llvm::Constant *copied = nullptr;
    if (written != nullptr && written->init != nullptr) {
        copied = const_array_copied(*written->init, found->second);
    } else if (auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found->second.value)) {
        copied = llvm::ConstantAggregateZero::get(global->getValueType());
    }
    const_names_.erase(named->text);
    return copied;
}

llvm::Constant *CodeGen::const_array_copied(const Expr &expr, const Binding &dest) {
    auto *slot = llvm::dyn_cast_or_null<llvm::GlobalVariable>(dest.value);
    if (slot == nullptr || !slot->getValueType()->isArrayTy()) {
        return nullptr;
    }
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        return const_array(*init, dest);
    }
    llvm::Constant *value = const_array_base(expr);
    if (value == nullptr || value->getType() != slot->getValueType()) {
        return nullptr;
    }
    return value;
}

llvm::Value *CodeGen::array_bits(llvm::Value *value, llvm::Type *array_ty, SourceLoc loc) {
    if (terminated()) {
        return nullptr;
    }
    if (value == nullptr || array_ty == nullptr) {
        error(loc, "not supported yet");
        return nullptr;
    }
    if (value->getType() == array_ty) {
        return value;
    }
    // An array name is the address of the array. A call yields the address of its first element.
    if (!value->getType()->isPointerTy()) {
        error(loc, "not supported yet");
        return nullptr;
    }
    return builder_.CreateLoad(array_ty, value);
}

void CodeGen::store_array_bits(llvm::Value *slot, llvm::Type *array_ty, llvm::Value *value, SourceLoc loc) {
    if (slot == nullptr || terminated()) {
        return;
    }
    llvm::Value *loaded = array_bits(value, array_ty, loc);
    if (loaded != nullptr && !terminated()) {
        builder_.CreateStore(loaded, slot);
    }
}

void CodeGen::store_array_list(llvm::Value *slot, llvm::ArrayType *array_ty, const Expr::Init &init) {
    if (slot == nullptr || array_ty == nullptr || terminated()) {
        return;
    }
    // `0...` zeros every element this list does not name. A short list leaves those elements as they are.
    if (init.zero_rest) {
        builder_.CreateStore(llvm::ConstantAggregateZero::get(array_ty), slot);
    }
    store_array_elements(slot, array_ty, init);
}

void CodeGen::store_array(llvm::Value *slot, llvm::ArrayType *array_ty, const Expr &expr, SourceLoc loc) {
    if (slot == nullptr || array_ty == nullptr || terminated()) {
        if (!terminated()) {
            error(loc, "not supported yet");
        }
        return;
    }
    if (const auto *list = std::get_if<Expr::Init>(&expr.data); list != nullptr && array_brace(*list)) {
        store_array_list(slot, array_ty, *list);
        return;
    }
    const TypedValue value = emit_rvalue(expr);
    if (terminated()) {
        return;
    }
    store_array_bits(slot, array_ty, value.value, loc);
}

void CodeGen::store_array_elements(llvm::Value *slot, llvm::ArrayType *array_ty, const Expr::Init &init) {
    if (slot == nullptr || array_ty == nullptr || terminated()) {
        return;
    }
    llvm::Type *elem = array_ty->getElementType();
    auto *inner = llvm::dyn_cast<llvm::ArrayType>(elem);
    const unsigned limit = array_ty->getNumElements();
    unsigned index = 0;
    for (const auto &element : init.elements) {
        if (index >= limit || terminated()) {
            break;
        }
        llvm::Value *slot_i =
            builder_.CreateInBoundsGEP(array_ty, slot, {const_i32(0), const_i32(static_cast<int>(index))});
        if (inner != nullptr) {
            // `{1, 2, 3}` and `[_]i32{1, 2, 3}` are both a list. The slot is that array.
            // A name, an index, or a call copies that row into the slot.
            const auto *nested = element == nullptr ? nullptr : std::get_if<Expr::Init>(&element->data);
            if (nested != nullptr && array_brace(*nested)) {
                store_array_list(slot_i, inner, *nested);
            } else if (element == nullptr) {
                error(init.loc, "not supported yet");
            } else {
                const TypedValue value = emit_rvalue(*element);
                if (terminated()) {
                    return;
                }
                store_array_bits(slot_i, inner, value.value, expr_loc(*element));
            }
        } else {
            const TypedValue value = element != nullptr ? emit_rvalue(*element) : TypedValue{};
            if (terminated()) {
                return;
            }
            llvm::Value *stored = nullptr;
            const SourceLoc loc = element == nullptr ? init.loc : expr_loc(*element);
            if (value.value == nullptr) {
                stored = llvm::Constant::getNullValue(elem);
            } else if (elem == slice_type()) {
                stored = as_slice_value(value, loc);
            } else {
                stored = as_stored(value.value, value.depth, elem, value.elem_unsigned);
            }
            builder_.CreateStore(stored, slot_i);
        }
        ++index;
    }
}

llvm::AllocaInst *CodeGen::emit_array_literal(const Expr::Init &init, Lowered &low) {
    const std::optional<Lowered> got = lower_array_literal(current_file_, init);
    if (!got || got->type == nullptr || !got->type->isArrayTy()) {
        error(init.loc, "not supported yet");
        return nullptr;
    }
    low = *got;
    llvm::Type *ty = stored_type(low);
    llvm::AllocaInst *slot = alloca_typed(ty, "array");
    store_array_list(slot, llvm::cast<llvm::ArrayType>(ty), init);
    return slot;
}

TypedValue CodeGen::emit_inferred_init(const Expr::Init &init) {
    // A nested `struct{...}` is emitted with its field, so it takes the earlier number.
    std::vector<llvm::Type *> body;
    std::vector<FieldLayout> fields;
    std::vector<llvm::Value *> values;
    body.reserve(init.fields.size());
    for (const Expr::FieldInit &field : init.fields) {
        if (field.value == nullptr || field.field.empty()) {
            error(field.loc, "type mismatch");
            return {zero(), 0};
        }
        TypedValue rhs = emit_rvalue(*field.value);
        if (terminated() || rhs.value == nullptr) {
            return {zero(), 0};
        }
        FieldLayout slot;
        slot.name = field.field;
        if (rhs.is_slice) {
            if (rhs.value->getType() != slice_type()) {
                rhs.value = as_slice_value(rhs, field.loc);
            }
            slot.type = slice_type();
            slot.is_slice = true;
            slot.depth = rhs.depth;
            slot.struct_id = rhs.struct_id;
            slot.elem_void = rhs.elem_void;
            slot.elem_bool = rhs.elem_bool;
            slot.elem_bits = rhs.elem_bits;
            slot.elem_unsigned = rhs.elem_unsigned;
            slot.elem_float = rhs.elem_float;
            slot.elem_char = rhs.elem_char;
            slot.elem_slice = rhs.elem_slice;
        } else if (rhs.array_len > 0 && rhs.value != nullptr) {
            llvm::Type *allocated = nullptr;
            if (auto *slot_ptr = llvm::dyn_cast<llvm::AllocaInst>(rhs.value)) {
                allocated = slot_ptr->getAllocatedType();
            }
            if (allocated == nullptr || !allocated->isArrayTy()) {
                error(field.loc, "type mismatch");
                return {zero(), 0};
            }
            slot.type = allocated;
            slot.is_array = true;
            slot.array_len = rhs.array_len;
            slot.elem_char = rhs.elem_char;
            slot.elem_bool = rhs.elem_bool;
            slot.elem_bits = rhs.elem_bits;
            slot.elem_unsigned = rhs.elem_unsigned;
            slot.elem_float = rhs.elem_float;
            slot.struct_id = rhs.struct_id;
        } else if (rhs.struct_id >= 0 && rhs.struct_id < static_cast<int>(structs_.size()) &&
                   structs_[static_cast<std::size_t>(rhs.struct_id)].type != nullptr) {
            slot.type = structs_[static_cast<std::size_t>(rhs.struct_id)].type;
            slot.struct_id = rhs.struct_id;
        } else if (rhs.depth > 0) {
            slot.type = ptr_type();
            slot.depth = rhs.depth;
            slot.struct_id = rhs.struct_id;
            slot.elem_void = rhs.elem_void;
            slot.elem_bool = rhs.elem_bool;
            slot.elem_bits = rhs.elem_bits;
            slot.elem_unsigned = rhs.elem_unsigned;
            slot.elem_float = rhs.elem_float;
            slot.elem_char = rhs.elem_char;
        } else if (std::holds_alternative<Expr::Bool>(field.value->data)) {
            // A bool value promotes to `i32`. The field keeps one bit, as a written `bool` field does.
            slot.type = i1();
            slot.elem_bool = true;
            rhs.value = builder_.CreateICmpNE(rhs.value, const_i32(0));
        } else if (rhs.elem_float || rhs.value->getType()->isFloatingPointTy()) {
            const bool wide = rhs.value->getType()->isDoubleTy() || rhs.elem_bits >= 64;
            slot.type = wide ? f64_ty() : f32_ty();
            slot.elem_float = true;
            slot.elem_bits = wide ? 64 : 32;
        } else {
            slot.type = rhs.value->getType();
            slot.elem_char = rhs.elem_char;
            slot.elem_bool = rhs.elem_bool;
            slot.elem_bits = rhs.elem_bits;
            slot.elem_unsigned = rhs.elem_unsigned;
        }
        if (slot.type == nullptr) {
            error(field.loc, "type mismatch");
            return {zero(), 0};
        }
        body.push_back(slot.type);
        fields.push_back(std::move(slot));
        values.push_back(rhs.value);
    }
    if (init.struct_id != static_cast<int>(structs_.size())) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    StructLayout layout;
    layout.name = ".struct." + std::to_string(init.struct_id);
    layout.type = llvm::StructType::create(context_, body, layout.name);
    layout.file_id = current_file_;
    layout.fields = std::move(fields);
    layout.ready = true;
    const int id = init.struct_id;
    structs_.push_back(std::move(layout));
    tags_[current_file_][structs_.back().name] = id;
    llvm::AllocaInst *slot = alloca_typed(structs_.back().type, "init");
    for (unsigned index = 0; index < values.size(); ++index) {
        const FieldLayout &field = structs_.back().fields[index];
        const TypedValue addr = field_addr(id, slot, field.name, init.loc);
        if (addr.value == nullptr) {
            continue;
        }
        llvm::Value *stored = values[static_cast<std::size_t>(index)];
        if (field.is_array) {
            stored = builder_.CreateLoad(field.type, stored);
        }
        builder_.CreateStore(stored, addr.value);
    }
    return load_object(slot, 0, false, init.struct_id);
}

TypedValue CodeGen::emit_init(const Expr::Init &init) {
    if (init.struct_id >= static_cast<int>(structs_.size()) && init.type_name.empty() && !init.fields.empty()) {
        return emit_inferred_init(init);
    }
    if (init.struct_id < 0 || init.struct_id >= static_cast<int>(structs_.size())) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(init.struct_id)];
    if (layout.type == nullptr) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    // Named fields and `0...` are stored. A field with a value is stored when the list does not zero it.
    llvm::AllocaInst *slot = alloca_typed(layout.type, "init");
    if (init.zero_rest || layout.is_union) {
        builder_.CreateStore(llvm::ConstantAggregateZero::get(layout.type), slot);
    } else if (!layout.is_union) {
        std::unordered_set<int> seen;
        store_struct_defaults(slot, init.struct_id, seen, &init);
    }
    store_init_fields(init.struct_id, slot, init);
    if (terminated()) {
        return {zero(), 0};
    }
    return load_object(slot, 0, false, init.struct_id);
}

void CodeGen::store_init_fields(int struct_id, llvm::Value *slot, const Expr::Init &init) {
    if (slot == nullptr) {
        return;
    }
    for (const Expr::FieldInit &field : init.fields) {
        if (terminated()) {
            return;
        }
        if (field.value == nullptr || field.field.empty()) {
            continue;
        }
        const TypedValue addr = field_addr(struct_id, slot, field.field, field.loc);
        if (addr.value == nullptr) {
            continue;
        }
        if (addr.is_array) {
            // `Holder{.raw = {4, 6}}` and `Holder{.raw = row}` both store into this field.
            if (addr.array_type == nullptr || !addr.array_type->isArrayTy() || addr.value == nullptr) {
                error(field.loc, "not supported yet");
                continue;
            }
            store_array(addr.value, llvm::cast<llvm::ArrayType>(addr.array_type), *field.value, field.loc);
            if (terminated()) {
                return;
            }
            store_union_tag(struct_id, slot, field.field);
            continue;
        }
        const TypedValue rhs = emit_rvalue(*field.value);
        if (terminated()) {
            return;
        }
        if (rhs.value == nullptr) {
            continue;
        }
        if (addr.is_slice) {
            builder_.CreateStore(as_slice_value(rhs, field.loc), addr.value);
        } else {
            builder_.CreateStore(stored_value(rhs, addr), addr.value);
        }
        // The enum tag is stored after the payload.
        store_union_tag(struct_id, slot, field.field);
    }
}

llvm::Constant *CodeGen::intern_type_name(const std::string &text) {
    const auto found = type_names_.find(text);
    if (found != type_names_.end()) {
        return found->second;
    }
    llvm::Constant *data = llvm::ConstantDataArray::getString(context_, text, true);
    auto *global = new llvm::GlobalVariable(module_, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data, ".type");
    type_names_.emplace(text, global);
    return global;
}

llvm::StructType *CodeGen::type_desc_type() {
    if (type_desc_ty_ != nullptr) {
        return type_desc_ty_;
    }
    // `{ *u8 name, []const Field fields }`. `name()` loads the first field. `fields()` loads the second.
    auto *type = llvm::StructType::create(context_, "tlc.type");
    type->setBody({ptr_type(), slice_type()});
    type_desc_ty_ = type;
    return type;
}

llvm::Constant *CodeGen::intern_type_desc(const std::string &text) {
    const auto found = type_descs_.find(text);
    if (found != type_descs_.end()) {
        return found->second;
    }
    auto *global = new llvm::GlobalVariable(module_, type_desc_type(), true, llvm::GlobalValue::PrivateLinkage, nullptr,
                                            ".tdesc");
    type_descs_.emplace(text, global);
    llvm::Constant *fields = llvm::ConstantStruct::get(slice_type(), {null_ptr(), const_i32(0)});
    if (graph_ != nullptr && field_struct_id_ >= 0 && field_struct_id_ < static_cast<int>(structs_.size())) {
        const auto reflected = graph_->reflected_fields.find(text);
        if (reflected != graph_->reflected_fields.end() && !reflected->second.empty()) {
            llvm::StructType *field_ty = structs_[static_cast<std::size_t>(field_struct_id_)].type;
            std::vector<llvm::Constant *> elems;
            elems.reserve(reflected->second.size());
            for (const ReflectedField &field : reflected->second) {
                llvm::Constant *bytes = intern_type_name(field.name);
                llvm::Constant *name = llvm::ConstantStruct::get(
                    slice_type(), {bytes, const_i32(static_cast<int32_t>(field.name.size()))});
                llvm::Constant *kind = type_kind_ptr(field.type);
                elems.push_back(llvm::ConstantStruct::get(field_ty, {name, kind}));
            }
            auto *array_ty = llvm::ArrayType::get(field_ty, elems.size());
            auto *array = new llvm::GlobalVariable(module_, array_ty, true, llvm::GlobalValue::PrivateLinkage,
                                                   llvm::ConstantArray::get(array_ty, elems), ".tfields");
            fields = llvm::ConstantStruct::get(slice_type(), {array, const_i32(static_cast<int32_t>(elems.size()))});
        }
    }
    global->setInitializer(llvm::ConstantStruct::get(type_desc_type(), {intern_type_name(text), fields}));
    return global;
}

llvm::Constant *CodeGen::type_kind_ptr(const std::string &text) {
    const auto found = type_kind_ptrs_.find(text);
    if (found != type_kind_ptrs_.end()) {
        return found->second;
    }
    // `*const Type` points at the descriptor address. One cell is shared by every field of that type.
    auto *cell = new llvm::GlobalVariable(module_, ptr_type(), true, llvm::GlobalValue::PrivateLinkage,
                                          intern_type_desc(text), ".tkind");
    type_kind_ptrs_.emplace(text, cell);
    return cell;
}

llvm::Constant *CodeGen::sized_int(const Binding &dest, int32_t value) const {
    // f64 is 64 bits wide. That width is an integer only when the slot is not a float.
    if (dest.depth != 0 || dest.struct_id >= 0 || dest.fn_ty != nullptr || dest.elem_float) {
        return nullptr;
    }
    if (dest.elem_bool) {
        return llvm::ConstantInt::get(i1(), value == 0 ? 0 : 1, false);
    }
    if (dest.elem_char || dest.elem_bits == 8) {
        return llvm::ConstantInt::get(i8(), static_cast<uint64_t>(value) & 0xFFU, false);
    }
    if (dest.elem_bits == 16) {
        return llvm::ConstantInt::get(i16(), static_cast<uint64_t>(value) & 0xFFFFU, false);
    }
    if (dest.elem_bits == 64) {
        return llvm::ConstantInt::get(i64(), static_cast<uint64_t>(static_cast<int64_t>(value)), true);
    }
    return const_i32(value);
}

llvm::Constant *CodeGen::const_initializer(const Expr &expr, const Binding &dest) {
    // Initializers run before function scopes exist. Names are the file's globals and functions.
    const auto find_here = [&](const std::string &name) -> const Binding * {
        const auto file = files_.find(current_file_);
        if (file == files_.end()) {
            return nullptr;
        }
        const auto global = file->second.globals.find(name);
        if (global != file->second.globals.end()) {
            return &global->second;
        }
        const auto function = file->second.functions.find(name);
        if (function != file->second.functions.end()) {
            return &function->second;
        }
        return nullptr;
    };
    const Expr *cursor = &expr;
    while (const auto *cast = std::get_if<Expr::Cast>(&cursor->data)) {
        if (cast->expr == nullptr || cast->indirections != 0) {
            break;
        }
        cursor = cast->expr.get();
    }
    if (const auto *init = std::get_if<Expr::Init>(&cursor->data)) {
        if (dest.is_array) {
            return const_array(*init, dest);
        }
        if (dest.struct_id < 0) {
            return nullptr;
        }
        return const_struct(*init, dest.struct_id);
    }
    if (const Binding *designator = function_designator(current_file_, *cursor)) {
        auto *function = llvm::dyn_cast_or_null<llvm::Function>(designator->value);
        if (function == nullptr || dest.depth <= 0 || dest.fn_ty == nullptr) {
            return nullptr;
        }
        return function;
    }
    llvm::Type *float_ty = dest.elem_bits >= 64 ? f64_ty() : f32_ty();
    const auto as_float = [&](llvm::Constant *value) -> llvm::Constant * {
        if (!dest.elem_float || value == nullptr) {
            return value;
        }
        if (value->getType() == float_ty) {
            return value;
        }
        if (const auto *fp = llvm::dyn_cast<llvm::ConstantFP>(value)) {
            return llvm::ConstantFP::get(float_ty, fp->getValueAPF().convertToDouble());
        }
        if (const auto *integer = llvm::dyn_cast<llvm::ConstantInt>(value)) {
            return llvm::ConstantFP::get(float_ty, static_cast<double>(integer->getSExtValue()));
        }
        return nullptr;
    };
    if (const auto *literal = std::get_if<Expr::Float>(&cursor->data)) {
        if (!dest.elem_float) {
            return nullptr;
        }
        const std::string number = literal->text.empty() ? "0" : literal->text;
        return llvm::ConstantFP::get(float_ty, std::strtod(number.c_str(), nullptr));
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
        unary != nullptr && unary->expr != nullptr && dest.elem_float &&
        (unary->op == TokenKind::Plus || unary->op == TokenKind::Minus)) {
        llvm::Constant *inner = const_initializer(*unary->expr, dest);
        if (inner == nullptr || unary->op == TokenKind::Plus) {
            return inner;
        }
        const auto *fp = llvm::dyn_cast<llvm::ConstantFP>(inner);
        if (fp == nullptr) {
            return nullptr;
        }
        llvm::APFloat negated = fp->getValueAPF();
        negated.changeSign();
        return llvm::ConstantFP::get(context_, negated);
    }
    if (std::holds_alternative<Expr::Nullptr>(cursor->data)) {
        return dest.depth > 0 ? null_ptr() : nullptr;
    }
    // `const label *const u8 = "unix"` stores the address of this string.
    if (const auto *text = std::get_if<Expr::String>(&cursor->data)) {
        if (dest.depth != 1 || (dest.elem_bits != 8 && !dest.elem_char)) {
            return nullptr;
        }
        llvm::Constant *data = llvm::ConstantDataArray::getString(context_, text->text, true);
        return new llvm::GlobalVariable(module_, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data, ".str");
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
        unary != nullptr && unary->op == TokenKind::Amp && unary->expr != nullptr) {
        if (const auto *named = std::get_if<Expr::Name>(&unary->expr->data)) {
            if (const Binding *binding = find_here(named->text)) {
                if (auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(binding->value);
                    global != nullptr && dest.depth > 0) {
                    return global;
                }
            }
        }
        return nullptr;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&cursor->data); selector != nullptr && selector->base != nullptr) {
        const int enum_id = enum_type_designator(*selector->base);
        if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
            const auto member = enums_[static_cast<std::size_t>(enum_id)].values.find(selector->field);
            if (member != enums_[static_cast<std::size_t>(enum_id)].values.end()) {
                return sized_int(dest, static_cast<int32_t>(member->second));
            }
        }
    }
    if (const auto *named = std::get_if<Expr::Name>(&cursor->data)) {
        if (const Binding *binding = find_here(named->text)) {
            if (binding->immediate) {
                if (dest.elem_float) {
                    return llvm::ConstantFP::get(float_ty, static_cast<double>(binding->const_value));
                }
                return sized_int(dest, binding->const_value);
            }
            if (auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(binding->value);
                global != nullptr && global->isConstant() && global->hasInitializer()) {
                return as_float(global->getInitializer());
            }
            // `var n = 1; var row = [_]i32{n}` copies n's constant initializer.
            if (const VarDecl *written = global_decl(named->text)) {
                if (!const_names_.insert(named->text).second) {
                    return nullptr;
                }
                llvm::Constant *copied = nullptr;
                if (written->has_const) {
                    copied = dest.elem_float
                                 ? llvm::ConstantFP::get(float_ty, static_cast<double>(written->const_value))
                                 : sized_int(dest, written->const_value);
                } else if (written->init != nullptr) {
                    copied = const_initializer(*written->init, dest);
                } else {
                    llvm::Type *ty = leaf_type(dest.depth, dest.elem_char, dest.struct_id, dest.elem_bool, dest.elem_bits,
                                               dest.elem_float);
                    copied = ty == nullptr ? nullptr : llvm::Constant::getNullValue(ty);
                }
                const_names_.erase(named->text);
                return copied;
            }
        }
    }
    if (dest.elem_float) {
        std::unordered_set<std::string> seen;
        if (const std::optional<FoldedNumber> number = fold_number(*cursor, seen); number && number->is_float) {
            llvm::Type *ty = dest.elem_bits >= 64 ? f64_ty() : f32_ty();
            return llvm::ConstantFP::get(ty, number->value);
        }
        const std::optional<int32_t> number = fold(*cursor);
        if (!number) {
            return nullptr;
        }
        llvm::Type *ty = dest.elem_bits >= 64 ? f64_ty() : f32_ty();
        return llvm::ConstantFP::get(ty, static_cast<double>(*number));
    }
    const std::optional<int32_t> number = fold(*cursor);
    if (!number) {
        return nullptr;
    }
    if (dest.depth > 0) {
        return *number == 0 ? null_ptr() : nullptr;
    }
    return sized_int(dest, *number);
}

std::optional<CodeGen::SlicePieces> CodeGen::slice_pieces(const Expr &expr, std::unordered_set<std::string> &seen) {
    if (const auto *text = std::get_if<Expr::String>(&expr.data)) {
        llvm::Constant *data = llvm::ConstantDataArray::getString(context_, text->text, true);
        auto *global = new llvm::GlobalVariable(module_, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data,
                                                ".str");
        SlicePieces pieces;
        pieces.element = i8();
        pieces.pointer = llvm::ConstantExpr::getInBoundsGetElementPtr(
            data->getType(), global, llvm::ArrayRef<llvm::Constant *>{const_i32(0), const_i32(0)});
        pieces.length = static_cast<int32_t>(text->text.size());
        return pieces;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        // `numbers[i:j]` is the header of that array, shifted by `i` elements.
        // A missing low bound is 0. A missing high bound is the base length.
        if (!index->is_range || index->base == nullptr) {
            return std::nullopt;
        }
        const std::optional<SlicePieces> base = slice_pieces(*index->base, seen);
        if (!base || base->pointer == nullptr || base->element == nullptr) {
            return std::nullopt;
        }
        const std::optional<int32_t> low = index->index == nullptr ? std::optional<int32_t>{0} : fold(*index->index);
        const std::optional<int32_t> high = index->end == nullptr ? std::optional<int32_t>{base->length} : fold(*index->end);
        if (!low || !high) {
            return std::nullopt;
        }
        llvm::Constant *pointer = base->pointer;
        if (*low != 0) {
            // An uninitialized slice has no element to step from.
            if (llvm::isa<llvm::ConstantPointerNull>(pointer)) {
                return std::nullopt;
            }
            pointer = llvm::ConstantExpr::getInBoundsGetElementPtr(base->element, pointer,
                                                                   llvm::ArrayRef<llvm::Constant *>{const_i32(*low)});
        }
        SlicePieces pieces;
        pieces.pointer = pointer;
        pieces.element = base->element;
        pieces.length = static_cast<int32_t>(static_cast<uint32_t>(*high) - static_cast<uint32_t>(*low));
        return pieces;
    }
    const auto *name = std::get_if<Expr::Name>(&expr.data);
    if (name == nullptr || !seen.insert(name->text).second) {
        return std::nullopt;
    }
    const Binding *binding = nullptr;
    if (const auto file = files_.find(current_file_); file != files_.end()) {
        const auto found = file->second.globals.find(name->text);
        if (found != file->second.globals.end()) {
            binding = &found->second;
        }
    }
    if (binding == nullptr) {
        binding = visible(current_file_, name->text, false);
    }
    std::optional<SlicePieces> pieces;
    auto *global = binding == nullptr ? nullptr : llvm::dyn_cast<llvm::GlobalVariable>(binding->value);
    if (binding != nullptr && global != nullptr && binding->is_array && global->getValueType()->isArrayTy()) {
        auto *array = llvm::cast<llvm::ArrayType>(global->getValueType());
        pieces = SlicePieces{};
        pieces->element = array->getElementType();
        pieces->pointer = llvm::ConstantExpr::getInBoundsGetElementPtr(
            array, global, llvm::ArrayRef<llvm::Constant *>{const_i32(0), const_i32(0)});
        pieces->length = binding->array_len > 0 ? binding->array_len : static_cast<int32_t>(array->getNumElements());
    } else if (binding != nullptr && binding->is_slice) {
        const VarDecl *source = global_decl(name->text);
        if (source == nullptr || source->init == nullptr) {
            pieces = SlicePieces{};
            pieces->pointer = null_ptr();
            pieces->element = i8();
            pieces->length = 0;
        } else {
            pieces = slice_pieces(*source->init, seen);
        }
    }
    seen.erase(name->text);
    return pieces;
}

llvm::Constant *CodeGen::constant_slice(const Expr &expr, std::unordered_set<std::string> &seen) {
    const std::optional<SlicePieces> pieces = slice_pieces(expr, seen);
    if (!pieces || pieces->pointer == nullptr) {
        return nullptr;
    }
    return llvm::ConstantStruct::get(slice_type(), {pieces->pointer, const_i32(pieces->length)});
}

void CodeGen::apply_initializer(const VarDecl &var) {
    if (!var.owner_params.empty()) {
        return;
    }
    // `var p *i32 = row` at file scope stores the address of the first element.
    auto element_pointer = [&](const Expr &expr) -> llvm::Constant * {
        const auto *named = std::get_if<Expr::Name>(&expr.data);
        if (named == nullptr) {
            return nullptr;
        }
        const Binding *binding = nullptr;
        if (const auto file = files_.find(current_file_); file != files_.end()) {
            const auto found = file->second.globals.find(named->text);
            if (found != file->second.globals.end()) {
                binding = &found->second;
            }
        }
        if (binding == nullptr) {
            binding = visible(current_file_, named->text, false);
        }
        auto *global = binding == nullptr ? nullptr : llvm::dyn_cast<llvm::GlobalVariable>(binding->value);
        if (binding == nullptr || global == nullptr || !binding->is_array) {
            return nullptr;
        }
        auto *array = llvm::dyn_cast<llvm::ArrayType>(global->getValueType());
        if (array == nullptr) {
            return nullptr;
        }
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            array, global, llvm::ArrayRef<llvm::Constant *>{const_i32(0), const_i32(0)});
    };
    const std::string key = global_key(var);
    FileSymbols &file = files_[current_file_];
    if (var.is_const_decl && var.has_const) {
        file.folded.insert(key);
        return;
    }
    if (var.is_const_decl) {
        const auto found = file.globals.find(key);
        auto *global = found == file.globals.end() ? nullptr : llvm::dyn_cast_or_null<llvm::GlobalVariable>(found->second.value);
        if (found != file.globals.end() && found->second.is_slice && var.init != nullptr && global != nullptr) {
            std::unordered_set<std::string> seen;
            llvm::Constant *packed = constant_slice(*var.init, seen);
            if (packed == nullptr || packed->getType() != global->getValueType()) {
                error(var.loc, "global initializer is not a constant");
                return;
            }
            global->setInitializer(packed);
            file.folded.insert(key);
            return;
        }
        // `const p *i32 = row` stores the address of the first element.
        if (found != file.globals.end() && found->second.depth > 0 && !found->second.is_array &&
            !found->second.is_slice && var.init != nullptr && global != nullptr) {
            if (llvm::Constant *pointer = element_pointer(*var.init)) {
                global->setInitializer(pointer);
                file.folded.insert(key);
                return;
            }
        }
        // `const frozen [2]i32 = held` copies that array. A list still uses the element path.
        if (found != file.globals.end() && found->second.is_array && var.init != nullptr && global != nullptr &&
            !std::holds_alternative<Expr::Init>(var.init->data)) {
            llvm::Constant *copied = const_array_copied(*var.init, found->second);
            if (copied == nullptr || copied->getType() != global->getValueType()) {
                error(var.loc, "global initializer is not a constant");
                return;
            }
            global->setInitializer(copied);
            file.folded.insert(key);
            return;
        }
        llvm::Constant *value = var.init == nullptr || global == nullptr ? nullptr : const_initializer(*var.init, found->second);
        if (value == nullptr || value->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(value);
        file.folded.insert(key);
        return;
    }
    if (var.is_extern && !var.init) {
        return;
    }
    if (!var.init) {
        file.folded.insert(key);
        return;
    }
    // A call is stored before main. The slot stays zero until that call runs.
    if (var.runtime_init) {
        return;
    }
    const auto found_early = file.globals.find(key);
    if (const auto *kind = std::get_if<Expr::Typeof>(&var.init->data)) {
        const bool type_slot = found_early != file.globals.end() && found_early->second.depth == 1 &&
                               !found_early->second.elem_char && found_early->second.struct_id < 0 &&
                               !found_early->second.elem_void && !found_early->second.elem_bool &&
                               !found_early->second.elem_float && found_early->second.elem_bits == 0 &&
                               found_early->second.enum_id < 0;
        if (!type_slot || kind->canonical.empty() || found_early->second.value == nullptr) {
            error(var.loc, "type mismatch");
            return;
        }
        auto *global = llvm::cast<llvm::GlobalVariable>(found_early->second.value);
        global->setInitializer(intern_type_desc(kind->canonical));
        file.folded.insert(key);
        return;
    }
    if (found_early != file.globals.end() && found_early->second.is_slice) {
        std::unordered_set<std::string> seen;
        llvm::Constant *packed = constant_slice(*var.init, seen);
        auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found_early->second.value);
        if (packed == nullptr || global == nullptr || packed->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(packed);
        file.folded.insert(key);
        return;
    }
    if (const auto *init = std::get_if<Expr::Init>(&var.init->data)) {
        if (found_early != file.globals.end() && found_early->second.is_array) {
            llvm::Constant *value = const_array(*init, found_early->second);
            if (value == nullptr || found_early->second.value == nullptr) {
                error(var.loc, "global initializer is not a constant");
                return;
            }
            llvm::cast<llvm::GlobalVariable>(found_early->second.value)->setInitializer(value);
            file.folded.insert(key);
            return;
        }
        if (found_early == file.globals.end() || found_early->second.struct_id < 0) {
            error(var.loc, "type mismatch");
            return;
        }
        llvm::Constant *value = const_struct(*init, found_early->second.struct_id);
        if (value == nullptr) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        auto *global = llvm::cast<llvm::GlobalVariable>(found_early->second.value);
        global->setInitializer(value);
        file.folded.insert(key);
        return;
    }
    // `var p *i32 = row` stores the address of the first element.
    if (found_early != file.globals.end() && found_early->second.depth > 0 && !found_early->second.is_array &&
        !found_early->second.is_slice && var.init != nullptr) {
        auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found_early->second.value);
        if (llvm::Constant *pointer = element_pointer(*var.init); pointer != nullptr && global != nullptr) {
            global->setInitializer(pointer);
            file.folded.insert(key);
            return;
        }
    }
    // `var copy [2]i32 = row` and `var line [2]i32 = grid[1]` copy that constant array.
    if (found_early != file.globals.end() && found_early->second.is_array && var.init != nullptr &&
        !std::holds_alternative<Expr::Init>(var.init->data)) {
        llvm::Constant *copied = const_array_copied(*var.init, found_early->second);
        auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found_early->second.value);
        if (copied == nullptr || global == nullptr || copied->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(copied);
        file.folded.insert(key);
        return;
    }
    if (const Binding *designator = function_designator(current_file_, *var.init)) {
        auto *function = llvm::dyn_cast<llvm::Function>(designator->value);
        if (found_early == file.globals.end() || found_early->second.fn_ty == nullptr ||
            found_early->second.value == nullptr || function == nullptr) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        llvm::cast<llvm::GlobalVariable>(found_early->second.value)->setInitializer(function);
        file.folded.insert(key);
        return;
    }
    if (found_early != file.globals.end() && found_early->second.elem_float && !found_early->second.is_array &&
        found_early->second.depth == 0) {
        auto *global = llvm::dyn_cast_or_null<llvm::GlobalVariable>(found_early->second.value);
        llvm::Constant *value = global == nullptr ? nullptr : const_initializer(*var.init, found_early->second);
        if (value == nullptr || global == nullptr || value->getType() != global->getValueType()) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(value);
        file.folded.insert(key);
        return;
    }
    const auto value = fold(*var.init);
    const auto found = file.globals.find(key);
    if (!value || found == file.globals.end()) {
        error(var.loc, "global initializer is not a constant");
        return;
    }
    auto *global = llvm::cast<llvm::GlobalVariable>(found->second.value);
    if (var.is_array) {
        error(var.loc, "not supported yet");
        return;
    }
    if (var.is_struct || !var.type_package.empty() || found->second.struct_id >= 0 || found->second.trait_id >= 0 ||
        found->second.elem_float) {
        error(var.loc, "global initializer is not a constant");
        return;
    }
    if (var.indirections > 0 || found->second.depth > 0) {
        if (*value != 0) {
            error(var.loc, "global initializer is not a constant");
            return;
        }
        global->setInitializer(null_ptr());
    } else if (found->second.elem_bool) {
        global->setInitializer(llvm::ConstantInt::get(i1(), *value == 0 ? 0 : 1, false));
    } else if (found->second.elem_bits == 8) {
        global->setInitializer(llvm::ConstantInt::get(i8(), static_cast<uint64_t>(*value) & 0xFFU, false));
    } else if (found->second.elem_bits == 16) {
        global->setInitializer(llvm::ConstantInt::get(i16(), static_cast<uint64_t>(*value) & 0xFFFFU, false));
    } else if (found->second.elem_bits == 64) {
        global->setInitializer(llvm::ConstantInt::get(i64(), static_cast<uint64_t>(static_cast<int64_t>(*value)), true));
    } else {
        global->setInitializer(const_i32(*value));
    }
    file.folded.insert(key);
}

} // namespace tlc
