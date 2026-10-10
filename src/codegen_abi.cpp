// x86-64 System V classification for extern structs and unions.
#include "gen.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tlc {

void CodeGen::collect_abi_leaves(llvm::Type *type, uint64_t offset, std::vector<AbiLeaf> &leaves) const {
    if (type == nullptr) {
        return;
    }
    const llvm::DataLayout &layout = module_.getDataLayout();
    if (auto *aggregate = llvm::dyn_cast<llvm::StructType>(type)) {
        if (!aggregate->isSized()) {
            return;
        }
        const llvm::StructLayout *slots = layout.getStructLayout(aggregate);
        for (unsigned index = 0; index < aggregate->getNumElements(); ++index) {
            collect_abi_leaves(aggregate->getElementType(index), offset + slots->getElementOffset(index), leaves);
        }
        return;
    }
    if (auto *array = llvm::dyn_cast<llvm::ArrayType>(type)) {
        const uint64_t step = layout.getTypeAllocSize(array->getElementType());
        for (uint64_t index = 0; index < array->getNumElements(); ++index) {
            collect_abi_leaves(array->getElementType(), offset + index * step, leaves);
        }
        return;
    }
    AbiLeaf leaf;
    leaf.offset = offset;
    leaf.bytes = type->isIntegerTy(1) ? 1 : layout.getTypeAllocSize(type);
    leaf.type = type;
    leaves.push_back(leaf);
}

CAbi CodeGen::c_abi(llvm::StructType *type) const {
    CAbi abi;
    if (type == nullptr || !type->isSized()) {
        abi.memory = true;
        return abi;
    }
    const llvm::DataLayout &layout = module_.getDataLayout();
    const uint64_t size = layout.getTypeAllocSize(type);
    const unsigned natural = static_cast<unsigned>(layout.getABITypeAlign(type).value());
    std::vector<AbiLeaf> leaves;
    collect_abi_leaves(type, 0, leaves);
    return classify_abi(size, natural == 0 ? 1 : natural, leaves);
}

CAbi CodeGen::c_abi_union(int struct_id) const {
    CAbi abi;
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size()) ||
        structs_[static_cast<std::size_t>(struct_id)].type == nullptr ||
        !structs_[static_cast<std::size_t>(struct_id)].type->isSized()) {
        abi.memory = true;
        return abi;
    }
    const StructLayout &union_layout = structs_[static_cast<std::size_t>(struct_id)];
    const llvm::DataLayout &layout = module_.getDataLayout();
    const uint64_t size = layout.getTypeAllocSize(union_layout.type);
    const unsigned natural = static_cast<unsigned>(layout.getABITypeAlign(union_layout.type).value());
    // Each field occupies the start of the union. The merge is the x86-64 rule for overlapping classes.
    std::vector<AbiLeaf> leaves;
    for (const FieldLayout &field : union_layout.fields) {
        collect_abi_leaves(field.type, 0, leaves);
    }
    return classify_abi(size, natural == 0 ? 1 : natural, leaves);
}

CAbi CodeGen::classify_abi(uint64_t size, unsigned natural, const std::vector<AbiLeaf> &leaves) const {
    CAbi abi;
    const llvm::DataLayout &layout = module_.getDataLayout();
    abi.natural_align = natural == 0 ? 1 : natural;
    abi.indirect_align = std::max(abi.natural_align, 8u);
    if (size == 0 || size > 16) {
        abi.memory = true;
        return abi;
    }

    auto aligned = [&](const AbiLeaf &leaf) {
        unsigned align = 1;
        if (leaf.type->isIntegerTy(1)) {
            align = 1;
        } else {
            align = static_cast<unsigned>(layout.getABITypeAlign(leaf.type).value());
        }
        return align == 0 || leaf.offset % align == 0;
    };
    for (const AbiLeaf &leaf : leaves) {
        if (!aligned(leaf)) {
            abi.memory = true;
            return abi;
        }
    }

    enum class Kind { None, Integer, Sse, Memory };
    auto merge = [](Kind acc, Kind field) {
        if (acc == Kind::Memory || field == Kind::Memory) {
            return Kind::Memory;
        }
        if (acc == Kind::None || acc == field) {
            return field;
        }
        if (field == Kind::None) {
            return acc;
        }
        if (acc == Kind::Integer || field == Kind::Integer) {
            return Kind::Integer;
        }
        return Kind::Sse;
    };
    auto overlaps = [](uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; };
    auto no_user = [&](uint64_t bit0, uint64_t bit1) {
        if (bit1 <= bit0) {
            return true;
        }
        for (const AbiLeaf &leaf : leaves) {
            const uint64_t start = leaf.offset * 8;
            const uint64_t end = start + leaf.bytes * 8;
            if (overlaps(start, end, bit0, bit1)) {
                return false;
            }
        }
        return true;
    };
    auto kind_of = [](llvm::Type *ty) {
        if (ty->isFloatingPointTy()) {
            return Kind::Sse;
        }
        if (ty->isIntegerTy() || ty->isPointerTy()) {
            return Kind::Integer;
        }
        return Kind::Memory;
    };
    Kind lo = Kind::None;
    Kind hi = Kind::None;
    for (const AbiLeaf &leaf : leaves) {
        const uint64_t start = leaf.offset;
        const uint64_t end = leaf.offset + std::max<uint64_t>(leaf.bytes, 1);
        const Kind kind = kind_of(leaf.type);
        if (kind == Kind::Memory) {
            abi.memory = true;
            return abi;
        }
        if (start < 8) {
            lo = merge(lo, kind);
        }
        if (end > 8) {
            hi = merge(hi, kind);
        }
    }
    if (lo == Kind::Memory || hi == Kind::Memory || lo == Kind::None) {
        abi.memory = true;
        return abi;
    }
    if (size <= 8) {
        hi = Kind::None;
    }

    auto integer_at = [&](uint64_t eight) -> llvm::Type * {
        const AbiLeaf *field = nullptr;
        for (const AbiLeaf &leaf : leaves) {
            if (leaf.offset <= eight && eight < leaf.offset + leaf.bytes) {
                field = &leaf;
                break;
            }
        }
        if (field != nullptr && field->offset == eight &&
            (field->type->isPointerTy() || field->type->isIntegerTy(64)) && field->bytes >= 8) {
            return field->type;
        }
        if (field != nullptr && field->offset == eight && field->type->isIntegerTy()) {
            unsigned bits = field->type->getIntegerBitWidth();
            if (bits == 1) {
                bits = 8;
            }
            if (bits == 8 || bits == 16 || bits == 32) {
                if (no_user(eight * 8 + bits, eight * 8 + 64)) {
                    return llvm::IntegerType::get(context_, bits);
                }
            }
        }
        unsigned width = static_cast<unsigned>(std::min<uint64_t>(size - eight, 8) * 8);
        if (width == 0) {
            width = 8;
        }
        return llvm::IntegerType::get(context_, width);
    };
    auto fp_at = [&](uint64_t offset) -> llvm::Type * {
        llvm::Type *found = nullptr;
        for (const AbiLeaf &leaf : leaves) {
            if (leaf.offset != offset || leaf.type == nullptr || !leaf.type->isFloatingPointTy()) {
                continue;
            }
            // A double at this offset covers the eightbyte, even when a float is listed first.
            if (leaf.type->isDoubleTy()) {
                return leaf.type;
            }
            if (found == nullptr) {
                found = leaf.type;
            }
        }
        return found;
    };
    auto sse_at = [&](uint64_t eight) -> llvm::Type * {
        llvm::Type *first = fp_at(eight);
        if (first == nullptr || first->isDoubleTy()) {
            return f64_ty();
        }
        const uint64_t first_size = layout.getTypeAllocSize(first);
        llvm::Type *second = size > eight + first_size ? fp_at(eight + first_size) : nullptr;
        if (first->isFloatTy() && second != nullptr && second->isFloatTy()) {
            return llvm::FixedVectorType::get(f32_ty(), 2);
        }
        if (second == nullptr) {
            return first;
        }
        return f64_ty();
    };
    auto piece = [&](Kind kind, uint64_t eight) {
        return kind == Kind::Sse ? sse_at(eight) : integer_at(eight);
    };

    llvm::Type *low = piece(lo, 0);
    if (hi == Kind::None) {
        abi.direct = low;
        abi.parts.push_back(low);
        abi.offsets.push_back(0);
        return abi;
    }
    llvm::Type *high = piece(hi, 8);
    const uint64_t low_size = layout.getTypeAllocSize(low);
    const uint64_t high_align = layout.getABITypeAlign(high).value();
    const uint64_t high_start = (low_size + high_align - 1) / high_align * high_align;
    if (high_start != 8) {
        low = low->isFloatTy() ? static_cast<llvm::Type *>(f64_ty()) : static_cast<llvm::Type *>(i64());
    }
    abi.direct = llvm::StructType::get(context_, {low, high});
    abi.parts.push_back(low);
    abi.parts.push_back(high);
    abi.offsets.push_back(0);
    abi.offsets.push_back(8);
    return abi;
}

ExternLayout CodeGen::extern_layout(const FunctionDecl &fn, uint32_t file) const {
    ExternLayout layout;
    if (!fn.extern_c) {
        return layout;
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const bool ret_fnptr = !many && fn.results.size() == 1 && fn.results.front().is_fn_ptr;
    Lowered ret;
    if (!returns_void && !many && !ret_fnptr) {
        ret = lower(file, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0, false);
    }
    const bool ret_struct = is_struct_value(ret);
    bool any = ret_struct;
    layout.args.resize(fn.params.size());
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
        const Lowered low = lower_param_type(file, fn.params[index]);
        if (!is_struct_value(low)) {
            continue;
        }
        any = true;
        layout.args[index].is_struct = true;
        layout.args[index].struct_id = low.struct_id;
        layout.args[index].struct_type = low.type;
        layout.args[index].abi = structs_[static_cast<std::size_t>(low.struct_id)].is_union
                                     ? c_abi_union(low.struct_id)
                                     : c_abi(llvm::cast<llvm::StructType>(low.type));
    }
    if (!any) {
        return layout;
    }
    layout.active = true;
    if (ret_struct) {
        layout.ret_struct = ret.type;
        layout.ret_struct_id = ret.struct_id;
        layout.ret = structs_[static_cast<std::size_t>(ret.struct_id)].is_union
                         ? c_abi_union(ret.struct_id)
                         : c_abi(llvm::cast<llvm::StructType>(ret.type));
        layout.sret = layout.ret.memory;
    }
    std::vector<llvm::Type *> params;
    llvm::Type *result = returns_void || many ? llvm::Type::getVoidTy(context_)
                                              : (ret_fnptr ? stored_type(lower_fnptr(file, fn.results.front().type_name,
                                                                                     fn.results.front().type_package,
                                                                                     fn.results.front().is_struct,
                                                                                     fn.results.front().is_enum,
                                                                                     fn.results.front().ret_indirections,
                                                                                     fn.results.front().indirections,
                                                                                     fn.results.front().fn_params,
                                                                                     fn.results.front().fn_variadic,
                                                                                     fn.results.front().fn_results))
                                                           : stored_type(ret));
    if (layout.sret) {
        params.push_back(ptr_type());
        result = llvm::Type::getVoidTy(context_);
    } else if (ret_struct) {
        result = layout.ret.direct;
    }
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
        const ExternLayout::Arg &arg = layout.args[index];
        if (arg.is_struct && arg.abi.memory) {
            params.push_back(ptr_type());
        } else if (arg.is_struct) {
            params.insert(params.end(), arg.abi.parts.begin(), arg.abi.parts.end());
        } else {
            params.push_back(stored_type(lower_param_type(file, fn.params[index])));
        }
    }
    layout.type = llvm::FunctionType::get(result, params, fn.variadic);
    return layout;
}

void CodeGen::apply_extern_attrs(llvm::Function *function, const ExternLayout &layout) const {
    unsigned index = 0;
    if (layout.sret) {
        function->addParamAttr(index, llvm::Attribute::getWithStructRetType(context_, layout.ret_struct));
        function->addParamAttr(index, llvm::Attribute::getWithAlignment(context_, llvm::Align(layout.ret.natural_align)));
        function->addParamAttr(index, llvm::Attribute::Writable);
        function->addParamAttr(index, llvm::Attribute::DeadOnUnwind);
        ++index;
    }
    for (const ExternLayout::Arg &arg : layout.args) {
        if (arg.is_struct && arg.abi.memory) {
            function->addParamAttr(index, llvm::Attribute::getWithByValType(context_, arg.struct_type));
            function->addParamAttr(index,
                                   llvm::Attribute::getWithAlignment(context_, llvm::Align(arg.abi.indirect_align)));
            ++index;
        } else if (arg.is_struct) {
            index += static_cast<unsigned>(arg.abi.parts.size());
        } else {
            ++index;
        }
    }
}

void CodeGen::annotate_call(llvm::CallInst *inst, const std::vector<AbiMark> &marks) const {
    for (const AbiMark &mark : marks) {
        if (mark.sret) {
            inst->addParamAttr(mark.index, llvm::Attribute::getWithStructRetType(context_, mark.type));
            inst->addParamAttr(mark.index, llvm::Attribute::getWithAlignment(context_, llvm::Align(mark.align)));
            inst->addParamAttr(mark.index, llvm::Attribute::Writable);
            inst->addParamAttr(mark.index, llvm::Attribute::DeadOnUnwind);
        } else {
            inst->addParamAttr(mark.index, llvm::Attribute::getWithByValType(context_, mark.type));
            inst->addParamAttr(mark.index, llvm::Attribute::getWithAlignment(context_, llvm::Align(mark.align)));
        }
    }
}

llvm::AllocaInst *CodeGen::aligned_slot(llvm::Type *type, unsigned align, const std::string &name) {
    llvm::AllocaInst *slot = alloca_typed(type, name);
    if (align > 0) {
        slot->setAlignment(llvm::Align(align));
    }
    return slot;
}

llvm::Value *CodeGen::addr_at(llvm::Value *base, uint64_t offset) {
    if (offset == 0) {
        return base;
    }
    return builder_.CreateInBoundsGEP(i8(), base, builder_.getInt64(offset));
}

llvm::Value *CodeGen::load_at(llvm::Value *base, llvm::Type *type, uint64_t offset) {
    llvm::LoadInst *load = builder_.CreateLoad(type, addr_at(base, offset));
    load->setAlignment(module_.getDataLayout().getABITypeAlign(type));
    return load;
}

void CodeGen::store_at(llvm::Value *base, llvm::Value *value, uint64_t offset) {
    if (value == nullptr) {
        return;
    }
    llvm::StoreInst *store = builder_.CreateStore(value, addr_at(base, offset));
    store->setAlignment(module_.getDataLayout().getABITypeAlign(value->getType()));
}

llvm::Value *CodeGen::pack_return(llvm::Value *slot, const CAbi &abi) {
    if (abi.parts.size() == 1) {
        return load_at(slot, abi.parts[0], abi.offsets.empty() ? 0 : abi.offsets[0]);
    }
    llvm::Value *aggregate = llvm::PoisonValue::get(abi.direct);
    for (unsigned index = 0; index < abi.parts.size(); ++index) {
        const uint64_t offset = index < abi.offsets.size() ? abi.offsets[index] : 0;
        aggregate = builder_.CreateInsertValue(aggregate, load_at(slot, abi.parts[index], offset), index);
    }
    return aggregate;
}

void CodeGen::unpack_return(llvm::Value *returned, llvm::Value *slot, const CAbi &abi) {
    if (returned == nullptr || abi.parts.empty()) {
        return;
    }
    if (abi.parts.size() == 1) {
        store_at(slot, returned, abi.offsets.empty() ? 0 : abi.offsets[0]);
        return;
    }
    for (unsigned index = 0; index < abi.parts.size(); ++index) {
        const uint64_t offset = index < abi.offsets.size() ? abi.offsets[index] : 0;
        store_at(slot, builder_.CreateExtractValue(returned, index), offset);
    }
}

} // namespace tlc
