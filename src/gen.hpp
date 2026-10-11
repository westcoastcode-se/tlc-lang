#pragma once

// Internal code generator. The public entry point is codegen() in codegen.hpp.

#include "codegen.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tlc {

inline std::string source_stem(const std::string &path) {
    const auto slash = path.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    return stem;
}

inline std::string function_key(const FunctionDecl &fn) {
    if (fn.method_struct.empty()) {
        return fn.name;
    }
    return fn.method_struct + "." + fn.name;
}

struct Binding {
    llvm::Value *value = nullptr;
    int depth = 0;
    bool is_function = false;
    bool elem_char = false;
    bool is_array = false;
    int array_len = 0;
    // The loaded value is a pointer to an array (`*[4]i32`).
    bool ptr_to_array = false;
    int ptr_array_len = 0;
    int struct_id = -1;
    bool elem_void = false;
    bool elem_bool = false;
    int elem_bits = 0;
    bool elem_unsigned = false;
    int trait_id = -1;
    bool is_generic = false;
    bool is_static = false;
    const FunctionDecl *decl = nullptr;
    bool elem_float = false;
    llvm::FunctionType *fn_ty = nullptr;
    int fn_ret_depth = 0;
    bool fn_ret_float = false;
    int fn_ret_bits = 0;
    bool fn_ret_unsigned = false;
    bool fn_ret_char = false;
    bool fn_ret_bool = false;
    int fn_ret_struct = -1;
    std::vector<FnParam> fn_results{};
    int enum_id = -1;
};

struct TypedValue {
    llvm::Value *value = nullptr;
    int depth = 0;
    bool elem_char = false;
    int struct_id = -1;
    bool is_array = false;
    bool elem_void = false;
    bool elem_bool = false;
    int elem_bits = 0;
    bool elem_unsigned = false;
    int trait_id = -1;
    bool elem_float = false;
    llvm::FunctionType *fn_ty = nullptr;
    int fn_ret_depth = 0;
    bool fn_ret_float = false;
    int fn_ret_bits = 0;
    bool fn_ret_unsigned = false;
    bool fn_ret_char = false;
    bool fn_ret_bool = false;
    int fn_ret_struct = -1;
    std::vector<FnParam> fn_results{};
    int enum_id = -1;
    // Set when this value is a pointer to an array (`*[4]i32`).
    bool ptr_to_array = false;
    int ptr_array_len = 0;
};

// How an extern function passes one struct on x86-64 System V.
// Clang lowers that ABI in the frontend. A plain LLVM struct in the
// signature does not match gcc.
struct CAbi {
    bool memory = false;
    unsigned natural_align = 1;
    unsigned indirect_align = 8;
    llvm::Type *direct = nullptr;
    std::vector<llvm::Type *> parts;
    std::vector<uint64_t> offsets;
};

struct AbiMark {
    unsigned index = 0;
    bool sret = false;
    llvm::Type *type = nullptr;
    unsigned align = 1;
};

struct ExternLayout {
    bool active = false;
    bool sret = false;
    CAbi ret;
    llvm::Type *ret_struct = nullptr;
    int ret_struct_id = -1;
    struct Arg {
        bool is_struct = false;
        CAbi abi;
        llvm::Type *struct_type = nullptr;
        int struct_id = -1;
    };
    std::vector<Arg> args;
    llvm::FunctionType *type = nullptr;
};

class CodeGen {
  public:
    CodeGen(llvm::LLVMContext &context, llvm::Module &module, DiagnosticEngine &diags)
        : context_(context), module_(module), diags_(diags), builder_(context) {}

    void emit(const PackageGraph &graph);
    void enable_tests() { testing_ = true; }

  private:
    struct FileSymbols {
        std::unordered_map<std::string, Binding> functions;
        std::unordered_map<std::string, Binding> globals;
        std::unordered_set<std::string> folded;
    };
    struct SymbolForm {
        std::string name;
        llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
    };

    llvm::LLVMContext &context_;
    llvm::Module &module_;
    DiagnosticEngine &diags_;
    llvm::IRBuilder<> builder_;
    const PackageGraph *graph_ = nullptr;
    llvm::Function *function_ = nullptr;
    llvm::Type *return_type_ = nullptr;
    int return_depth_ = 0;
    bool return_many_ = false;
    bool extern_struct_ret_ = false;
    bool extern_sret_ = false;
    llvm::Value *extern_sret_ptr_ = nullptr;
    CAbi extern_ret_abi_{};
    llvm::Type *extern_ret_struct_ = nullptr;
    bool share_function_scope_ = false;
    // Set while computing the address of an assignment, so writing a variant stores its tag.
    bool arm_union_tag_ = false;
    std::unordered_map<const llvm::Function *, ExternLayout> extern_abis_;
    int local_seq_ = 0;
    uint32_t entry_file_ = 0;
    bool testing_ = false;
    // Set while emitting one test body. `testing.error` reads `test_name_`.
    bool emitting_test_ = false;
    std::string test_name_;
    uint32_t current_file_ = 0;
    std::vector<std::unordered_map<std::string, Binding>> scopes_;
    std::unordered_map<uint32_t, FileSymbols> files_;
    std::unordered_map<std::string, uint32_t> packages_;
    struct LoopTargets {
        llvm::BasicBlock *step = nullptr;
        llvm::BasicBlock *after = nullptr;
        bool is_switch = false;
    };
    struct DeferredCall {
        llvm::AllocaInst *flag = nullptr;
        llvm::Function *function = nullptr;
        std::vector<llvm::AllocaInst *> args;
    };
    struct DeferFrame {
        int loop_index = 0;
        std::vector<DeferredCall> calls;
    };
    struct PreparedCall {
        llvm::Function *function = nullptr;
        std::vector<llvm::Value *> args;
        int depth = 0;
        bool elem_char = false;
        int struct_id = -1;
        bool elem_void = false;
        bool elem_bool = false;
        int elem_bits = 0;
        bool elem_unsigned = false;
        const FunctionDecl *decl = nullptr;
        bool elem_float = false;
        int enum_id = -1;
        llvm::Value *indirect = nullptr;
        llvm::FunctionType *fn_ty = nullptr;
        std::vector<FnParam> fn_results{};
        bool abi_return = false;
        bool abi_sret = false;
        llvm::AllocaInst *abi_slot = nullptr;
        llvm::Type *abi_struct = nullptr;
        int abi_struct_id = -1;
        CAbi abi_ret{};
        std::vector<AbiMark> abi_attrs;
    };
    struct AbiLeaf {
        uint64_t offset = 0;
        uint64_t bytes = 0;
        llvm::Type *type = nullptr;
    };
    struct FieldLayout {
        std::string name;
        llvm::Type *type = nullptr;
        int depth = 0;
        bool elem_char = false;
        bool is_array = false;
        int array_len = 0;
        int struct_id = -1;
        bool elem_void = false;
        bool elem_bool = false;
        int elem_bits = 0;
        bool elem_unsigned = false;
        bool elem_float = false;
    };
    struct StructLayout {
        std::string name;
        llvm::StructType *type = nullptr;
        std::vector<FieldLayout> fields;
        bool ready = false;
        bool is_extern = false;
        bool extern_c = false;
        bool is_union = false;
        // Enum stored in a tlc union. Negative for an `extern(abi="c")` overlay.
        int tag_enum = -1;
    };
    struct Lowered {
        llvm::Type *type = nullptr;
        int depth = 0;
        bool elem_char = false;
        bool is_array = false;
        int array_len = 0;
        bool ptr_to_array = false;
        int ptr_array_len = 0;
        int struct_id = -1;
        bool elem_void = false;
        bool elem_bool = false;
        int elem_bits = 0;
        bool elem_unsigned = false;
        int trait_id = -1;
        bool elem_float = false;
        llvm::FunctionType *fn_ty = nullptr;
        int fn_ret_depth = 0;
        bool fn_ret_float = false;
        int fn_ret_bits = 0;
        bool fn_ret_unsigned = false;
        bool fn_ret_char = false;
        bool fn_ret_bool = false;
        int fn_ret_struct = -1;
        std::vector<FnParam> fn_results{};
        int enum_id = -1;
    };
    struct EnumLayout {
        std::string name;
        int bits = 32;
        bool is_unsigned = false;
        bool is_bool = false;
        bool is_char = false;
        std::unordered_map<std::string, int64_t> values;
    };
    struct Alias {
        int depth = 0;
        bool elem_char = false;
        bool is_array = false;
        int array_len = 0;
        int struct_id = -1;
        bool elem_void = false;
        bool elem_bool = false;
        int elem_bits = 0;
        bool elem_unsigned = false;
        int trait_id = -1;
        bool elem_float = false;
        llvm::FunctionType *fn_ty = nullptr;
        int fn_ret_depth = 0;
        bool fn_ret_float = false;
        int fn_ret_bits = 0;
        bool fn_ret_unsigned = false;
        bool fn_ret_char = false;
        bool fn_ret_bool = false;
        int fn_ret_struct = -1;
        std::vector<FnParam> fn_results{};
        int enum_id = -1;
    };
    struct Shape {
        llvm::Type *type = nullptr;
        int depth = 0;
        bool elem_char = false;
        bool elem_void = false;
        int struct_id = -1;
        bool is_array = false;
        int array_len = 0;
        bool is_function = false;
        bool elem_bool = false;
        int elem_bits = 0;
        bool elem_unsigned = false;
        bool elem_float = false;
    };
    struct CaseArm {
        const Stmt::Case *node = nullptr;
        llvm::BasicBlock *block = nullptr;
        int value = 0;
        bool is_default = false;
        bool has_value = false;
    };

    std::vector<LoopTargets> loops_;
    std::vector<DeferFrame> defers_;
    std::vector<std::unordered_map<const Stmt::Case *, llvm::BasicBlock *>> case_blocks_;
    std::vector<StructLayout> structs_;
    std::vector<EnumLayout> enums_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> enum_ids_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> trait_names_;
    std::unordered_map<std::string, llvm::Function *> specs_;
    std::unordered_set<std::string> emitting_specs_;
    std::vector<int> active_spec_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> tags_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Alias>> aliases_;

    llvm::Type *i32() const { return llvm::Type::getInt32Ty(context_); }
    llvm::Type *i16() const { return llvm::Type::getInt16Ty(context_); }
    llvm::Type *i64() const { return llvm::Type::getInt64Ty(context_); }
    llvm::Type *i8() const { return llvm::Type::getInt8Ty(context_); }
    llvm::Type *i1() const { return llvm::Type::getInt1Ty(context_); }
    llvm::PointerType *ptr_type() const { return llvm::PointerType::get(context_, 0); }
    llvm::Type *mem_type(int depth, bool elem_char = false) const { return leaf_type(depth, elem_char, -1); }
    llvm::Type *f32_ty() const { return llvm::Type::getFloatTy(context_); }
    llvm::Type *f64_ty() const { return llvm::Type::getDoubleTy(context_); }
    llvm::Type *leaf_type(int depth, bool elem_char, int struct_id, bool elem_bool = false, int elem_bits = 0,
                          bool elem_float = false) const {
        if (depth > 0) {
            return ptr_type();
        }
        if (struct_id >= 0 && struct_id < static_cast<int>(structs_.size()) && structs_[static_cast<std::size_t>(struct_id)].type != nullptr) {
            return structs_[static_cast<std::size_t>(struct_id)].type;
        }
        if (elem_float) {
            return elem_bits >= 64 ? f64_ty() : f32_ty();
        }
        if (elem_bool) {
            return i1();
        }
        if (elem_bits == 64) {
            return i64();
        }
        if (elem_bits == 16) {
            return i16();
        }
        if (elem_bits == 8 || elem_char) {
            return i8();
        }
        return i32();
    }
    llvm::Type *bits_type(int bits) const {
        if (bits == 64) {
            return i64();
        }
        if (bits == 16) {
            return i16();
        }
        if (bits == 8) {
            return i8();
        }
        if (bits == 1) {
            return i1();
        }
        return i32();
    }
    llvm::Value *resize_int(llvm::Value *value, llvm::Type *dest, bool source_unsigned) {
        if (value == nullptr || dest == nullptr || !dest->isIntegerTy()) {
            return value == nullptr ? zero() : value;
        }
        if (!value->getType()->isIntegerTy()) {
            return llvm::ConstantInt::get(dest, 0);
        }
        if (value->getType() == dest) {
            return value;
        }
        if (dest->isIntegerTy(1)) {
            return as_bool(value);
        }
        if (value->getType()->getIntegerBitWidth() > dest->getIntegerBitWidth()) {
            return builder_.CreateTrunc(value, dest);
        }
        if (source_unsigned) {
            return builder_.CreateZExt(value, dest);
        }
        return builder_.CreateSExt(value, dest);
    }
    llvm::Value *typed_zero(llvm::Value *value) const {
        if (value != nullptr && value->getType()->isIntOrIntVectorTy()) {
            return llvm::ConstantInt::get(value->getType(), 0);
        }
        return const_i32(0);
    }
    llvm::Value *is_nonzero(llvm::Value *value) {
        if (value == nullptr) {
            return llvm::ConstantInt::getFalse(context_);
        }
        if (value->getType()->isIntegerTy(1)) {
            return value;
        }
        return builder_.CreateICmpNE(value, typed_zero(value));
    }
    int value_bits(const TypedValue &slot) const {
        if (slot.elem_bits >= 64) {
            return 64;
        }
        if (slot.value != nullptr && slot.value->getType()->isIntegerTy(64)) {
            return 64;
        }
        return 32;
    }
    llvm::Value *as_bool(llvm::Value *value) {
        if (value == nullptr) {
            return llvm::ConstantInt::getFalse(context_);
        }
        if (value->getType()->isIntegerTy(1)) {
            return value;
        }
        return builder_.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0));
    }
    llvm::Type *stored_type(const Lowered &low) const {
        if (low.type == nullptr || low.type->isVoidTy()) {
            return i32();
        }
        return low.type;
    }
    llvm::Constant *const_i32(int32_t value) const {
        return llvm::ConstantInt::get(i32(), static_cast<uint64_t>(static_cast<uint32_t>(value)), true);
    }
    llvm::Constant *null_ptr() const { return llvm::ConstantPointerNull::get(ptr_type()); }
    llvm::Value *zero() { return const_i32(0); }
    llvm::Value *as_stored(llvm::Value *value, int from_depth, llvm::Type *slot_type, bool source_unsigned = false) {
        if (value == nullptr) {
            return zero();
        }
        if (slot_type != nullptr && slot_type->isPointerTy() && from_depth == 0) {
            // A function designator is already a pointer. Its depth is the return depth, which is 0 for a scalar.
            if (value->getType()->isPointerTy()) {
                return value;
            }
            return null_ptr();
        }
        if (slot_type != nullptr && slot_type->isFloatingPointTy() && from_depth == 0) {
            if (value->getType()->isFloatingPointTy()) {
                if (value->getType() == slot_type) {
                    return value;
                }
                return value->getType()->getPrimitiveSizeInBits() < slot_type->getPrimitiveSizeInBits()
                           ? builder_.CreateFPExt(value, slot_type)
                           : builder_.CreateFPTrunc(value, slot_type);
            }
            if (value->getType()->isIntegerTy()) {
                return source_unsigned ? builder_.CreateUIToFP(value, slot_type) : builder_.CreateSIToFP(value, slot_type);
            }
            return llvm::ConstantFP::get(slot_type, 0.0);
        }
        if (slot_type != nullptr && slot_type->isIntegerTy() && from_depth == 0 && value->getType()->isFloatingPointTy()) {
            return builder_.CreateFPToSI(value, slot_type);
        }
        if (slot_type != nullptr && slot_type->isIntegerTy() && from_depth == 0 && value->getType()->isIntegerTy() &&
            value->getType() != slot_type) {
            return resize_int(value, slot_type, source_unsigned);
        }
        return value;
    }
    llvm::Type *slot_int_type(const TypedValue &slot) const {
        if (slot.depth != 0 || slot.struct_id >= 0 || slot.elem_void) {
            return nullptr;
        }
        if (slot.elem_float) {
            return slot.elem_bits >= 64 ? f64_ty() : f32_ty();
        }
        if (slot.elem_bool) {
            return i1();
        }
        if (slot.elem_char || slot.elem_bits == 8) {
            return i8();
        }
        if (slot.elem_bits == 16) {
            return i16();
        }
        if (slot.elem_bits == 32) {
            return i32();
        }
        if (slot.elem_bits == 64) {
            return i64();
        }
        return nullptr;
    }
    llvm::Value *stored_value(const TypedValue &rhs, const TypedValue &slot) {
        if (rhs.value == nullptr) {
            return zero();
        }
        if (slot.fn_ty != nullptr) {
            return rhs.value;
        }
        if (slot.elem_float && slot.depth == 0) {
            return as_stored(rhs.value, rhs.depth, slot.elem_bits >= 64 ? f64_ty() : f32_ty(), rhs.elem_unsigned);
        }
        if (slot.depth > 0 && rhs.depth == 0) {
            return null_ptr();
        }
        if (llvm::Type *slot_type = slot_int_type(slot); slot_type != nullptr && rhs.value->getType()->isIntegerTy()) {
            return resize_int(rhs.value, slot_type, rhs.elem_unsigned);
        }
        return rhs.value;
    }
    TypedValue load_object(llvm::Value *addr, int depth, bool elem_char, int struct_id = -1, bool elem_void = false,
                           bool elem_bool = false, int elem_bits = 0, bool elem_unsigned = false, bool elem_float = false) {
        llvm::Value *loaded =
            builder_.CreateLoad(leaf_type(depth, elem_char, struct_id, elem_bool, elem_bits, elem_float), addr);
        if (depth != 0) {
            return {loaded, depth, elem_char, struct_id, false, elem_void, elem_bool, elem_bits, elem_unsigned, -1,
                    elem_float};
        }
        if (struct_id >= 0) {
            return {loaded, 0, false, struct_id};
        }
        if (elem_bool) {
            return {builder_.CreateZExt(loaded, i32()), 0};
        }
        if (elem_char || elem_bits == 8 || elem_bits == 16) {
            const bool zero_extend = elem_unsigned || elem_char;
            llvm::Value *wide = zero_extend ? builder_.CreateZExt(loaded, i32()) : builder_.CreateSExt(loaded, i32());
            return {wide, 0};
        }
        if (elem_float) {
            return {loaded, 0, false, -1, false, false, false, elem_bits >= 64 ? 64 : 32, false, -1, true};
        }
        if (elem_bits == 64) {
            return {loaded, 0, false, -1, false, false, false, 64, elem_unsigned};
        }
        if (elem_unsigned) {
            return {loaded, 0, false, -1, false, false, false, 32, true};
        }
        return {loaded, 0};
    }
    TypedValue value_of_binding(const Binding &binding) {
        if (binding.is_function) {
            TypedValue value{binding.value, binding.depth, binding.elem_char, binding.struct_id, false, binding.elem_void,
                             binding.elem_bool, binding.elem_bits, binding.elem_unsigned, -1, binding.elem_float};
            value.enum_id = binding.enum_id;
            return value;
        }
        if (binding.is_array) {
            return {binding.value, binding.depth + 1, binding.elem_char, binding.struct_id, false, binding.elem_void,
                    binding.elem_bool, binding.elem_bits, binding.elem_unsigned, -1, binding.elem_float};
        }
        TypedValue loaded = load_object(binding.value, binding.depth, binding.elem_char, binding.struct_id, binding.elem_void,
                                        binding.elem_bool, binding.elem_bits, binding.elem_unsigned, binding.elem_float);
        loaded.ptr_to_array = binding.ptr_to_array;
        loaded.ptr_array_len = binding.ptr_array_len;
        loaded.fn_ty = binding.fn_ty;
        loaded.fn_ret_depth = binding.fn_ret_depth;
        loaded.fn_ret_float = binding.fn_ret_float;
        loaded.fn_ret_bits = binding.fn_ret_bits;
        loaded.fn_ret_unsigned = binding.fn_ret_unsigned;
        loaded.fn_ret_char = binding.fn_ret_char;
        loaded.fn_ret_bool = binding.fn_ret_bool;
        loaded.fn_ret_struct = binding.fn_ret_struct;
        loaded.fn_results = binding.fn_results;
        loaded.enum_id = binding.enum_id;
        return loaded;
    }
    llvm::Type *object_type(llvm::Value *addr) const {
        if (const auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
            return alloca->getAllocatedType();
        }
        if (const auto *global = llvm::dyn_cast<llvm::GlobalVariable>(addr)) {
            return global->getValueType();
        }
        return nullptr;
    }

    void error(SourceLoc loc, const std::string &message) { diags_.error(loc, message); }
    bool terminated() const {
        const llvm::BasicBlock *block = builder_.GetInsertBlock();
        return block != nullptr && block->hasTerminator();
    }
    void push() { scopes_.emplace_back(); }
    void pop() { scopes_.pop_back(); }
    const Binding *lookup(const std::string &name) const;
    llvm::AllocaInst *alloca_typed(llvm::Type *type, const std::string &name);
    llvm::FunctionType *function_type(const FunctionDecl &fn, uint32_t file) const;
    bool is_struct_value(const Lowered &low) const;
    CAbi c_abi(llvm::StructType *type) const;
    CAbi c_abi_union(int struct_id) const;
    CAbi classify_abi(uint64_t size, unsigned natural, const std::vector<AbiLeaf> &leaves) const;
    void collect_abi_leaves(llvm::Type *type, uint64_t offset, std::vector<AbiLeaf> &leaves) const;
    ExternLayout extern_layout(const FunctionDecl &fn, uint32_t file) const;
    void apply_extern_attrs(llvm::Function *function, const ExternLayout &layout) const;
    void annotate_call(llvm::CallInst *inst, const std::vector<AbiMark> &marks) const;
    llvm::AllocaInst *aligned_slot(llvm::Type *type, unsigned align, const std::string &name);
    llvm::Value *addr_at(llvm::Value *base, uint64_t offset);
    llvm::Value *load_at(llvm::Value *base, llvm::Type *type, uint64_t offset);
    void store_at(llvm::Value *base, llvm::Value *value, uint64_t offset);
    llvm::Value *pack_return(llvm::Value *slot, const CAbi &abi);
    void unpack_return(llvm::Value *returned, llvm::Value *slot, const CAbi &abi);
    llvm::Type *aggregate_return(const FunctionDecl &fn, uint32_t file) const;
    const FunctionDecl *callee_decl(const Expr &callee) const;
    SymbolForm symbol_form(const PackageNode &node, const std::string &name, bool is_extern, bool extern_c,
                           bool is_static, bool exported, bool entry_main) const;
    const PackageNode *find_node(uint32_t file_id) const;
    std::vector<uint32_t> package_files(uint32_t file) const;
    const Binding *visible(uint32_t file, const std::string &name, bool function) const;
    void bind_package_globals();
    int tag_id(uint32_t file, const std::string &name) const;
    uint32_t package_file(uint32_t file, const std::string &package) const;
    int selective_tag(uint32_t file, const std::string &name) const;
    const Alias *find_alias(uint32_t file, const std::string &name) const;
    const Alias *selective_alias(uint32_t file, const std::string &name) const;
    bool has_typedef(uint32_t file, const std::string &name) const;
    bool alias_base_ready(uint32_t file, const TypedefDecl &item) const;
    Lowered lower(uint32_t file, const std::string &name, const std::string &package, bool is_struct, int indirections,
                  bool is_array, int array_len, bool parameter) const;
    Lowered lower_declared(uint32_t file, const VarDecl &var) const;
    Lowered lower_fnptr(uint32_t file, const std::string &ret_name, const std::string &ret_package, bool ret_struct,
                        bool ret_enum, int ret_indirections, int fn_depth, const std::vector<FnParam> &params,
                        bool variadic, const std::vector<FnParam> &results) const;
    Lowered lower_param_type(uint32_t file, const Param &param) const;
    bool infer_global(uint32_t file, const Expr &expr, Lowered &out) const;
    void apply_low(Binding &binding, const Lowered &low) const;
    const FieldLayout *find_field(int struct_id, const std::string &name, int &index) const;
    TypedValue field_addr(int struct_id, llvm::Value *pointer, const std::string &field, SourceLoc loc);
    void store_union_tag(int struct_id, llvm::Value *pointer, const std::string &field);
    llvm::Type *enum_mem_type(int enum_id) const;
    bool write_constant_bytes(llvm::Constant *value, uint64_t offset, std::vector<uint8_t> &bytes) const;
    bool addressable(const Expr &expr) const;
    void prepare_structs();
    void prepare_traits();
    int trait_named(uint32_t file, const std::string &name) const;
    bool param_is_trait(uint32_t file, const Param &param) const;
    bool function_is_generic(uint32_t file, const FunctionDecl &fn) const;
    Lowered lower_param(const FunctionDecl &fn, std::size_t index) const;
    llvm::FunctionType *spec_type(const FunctionDecl &fn, const std::vector<int> &spec) const;
    void emit_function_body(const FunctionDecl &fn, llvm::Function *function, int return_depth);
    TypedValue emit_generic_call(const Binding &binding, const Expr::Call &node);
    void prepare_aliases();
    void prepare_enums();
    int enum_named(uint32_t file, const std::string &name) const;
    int selective_enum(uint32_t file, const std::string &name) const;
    int enum_type_designator(const Expr &expr) const;
    int enum_value_id(const Expr &expr) const;
    llvm::Constant *enum_constant(int enum_id, int64_t value) const;
    void describe_enum(const std::string &underlying, EnumLayout &layout) const;
    void note_enum(Alias &base, int id) const;
    void declare_file(const PackageNode &node);
    std::optional<int32_t> fold_global(uint32_t file, const std::string &name, bool allow_local_static = true) const;
    void bind_imports(const PackageNode &node);
    void emit_bodies(const PackageNode &node);
    void emit_tests();
    bool is_testing_error_call(const Expr::Call &node) const;
    TypedValue emit_testing_error(const Expr::Call &node);
    void apply_initializer(const VarDecl &var);
    llvm::Constant *const_struct(const Expr::Init &init, int struct_id);
    llvm::Constant *const_field(const Expr &expr, const FieldLayout &slot);
    std::optional<int32_t> fold(const Expr &expr) const;
    void emit_function(const FunctionDecl &fn);
    void emit_local_function(const FunctionDecl &fn);
    void emit_nested_body(const FunctionDecl &fn, llvm::Function *function, int depth);
    TypedValue emit_anon_function(const FunctionDecl &fn);
    PreparedCall prepare_call(const Expr::Call &node);
    TypedValue emit_prepared(const PreparedCall &call);
    llvm::AllocaInst *defer_flag();
    void emit_one_defer(const DeferredCall &call);
    void emit_defer_frame(DeferFrame &frame);
    void emit_defers_from(int min_loop_index);
    void finish_function();
    void emit_stmt(const Stmt &stmt);
    void emit_bind(const Stmt::Bind &node);
    void collect_cases(const Stmt &stmt, std::vector<CaseArm> &arms) const;
    void emit_case(const Stmt::Case &node);
    void emit_switch(const Stmt::Switch &node);
    TypedValue emit_rvalue(const Expr &expr);
    TypedValue emit_init(const Expr::Init &init);
    TypedValue emit_addr(const Expr &expr);
    llvm::Type *shape_type(const Shape &shape) const;
    Shape decay_shape(Shape shape) const;
    Shape shape_from_binding(const Binding &binding) const;
    Shape raw_shape(const Expr &expr) const;
    llvm::Value *emit_short_circuit(const Expr::Binary &expr, bool is_and);
};

} // namespace tlc
