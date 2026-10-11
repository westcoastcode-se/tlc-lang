// Emitting function bodies, calls, expressions, and statements.
#include "gen.hpp"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tlc {

void CodeGen::declare_file(const PackageNode &node) {
    struct Acc {
        const FunctionDecl *fn = nullptr;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
        bool extern_c = false;
    };
    std::unordered_map<std::string, Acc> functions;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->name.empty()) {
            continue;
        }
        Acc &acc = functions[function_key(*fn)];
        if (acc.fn == nullptr || fn->body != nullptr) {
            acc.fn = fn;
        }
        acc.exported = acc.exported || fn->exported;
        acc.is_static = acc.is_static || fn->is_static;
        acc.is_extern = acc.is_extern || fn->is_extern;
        acc.extern_c = acc.extern_c || fn->extern_c;
    }
    FileSymbols &file = files_[node.file_id];
    const bool entry = node.file_id == entry_file_;
    for (const auto &[name, acc] : functions) {
        const bool many = acc.fn->results.size() > 1;
        const bool returns_void = !many && acc.fn->return_type == "void" && acc.fn->indirections == 0 && !acc.fn->is_struct &&
                                  acc.fn->type_package.empty();
        const bool ret_fnptr = !many && acc.fn->results.size() == 1 && acc.fn->results.front().is_fn_ptr;
        const ResultSlot *ret_slot = ret_fnptr ? &acc.fn->results.front() : nullptr;
        const Lowered ret = returns_void || many
                                ? Lowered{}
                                : (ret_fnptr ? lower_fnptr(node.file_id, ret_slot->type_name, ret_slot->type_package,
                                                          ret_slot->is_struct, ret_slot->is_enum, ret_slot->ret_indirections,
                                                          ret_slot->indirections, ret_slot->fn_params, ret_slot->fn_variadic,
                                                          ret_slot->fn_results)
                                             : lower(node.file_id, acc.fn->return_type, acc.fn->type_package, acc.fn->is_struct,
                                                     acc.fn->indirections, false, 0, false));
        if (function_is_generic(node.file_id, *acc.fn)) {
            Binding binding;
            binding.is_function = true;
            binding.is_generic = true;
            binding.is_static = acc.is_static;
            binding.decl = acc.fn;
            binding.depth = returns_void ? 0 : ret.depth;
            binding.elem_char = !returns_void && ret.elem_char;
            binding.struct_id = returns_void ? -1 : ret.struct_id;
            binding.elem_void = !returns_void && ret.elem_void;
            binding.elem_bool = !returns_void && ret.elem_bool;
            binding.elem_bits = returns_void ? 0 : ret.elem_bits;
            binding.elem_unsigned = !returns_void && ret.elem_unsigned;
            file.functions.emplace(name, binding);
            continue;
        }
        const SymbolForm form =
            symbol_form(node, name, acc.is_extern, acc.extern_c, acc.is_static, acc.exported, entry && name == "main");
        llvm::Function *function = acc.is_extern ? module_.getFunction(form.name) : nullptr;
        const ExternLayout layout = acc.extern_c ? extern_layout(*acc.fn, node.file_id) : ExternLayout{};
        if (function == nullptr) {
            llvm::FunctionType *ftype = layout.active ? layout.type : function_type(*acc.fn, node.file_id);
            function = llvm::Function::Create(ftype, form.linkage, form.name, module_);
            function->setCallingConv(llvm::CallingConv::C);
            if (layout.active) {
                apply_extern_attrs(function, layout);
                extern_abis_.emplace(function, layout);
            }
            if (!layout.active) {
                unsigned index = 0;
                for (auto &arg : function->args()) {
                    if (index < acc.fn->params.size() && !acc.fn->params[index].name.empty()) {
                        arg.setName(acc.fn->params[index].name);
                    }
                    ++index;
                }
            }
        } else if (layout.active && !extern_abis_.contains(function)) {
            apply_extern_attrs(function, layout);
            extern_abis_.emplace(function, layout);
        }
        Binding binding;
        binding.value = function;
        binding.depth = returns_void ? 0 : ret.depth;
        binding.is_function = true;
        binding.elem_char = !returns_void && ret.elem_char;
        binding.struct_id = returns_void ? -1 : ret.struct_id;
        binding.elem_void = !returns_void && ret.elem_void;
        binding.elem_bool = !returns_void && ret.elem_bool;
        binding.elem_bits = returns_void ? 0 : ret.elem_bits;
        binding.elem_unsigned = !returns_void && ret.elem_unsigned;
        binding.elem_float = !returns_void && ret.elem_float;
        binding.trait_id = returns_void ? -1 : ret.trait_id;
        binding.enum_id = returns_void ? -1 : ret.enum_id;
        binding.is_static = acc.is_static;
        binding.decl = acc.fn;
        file.functions.emplace(name, binding);
    }
    for (const TopDecl &decl : node.unit.decls) {
        const auto *var = std::get_if<VarDecl>(&decl.data);
        if (var == nullptr || var->name.empty() || file.globals.contains(var->name)) {
            continue;
        }
        const SymbolForm form =
            symbol_form(node, var->name, var->is_extern, var->extern_c, var->is_static, var->exported, false);
        Lowered low;
        if (var->inferred) {
            if (var->init == nullptr || !infer_global(node.file_id, *var->init, low)) {
                error(var->loc, "type mismatch");
                low = lower(node.file_id, "int", "", false, 0, false, 0, false);
            }
        } else {
            low = lower_declared(node.file_id, *var);
        }
        llvm::Type *ty = stored_type(low);
        llvm::GlobalVariable *existing = var->is_extern ? module_.getGlobalVariable(form.name) : nullptr;
        llvm::GlobalVariable *global = existing;
        if (global == nullptr) {
            llvm::Constant *init = nullptr;
            if (!var->is_extern) {
                if (low.is_array || (low.depth == 0 && (low.struct_id >= 0 || low.trait_id >= 0))) {
                    init = llvm::ConstantAggregateZero::get(ty);
                } else if (low.depth > 0) {
                    init = null_ptr();
                } else if (low.elem_float) {
                    init = llvm::ConstantFP::get(low.elem_bits >= 64 ? f64_ty() : f32_ty(), 0.0);
                } else if (low.elem_bool) {
                    init = llvm::ConstantInt::get(i1(), 0, false);
                } else if (low.elem_char || low.elem_bits == 8) {
                    init = llvm::ConstantInt::get(i8(), 0, false);
                } else if (low.elem_bits == 16) {
                    init = llvm::ConstantInt::get(i16(), 0, false);
                } else if (low.elem_bits == 64) {
                    init = llvm::ConstantInt::get(i64(), 0, false);
                } else {
                    init = const_i32(0);
                }
            }
            global = new llvm::GlobalVariable(module_, ty, false, form.linkage, init, form.name);
        }
        Binding binding;
        binding.value = global;
        binding.depth = low.depth;
        binding.elem_char = low.elem_char;
        binding.is_array = low.is_array;
        binding.array_len = low.array_len;
        binding.ptr_to_array = low.ptr_to_array;
        binding.ptr_array_len = low.ptr_array_len;
        binding.struct_id = low.struct_id;
        binding.elem_void = low.elem_void;
        binding.elem_bool = low.elem_bool;
        binding.elem_bits = low.elem_bits;
        binding.elem_unsigned = low.elem_unsigned;
        binding.elem_float = low.elem_float;
        binding.trait_id = low.trait_id;
        binding.fn_ty = low.fn_ty;
        binding.fn_ret_depth = low.fn_ret_depth;
        binding.fn_ret_float = low.fn_ret_float;
        binding.fn_ret_bits = low.fn_ret_bits;
        binding.fn_ret_unsigned = low.fn_ret_unsigned;
        binding.fn_ret_char = low.fn_ret_char;
        binding.fn_ret_bool = low.fn_ret_bool;
        binding.fn_ret_struct = low.fn_ret_struct;
        binding.fn_results = low.fn_results;
        binding.enum_id = low.enum_id;
        binding.is_static = var->is_static;
        file.globals.emplace(var->name, binding);
    }
}

void CodeGen::bind_imports(const PackageNode &node) {
    if (graph_ == nullptr) {
        return;
    }
    for (const ImportDecl &import : node.unit.imports) {
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == node.file_id && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        if (import.form == ImportDecl::Form::Selective) {
            for (const std::string &name : import.names) {
                if (const Binding *function = visible(target->file_id, name, true)) {
                    scopes_.back()[name] = *function;
                } else if (const Binding *global = visible(target->file_id, name, false)) {
                    scopes_.back()[name] = *global;
                }
            }
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (!binding.empty()) {
            packages_[binding] = target->file_id;
        }
    }
}

void CodeGen::emit_bodies(const PackageNode &node) {
    current_file_ = node.file_id;
    packages_.clear();
    push();
    bind_package_globals();
    bind_imports(node);
    std::unordered_set<std::string> emitted;
    for (const TopDecl &decl : node.unit.decls) {
        const auto *fn = std::get_if<FunctionDecl>(&decl.data);
        if (fn == nullptr || fn->body == nullptr || function_is_generic(node.file_id, *fn) ||
            !emitted.insert(function_key(*fn)).second) {
            continue;
        }
        emit_function(*fn);
    }
    pop();
}

std::optional<int32_t> CodeGen::fold_global(uint32_t file, const std::string &name, bool allow_local_static) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = files_.find(id);
        if (found == files_.end() || !found->second.folded.contains(name)) {
            continue;
        }
        const auto global = found->second.globals.find(name);
        if (global == found->second.globals.end()) {
            continue;
        }
        if (global->second.is_static && !(allow_local_static && id == file)) {
            continue;
        }
        const auto *variable = llvm::dyn_cast<llvm::GlobalVariable>(global->second.value);
        if (variable == nullptr || !variable->hasInitializer()) {
            continue;
        }
        if (const auto *constant = llvm::dyn_cast<llvm::ConstantInt>(variable->getInitializer())) {
            return static_cast<int32_t>(constant->getSExtValue());
        }
    }
    return std::nullopt;
}

std::optional<int32_t> CodeGen::fold(const Expr &expr) const {
    auto wrap = [](int64_t value) { return static_cast<int32_t>(static_cast<uint32_t>(value)); };
    return std::visit(
        [&](const auto &node) -> std::optional<int32_t> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int>) {
                return static_cast<int32_t>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                return static_cast<int32_t>(node.value & 0xFFULL);
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return 0;
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const auto local = fold_global(current_file_, node.text)) {
                    return local;
                }
                const PackageNode *owner = find_node(current_file_);
                if (owner == nullptr || graph_ == nullptr) {
                    return std::nullopt;
                }
                for (const ImportDecl &import : owner->unit.imports) {
                    if (import.form != ImportDecl::Form::Selective) {
                        continue;
                    }
                    bool wanted = false;
                    for (const std::string &item : import.names) {
                        if (item == node.text) {
                            wanted = true;
                        }
                    }
                    if (!wanted) {
                        continue;
                    }
                    for (const ImportEdge &edge : graph_->edges) {
                        if (edge.from_file == current_file_ && edge.spec == import.path && edge.loc.file == import.loc.file &&
                            edge.loc.offset == import.loc.offset) {
                            if (const auto imported = fold_global(edge.to_file, node.text, false)) {
                                return imported;
                            }
                        }
                    }
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base == nullptr) {
                    return std::nullopt;
                }
                const uint32_t target = package_file(current_file_, base->text);
                if (target == 0) {
                    return std::nullopt;
                }
                return fold_global(target, node.field, false);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                const auto inner = node.expr ? fold(*node.expr) : std::nullopt;
                if (!inner) {
                    return std::nullopt;
                }
                switch (node.op) {
                case TokenKind::Plus: return *inner;
                case TokenKind::Minus: return wrap(-static_cast<int64_t>(*inner));
                case TokenKind::Tilde: return wrap(~static_cast<uint32_t>(*inner));
                case TokenKind::Bang: return node.postfix ? std::nullopt : std::optional<int32_t>(*inner == 0 ? 1 : 0);
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.op == TokenKind::AmpAmp || node.op == TokenKind::PipePipe) {
                    const auto lhs = node.lhs ? fold(*node.lhs) : std::nullopt;
                    if (!lhs) {
                        return std::nullopt;
                    }
                    if (node.op == TokenKind::AmpAmp && *lhs == 0) {
                        return 0;
                    }
                    if (node.op == TokenKind::PipePipe && *lhs != 0) {
                        return 1;
                    }
                    const auto rhs = node.rhs ? fold(*node.rhs) : std::nullopt;
                    if (!rhs) {
                        return std::nullopt;
                    }
                    return *rhs != 0 ? 1 : 0;
                }
                const auto lhs = node.lhs ? fold(*node.lhs) : std::nullopt;
                const auto rhs = node.rhs ? fold(*node.rhs) : std::nullopt;
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                const int64_t a = *lhs;
                const int64_t b = *rhs;
                switch (node.op) {
                case TokenKind::Plus: return wrap(a + b);
                case TokenKind::Minus: return wrap(a - b);
                case TokenKind::Star: return wrap(a * b);
                case TokenKind::Slash:
                    if (b == 0) return std::nullopt;
                    return wrap(a / b);
                case TokenKind::Percent:
                    if (b == 0) return std::nullopt;
                    return wrap(a % b);
                case TokenKind::Amp: return wrap(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
                case TokenKind::Pipe: return wrap(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
                case TokenKind::Caret: return wrap(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
                case TokenKind::LtLt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return wrap(static_cast<uint32_t>(a) << static_cast<uint32_t>(b));
                case TokenKind::GtGt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(a) >> static_cast<uint32_t>(b)));
                case TokenKind::EqEq: return a == b ? 1 : 0;
                case TokenKind::BangEq: return a != b ? 1 : 0;
                case TokenKind::Lt: return a < b ? 1 : 0;
                case TokenKind::Gt: return a > b ? 1 : 0;
                case TokenKind::Le: return a <= b ? 1 : 0;
                case TokenKind::Ge: return a >= b ? 1 : 0;
                default: return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

llvm::Constant *CodeGen::const_field(const Expr &expr, const FieldLayout &slot) {
    if (slot.is_array || slot.type == nullptr) {
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
    if (tag == nullptr || tag->getType() != layout.type->getElementType(0)) {
        return;
    }
    llvm::Value *addr = builder_.CreateInBoundsGEP(layout.type, pointer,
                                                    llvm::ArrayRef<llvm::Value *>{const_i32(0), const_i32(0)});
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
        const auto *bytes_type = llvm::dyn_cast<llvm::ArrayType>(layout.type->getElementType(2));
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
        if (tag == nullptr || tag->getType() != layout.type->getElementType(0)) {
            return nullptr;
        }
        llvm::Constant *bytes = llvm::ConstantDataArray::get(context_, raw);
        if (bytes->getType() != bytes_type) {
            return nullptr;
        }
        return llvm::ConstantStruct::get(layout.type,
                                         {tag, llvm::ConstantAggregateZero::get(layout.type->getElementType(1)), bytes});
    }
    const unsigned count = layout.type->getNumElements();
    std::vector<llvm::Constant *> elements;
    elements.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        elements.push_back(llvm::Constant::getNullValue(layout.type->getElementType(index)));
    }
    for (const Expr::FieldInit &field : init.fields) {
        if (field.value == nullptr || field.field.empty()) {
            return nullptr;
        }
        int index = -1;
        const FieldLayout *slot = find_field(struct_id, field.field, index);
        if (slot == nullptr || index < 0 || static_cast<unsigned>(index) >= count) {
            return nullptr;
        }
        llvm::Constant *value = const_field(*field.value, *slot);
        if (value == nullptr || value->getType() != elements[static_cast<std::size_t>(index)]->getType()) {
            return nullptr;
        }
        elements[static_cast<std::size_t>(index)] = value;
    }
    return llvm::ConstantStruct::get(layout.type, elements);
}

TypedValue CodeGen::emit_init(const Expr::Init &init) {
    if (init.struct_id < 0 || init.struct_id >= static_cast<int>(structs_.size())) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    const StructLayout &layout = structs_[static_cast<std::size_t>(init.struct_id)];
    if (layout.type == nullptr) {
        error(init.loc, "type mismatch");
        return {zero(), 0};
    }
    // Omitted fields stay zero. A later assignment replaces the whole struct.
    llvm::AllocaInst *slot = alloca_typed(layout.type, "init");
    builder_.CreateStore(llvm::ConstantAggregateZero::get(layout.type), slot);
    for (const Expr::FieldInit &field : init.fields) {
        if (field.value == nullptr || field.field.empty()) {
            continue;
        }
        store_union_tag(init.struct_id, slot, field.field);
        const TypedValue addr = field_addr(init.struct_id, slot, field.field, field.loc);
        if (addr.value == nullptr) {
            continue;
        }
        if (addr.is_array) {
            error(field.loc, "not supported yet");
            continue;
        }
        const TypedValue rhs = emit_rvalue(*field.value);
        if (terminated() || rhs.value == nullptr) {
            continue;
        }
        builder_.CreateStore(stored_value(rhs, addr), addr.value);
    }
    if (terminated()) {
        return {zero(), 0};
    }
    return load_object(slot, 0, false, init.struct_id);
}

void CodeGen::apply_initializer(const VarDecl &var) {
    FileSymbols &file = files_[current_file_];
    if (var.is_extern && !var.init) {
        return;
    }
    if (!var.init) {
        file.folded.insert(var.name);
        return;
    }
    const auto found_early = file.globals.find(var.name);
    if (const auto *init = std::get_if<Expr::Init>(&var.init->data)) {
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
        file.folded.insert(var.name);
        return;
    }
    const auto value = fold(*var.init);
    const auto found = file.globals.find(var.name);
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
    } else if (var.type_name == "char" || found->second.elem_bits == 8) {
        global->setInitializer(llvm::ConstantInt::get(i8(), static_cast<uint64_t>(*value) & 0xFFU, false));
    } else if (found->second.elem_bits == 16) {
        global->setInitializer(llvm::ConstantInt::get(i16(), static_cast<uint64_t>(*value) & 0xFFFFU, false));
    } else if (found->second.elem_bits == 64) {
        global->setInitializer(llvm::ConstantInt::get(i64(), static_cast<uint64_t>(static_cast<int64_t>(*value)), true));
    } else {
        global->setInitializer(const_i32(*value));
    }
    file.folded.insert(var.name);
}

void CodeGen::finish_function() {
    const bool returns_void = return_type_ != nullptr && return_type_->isVoidTy();
    for (llvm::BasicBlock &block : *function_) {
        if (block.hasTerminator()) {
            continue;
        }
        llvm::IRBuilder<> fix(&block);
        if (extern_sret_ && extern_sret_ptr_ != nullptr && extern_ret_struct_ != nullptr) {
            fix.CreateStore(llvm::ConstantAggregateZero::get(extern_ret_struct_), extern_sret_ptr_);
            fix.CreateRetVoid();
        } else if (returns_void) {
            fix.CreateRetVoid();
        } else if (return_depth_ > 0) {
            fix.CreateRet(null_ptr());
        } else if (return_type_ != nullptr && return_type_->isIntegerTy(32)) {
            fix.CreateRet(const_i32(0));
        } else if (return_type_ != nullptr) {
            fix.CreateRet(llvm::Constant::getNullValue(return_type_));
        } else {
            fix.CreateRet(const_i32(0));
        }
    }
}

bool CodeGen::param_is_trait(uint32_t file, const Param &param) const {
    if (param.is_struct || param.is_enum || param.is_array || !param.type_package.empty() || param.indirections > 1) {
        return false;
    }
    if (has_typedef(file, param.type_name) || tag_id(file, param.type_name) >= 0 ||
        selective_tag(file, param.type_name) >= 0) {
        return false;
    }
    return trait_named(file, param.type_name) >= 0;
}

bool CodeGen::function_is_generic(uint32_t file, const FunctionDecl &fn) const {
    if (!fn.method_struct.empty()) {
        return false;
    }
    for (const Param &param : fn.params) {
        if (param_is_trait(file, param)) {
            return true;
        }
    }
    return false;
}

CodeGen::Lowered CodeGen::lower_param(const FunctionDecl &fn, std::size_t index) const {
    const Param &param = fn.params[index];
    if (index < active_spec_.size() && active_spec_[index] >= 0 &&
        active_spec_[index] < static_cast<int>(structs_.size())) {
        return lower(current_file_, structs_[static_cast<std::size_t>(active_spec_[index])].name, "", true,
                     param.indirections, false, 0, true);
    }
    return lower_param_type(current_file_, param);
}

llvm::FunctionType *CodeGen::spec_type(const FunctionDecl &fn, const std::vector<int> &spec) const {
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    llvm::Type *ret = many ? aggregate_return(fn, current_file_)
                           : (returns_void ? llvm::Type::getVoidTy(context_)
                                           : stored_type(lower(current_file_, fn.return_type, fn.type_package, fn.is_struct,
                                                               fn.indirections, false, 0, false)));
    std::vector<llvm::Type *> params;
    params.reserve(fn.params.size());
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
        const Param &param = fn.params[index];
        Lowered low = index < spec.size() && spec[index] >= 0 && spec[index] < static_cast<int>(structs_.size())
                          ? lower(current_file_, structs_[static_cast<std::size_t>(spec[index])].name, "", true,
                                  param.indirections, false, 0, true)
                          : lower(current_file_, param.type_name, param.type_package, param.is_struct, param.indirections,
                                  param.is_array, param.array_len, true);
        params.push_back(stored_type(low));
    }
    return llvm::FunctionType::get(ret, params, false);
}

void CodeGen::emit_function_body(const FunctionDecl &fn, llvm::Function *function, int return_depth) {
    if (function == nullptr || !fn.body || !function->empty()) {
        return;
    }
    const bool saved_many = return_many_;
    const bool saved_share = share_function_scope_;
    const bool saved_extern = extern_struct_ret_;
    const bool saved_sret = extern_sret_;
    llvm::Value *saved_sret_ptr = extern_sret_ptr_;
    const CAbi saved_abi = extern_ret_abi_;
    llvm::Type *saved_struct = extern_ret_struct_;
    function_ = function;
    return_type_ = function->getReturnType();
    return_depth_ = return_depth;
    return_many_ = fn.results.size() > 1;
    share_function_scope_ = true;
    extern_struct_ret_ = false;
    extern_sret_ = false;
    extern_sret_ptr_ = nullptr;
    extern_ret_abi_ = {};
    extern_ret_struct_ = nullptr;
    const auto abi_found = extern_abis_.find(function);
    const bool abi_body = abi_found != extern_abis_.end() && abi_found->second.active;
    const ExternLayout abi_layout = abi_body ? abi_found->second : ExternLayout{};
    if (abi_body && abi_layout.ret_struct != nullptr) {
        extern_struct_ret_ = true;
        extern_sret_ = abi_layout.sret;
        extern_ret_abi_ = abi_layout.ret;
        extern_ret_struct_ = abi_layout.ret_struct;
    }
    llvm::BasicBlock *entry = llvm::BasicBlock::Create(context_, "entry", function);
    builder_.SetInsertPoint(entry);
    push();
    if (abi_body) {
        const ExternLayout &layout = abi_layout;
        unsigned abi = 0;
        if (layout.sret && abi < function->arg_size()) {
            extern_sret_ptr_ = function->getArg(abi++);
        }
        for (std::size_t index = 0; index < fn.params.size(); ++index) {
            const std::string name = fn.params[index].name;
            const ExternLayout::Arg &info = layout.args[index];
            const Lowered low = lower_param(fn, index);
            if (info.is_struct && info.abi.memory) {
                llvm::Argument *arg = function->getArg(abi++);
                if (!name.empty()) {
                    Binding binding;
                    binding.value = arg;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                }
            } else if (info.is_struct) {
                const unsigned align = std::max(info.abi.natural_align, 8u);
                llvm::AllocaInst *slot = aligned_slot(info.struct_type, align, name.empty() ? "arg" : name);
                for (std::size_t part = 0; part < info.abi.parts.size() && abi < function->arg_size(); ++part) {
                    const uint64_t offset = part < info.abi.offsets.size() ? info.abi.offsets[part] : 0;
                    store_at(slot, function->getArg(abi++), offset);
                }
                if (!name.empty()) {
                    Binding binding;
                    binding.value = slot;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                }
            } else if (abi < function->arg_size()) {
                llvm::Argument *arg = function->getArg(abi++);
                if (!name.empty()) {
                    llvm::AllocaInst *slot = alloca_typed(stored_type(low), name);
                    builder_.CreateStore(arg, slot);
                    Binding binding;
                    binding.value = slot;
                    apply_low(binding, low);
                    scopes_.back()[name] = binding;
                }
            }
        }
    } else {
        unsigned index = 0;
        for (auto &arg : function->args()) {
            const std::string name = index < fn.params.size() ? fn.params[index].name : "";
            if (!name.empty()) {
                const Lowered low = lower_param(fn, index);
                llvm::AllocaInst *slot = alloca_typed(stored_type(low), name);
                builder_.CreateStore(&arg, slot);
                Binding binding;
                binding.value = slot;
                apply_low(binding, low);
                scopes_.back()[name] = binding;
            }
            ++index;
        }
    }
    emit_stmt(*fn.body);
    finish_function();
    pop();
    function_ = nullptr;
    return_type_ = nullptr;
    return_depth_ = 0;
    return_many_ = saved_many;
    share_function_scope_ = saved_share;
    extern_struct_ret_ = saved_extern;
    extern_sret_ = saved_sret;
    extern_sret_ptr_ = saved_sret_ptr;
    extern_ret_abi_ = saved_abi;
    extern_ret_struct_ = saved_struct;
}

void CodeGen::emit_function(const FunctionDecl &fn) {
    const std::string key = function_key(fn);
    const auto found = files_.at(current_file_).functions.find(key);
    if (found == files_.at(current_file_).functions.end() || found->second.is_generic || found->second.value == nullptr) {
        return;
    }
    emit_function_body(fn, llvm::cast<llvm::Function>(found->second.value), found->second.depth);
}

void CodeGen::emit_local_function(const FunctionDecl &fn) {
    if (fn.name.empty() || !fn.method_struct.empty() || scopes_.empty()) {
        return;
    }
    if (function_is_generic(current_file_, fn)) {
        error(fn.loc, "not supported yet");
        return;
    }
    llvm::Function *function = nullptr;
    const auto found = scopes_.back().find(fn.name);
    if (found != scopes_.back().end()) {
        if (!found->second.is_function) {
            return;
        }
        function = llvm::dyn_cast_or_null<llvm::Function>(found->second.value);
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const Lowered ret = returns_void || many
                            ? Lowered{}
                            : lower(current_file_, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0,
                                    false);
    if (function == nullptr) {
        const PackageNode *owner = find_node(current_file_);
        const std::string qualifier = owner != nullptr && !owner->qualifier.empty() ? owner->qualifier : "local";
        const std::string symbol = qualifier + ".local." + fn.name + "." + std::to_string(++local_seq_);
        function = llvm::Function::Create(function_type(fn, current_file_), llvm::GlobalValue::InternalLinkage, symbol,
                                           module_);
        function->setCallingConv(llvm::CallingConv::C);
        unsigned index = 0;
        for (auto &arg : function->args()) {
            if (index < fn.params.size() && !fn.params[index].name.empty()) {
                arg.setName(fn.params[index].name);
            }
            ++index;
        }
        Binding binding;
        binding.value = function;
        binding.depth = returns_void ? 0 : ret.depth;
        binding.is_function = true;
        binding.elem_char = !returns_void && ret.elem_char;
        binding.struct_id = returns_void ? -1 : ret.struct_id;
        binding.elem_void = !returns_void && ret.elem_void;
        binding.elem_bool = !returns_void && ret.elem_bool;
        binding.elem_bits = returns_void ? 0 : ret.elem_bits;
        binding.elem_unsigned = !returns_void && ret.elem_unsigned;
        binding.elem_float = !returns_void && ret.elem_float;
        binding.trait_id = returns_void ? -1 : ret.trait_id;
        binding.enum_id = returns_void ? -1 : ret.enum_id;
        binding.decl = &fn;
        scopes_.back()[fn.name] = binding;
    }
    emit_nested_body(fn, function, returns_void ? 0 : ret.depth);
}

void CodeGen::emit_nested_body(const FunctionDecl &fn, llvm::Function *function, int depth) {
    if (fn.body == nullptr || function == nullptr || !function->empty()) {
        return;
    }
    std::vector<std::unordered_map<std::string, Binding>> kept;
    for (const auto &scope : scopes_) {
        std::unordered_map<std::string, Binding> filtered;
        for (const auto &[name, binding] : scope) {
            if (binding.is_function || llvm::isa_and_nonnull<llvm::GlobalVariable>(binding.value)) {
                filtered.emplace(name, binding);
            }
        }
        if (!filtered.empty()) {
            kept.push_back(std::move(filtered));
        }
    }
    auto saved_scopes = std::move(scopes_);
    llvm::Function *saved_function = function_;
    llvm::Type *saved_return = return_type_;
    const int saved_depth = return_depth_;
    auto saved_loops = std::move(loops_);
    auto saved_defers = std::move(defers_);
    auto saved_cases = std::move(case_blocks_);
    const llvm::IRBuilderBase::InsertPoint saved_ip = builder_.saveIP();
    scopes_ = std::move(kept);
    loops_.clear();
    defers_.clear();
    case_blocks_.clear();
    emit_function_body(fn, function, depth);
    scopes_ = std::move(saved_scopes);
    function_ = saved_function;
    return_type_ = saved_return;
    return_depth_ = saved_depth;
    loops_ = std::move(saved_loops);
    defers_ = std::move(saved_defers);
    case_blocks_ = std::move(saved_cases);
    builder_.restoreIP(saved_ip);
}

TypedValue CodeGen::emit_anon_function(const FunctionDecl &fn) {
    if (!fn.method_struct.empty() || fn.variadic || function_is_generic(current_file_, fn) || fn.body == nullptr) {
        error(fn.loc, "not supported yet");
        return {zero(), 0};
    }
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    const Lowered ret = returns_void || many
                            ? Lowered{}
                            : lower(current_file_, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0,
                                    false);
    const PackageNode *owner = find_node(current_file_);
    const std::string qualifier = owner != nullptr && !owner->qualifier.empty() ? owner->qualifier : "anon";
    const std::string symbol = qualifier + ".anon." + std::to_string(++local_seq_);
    llvm::Function *function = llvm::Function::Create(function_type(fn, current_file_), llvm::GlobalValue::InternalLinkage,
                                                       symbol, module_);
    function->setCallingConv(llvm::CallingConv::C);
    unsigned index = 0;
    for (auto &arg : function->args()) {
        if (index < fn.params.size() && !fn.params[index].name.empty()) {
            arg.setName(fn.params[index].name);
        }
        ++index;
    }
    emit_nested_body(fn, function, returns_void ? 0 : ret.depth);
    return {function,
            returns_void ? 0 : ret.depth,
            !returns_void && ret.elem_char,
            returns_void ? -1 : ret.struct_id,
            false,
            !returns_void && ret.elem_void,
            !returns_void && ret.elem_bool,
            returns_void ? 0 : ret.elem_bits,
            !returns_void && ret.elem_unsigned};
}

CodeGen::PreparedCall CodeGen::prepare_call(const Expr::Call &node) {
    PreparedCall prepared;
    if (node.callee == nullptr) {
        error(node.loc, "type mismatch");
        return prepared;
    }
    auto remember = [&](llvm::Function *function, std::vector<llvm::Value *> args, int depth, bool elem_char, int struct_id,
                        bool elem_void, bool elem_bool, int elem_bits, bool elem_unsigned) {
        prepared.function = function;
        prepared.args = std::move(args);
        prepared.depth = depth;
        prepared.elem_char = elem_char;
        prepared.struct_id = struct_id;
        prepared.elem_void = elem_void;
        prepared.elem_bool = elem_bool;
        prepared.elem_bits = elem_bits;
        prepared.elem_unsigned = elem_unsigned;
    };
    if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data);
        selector != nullptr && selector->base != nullptr) {
        const auto *base_name = std::get_if<Expr::Name>(&selector->base->data);
        const bool package_call = base_name != nullptr && packages_.contains(base_name->text);
        if (!package_call) {
            const int receiver_enum = enum_value_id(*selector->base);
            if (receiver_enum >= 0 && receiver_enum < static_cast<int>(enums_.size())) {
                const std::string key = enums_[static_cast<std::size_t>(receiver_enum)].name + "." + selector->field;
                const Binding *method = nullptr;
                for (const uint32_t id : package_files(current_file_)) {
                    const auto file = files_.find(id);
                    if (file == files_.end()) {
                        continue;
                    }
                    const auto found = file->second.functions.find(key);
                    if (found == file->second.functions.end() || !found->second.is_function) {
                        continue;
                    }
                    if (found->second.is_static && id != current_file_) {
                        continue;
                    }
                    method = &found->second;
                    break;
                }
                auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
                if (function == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                const bool receiver_pointer = function->getFunctionType()->getNumParams() > 0 &&
                                              function->getFunctionType()->getParamType(0)->isPointerTy();
                llvm::Value *receiver_arg = nullptr;
                if (receiver_pointer) {
                    if (!addressable(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        return prepared;
                    }
                    const TypedValue addr = emit_addr(*selector->base);
                    receiver_arg = addr.value;
                } else {
                    const TypedValue value = emit_rvalue(*selector->base);
                    llvm::Type *param = function->getFunctionType()->getNumParams() > 0
                                            ? function->getFunctionType()->getParamType(0)
                                            : nullptr;
                    receiver_arg = as_stored(value.value, value.depth, param, value.elem_unsigned || value.elem_char);
                }
                if (receiver_arg == nullptr) {
                    error(node.loc, "type mismatch");
                    return prepared;
                }
                std::vector<llvm::Value *> args;
                args.reserve(node.args.size() + 1);
                args.push_back(receiver_arg);
                unsigned index = 1;
                for (const auto &arg : node.args) {
                    const TypedValue value = emit_rvalue(*arg);
                    llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                            ? function->getFunctionType()->getParamType(index)
                                            : nullptr;
                    args.push_back(as_stored(value.value, value.depth, param, value.elem_unsigned));
                    ++index;
                }
                remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                         method->elem_bool, method->elem_bits, method->elem_unsigned);
                prepared.decl = method->decl;
                prepared.elem_float = method->elem_float;
                prepared.enum_id = method->enum_id;
                return prepared;
            }
            int struct_id = -1;
            llvm::Value *struct_ptr = nullptr;
            llvm::Value *struct_value = nullptr;
            llvm::Value *pointer_value = nullptr;
            if (addressable(*selector->base)) {
                const TypedValue addr = emit_addr(*selector->base);
                if (addr.value == nullptr) {
                    return prepared;
                }
                struct_id = addr.struct_id;
                if (addr.depth == 0 && addr.struct_id >= 0) {
                    struct_ptr = addr.value;
                } else if (addr.depth == 1 && addr.struct_id >= 0) {
                    pointer_value = builder_.CreateLoad(ptr_type(), addr.value);
                }
            } else {
                const TypedValue base = emit_rvalue(*selector->base);
                if (base.value == nullptr) {
                    return prepared;
                }
                struct_id = base.struct_id;
                if (base.depth == 0 && base.struct_id >= 0) {
                    struct_value = base.value;
                } else if (base.depth == 1 && base.struct_id >= 0) {
                    pointer_value = base.value;
                }
            }
            const Binding *method = nullptr;
            if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size())) {
                const std::string key = structs_[static_cast<std::size_t>(struct_id)].name + "." + selector->field;
                for (const uint32_t id : package_files(current_file_)) {
                    const auto file = files_.find(id);
                    if (file == files_.end()) {
                        continue;
                    }
                    const auto found = file->second.functions.find(key);
                    if (found == file->second.functions.end() || !found->second.is_function) {
                        continue;
                    }
                    if (found->second.is_static && id != current_file_) {
                        continue;
                    }
                    method = &found->second;
                    break;
                }
            }
            auto *function = method == nullptr ? nullptr : llvm::dyn_cast<llvm::Function>(method->value);
            if (function == nullptr) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            const bool receiver_pointer = function->getFunctionType()->getNumParams() > 0 &&
                                          function->getFunctionType()->getParamType(0)->isPointerTy();
            llvm::Value *receiver_arg = nullptr;
            if (receiver_pointer) {
                if (pointer_value != nullptr) {
                    receiver_arg = pointer_value;
                } else if (struct_ptr != nullptr) {
                    receiver_arg = struct_ptr;
                }
            } else if (struct_value != nullptr) {
                receiver_arg = struct_value;
            } else if (pointer_value != nullptr) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, pointer_value);
            } else if (struct_ptr != nullptr) {
                receiver_arg = builder_.CreateLoad(structs_[static_cast<std::size_t>(struct_id)].type, struct_ptr);
            }
            if (receiver_arg == nullptr) {
                error(node.loc, "type mismatch");
                return prepared;
            }
            std::vector<llvm::Value *> args;
            args.reserve(node.args.size() + 1);
            args.push_back(receiver_arg);
            unsigned index = 1;
            for (const auto &arg : node.args) {
                const TypedValue value = emit_rvalue(*arg);
                llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                        ? function->getFunctionType()->getParamType(index)
                                        : nullptr;
                args.push_back(as_stored(value.value, value.depth, param, value.elem_unsigned));
                ++index;
            }
            remember(function, std::move(args), method->depth, method->elem_char, method->struct_id, method->elem_void,
                     method->elem_bool, method->elem_bits, method->elem_unsigned);
            prepared.decl = method->decl;
            prepared.elem_float = method->elem_float;
            return prepared;
        }
    }
    prepared.decl = node.callee == nullptr ? nullptr : callee_decl(*node.callee);
    const TypedValue callee = emit_rvalue(*node.callee);
    prepared.enum_id = callee.enum_id;
    if (callee.fn_ty != nullptr && callee.value != nullptr && !llvm::isa<llvm::Function>(callee.value)) {
        std::vector<llvm::Value *> args;
        args.reserve(node.args.size());
        unsigned index = 0;
        for (const auto &arg : node.args) {
            const TypedValue value = emit_rvalue(*arg);
            llvm::Type *param = index < callee.fn_ty->getNumParams() ? callee.fn_ty->getParamType(index) : nullptr;
            args.push_back(as_stored(value.value, value.depth, param, value.elem_unsigned));
            ++index;
        }
        prepared.indirect = callee.value;
        prepared.fn_ty = callee.fn_ty;
        prepared.args = std::move(args);
        prepared.depth = callee.fn_ret_depth;
        prepared.elem_float = callee.fn_ret_float;
        prepared.elem_bits = callee.fn_ret_bits;
        prepared.elem_unsigned = callee.fn_ret_unsigned;
        prepared.elem_char = callee.fn_ret_char;
        prepared.elem_bool = callee.fn_ret_bool;
        prepared.struct_id = callee.fn_ret_struct;
        prepared.fn_results = callee.fn_results;
        return prepared;
    }
    auto *function = llvm::dyn_cast_or_null<llvm::Function>(callee.value);
    if (function == nullptr) {
        error(node.loc, "type mismatch");
        return prepared;
    }
    const auto abi = extern_abis_.find(function);
    if (abi != extern_abis_.end() && abi->second.active) {
        const ExternLayout layout = abi->second;
        std::vector<llvm::Value *> args;
        unsigned attr_index = 0;
        if (layout.sret && layout.ret_struct != nullptr) {
            llvm::AllocaInst *slot = aligned_slot(layout.ret_struct, std::max(layout.ret.natural_align, 1u), "sret");
            args.push_back(slot);
            prepared.abi_return = true;
            prepared.abi_sret = true;
            prepared.abi_slot = slot;
            prepared.abi_struct = layout.ret_struct;
            prepared.abi_struct_id = layout.ret_struct_id;
            prepared.abi_ret = layout.ret;
            AbiMark mark;
            mark.index = attr_index++;
            mark.sret = true;
            mark.type = layout.ret_struct;
            mark.align = layout.ret.natural_align == 0 ? 1 : layout.ret.natural_align;
            prepared.abi_attrs.push_back(mark);
        } else if (layout.ret_struct != nullptr) {
            llvm::AllocaInst *slot = aligned_slot(layout.ret_struct, std::max(layout.ret.natural_align, 8u), "cret");
            prepared.abi_return = true;
            prepared.abi_slot = slot;
            prepared.abi_struct = layout.ret_struct;
            prepared.abi_struct_id = layout.ret_struct_id;
            prepared.abi_ret = layout.ret;
        }
        std::size_t source = 0;
        for (const auto &arg : node.args) {
            const TypedValue value = emit_rvalue(*arg);
            if (source < layout.args.size() && layout.args[source].is_struct) {
                const ExternLayout::Arg &info = layout.args[source];
                llvm::AllocaInst *slot =
                    aligned_slot(info.struct_type, std::max(info.abi.natural_align, 8u), "carg");
                if (value.value != nullptr && value.value->getType() == info.struct_type) {
                    builder_.CreateStore(value.value, slot);
                } else if (info.struct_type != nullptr) {
                    builder_.CreateStore(llvm::ConstantAggregateZero::get(info.struct_type), slot);
                }
                if (info.abi.memory) {
                    args.push_back(slot);
                    AbiMark mark;
                    mark.index = attr_index++;
                    mark.type = info.struct_type;
                    mark.align = info.abi.indirect_align == 0 ? 8 : info.abi.indirect_align;
                    prepared.abi_attrs.push_back(mark);
                } else {
                    for (std::size_t part = 0; part < info.abi.parts.size(); ++part) {
                        const uint64_t offset = part < info.abi.offsets.size() ? info.abi.offsets[part] : 0;
                        args.push_back(load_at(slot, info.abi.parts[part], offset));
                        ++attr_index;
                    }
                }
            } else {
                llvm::Type *param = attr_index < function->getFunctionType()->getNumParams()
                                        ? function->getFunctionType()->getParamType(attr_index)
                                        : nullptr;
                args.push_back(as_stored(value.value, value.depth, param, value.elem_unsigned));
                ++attr_index;
            }
            ++source;
        }
        remember(function, std::move(args), callee.depth, callee.elem_char, callee.struct_id, callee.elem_void,
                 callee.elem_bool, callee.elem_bits, callee.elem_unsigned);
        prepared.elem_float = callee.elem_float;
        return prepared;
    }
    std::vector<llvm::Value *> args;
    args.reserve(node.args.size());
    unsigned index = 0;
    for (const auto &arg : node.args) {
        const TypedValue value = emit_rvalue(*arg);
        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                ? function->getFunctionType()->getParamType(index)
                                : nullptr;
        args.push_back(as_stored(value.value, value.depth, param, value.elem_unsigned));
        ++index;
    }
    remember(function, std::move(args), callee.depth, callee.elem_char, callee.struct_id, callee.elem_void, callee.elem_bool,
             callee.elem_bits, callee.elem_unsigned);
    prepared.elem_float = callee.elem_float;
    return prepared;
}

TypedValue CodeGen::emit_prepared(const PreparedCall &call) {
    if (terminated() || (call.function == nullptr && (call.indirect == nullptr || call.fn_ty == nullptr))) {
        return {zero(), 0};
    }
    llvm::CallInst *inst = call.indirect != nullptr ? builder_.CreateCall(call.fn_ty, call.indirect, call.args)
                                                    : builder_.CreateCall(call.function, call.args);
    inst->setCallingConv(llvm::CallingConv::C);
    if (!call.abi_attrs.empty()) {
        annotate_call(inst, call.abi_attrs);
    }
    if (call.abi_return && call.abi_slot != nullptr && call.abi_struct != nullptr) {
        if (!call.abi_sret) {
            unpack_return(inst, call.abi_slot, call.abi_ret);
        }
        return load_object(call.abi_slot, 0, false, call.abi_struct_id, false, false, 0, false, false);
    }
    if (call.elem_float) {
        return {inst, 0, false, -1, false, false, false, call.elem_bits >= 64 ? 64 : 32, false, -1, true};
    }
    if (inst->getType()->isVoidTy()) {
        return {zero(), 0};
    }
    auto tagged = [&](TypedValue value) {
        value.enum_id = call.enum_id;
        return value;
    };
    if (call.depth == 0 && call.elem_bool) {
        return tagged({builder_.CreateZExt(inst, i32()), 0});
    }
    if (call.depth == 0 && (call.elem_char || call.elem_bits == 8 || call.elem_bits == 16)) {
        const bool zero_extend = call.elem_unsigned || call.elem_char;
        llvm::Value *wide = zero_extend ? builder_.CreateZExt(inst, i32()) : builder_.CreateSExt(inst, i32());
        return tagged({wide, 0});
    }
    return tagged({inst, call.depth, call.elem_char, call.struct_id, false, call.elem_void, call.elem_bool, call.elem_bits,
                   call.elem_unsigned});
}

llvm::AllocaInst *CodeGen::defer_flag() {
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    llvm::AllocaInst *flag = allocas.CreateAlloca(i1(), nullptr, "defer.on");
    allocas.CreateStore(llvm::ConstantInt::getFalse(context_), flag);
    return flag;
}

void CodeGen::emit_one_defer(const DeferredCall &call) {
    if (terminated() || call.function == nullptr || call.flag == nullptr) {
        return;
    }
    llvm::BasicBlock *call_block = llvm::BasicBlock::Create(context_, "defer.call", function_);
    llvm::BasicBlock *skip_block = llvm::BasicBlock::Create(context_, "defer.skip", function_);
    llvm::Value *flag = builder_.CreateLoad(i1(), call.flag);
    builder_.CreateCondBr(flag, call_block, skip_block);
    builder_.SetInsertPoint(call_block);
    std::vector<llvm::Value *> args;
    args.reserve(call.args.size());
    for (llvm::AllocaInst *slot : call.args) {
        llvm::Type *type = slot->getAllocatedType();
        args.push_back(builder_.CreateLoad(type, slot));
    }
    llvm::CallInst *inst = builder_.CreateCall(call.function, args);
    inst->setCallingConv(call.function->getCallingConv());
    builder_.CreateStore(llvm::ConstantInt::getFalse(context_), call.flag);
    builder_.CreateBr(skip_block);
    builder_.SetInsertPoint(skip_block);
}

void CodeGen::emit_defer_frame(DeferFrame &frame) {
    for (int index = static_cast<int>(frame.calls.size()) - 1; index >= 0; --index) {
        emit_one_defer(frame.calls[static_cast<std::size_t>(index)]);
        if (terminated()) {
            return;
        }
    }
}

void CodeGen::emit_defers_from(int min_loop_index) {
    for (int frame = static_cast<int>(defers_.size()) - 1; frame >= 0; --frame) {
        if (defers_[static_cast<std::size_t>(frame)].loop_index < min_loop_index) {
            break;
        }
        emit_defer_frame(defers_[static_cast<std::size_t>(frame)]);
        if (terminated()) {
            return;
        }
    }
}

TypedValue CodeGen::emit_generic_call(const Binding &binding, const Expr::Call &node) {
    const Binding meta = binding;
    const FunctionDecl *fn = meta.decl;
    std::vector<TypedValue> arg_values;
    arg_values.reserve(node.args.size());
    for (const auto &arg : node.args) {
        arg_values.push_back(emit_rvalue(*arg));
    }
    if (fn == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    if (arg_values.size() != fn->params.size()) {
        error(node.loc, "argument count");
        return {zero(), 0};
    }
    std::vector<int> spec(fn->params.size(), -1);
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (!param_is_trait(current_file_, fn->params[i])) {
            continue;
        }
        const TypedValue &arg = arg_values[i];
        const int indirections = fn->params[i].indirections;
        const bool value_ok = indirections == 0 && arg.depth == 0 && arg.struct_id >= 0;
        const bool pointer_ok = indirections == 1 && arg.depth == 1 && arg.struct_id >= 0;
        if (!value_ok && !pointer_ok) {
            error(node.loc, "type mismatch");
            return {zero(), 0};
        }
        spec[i] = arg.struct_id;
    }
    const PackageNode *owner = find_node(current_file_);
    if (owner == nullptr) {
        error(node.loc, "type mismatch");
        return {zero(), 0};
    }
    std::string symbol = owner->qualifier + "." + fn->name + ".spec";
    for (int sid : spec) {
        if (sid < 0 || sid >= static_cast<int>(structs_.size())) {
            continue;
        }
        symbol += '.';
        symbol += structs_[static_cast<std::size_t>(sid)].name;
    }
    llvm::Function *function = nullptr;
    if (const auto found = specs_.find(symbol); found != specs_.end()) {
        function = found->second;
    } else {
        function = llvm::Function::Create(spec_type(*fn, spec), llvm::GlobalValue::InternalLinkage, symbol, module_);
        function->setCallingConv(llvm::CallingConv::C);
        unsigned index = 0;
        for (auto &arg : function->args()) {
            if (index < fn->params.size() && !fn->params[index].name.empty()) {
                arg.setName(fn->params[index].name);
            }
            ++index;
        }
        specs_.emplace(symbol, function);
    }
    if (fn->body != nullptr && function->empty() && !emitting_specs_.contains(symbol)) {
        emitting_specs_.insert(symbol);
        auto saved_scopes = std::move(scopes_);
        auto saved_packages = std::move(packages_);
        llvm::Function *saved_function = function_;
        llvm::Type *saved_return = return_type_;
        const int saved_depth = return_depth_;
        auto saved_spec = active_spec_;
        auto saved_loops = std::move(loops_);
        auto saved_defers = std::move(defers_);
        auto saved_cases = std::move(case_blocks_);
        const auto saved_ip = builder_.saveIP();
        scopes_.clear();
        packages_.clear();
        loops_.clear();
        defers_.clear();
        case_blocks_.clear();
        active_spec_ = spec;
        push();
        bind_package_globals();
        if (const PackageNode *node_owner = find_node(current_file_)) {
            bind_imports(*node_owner);
        }
        emit_function_body(*fn, function, meta.depth);
        pop();
        scopes_ = std::move(saved_scopes);
        packages_ = std::move(saved_packages);
        function_ = saved_function;
        return_type_ = saved_return;
        return_depth_ = saved_depth;
        active_spec_ = std::move(saved_spec);
        loops_ = std::move(saved_loops);
        defers_ = std::move(saved_defers);
        case_blocks_ = std::move(saved_cases);
        builder_.restoreIP(saved_ip);
        emitting_specs_.erase(symbol);
    }
    std::vector<llvm::Value *> args;
    args.reserve(arg_values.size());
    unsigned index = 0;
    for (const TypedValue &value : arg_values) {
        llvm::Type *param = index < function->getFunctionType()->getNumParams()
                                ? function->getFunctionType()->getParamType(index)
                                : nullptr;
        args.push_back(as_stored(value.value, value.depth, param, value.elem_unsigned));
        ++index;
    }
    llvm::CallInst *call = builder_.CreateCall(function, args);
    call->setCallingConv(function->getCallingConv());
    if (call->getType()->isVoidTy()) {
        return {zero(), 0};
    }
    if (meta.depth == 0 && meta.elem_bool) {
        return {builder_.CreateZExt(call, i32()), 0};
    }
    if (meta.depth == 0 && (meta.elem_char || meta.elem_bits == 8 || meta.elem_bits == 16)) {
        const bool zero_extend = meta.elem_unsigned || meta.elem_char;
        llvm::Value *wide = zero_extend ? builder_.CreateZExt(call, i32()) : builder_.CreateSExt(call, i32());
        return {wide, 0};
    }
    return {call, meta.depth, meta.elem_char, meta.struct_id, false, meta.elem_void, meta.elem_bool, meta.elem_bits,
            meta.elem_unsigned};
}

TypedValue CodeGen::emit_addr(const Expr &expr) {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(name->text);
        if (binding == nullptr || binding->is_function || binding->is_array) {
            error(name->loc, binding != nullptr && binding->is_array ? "not supported yet" : "not an lvalue");
            return {};
        }
        return {binding->value, binding->depth, binding->elem_char, binding->struct_id, false, binding->elem_void,
                binding->elem_bool, binding->elem_bits, binding->elem_unsigned, binding->trait_id, binding->elem_float,
                binding->fn_ty, binding->fn_ret_depth, binding->fn_ret_float, binding->fn_ret_bits,
                binding->fn_ret_unsigned, binding->fn_ret_char, binding->fn_ret_bool, binding->fn_ret_struct,
                binding->fn_results, binding->enum_id};
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data); star != nullptr && star->op == TokenKind::Star) {
        const TypedValue inner = emit_rvalue(*star->expr);
        if (inner.depth <= 0 || inner.value == nullptr) {
            error(star->loc, "not an lvalue");
            return {};
        }
        return {inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, false, inner.elem_void, inner.elem_bool,
                inner.elem_bits, inner.elem_unsigned};
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        const TypedValue base = emit_rvalue(*index->base);
        const TypedValue subscript = emit_rvalue(*index->index);
        if (base.depth <= 0 || base.value == nullptr || subscript.value == nullptr) {
            error(index->loc, "not an lvalue");
            return {};
        }
        llvm::Value *offset = subscript.value;
        if (base.ptr_to_array) {
            const int depth = base.depth - 1;
            llvm::Type *element =
                leaf_type(depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits, base.elem_float);
            llvm::Type *array = llvm::ArrayType::get(element, static_cast<uint64_t>(base.ptr_array_len));
            llvm::Value *slot = builder_.CreateInBoundsGEP(
                array, base.value, llvm::ArrayRef<llvm::Value *>{const_i32(0), offset});
            return {slot, depth, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool, base.elem_bits,
                    base.elem_unsigned};
        }
        const int depth = base.depth - 1;
        llvm::Value *slot = builder_.CreateInBoundsGEP(
            leaf_type(depth, base.elem_char, base.struct_id, base.elem_bool, base.elem_bits), base.value,
            llvm::ArrayRef<llvm::Value *>{offset});
        return {slot, depth, base.elem_char, base.struct_id, false, base.elem_void, base.elem_bool, base.elem_bits,
                base.elem_unsigned};
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        const auto *base_name = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        const auto package = base_name == nullptr ? packages_.end() : packages_.find(base_name->text);
        if (base_name != nullptr && package != packages_.end()) {
            const Binding *found = visible(package->second, selector->field, false);
            if (found == nullptr || found->is_function || found->is_array) {
                error(selector->loc, found != nullptr && found->is_array ? "not supported yet" : "not an lvalue");
                return {};
            }
            return {found->value, found->depth, found->elem_char, found->struct_id, false, found->elem_void, found->elem_bool,
                    found->elem_bits, found->elem_unsigned};
        }
        if (selector->base == nullptr) {
            error(selector->loc, "not an lvalue");
            return {};
        }
        if (addressable(*selector->base)) {
            const TypedValue base = emit_addr(*selector->base);
            if (base.value == nullptr) {
                return {};
            }
            if (base.depth == 0 && base.struct_id >= 0) {
                return field_addr(base.struct_id, base.value, selector->field, selector->loc);
            }
            if (base.depth == 1 && base.struct_id >= 0) {
                llvm::Value *pointer = builder_.CreateLoad(ptr_type(), base.value);
                return field_addr(base.struct_id, pointer, selector->field, selector->loc);
            }
            error(selector->loc, "not an lvalue");
            return {};
        }
        const TypedValue base = emit_rvalue(*selector->base);
        if (base.value != nullptr && base.depth == 1 && base.struct_id >= 0) {
            return field_addr(base.struct_id, base.value, selector->field, selector->loc);
        }
        error(selector->loc, "not an lvalue");
        return {};
    }
    error(expr_loc(expr), "not an lvalue");
    return {};
}

llvm::Value *CodeGen::emit_short_circuit(const Expr::Binary &expr, bool is_and) {
    llvm::Value *lhs = emit_rvalue(*expr.lhs).value;
    llvm::BasicBlock *lhs_block = builder_.GetInsertBlock();
    llvm::BasicBlock *rhs_block = llvm::BasicBlock::Create(context_, is_and ? "land.rhs" : "lor.rhs", function_);
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, is_and ? "land.end" : "lor.end", function_);
    builder_.CreateCondBr(is_nonzero(lhs), is_and ? rhs_block : end_block, is_and ? end_block : rhs_block);
    builder_.SetInsertPoint(rhs_block);
    llvm::Value *rhs = emit_rvalue(*expr.rhs).value;
    llvm::Value *rhs_flag = is_nonzero(rhs);
    llvm::BasicBlock *rhs_end = builder_.GetInsertBlock();
    if (!terminated()) {
        builder_.CreateBr(end_block);
    }
    builder_.SetInsertPoint(end_block);
    auto *phi = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
    phi->addIncoming(is_and ? llvm::ConstantInt::getFalse(context_) : llvm::ConstantInt::getTrue(context_), lhs_block);
    phi->addIncoming(rhs_flag, rhs_end);
    return builder_.CreateZExt(phi, i32());
}

llvm::Type *CodeGen::shape_type(const Shape &shape) const {
    if (shape.is_function || (shape.depth == 0 && shape.elem_void && !shape.is_array)) {
        return nullptr;
    }
    if (shape.is_array) {
        if (shape.array_len <= 0) {
            return nullptr;
        }
        return llvm::ArrayType::get(
            leaf_type(shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits),
            static_cast<uint64_t>(shape.array_len));
    }
    return leaf_type(shape.depth, shape.elem_char, shape.struct_id, shape.elem_bool, shape.elem_bits);
}

CodeGen::Shape CodeGen::decay_shape(Shape shape) const {
    if (shape.is_array) {
        shape.depth += 1;
        shape.is_array = false;
        shape.array_len = 0;
        shape.type = ptr_type();
        return shape;
    }
    if (shape.depth == 0 && shape.elem_char) {
        shape.elem_char = false;
        shape.type = i32();
    }
    if (shape.depth == 0 && shape.elem_bool) {
        shape.elem_bool = false;
        shape.type = i32();
    }
    return shape;
}

CodeGen::Shape CodeGen::shape_from_binding(const Binding &binding) const {
    Shape shape;
    shape.depth = binding.depth;
    shape.elem_char = binding.elem_char;
    shape.elem_void = binding.elem_void;
    shape.elem_bool = binding.elem_bool;
    shape.elem_bits = binding.elem_bits;
    shape.elem_unsigned = binding.elem_unsigned;
    shape.struct_id = binding.struct_id;
    shape.is_array = binding.is_array;
    shape.array_len = binding.array_len;
    shape.is_function = binding.is_function;
    shape.type = shape_type(shape);
    return shape;
}

CodeGen::Shape CodeGen::raw_shape(const Expr &expr) const {
    return std::visit(
        [&](const auto &node) -> Shape {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const Binding *binding = lookup(node.text)) {
                    return shape_from_binding(*binding);
                }
                const Alias *alias = find_alias(current_file_, node.text);
                if (alias == nullptr) {
                    alias = selective_alias(current_file_, node.text);
                }
                if (alias != nullptr) {
                    Shape shape;
                    shape.depth = alias->depth;
                    shape.elem_char = alias->elem_char;
                    shape.elem_void = alias->elem_void;
                    shape.elem_bool = alias->elem_bool;
                    shape.elem_bits = alias->elem_bits;
                    shape.elem_unsigned = alias->elem_unsigned;
                    shape.struct_id = alias->struct_id;
                    shape.is_array = alias->is_array;
                    shape.array_len = alias->array_len;
                    shape.type = shape_type(shape);
                    return shape;
                }
                int sid = tag_id(current_file_, node.text);
                if (sid < 0) {
                    sid = selective_tag(current_file_, node.text);
                }
                if (sid < 0) {
                    int eid = enum_named(current_file_, node.text);
                    if (eid < 0) {
                        eid = selective_enum(current_file_, node.text);
                    }
                    if (eid < 0 || eid >= static_cast<int>(enums_.size())) {
                        return {};
                    }
                    const EnumLayout &layout = enums_[static_cast<std::size_t>(eid)];
                    Shape shape;
                    shape.elem_bool = layout.is_bool;
                    shape.elem_char = layout.is_char;
                    shape.elem_unsigned = layout.is_unsigned;
                    shape.elem_bits = layout.is_bool ? 0 : layout.bits;
                    shape.type = shape_type(shape);
                    return shape;
                }
                Shape shape;
                shape.struct_id = sid;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Sizeof>) {
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                Shape shape;
                shape.elem_bool = true;
                shape.type = i1();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                Shape shape;
                shape.depth = 1;
                shape.elem_void = true;
                shape.type = ptr_type();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                Shape shape;
                shape.elem_char = true;
                shape.is_array = true;
                shape.array_len = static_cast<int>(node.text.size()) + 1;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.postfix && node.op == TokenKind::Bang && node.expr) {
                    return raw_shape(*node.expr);
                }
                if (node.op == TokenKind::Star && node.expr) {
                    const Shape inner = decay_shape(raw_shape(*node.expr));
                    if (inner.depth <= 0) {
                        return {};
                    }
                    Shape pointee = inner;
                    pointee.depth -= 1;
                    pointee.is_array = false;
                    pointee.array_len = 0;
                    pointee.is_function = false;
                    pointee.type = shape_type(pointee);
                    return pointee;
                }
                if (node.op == TokenKind::Amp && node.expr) {
                    Shape inner = raw_shape(*node.expr);
                    if (inner.is_array || inner.is_function || inner.type == nullptr) {
                        return {};
                    }
                    inner.depth += 1;
                    inner.type = ptr_type();
                    return inner;
                }
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                Shape shape;
                shape.type = i32();
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                return node.lhs ? raw_shape(*node.lhs) : Shape{};
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                if (node.base == nullptr) {
                    return {};
                }
                const Shape inner = decay_shape(raw_shape(*node.base));
                if (inner.depth <= 0) {
                    return {};
                }
                Shape pointee = inner;
                pointee.depth -= 1;
                pointee.is_array = false;
                pointee.array_len = 0;
                pointee.type = shape_type(pointee);
                return pointee;
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct,
                                          node.indirections, false, 0, false);
                Shape shape;
                shape.depth = low.depth;
                shape.elem_char = low.elem_char;
                shape.elem_void = low.elem_void;
                shape.elem_bool = low.elem_bool;
                shape.elem_bits = low.elem_bits;
                shape.elem_unsigned = low.elem_unsigned;
                shape.struct_id = low.struct_id;
                shape.type = low.depth == 0 && low.elem_void ? nullptr : low.type;
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.callee == nullptr) {
                    return {};
                }
                const Binding *binding = nullptr;
                if (const auto *name = std::get_if<Expr::Name>(&node.callee->data)) {
                    binding = lookup(name->text);
                } else if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                    const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
                    if (base != nullptr) {
                        const auto package = packages_.find(base->text);
                        if (package != packages_.end()) {
                            binding = visible(package->second, selector->field, true);
                        }
                    }
                }
                if (binding == nullptr || !binding->is_function) {
                    return {};
                }
                Shape shape;
                shape.depth = binding->depth;
                shape.elem_char = binding->elem_char;
                shape.elem_void = binding->elem_void;
                shape.elem_bool = binding->elem_bool;
                shape.elem_bits = binding->elem_bits;
                shape.elem_unsigned = binding->elem_unsigned;
                shape.struct_id = binding->struct_id;
                shape.type = shape_type(shape);
                return shape;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                const auto *base_name = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base_name != nullptr) {
                    const auto package = packages_.find(base_name->text);
                    if (package != packages_.end()) {
                        if (const Binding *function = visible(package->second, node.field, true)) {
                            return shape_from_binding(*function);
                        }
                        if (const Binding *global = visible(package->second, node.field, false)) {
                            return shape_from_binding(*global);
                        }
                        return {};
                    }
                }
                if (node.base == nullptr) {
                    return {};
                }
                const Shape base = raw_shape(*node.base);
                if (base.depth > 1 || base.struct_id < 0 || base.is_array) {
                    return {};
                }
                int field_index = -1;
                const FieldLayout *field = find_field(base.struct_id, node.field, field_index);
                if (field == nullptr) {
                    return {};
                }
                Shape shape;
                shape.depth = field->depth;
                shape.elem_char = field->elem_char;
                shape.elem_void = field->elem_void;
                shape.elem_bool = field->elem_bool;
                shape.elem_bits = field->elem_bits;
                shape.elem_unsigned = field->elem_unsigned;
                shape.struct_id = field->struct_id;
                shape.is_array = field->is_array;
                shape.array_len = field->array_len;
                shape.type = field->type != nullptr ? field->type : shape_type(shape);
                return shape;
            } else {
                return {};
            }
        },
        expr.data);
}

TypedValue CodeGen::emit_rvalue(const Expr &expr) {
    if (terminated()) {
        return {zero(), 0};
    }
    return std::visit(
        [&](const auto &node) -> TypedValue {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                const Binding *binding = lookup(node.text);
                if (binding == nullptr) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (binding->is_generic) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                if (binding->value == nullptr) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                return value_of_binding(*binding);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                const std::string number = node.text.empty() ? "0" : node.text;
                const double parsed = std::strtod(number.c_str(), nullptr);
                llvm::Type *ty = node.is_f32 ? f32_ty() : f64_ty();
                return {llvm::ConstantFP::get(ty, parsed), 0, false, -1, false, false, false, node.is_f32 ? 32 : 64, false,
                        -1, true};
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                const int bits = node.suffix.bits;
                const bool uns = node.suffix.is_unsigned;
                if (bits >= 64) {
                    return {llvm::ConstantInt::get(i64(), node.value, false), 0, false, -1, false, false, false, 64, uns};
                }
                if (uns && bits == 32) {
                    return {const_i32(static_cast<int32_t>(node.value)), 0, false, -1, false, false, false, 32, true};
                }
                // A narrower suffix promotes to int when it is used as a value.
                return {const_i32(static_cast<int32_t>(node.value)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                return {const_i32(static_cast<int32_t>(node.value & 0xFFULL)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::Star) {
                    const TypedValue inner = emit_rvalue(*node.expr);
                    if (inner.depth <= 0 || inner.value == nullptr) {
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                    // `*arr` on a pointer to an array is the array, which decays to a pointer to its element.
                    if (inner.ptr_to_array) {
                        TypedValue decayed = inner;
                        decayed.ptr_to_array = false;
                        decayed.ptr_array_len = 0;
                        return decayed;
                    }
                    return load_object(inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, inner.elem_void,
                                       inner.elem_bool, inner.elem_bits, inner.elem_unsigned, inner.elem_float);
                }
                if (node.op == TokenKind::Amp) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Binding *binding = lookup(named->text); binding != nullptr && binding->is_array) {
                            TypedValue pointer;
                            pointer.value = binding->value;
                            pointer.depth = binding->depth + 1;
                            pointer.elem_char = binding->elem_char;
                            pointer.struct_id = binding->struct_id;
                            pointer.elem_void = binding->elem_void;
                            pointer.elem_bool = binding->elem_bool;
                            pointer.elem_bits = binding->elem_bits;
                            pointer.elem_unsigned = binding->elem_unsigned;
                            pointer.elem_float = binding->elem_float;
                            pointer.ptr_to_array = true;
                            pointer.ptr_array_len = binding->array_len;
                            return pointer;
                        }
                    }
                    const TypedValue addr = emit_addr(*node.expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return {addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void, addr.elem_bool,
                            addr.elem_bits, addr.elem_unsigned};
                }
                const TypedValue inner_value = emit_rvalue(*node.expr);
                if (node.op == TokenKind::Bang && node.postfix) {
                    return inner_value;
                }
                llvm::Value *inner = inner_value.value;
                switch (node.op) {
                case TokenKind::Plus: return inner_value;
                case TokenKind::Minus:
                    if (inner_value.elem_float || (inner != nullptr && inner->getType()->isFloatingPointTy())) {
                        return {builder_.CreateFNeg(inner), 0, false, -1, false, false, false, inner_value.elem_bits, false,
                                -1, true};
                    }
                    return {builder_.CreateNeg(inner), 0, false, -1, false, false, false, inner_value.elem_bits,
                            inner_value.elem_unsigned};
                case TokenKind::Tilde:
                    return {builder_.CreateNot(inner), 0, false, -1, false, false, false, inner_value.elem_bits,
                            inner_value.elem_unsigned};
                case TokenKind::Bang:
                    if (inner_value.elem_float || (inner != nullptr && inner->getType()->isFloatingPointTy())) {
                        llvm::Value *zero_fp = llvm::ConstantFP::get(inner->getType(), 0.0);
                        return {builder_.CreateZExt(builder_.CreateFCmpOEQ(inner, zero_fp), i32()), 0};
                    }
                    return {builder_.CreateZExt(builder_.CreateICmpEQ(inner, typed_zero(inner)), i32()), 0};
                default: return {zero(), 0};
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.op == TokenKind::AmpAmp) {
                    return {emit_short_circuit(node, true), 0};
                }
                if (node.op == TokenKind::PipePipe) {
                    return {emit_short_circuit(node, false), 0};
                }
                const TypedValue left = emit_rvalue(*node.lhs);
                const TypedValue right = emit_rvalue(*node.rhs);
                llvm::Value *lhs = left.value;
                llvm::Value *rhs = right.value;
                if ((node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) && (left.depth > 0 || right.depth > 0)) {
                    if (left.depth <= 0) {
                        lhs = null_ptr();
                    }
                    if (right.depth <= 0) {
                        rhs = null_ptr();
                    }
                    llvm::Value *cmp = node.op == TokenKind::EqEq ? builder_.CreateICmpEQ(lhs, rhs)
                                                                  : builder_.CreateICmpNE(lhs, rhs);
                    return {builder_.CreateZExt(cmp, i32()), 0};
                }
                if (left.elem_float || right.elem_float ||
                    (lhs != nullptr && lhs->getType()->isFloatingPointTy()) ||
                    (rhs != nullptr && rhs->getType()->isFloatingPointTy())) {
                    const bool wide = (left.elem_float && left.elem_bits >= 64) || (right.elem_float && right.elem_bits >= 64) ||
                                      (lhs != nullptr && lhs->getType()->isDoubleTy()) ||
                                      (rhs != nullptr && rhs->getType()->isDoubleTy());
                    llvm::Type *dest = wide ? f64_ty() : f32_ty();
                    auto to_fp = [&](llvm::Value *value, const TypedValue &meta) -> llvm::Value * {
                        if (value == nullptr) {
                            return llvm::ConstantFP::get(dest, 0.0);
                        }
                        return as_stored(value, meta.depth, dest, meta.elem_unsigned);
                    };
                    lhs = to_fp(lhs, left);
                    rhs = to_fp(rhs, right);
                    auto widened_fp = [&](llvm::Value *value) -> TypedValue {
                        return {value, 0, false, -1, false, false, false, wide ? 64 : 32, false, -1, true};
                    };
                    auto compared_fp = [&](llvm::CmpInst::Predicate pred) -> TypedValue {
                        return {builder_.CreateZExt(builder_.CreateFCmp(pred, lhs, rhs), i32()), 0};
                    };
                    switch (node.op) {
                    case TokenKind::Plus: return widened_fp(builder_.CreateFAdd(lhs, rhs));
                    case TokenKind::Minus: return widened_fp(builder_.CreateFSub(lhs, rhs));
                    case TokenKind::Star: return widened_fp(builder_.CreateFMul(lhs, rhs));
                    case TokenKind::Slash: return widened_fp(builder_.CreateFDiv(lhs, rhs));
                    case TokenKind::EqEq: return compared_fp(llvm::CmpInst::FCMP_OEQ);
                    case TokenKind::BangEq: return compared_fp(llvm::CmpInst::FCMP_ONE);
                    case TokenKind::Lt: return compared_fp(llvm::CmpInst::FCMP_OLT);
                    case TokenKind::Gt: return compared_fp(llvm::CmpInst::FCMP_OGT);
                    case TokenKind::Le: return compared_fp(llvm::CmpInst::FCMP_OLE);
                    case TokenKind::Ge: return compared_fp(llvm::CmpInst::FCMP_OGE);
                    default:
                        error(node.loc, "type mismatch");
                        return {zero(), 0};
                    }
                }
                const int left_bits = value_bits(left);
                const int right_bits = value_bits(right);
                const int common_bits = left_bits > right_bits ? left_bits : right_bits;
                bool common_unsigned = false;
                if (left.elem_unsigned == right.elem_unsigned) {
                    common_unsigned = left.elem_unsigned;
                } else if (left_bits == right_bits) {
                    common_unsigned = true;
                } else if (left.elem_unsigned && left_bits > right_bits) {
                    common_unsigned = true;
                } else if (right.elem_unsigned && right_bits > left_bits) {
                    common_unsigned = true;
                }
                lhs = resize_int(lhs, bits_type(common_bits), left.elem_unsigned);
                rhs = resize_int(rhs, bits_type(common_bits), right.elem_unsigned);
                auto widened = [&](llvm::Value *value) -> TypedValue {
                    return {value, 0, false, -1, false, false, false, common_bits, common_unsigned};
                };
                auto compared = [&](llvm::CmpInst::Predicate pred) -> TypedValue {
                    return {builder_.CreateZExt(builder_.CreateICmp(pred, lhs, rhs), i32()), 0};
                };
                const bool uns = common_unsigned;
                switch (node.op) {
                case TokenKind::Plus: return widened(builder_.CreateAdd(lhs, rhs));
                case TokenKind::Minus: return widened(builder_.CreateSub(lhs, rhs));
                case TokenKind::Star: return widened(builder_.CreateMul(lhs, rhs));
                case TokenKind::Slash: return widened(uns ? builder_.CreateUDiv(lhs, rhs) : builder_.CreateSDiv(lhs, rhs));
                case TokenKind::Percent: return widened(uns ? builder_.CreateURem(lhs, rhs) : builder_.CreateSRem(lhs, rhs));
                case TokenKind::Amp: return widened(builder_.CreateAnd(lhs, rhs));
                case TokenKind::Pipe: return widened(builder_.CreateOr(lhs, rhs));
                case TokenKind::Caret: return widened(builder_.CreateXor(lhs, rhs));
                case TokenKind::LtLt: return widened(builder_.CreateShl(lhs, rhs));
                case TokenKind::GtGt:
                    return widened(uns ? builder_.CreateLShr(lhs, rhs) : builder_.CreateAShr(lhs, rhs));
                case TokenKind::EqEq: return compared(llvm::CmpInst::ICMP_EQ);
                case TokenKind::BangEq: return compared(llvm::CmpInst::ICMP_NE);
                case TokenKind::Lt: return compared(uns ? llvm::CmpInst::ICMP_ULT : llvm::CmpInst::ICMP_SLT);
                case TokenKind::Gt: return compared(uns ? llvm::CmpInst::ICMP_UGT : llvm::CmpInst::ICMP_SGT);
                case TokenKind::Le: return compared(uns ? llvm::CmpInst::ICMP_ULE : llvm::CmpInst::ICMP_SLE);
                case TokenKind::Ge: return compared(uns ? llvm::CmpInst::ICMP_UGE : llvm::CmpInst::ICMP_SGE);
                default: return {zero(), 0};
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                arm_union_tag_ = true;
                const TypedValue slot = emit_addr(*node.lhs);
                arm_union_tag_ = false;
                const TypedValue rhs = emit_rvalue(*node.rhs);
                llvm::Value *stored = nullptr;
                if (slot.value != nullptr && rhs.value != nullptr) {
                    stored = stored_value(rhs, slot);
                    if (!terminated()) {
                        builder_.CreateStore(stored, slot.value);
                    }
                }
                if (slot.depth == 0 && slot.elem_bool && stored != nullptr) {
                    return {builder_.CreateZExt(stored, i32()), 0};
                }
                if (rhs.depth == 0 && slot.depth > 0) {
                    return {null_ptr(), slot.depth, slot.elem_char, slot.struct_id, false, slot.elem_void};
                }
                return rhs;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                if (node.callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (const Binding *binding = lookup(named->text);
                            binding != nullptr && binding->is_generic && binding->decl != nullptr) {
                            return emit_generic_call(*binding, node);
                        }
                        if (lookup(named->text) == nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                            int enum_id = enum_named(current_file_, named->text);
                            if (enum_id < 0) {
                                enum_id = selective_enum(current_file_, named->text);
                            }
                            if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                                const TypedValue source = emit_rvalue(*node.args.front());
                                if (source.value != nullptr && source.depth == 0 && source.struct_id >= 0 &&
                                    source.struct_id < static_cast<int>(structs_.size()) &&
                                    source.value->getType()->isStructTy() &&
                                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                    llvm::Value *tag = builder_.CreateExtractValue(source.value, 0);
                                    const EnumLayout &enumeration = enums_[static_cast<std::size_t>(enum_id)];
                                    TypedValue value;
                                    value.value = tag;
                                    value.enum_id = enum_id;
                                    value.elem_bool = enumeration.is_bool;
                                    value.elem_char = enumeration.is_char;
                                    value.elem_unsigned = enumeration.is_unsigned;
                                    value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                                    return value;
                                }
                                error(node.loc, "type mismatch");
                                return {zero(), 0};
                            }
                        }
                    }
                }
                return emit_prepared(prepare_call(node));
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base != nullptr) {
                    const int enum_id = enum_type_designator(*node.base);
                    if (enum_id >= 0 && enum_id < static_cast<int>(enums_.size())) {
                        const EnumLayout &layout = enums_[static_cast<std::size_t>(enum_id)];
                        const auto member = layout.values.find(node.field);
                        if (member == layout.values.end()) {
                            error(node.loc, "undeclared identifier");
                            return {zero(), 0};
                        }
                        TypedValue value;
                        value.value = enum_constant(enum_id, member->second);
                        value.enum_id = enum_id;
                        value.elem_bool = layout.is_bool;
                        value.elem_char = layout.is_char;
                        value.elem_unsigned = layout.is_unsigned;
                        value.elem_bits = layout.is_bool ? 0 : layout.bits;
                        return value;
                    }
                }
                const auto *base_name = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                const auto package = base_name == nullptr ? packages_.end() : packages_.find(base_name->text);
                if (base_name != nullptr && package != packages_.end()) {
                    if (const Binding *function = visible(package->second, node.field, true)) {
                        return value_of_binding(*function);
                    }
                    if (const Binding *global = visible(package->second, node.field, false)) {
                        return value_of_binding(*global);
                    }
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (node.base != nullptr && addressable(*node.base)) {
                    const TypedValue addr = emit_addr(expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    if (addr.is_array) {
                        return {addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void,
                                addr.elem_bool, addr.elem_bits, addr.elem_unsigned};
                    }
                    return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                       addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                }
                const TypedValue base = node.base != nullptr ? emit_rvalue(*node.base) : TypedValue{};
                if (base.value != nullptr && base.depth == 1 && base.struct_id >= 0) {
                    const TypedValue addr = field_addr(base.struct_id, base.value, node.field, node.loc);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    if (addr.is_array) {
                        return {addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void,
                                addr.elem_bool, addr.elem_bits, addr.elem_unsigned};
                    }
                    return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                       addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                }
                if (base.value == nullptr || base.depth != 0 || base.struct_id < 0) {
                    error(node.loc, "type mismatch");
                    return {zero(), 0};
                }
                int field_index = -1;
                const FieldLayout *field = find_field(base.struct_id, node.field, field_index);
                if (field == nullptr || field_index < 0) {
                    error(node.loc, "undeclared identifier");
                    return {zero(), 0};
                }
                if (field->is_array) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                if (structs_[static_cast<std::size_t>(base.struct_id)].is_union) {
                    llvm::AllocaInst *slot =
                        alloca_typed(structs_[static_cast<std::size_t>(base.struct_id)].type, "uval");
                    if (base.value != nullptr) {
                        builder_.CreateStore(base.value, slot);
                    }
                    if (structs_[static_cast<std::size_t>(base.struct_id)].tag_enum >= 0) {
                        const TypedValue addr = field_addr(base.struct_id, slot, node.field, node.loc);
                        if (addr.value == nullptr) {
                            return {zero(), 0};
                        }
                        return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void,
                                           addr.elem_bool, addr.elem_bits, addr.elem_unsigned, addr.elem_float);
                    }
                    return load_object(slot, field->depth, field->elem_char, field->struct_id, field->elem_void,
                                       field->elem_bool, field->elem_bits, field->elem_unsigned, field->elem_float);
                }
                llvm::Value *extracted = builder_.CreateExtractValue(base.value, static_cast<unsigned>(field_index));
                if (field->depth == 0 && field->elem_bool) {
                    return {builder_.CreateZExt(extracted, i32()), 0};
                }
                if (field->depth == 0 && (field->elem_char || field->elem_bits == 8 || field->elem_bits == 16)) {
                    const bool zero_extend = field->elem_unsigned || field->elem_char;
                    llvm::Value *wide =
                        zero_extend ? builder_.CreateZExt(extracted, i32()) : builder_.CreateSExt(extracted, i32());
                    return {wide, 0};
                }
                if (field->depth == 0 && field->elem_float) {
                    return {extracted, 0, false, -1, false, false, false, field->elem_bits, false, -1, true};
                }
                if (field->depth == 0 && field->elem_bits == 64) {
                    return {extracted, 0, false, -1, false, false, false, 64, field->elem_unsigned};
                }
                if (field->depth == 0 && field->elem_unsigned) {
                    return {extracted, 0, false, -1, false, false, false, 32, true};
                }
                return {extracted, field->depth, field->elem_char, field->struct_id, false, field->elem_void,
                        field->elem_bool, field->elem_bits, field->elem_unsigned, -1, field->elem_float};
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                llvm::Constant *data = llvm::ConstantDataArray::getString(context_, node.text, true);
                auto *global = new llvm::GlobalVariable(module_, data->getType(), true,
                                                        llvm::GlobalValue::PrivateLinkage, data, ".str");
                return {global, 1, true};
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                const TypedValue addr = emit_addr(expr);
                if (addr.value == nullptr) {
                    return {zero(), 0};
                }
                return load_object(addr.value, addr.depth, addr.elem_char, addr.struct_id, addr.elem_void, addr.elem_bool,
                                   addr.elem_bits, addr.elem_unsigned, addr.elem_float);
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                llvm::Type *measured = nullptr;
                if (node.on_type) {
                    const Lowered low = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections,
                                              false, 0, false);
                    if (low.depth == 0 && low.elem_void) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    measured = low.type;
                } else if (node.expr) {
                    const Shape shape = raw_shape(*node.expr);
                    if (shape.is_function || (shape.depth == 0 && shape.elem_void)) {
                        error(node.loc, "not supported yet");
                        return {zero(), 0};
                    }
                    measured = shape.type;
                }
                if (measured == nullptr || measured->isVoidTy() || measured->isFunctionTy()) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                const uint64_t bytes = module_.getDataLayout().getTypeAllocSize(measured).getFixedValue();
                if (bytes > 2147483647ULL) {
                    error(node.loc, "not supported yet");
                    return {zero(), 0};
                }
                return {const_i32(static_cast<int32_t>(bytes)), 0};
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return {const_i32(node.value ? 1 : 0), 0};
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                return {null_ptr(), 1, false, -1, false, true};
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                const TypedValue source = node.expr ? emit_rvalue(*node.expr) : TypedValue{};
                const Lowered target = lower(current_file_, node.type_name, node.type_package, node.is_struct, node.indirections,
                                             false, 0, false);
                if (source.value == nullptr) {
                    return {zero(), 0};
                }
                if (source.depth == 0 && source.struct_id >= 0 && source.struct_id < static_cast<int>(structs_.size()) &&
                    source.value->getType()->isStructTy()) {
                    const StructLayout &layout = structs_[static_cast<std::size_t>(source.struct_id)];
                    if (layout.tag_enum >= 0 && target.enum_id == layout.tag_enum &&
                        layout.tag_enum < static_cast<int>(enums_.size()) && layout.type != nullptr &&
                        layout.type->getNumElements() > 0) {
                        llvm::Value *tag = builder_.CreateExtractValue(source.value, 0);
                        const EnumLayout &enumeration = enums_[static_cast<std::size_t>(layout.tag_enum)];
                        TypedValue value;
                        value.value = tag;
                        value.enum_id = layout.tag_enum;
                        value.elem_bool = enumeration.is_bool;
                        value.elem_char = enumeration.is_char;
                        value.elem_unsigned = enumeration.is_unsigned;
                        value.elem_bits = enumeration.is_bool ? 0 : enumeration.bits;
                        return value;
                    }
                }
                if (target.depth > 0 && source.depth > 0) {
                    return {source.value, target.depth, target.elem_char, target.struct_id, false, target.elem_void,
                            target.elem_bool, target.elem_bits, target.elem_unsigned};
                }
                if (target.depth > 0 && source.depth == 0) {
                    llvm::IntegerType *intptr = module_.getDataLayout().getIntPtrType(context_, 0);
                    llvm::Value *wide = source.value;
                    if (wide->getType()->isIntegerTy() && wide->getType() != intptr) {
                        wide = wide->getType()->getIntegerBitWidth() < intptr->getIntegerBitWidth()
                                   ? builder_.CreateZExt(wide, intptr)
                                   : builder_.CreateTrunc(wide, intptr);
                    }
                    return {builder_.CreateIntToPtr(wide, ptr_type()), target.depth, target.elem_char, target.struct_id, false,
                            target.elem_void, target.elem_bool, target.elem_bits, target.elem_unsigned};
                }
                if (source.depth > 0 && target.depth == 0 && !target.elem_char && !target.elem_void && target.struct_id < 0 &&
                    !target.elem_bool && target.elem_bits != 8 && target.elem_bits != 16 && target.elem_bits != 64) {
                    llvm::IntegerType *intptr = module_.getDataLayout().getIntPtrType(context_, 0);
                    llvm::Value *wide = builder_.CreatePtrToInt(source.value, intptr);
                    llvm::Value *narrow = wide->getType() == i32() ? wide : builder_.CreateTrunc(wide, i32());
                    return {narrow, 0};
                }
                if (target.depth == 0 && source.depth == 0 && (target.elem_float || source.elem_float ||
                                                              source.value->getType()->isFloatingPointTy())) {
                    llvm::Type *dest = target.elem_float ? (target.elem_bits >= 64 ? f64_ty() : f32_ty())
                                                         : bits_type(target.elem_bits == 0 ? 32 : target.elem_bits);
                    llvm::Value *converted = as_stored(source.value, 0, dest, source.elem_unsigned);
                    if (target.elem_float) {
                        return {converted, 0, false, -1, false, false, false, target.elem_bits, false, -1, true};
                    }
                    return {converted, 0, target.elem_char, -1, false, false, target.elem_bool, target.elem_bits,
                            target.elem_unsigned};
                }
                if (target.depth == 0 && source.depth == 0 && target.struct_id < 0 && !target.elem_void) {
                    int bits = target.elem_bits;
                    if (target.elem_bool) {
                        bits = 1;
                    } else if (bits == 0 && target.elem_char) {
                        bits = 8;
                    } else if (bits == 0) {
                        bits = 32;
                    }
                    llvm::Value *converted = resize_int(source.value, bits_type(bits), source.elem_unsigned);
                    if (bits < 32) {
                        const bool zero_extend = target.elem_unsigned || target.elem_char;
                        converted = zero_extend ? builder_.CreateZExt(converted, i32()) : builder_.CreateSExt(converted, i32());
                        return {converted, 0};
                    }
                    return {converted, 0, false, -1, false, false, false, bits, target.elem_unsigned};
                }
                return {source.value, 0};
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                if (node.decl == nullptr) {
                    return {zero(), 0};
                }
                return emit_anon_function(*node.decl);
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                return emit_init(node);
            } else {
                return {zero(), 0};
            }
        },
        expr.data);
}

void CodeGen::collect_cases(const Stmt &stmt, std::vector<CaseArm> &arms) const {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                CaseArm arm;
                arm.node = &node;
                arm.is_default = node.is_default;
                if (!node.is_default && node.value) {
                    if (const std::optional<int32_t> folded = fold(*node.value)) {
                        arm.value = *folded;
                        arm.has_value = true;
                    }
                }
                arms.push_back(arm);
                if (node.stmt) {
                    collect_cases(*node.stmt, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                for (const auto &child : node.statements) {
                    if (child) {
                        collect_cases(*child, arms);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.then_arm) {
                    collect_cases(*node.then_arm, arms);
                }
                if (node.else_arm) {
                    collect_cases(*node.else_arm, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While> || std::is_same_v<T, Stmt::DoWhile>) {
                if (node.body) {
                    collect_cases(*node.body, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                if (node.init) {
                    collect_cases(*node.init, arms);
                }
                if (node.body) {
                    collect_cases(*node.body, arms);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                return;
            } else {
                return;
            }
        },
        stmt.data);
}

void CodeGen::emit_case(const Stmt::Case &node) {
    llvm::BasicBlock *block = nullptr;
    for (auto it = case_blocks_.rbegin(); it != case_blocks_.rend(); ++it) {
        const auto found = it->find(&node);
        if (found != it->end()) {
            block = found->second;
            break;
        }
    }
    if (block == nullptr) {
        error(node.loc, node.is_default ? "default outside switch" : "case outside switch");
        if (node.stmt && !terminated()) {
            emit_stmt(*node.stmt);
        }
        return;
    }
    if (!terminated()) {
        builder_.CreateBr(block);
    }
    builder_.SetInsertPoint(block);
    if (node.stmt) {
        emit_stmt(*node.stmt);
    }
}

void CodeGen::emit_switch(const Stmt::Switch &node) {
    const TypedValue cond = node.cond ? emit_rvalue(*node.cond) : TypedValue{zero(), 0};
    if (terminated()) {
        return;
    }
    std::vector<CaseArm> arms;
    if (node.body) {
        collect_cases(*node.body, arms);
    }
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "switch.end", function_);
    llvm::BasicBlock *default_block = end_block;
    unsigned cases = 0;
    for (CaseArm &arm : arms) {
        if (arm.is_default) {
            if (default_block == end_block) {
                arm.block = llvm::BasicBlock::Create(context_, "switch.default", function_);
                default_block = arm.block;
            }
            continue;
        }
        arm.block = llvm::BasicBlock::Create(context_, "switch.case", function_);
        if (arm.has_value) {
            ++cases;
        }
    }
    llvm::SwitchInst *inst = builder_.CreateSwitch(cond.value == nullptr ? zero() : cond.value, default_block, cases);
    for (const CaseArm &arm : arms) {
        if (!arm.is_default && arm.has_value && arm.block != nullptr) {
            inst->addCase(llvm::cast<llvm::ConstantInt>(const_i32(arm.value)), arm.block);
        }
    }
    std::unordered_map<const Stmt::Case *, llvm::BasicBlock *> blocks;
    for (const CaseArm &arm : arms) {
        if (arm.node != nullptr && arm.block != nullptr) {
            blocks.emplace(arm.node, arm.block);
        }
    }
    case_blocks_.push_back(std::move(blocks));
    llvm::BasicBlock *step = loops_.empty() ? nullptr : loops_.back().step;
    loops_.push_back(LoopTargets{step, end_block, true});
    llvm::BasicBlock *skip_block = llvm::BasicBlock::Create(context_, "switch.skip", function_);
    builder_.SetInsertPoint(skip_block);
    if (node.body) {
        emit_stmt(*node.body);
    }
    if (!terminated()) {
        builder_.CreateBr(end_block);
    }
    loops_.pop_back();
    case_blocks_.pop_back();
    builder_.SetInsertPoint(end_block);
}

const FunctionDecl *CodeGen::callee_decl(const Expr &callee) const {
    if (const auto *fn = std::get_if<Expr::Func>(&callee.data)) {
        return fn->decl.get();
    }
    if (const auto *name = std::get_if<Expr::Name>(&callee.data)) {
        const Binding *binding = lookup(name->text);
        return binding == nullptr ? nullptr : binding->decl;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data); selector != nullptr && selector->base != nullptr) {
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            const auto package = packages_.find(base->text);
            if (package != packages_.end()) {
                const Binding *binding = visible(package->second, selector->field, true);
                return binding == nullptr ? nullptr : binding->decl;
            }
        }
    }
    return nullptr;
}

void CodeGen::emit_bind(const Stmt::Bind &node) {
    if (node.value == nullptr || node.names.empty() || scopes_.empty()) {
        error(node.loc, "type mismatch");
        return;
    }
    auto create_scalar = [&](const std::string &name, const TypedValue &got) {
        llvm::Type *ty = i32();
        Binding binding;
        if (got.enum_id >= 0 && got.enum_id < static_cast<int>(enums_.size())) {
            const EnumLayout &layout = enums_[static_cast<std::size_t>(got.enum_id)];
            binding.enum_id = got.enum_id;
            binding.elem_bool = layout.is_bool;
            binding.elem_char = layout.is_char;
            binding.elem_unsigned = layout.is_unsigned;
            binding.elem_bits = layout.is_bool ? 0 : layout.bits;
            ty = leaf_type(0, binding.elem_char, -1, binding.elem_bool, binding.elem_bits, false);
        } else if (got.depth > 0) {
            ty = ptr_type();
            binding.depth = got.depth;
            binding.elem_char = got.elem_char;
            binding.struct_id = got.struct_id;
            binding.elem_void = got.elem_void;
            binding.elem_bool = got.elem_bool;
            binding.elem_bits = got.elem_bits;
            binding.elem_unsigned = got.elem_unsigned;
            binding.elem_float = got.elem_float;
            binding.ptr_to_array = got.ptr_to_array;
            binding.ptr_array_len = got.ptr_array_len;
        } else if (got.struct_id >= 0 && got.struct_id < static_cast<int>(structs_.size()) &&
                   structs_[static_cast<std::size_t>(got.struct_id)].type != nullptr) {
            ty = structs_[static_cast<std::size_t>(got.struct_id)].type;
            binding.struct_id = got.struct_id;
        } else if (got.elem_bits == 64) {
            ty = i64();
            binding.elem_bits = 64;
            binding.elem_unsigned = got.elem_unsigned;
        } else if (got.elem_unsigned) {
            binding.elem_bits = 32;
            binding.elem_unsigned = true;
        }
        llvm::AllocaInst *slot = alloca_typed(ty, name);
        llvm::Value *stored = got.value == nullptr ? llvm::Constant::getNullValue(ty) : got.value;
        if (stored->getType() != ty) {
            stored = as_stored(stored, got.depth, ty, got.elem_unsigned);
        }
        builder_.CreateStore(stored, slot);
        binding.value = slot;
        scopes_.back()[name] = binding;
    };
    auto destination = [&](const std::string &name, bool &create) -> llvm::Value * {
        create = node.is_var;
        if (create) {
            return nullptr;
        }
        if (node.is_define) {
            const auto found = scopes_.back().find(name);
            if (found == scopes_.back().end()) {
                create = true;
                return nullptr;
            }
            return found->second.value;
        }
        const Binding *found = lookup(name);
        return found == nullptr ? nullptr : found->value;
    };
    if (node.names.size() == 1) {
        const TypedValue value = emit_rvalue(*node.value);
        if (terminated()) {
            return;
        }
        bool create = false;
        llvm::Value *dest = destination(node.names[0], create);
        if (create) {
            create_scalar(node.names[0], value);
        } else if (dest != nullptr) {
            llvm::Type *ty = object_type(dest);
            builder_.CreateStore(as_stored(value.value, value.depth, ty, value.elem_unsigned), dest);
        }
        return;
    }
    // The component types live on the function declaration, or on the function-pointer
    // type when the call is indirect. prepare_call resolves the same callee the call uses.
    const FunctionDecl *decl = nullptr;
    std::vector<FnParam> fn_results;
    TypedValue value;
    if (const auto *call = std::get_if<Expr::Call>(&node.value->data)) {
        const PreparedCall prepared = prepare_call(*call);
        decl = prepared.decl;
        fn_results = prepared.fn_results;
        value = emit_prepared(prepared);
    } else {
        value = emit_rvalue(*node.value);
    }
    if (terminated()) {
        return;
    }
    std::vector<Lowered> parts;
    if (decl != nullptr && value.value != nullptr && decl->results.size() == node.names.size() &&
        value.value->getType()->isStructTy()) {
        parts.reserve(decl->results.size());
        for (const ResultSlot &slot : decl->results) {
            parts.push_back(lower(current_file_, slot.type_name, slot.type_package, slot.is_struct, slot.indirections,
                                  false, 0, false));
        }
    } else if (decl == nullptr && value.value != nullptr && fn_results.size() == node.names.size() &&
               value.value->getType()->isStructTy()) {
        parts.reserve(fn_results.size());
        for (const FnParam &part : fn_results) {
            parts.push_back(lower(current_file_, part.type_name, part.type_package, part.is_struct, part.indirections,
                                  false, 0, false));
        }
    } else {
        error(node.loc, "type mismatch");
        return;
    }
    for (std::size_t index = 0; index < node.names.size(); ++index) {
        const Lowered &low = parts[index];
        llvm::Value *field = builder_.CreateExtractValue(value.value, static_cast<unsigned>(index));
        bool create = false;
        llvm::Value *dest = destination(node.names[index], create);
        if (create) {
            llvm::Type *ty = stored_type(low);
            llvm::AllocaInst *slot_alloca = alloca_typed(ty, node.names[index]);
            llvm::Value *stored = field;
            if (stored == nullptr) {
                stored = llvm::Constant::getNullValue(ty);
            } else if (stored->getType() != ty) {
                stored = as_stored(stored, low.depth, ty, low.elem_unsigned);
            }
            builder_.CreateStore(stored, slot_alloca);
            Binding binding;
            binding.value = slot_alloca;
            binding.depth = low.depth;
            binding.elem_char = low.elem_char;
            binding.struct_id = low.struct_id;
            binding.elem_void = low.elem_void;
            binding.elem_bool = low.elem_bool;
            binding.elem_bits = low.elem_bits;
            binding.elem_unsigned = low.elem_unsigned;
            binding.trait_id = low.trait_id;
            scopes_.back()[node.names[index]] = binding;
        } else if (dest != nullptr) {
            llvm::Type *ty = object_type(dest);
            builder_.CreateStore(as_stored(field, low.depth, ty, low.elem_unsigned), dest);
        }
        if (terminated()) {
            return;
        }
    }
}

void CodeGen::emit_stmt(const Stmt &stmt) {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Case>) {
                emit_case(node);
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                const bool share = share_function_scope_;
                share_function_scope_ = false;
                if (!share) {
                    push();
                }
                defers_.push_back(DeferFrame{static_cast<int>(loops_.size()), {}});
                for (const auto &child : node.statements) {
                    if (child) {
                        emit_stmt(*child);
                    }
                }
                if (!terminated() && !defers_.empty()) {
                    emit_defer_frame(defers_.back());
                }
                if (!defers_.empty()) {
                    defers_.pop_back();
                }
                if (!share) {
                    pop();
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    emit_local_function(*node.decl);
                }
            } else if (terminated()) {
                return;
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                if (extern_struct_ret_ && extern_ret_struct_ != nullptr) {
                    llvm::Value *value = nullptr;
                    if (node.value) {
                        const TypedValue got = emit_rvalue(*node.value);
                        value = got.value;
                    }
                    if (value == nullptr || value->getType() != extern_ret_struct_) {
                        value = llvm::ConstantAggregateZero::get(extern_ret_struct_);
                    }
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (terminated()) {
                        return;
                    }
                    if (extern_sret_ && extern_sret_ptr_ != nullptr) {
                        builder_.CreateStore(value, extern_sret_ptr_);
                        builder_.CreateRetVoid();
                        return;
                    }
                    const unsigned align = std::max(extern_ret_abi_.natural_align, 8u);
                    llvm::AllocaInst *slot = aligned_slot(extern_ret_struct_, align, "ret");
                    builder_.CreateStore(value, slot);
                    builder_.CreateRet(pack_return(slot, extern_ret_abi_));
                    return;
                }
                if (return_many_ && return_type_ != nullptr && return_type_->isStructTy()) {
                    std::vector<const Expr *> exprs;
                    if (node.value) {
                        exprs.push_back(node.value.get());
                    }
                    for (const auto &extra : node.extra) {
                        if (extra) {
                            exprs.push_back(extra.get());
                        }
                    }
                    auto *aggregate_type = llvm::cast<llvm::StructType>(return_type_);
                    llvm::Value *aggregate = llvm::ConstantAggregateZero::get(aggregate_type);
                    const unsigned count = aggregate_type->getNumElements();
                    for (unsigned index = 0; index < count; ++index) {
                        llvm::Type *field = aggregate_type->getElementType(index);
                        llvm::Value *piece = llvm::Constant::getNullValue(field);
                        if (index < exprs.size() && exprs[index] != nullptr) {
                            const TypedValue value = emit_rvalue(*exprs[index]);
                            if (value.value != nullptr) {
                                if (field->isPointerTy()) {
                                    piece = value.depth > 0 && value.value->getType()->isPointerTy() ? value.value : null_ptr();
                                } else if (field->isIntegerTy() && value.value->getType()->isIntegerTy()) {
                                    piece = resize_int(value.value, field, value.elem_unsigned);
                                } else if (value.value->getType() == field) {
                                    piece = value.value;
                                }
                            }
                        }
                        if (terminated()) {
                            return;
                        }
                        aggregate = builder_.CreateInsertValue(aggregate, piece, index);
                    }
                    if (!terminated()) {
                        emit_defers_from(0);
                    }
                    if (terminated()) {
                        return;
                    }
                    builder_.CreateRet(aggregate);
                    return;
                }
                llvm::Value *ret = nullptr;
                const bool returns_void = return_type_ != nullptr && return_type_->isVoidTy();
                if (!returns_void && node.value) {
                    const TypedValue value = emit_rvalue(*node.value);
                    ret = value.value;
                    if (return_depth_ > 0 && value.depth == 0 && ret != nullptr && !ret->getType()->isPointerTy()) {
                        ret = null_ptr();
                    } else if (ret != nullptr && return_type_ != nullptr &&
                               (return_type_->isFloatingPointTy() || ret->getType()->isFloatingPointTy())) {
                        ret = as_stored(ret, 0, return_type_, value.elem_unsigned);
                    } else if (ret != nullptr && return_type_ != nullptr && ret->getType()->isIntegerTy() &&
                               return_type_->isIntegerTy() && ret->getType() != return_type_) {
                        ret = resize_int(ret, return_type_, value.elem_unsigned);
                    }
                }
                if (!terminated()) {
                    emit_defers_from(0);
                }
                if (terminated()) {
                    return;
                }
                if (returns_void) {
                    builder_.CreateRetVoid();
                } else if (node.value) {
                    builder_.CreateRet(ret);
                } else if (return_depth_ > 0) {
                    builder_.CreateRet(null_ptr());
                } else if (return_type_ != nullptr && !return_type_->isIntegerTy(32)) {
                    builder_.CreateRet(llvm::Constant::getNullValue(return_type_));
                } else {
                    builder_.CreateRet(zero());
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    emit_rvalue(*node.expr);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                llvm::Value *cond = emit_rvalue(*node.cond).value;
                if (terminated()) {
                    return;
                }
                llvm::BasicBlock *then_block = llvm::BasicBlock::Create(context_, "if.then", function_);
                llvm::BasicBlock *else_block = llvm::BasicBlock::Create(context_, "if.else", function_);
                llvm::BasicBlock *merge_block = llvm::BasicBlock::Create(context_, "if.end", function_);
                builder_.CreateCondBr(is_nonzero(cond), then_block, else_block);
                builder_.SetInsertPoint(then_block);
                if (node.then_arm) {
                    emit_stmt(*node.then_arm);
                }
                if (!terminated()) {
                    builder_.CreateBr(merge_block);
                }
                builder_.SetInsertPoint(else_block);
                if (node.else_arm) {
                    emit_stmt(*node.else_arm);
                }
                if (!terminated()) {
                    builder_.CreateBr(merge_block);
                }
                builder_.SetInsertPoint(merge_block);
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "while.cond", function_);
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "while.body", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "while.end", function_);
                builder_.CreateBr(cond_block);
                builder_.SetInsertPoint(cond_block);
                llvm::Value *cond = emit_rvalue(*node.cond).value;
                if (!terminated()) {
                    builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                }
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{cond_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(end_block);
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "do.body", function_);
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "do.cond", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "do.end", function_);
                builder_.CreateBr(body_block);
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{cond_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(cond_block);
                llvm::Value *cond = node.cond ? emit_rvalue(*node.cond).value : zero();
                if (!terminated()) {
                    builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                }
                builder_.SetInsertPoint(end_block);
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                emit_switch(node);
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                push();
                if (node.init) {
                    emit_stmt(*node.init);
                }
                llvm::BasicBlock *cond_block = llvm::BasicBlock::Create(context_, "for.cond", function_);
                llvm::BasicBlock *body_block = llvm::BasicBlock::Create(context_, "for.body", function_);
                llvm::BasicBlock *step_block = llvm::BasicBlock::Create(context_, "for.step", function_);
                llvm::BasicBlock *end_block = llvm::BasicBlock::Create(context_, "for.end", function_);
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(cond_block);
                if (node.cond) {
                    llvm::Value *cond = emit_rvalue(*node.cond).value;
                    if (!terminated()) {
                        builder_.CreateCondBr(is_nonzero(cond), body_block, end_block);
                    }
                } else if (!terminated()) {
                    builder_.CreateBr(body_block);
                }
                builder_.SetInsertPoint(body_block);
                loops_.push_back(LoopTargets{step_block, end_block});
                if (node.body) {
                    emit_stmt(*node.body);
                }
                loops_.pop_back();
                if (!terminated()) {
                    builder_.CreateBr(step_block);
                }
                builder_.SetInsertPoint(step_block);
                if (node.step && !terminated()) {
                    emit_rvalue(*node.step);
                }
                if (!terminated()) {
                    builder_.CreateBr(cond_block);
                }
                builder_.SetInsertPoint(end_block);
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                if (!loops_.empty()) {
                    emit_defers_from(static_cast<int>(loops_.size()));
                    if (!terminated()) {
                        builder_.CreateBr(loops_.back().after);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                int target = -1;
                for (int index = static_cast<int>(loops_.size()) - 1; index >= 0; --index) {
                    if (!loops_[static_cast<std::size_t>(index)].is_switch) {
                        target = index;
                        break;
                    }
                }
                if (target >= 0 && loops_[static_cast<std::size_t>(target)].step != nullptr) {
                    emit_defers_from(target + 1);
                    if (!terminated()) {
                        builder_.CreateBr(loops_[static_cast<std::size_t>(target)].step);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call == nullptr) {
                    return;
                }
                const auto *call = std::get_if<Expr::Call>(&node.call->data);
                if (call == nullptr) {
                    error(node.loc, "type mismatch");
                    return;
                }
                if (call->callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&call->callee->data)) {
                        if (const Binding *binding = lookup(named->text); binding != nullptr && binding->is_generic) {
                            error(node.loc, "not supported yet");
                            return;
                        }
                    }
                }
                // Arguments and the receiver are evaluated here. The call runs when the block is left.
                const PreparedCall prepared = prepare_call(*call);
                if (prepared.function == nullptr || terminated() || defers_.empty()) {
                    return;
                }
                DeferredCall item;
                item.flag = defer_flag();
                item.function = prepared.function;
                for (llvm::Value *arg : prepared.args) {
                    if (arg == nullptr) {
                        continue;
                    }
                    llvm::AllocaInst *slot = alloca_typed(arg->getType(), "defer.arg");
                    builder_.CreateStore(arg, slot);
                    item.args.push_back(slot);
                }
                builder_.CreateStore(llvm::ConstantInt::getTrue(context_), item.flag);
                defers_.back().calls.push_back(std::move(item));
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                emit_bind(node);
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    if (var.name.empty()) {
                        continue;
                    }
                    const Lowered low = lower_declared(current_file_, var);
                    if (!var.ptr_to_array && (var.is_array || low.is_array)) {
                        if (!low.is_array || low.array_len <= 0) {
                            error(var.loc, "not supported yet");
                            continue;
                        }
                        llvm::Type *ty = stored_type(low);
                        llvm::AllocaInst *slot = alloca_typed(ty, var.name);
                        builder_.CreateStore(llvm::ConstantAggregateZero::get(ty), slot);
                        Binding binding;
                        binding.value = slot;
                        binding.depth = low.depth;
                        binding.elem_char = low.elem_char;
                        binding.is_array = true;
                        binding.array_len = low.array_len;
                        binding.struct_id = low.struct_id;
                        binding.elem_void = low.elem_void;
                        binding.elem_bool = low.elem_bool;
                        binding.elem_bits = low.elem_bits;
                        binding.elem_unsigned = low.elem_unsigned;
                        scopes_.back()[var.name] = binding;
                        continue;
                    }
                    llvm::Type *ty = stored_type(low);
                    llvm::AllocaInst *slot = alloca_typed(ty, var.name);
                    llvm::Value *initial = zero();
                    if (low.depth > 0) {
                        initial = null_ptr();
                    } else if (low.elem_float) {
                        initial = llvm::ConstantFP::get(low.elem_bits >= 64 ? f64_ty() : f32_ty(), 0.0);
                    } else if (low.elem_bool) {
                        initial = llvm::ConstantInt::get(i1(), 0, false);
                    } else if (low.elem_char || low.elem_bits == 8) {
                        initial = llvm::ConstantInt::get(i8(), 0, false);
                    } else if (low.elem_bits == 16) {
                        initial = llvm::ConstantInt::get(i16(), 0, false);
                    } else if (low.elem_bits == 64) {
                        initial = llvm::ConstantInt::get(i64(), 0, false);
                    } else if (low.trait_id >= 0 || low.struct_id >= 0) {
                        initial = llvm::ConstantAggregateZero::get(ty);
                    }
                    builder_.CreateStore(initial, slot);
                    Binding binding;
                    binding.value = slot;
                    binding.depth = low.depth;
                    binding.elem_char = low.elem_char;
                    binding.struct_id = low.struct_id;
                    binding.elem_void = low.elem_void;
                    binding.elem_bool = low.elem_bool;
                    apply_low(binding, low);
                    scopes_.back()[var.name] = binding;
                    if (var.init) {
                        const TypedValue value = emit_rvalue(*var.init);
                        if (!terminated()) {
                            builder_.CreateStore(
                                as_stored(value.value, value.depth, object_type(slot), value.elem_unsigned), slot);
                        }
                    }
                }
            }
        },
        stmt.data);
}

void CodeGen::emit(const PackageGraph &graph) {
    if (graph.nodes.empty()) {
        return;
    }
    graph_ = &graph;
    entry_file_ = graph.nodes.front().file_id;
    prepare_enums();
    prepare_structs();
    prepare_traits();
    for (const PackageNode &node : graph.nodes) {
        declare_file(node);
    }
    for (const PackageNode &node : graph.nodes) {
        current_file_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                apply_initializer(*var);
            }
        }
    }
    for (const PackageNode &node : graph.nodes) {
        emit_bodies(node);
    }
    graph_ = nullptr;
}

} // namespace tlc
