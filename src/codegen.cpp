#include "codegen.hpp"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

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
namespace {

std::string source_stem(const std::string &path) {
    const auto slash = path.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    const auto dot = stem.rfind('.');
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    return stem;
}

struct TargetState {
    llvm::Triple triple;
    std::unique_ptr<llvm::TargetMachine> machine;
    std::string error;
};

TargetState prepare_target() {
    TargetState state;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    std::string lookup_error;
    state.triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(state.triple, lookup_error);
    if (!target) {
        state.error = lookup_error;
        return state;
    }
    llvm::TargetOptions options;
    state.machine.reset(target->createTargetMachine(
        state.triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::None));
    if (!state.machine) {
        state.error = "cannot create the host target machine";
    }
    return state;
}

struct Binding {
    llvm::Value *value = nullptr;
    int depth = 0;
    bool is_function = false;
    bool elem_char = false;
    bool is_array = false;
    int array_len = 0;
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
    std::unordered_map<const llvm::Function *, ExternLayout> extern_abis_;
    int local_seq_ = 0;
    uint32_t entry_file_ = 0;
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
    };
    struct Lowered {
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
    SymbolForm symbol_form(const PackageNode &node, const std::string &name, bool is_extern, bool is_static,
                           bool exported, bool entry_main) const;
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
    Lowered lower_fnptr(uint32_t file, const std::string &ret_name, const std::string &ret_package, bool ret_struct,
                        bool ret_enum, int ret_indirections, int fn_depth, const std::vector<FnParam> &params,
                        bool variadic, const std::vector<FnParam> &results) const;
    Lowered lower_param_type(uint32_t file, const Param &param) const;
    void apply_low(Binding &binding, const Lowered &low) const;
    const FieldLayout *find_field(int struct_id, const std::string &name, int &index) const;
    TypedValue field_addr(int struct_id, llvm::Value *pointer, const std::string &field, SourceLoc loc);
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
    void apply_initializer(const VarDecl &var);
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
    TypedValue emit_addr(const Expr &expr);
    llvm::Type *shape_type(const Shape &shape) const;
    Shape decay_shape(Shape shape) const;
    Shape shape_from_binding(const Binding &binding) const;
    Shape raw_shape(const Expr &expr) const;
    llvm::Value *emit_short_circuit(const Expr::Binary &expr, bool is_and);
};

const Binding *CodeGen::lookup(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    const auto file = files_.find(current_file_);
    if (file != files_.end()) {
        const auto function = file->second.functions.find(name);
        if (function != file->second.functions.end()) {
            return &function->second;
        }
    }
    for (const uint32_t id : package_files(current_file_)) {
        if (id == current_file_) {
            continue;
        }
        const auto other = files_.find(id);
        if (other == files_.end()) {
            continue;
        }
        const auto sibling = other->second.functions.find(name);
        if (sibling == other->second.functions.end() || sibling->second.is_static) {
            continue;
        }
        return &sibling->second;
    }
    return nullptr;
}

std::vector<uint32_t> CodeGen::package_files(uint32_t file) const {
    std::vector<uint32_t> ids;
    ids.push_back(file);
    const PackageNode *self = find_node(file);
    if (self == nullptr || self->package_key.empty() || graph_ == nullptr) {
        return ids;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.package_key == self->package_key && node.file_id != file) {
            ids.push_back(node.file_id);
        }
    }
    return ids;
}

const Binding *CodeGen::visible(uint32_t file, const std::string &name, bool function) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = files_.find(id);
        if (found == files_.end()) {
            continue;
        }
        const auto &table = function ? found->second.functions : found->second.globals;
        const auto it = table.find(name);
        if (it == table.end() || it->second.is_static) {
            continue;
        }
        return &it->second;
    }
    return nullptr;
}

void CodeGen::bind_package_globals() {
    for (const uint32_t id : package_files(current_file_)) {
        if (id == current_file_) {
            continue;
        }
        const auto found = files_.find(id);
        if (found == files_.end()) {
            continue;
        }
        for (const auto &[name, global] : found->second.globals) {
            if (global.is_static) {
                continue;
            }
            scopes_.back()[name] = global;
        }
    }
    const auto self = files_.find(current_file_);
    if (self == files_.end()) {
        return;
    }
    for (const auto &[name, global] : self->second.globals) {
        scopes_.back()[name] = global;
    }
}

llvm::AllocaInst *CodeGen::alloca_typed(llvm::Type *type, const std::string &name) {
    llvm::IRBuilder<> allocas(&function_->getEntryBlock(), function_->getEntryBlock().begin());
    return allocas.CreateAlloca(type, nullptr, name);
}

bool CodeGen::is_struct_value(const Lowered &low) const {
    return low.struct_id >= 0 && low.depth == 0 && !low.is_array && low.type != nullptr && low.type->isStructTy();
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
    abi.natural_align = natural == 0 ? 1 : natural;
    abi.indirect_align = std::max(abi.natural_align, 8u);
    if (size == 0 || size > 16) {
        abi.memory = true;
        return abi;
    }

    struct Leaf {
        uint64_t offset = 0;
        uint64_t bytes = 0;
        llvm::Type *type = nullptr;
    };
    std::vector<Leaf> leaves;
    auto append = [&](auto &&self, llvm::Type *ty, uint64_t offset) -> void {
        if (auto *aggregate = llvm::dyn_cast<llvm::StructType>(ty)) {
            const llvm::StructLayout *slots = layout.getStructLayout(aggregate);
            for (unsigned index = 0; index < aggregate->getNumElements(); ++index) {
                self(self, aggregate->getElementType(index), offset + slots->getElementOffset(index));
            }
            return;
        }
        if (auto *array = llvm::dyn_cast<llvm::ArrayType>(ty)) {
            const uint64_t step = layout.getTypeAllocSize(array->getElementType());
            for (uint64_t index = 0; index < array->getNumElements(); ++index) {
                self(self, array->getElementType(), offset + index * step);
            }
            return;
        }
        Leaf leaf;
        leaf.offset = offset;
        leaf.bytes = ty->isIntegerTy(1) ? 1 : layout.getTypeAllocSize(ty);
        leaf.type = ty;
        leaves.push_back(leaf);
    };
    append(append, type, 0);

    auto aligned = [&](const Leaf &leaf) {
        unsigned align = 1;
        if (leaf.type->isIntegerTy(1)) {
            align = 1;
        } else {
            align = static_cast<unsigned>(layout.getABITypeAlign(leaf.type).value());
        }
        return align == 0 || leaf.offset % align == 0;
    };
    for (const Leaf &leaf : leaves) {
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
        for (const Leaf &leaf : leaves) {
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
    for (const Leaf &leaf : leaves) {
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
        const Leaf *field = nullptr;
        for (const Leaf &leaf : leaves) {
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
        for (const Leaf &leaf : leaves) {
            if (leaf.offset == offset && leaf.type->isFloatingPointTy()) {
                return leaf.type;
            }
        }
        return nullptr;
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
    if (!fn.is_extern) {
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
        layout.args[index].abi = c_abi(llvm::cast<llvm::StructType>(low.type));
    }
    if (!any) {
        return layout;
    }
    layout.active = true;
    if (ret_struct) {
        layout.ret_struct = ret.type;
        layout.ret_struct_id = ret.struct_id;
        layout.ret = c_abi(llvm::cast<llvm::StructType>(ret.type));
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

llvm::Type *CodeGen::aggregate_return(const FunctionDecl &fn, uint32_t file) const {
    std::vector<llvm::Type *> fields;
    fields.reserve(fn.results.size());
    for (const ResultSlot &slot : fn.results) {
        fields.push_back(stored_type(
            lower(file, slot.type_name, slot.type_package, slot.is_struct, slot.indirections, false, 0, false)));
    }
    if (fields.empty()) {
        fields.push_back(i32());
    }
    return llvm::StructType::get(context_, fields);
}

llvm::FunctionType *CodeGen::function_type(const FunctionDecl &fn, uint32_t file) const {
    const bool many = fn.results.size() > 1;
    const bool returns_void =
        !many && fn.return_type == "void" && fn.indirections == 0 && !fn.is_struct && fn.type_package.empty();
    llvm::Type *ret = many ? aggregate_return(fn, file)
                           : (returns_void ? llvm::Type::getVoidTy(context_)
                                           : stored_type(fn.results.size() == 1 && fn.results.front().is_fn_ptr
                                                            ? lower_fnptr(file, fn.results.front().type_name,
                                                                          fn.results.front().type_package,
                                                                          fn.results.front().is_struct,
                                                                          fn.results.front().is_enum,
                                                                          fn.results.front().ret_indirections,
                                                                          fn.results.front().indirections,
                                                                          fn.results.front().fn_params,
                                                                          fn.results.front().fn_variadic,
                                                                          fn.results.front().fn_results)
                                                            : lower(file, fn.return_type, fn.type_package, fn.is_struct,
                                                                    fn.indirections, false, 0, false)));
    std::vector<llvm::Type *> params;
    params.reserve(fn.params.size());
    for (const Param &param : fn.params) {
        params.push_back(stored_type(lower_param_type(file, param)));
    }
    return llvm::FunctionType::get(ret, params, fn.variadic);
}

int CodeGen::tag_id(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = tags_.find(id);
        if (found == tags_.end()) {
            continue;
        }
        const auto tag = found->second.find(name);
        if (tag != found->second.end()) {
            return tag->second;
        }
    }
    return -1;
}

uint32_t CodeGen::package_file(uint32_t file, const std::string &package) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return 0;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form == ImportDecl::Form::Selective) {
            continue;
        }
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (binding == package) {
            return target->file_id;
        }
    }
    return 0;
}

int CodeGen::selective_tag(uint32_t file, const std::string &name) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return -1;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form != ImportDecl::Form::Selective) {
            continue;
        }
        bool wanted = false;
        for (const std::string &item : import.names) {
            if (item == name) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        const int id = tag_id(target->file_id, name);
        if (id >= 0) {
            return id;
        }
    }
    return -1;
}

const CodeGen::Alias *CodeGen::find_alias(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = aliases_.find(id);
        if (found == aliases_.end()) {
            continue;
        }
        const auto alias = found->second.find(name);
        if (alias != found->second.end()) {
            return &alias->second;
        }
    }
    return nullptr;
}

const CodeGen::Alias *CodeGen::selective_alias(uint32_t file, const std::string &name) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return nullptr;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form != ImportDecl::Form::Selective) {
            continue;
        }
        bool wanted = false;
        for (const std::string &item : import.names) {
            if (item == name) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        const PackageNode *target = nullptr;
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file == file && edge.spec == import.path && edge.loc.file == import.loc.file &&
                edge.loc.offset == import.loc.offset) {
                target = find_node(edge.to_file);
                break;
            }
        }
        if (target == nullptr) {
            continue;
        }
        if (const Alias *alias = find_alias(target->file_id, name)) {
            return alias;
        }
    }
    return nullptr;
}

bool CodeGen::has_typedef(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const PackageNode *node = find_node(id);
        if (node == nullptr) {
            continue;
        }
        for (const TopDecl &decl : node->unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item != nullptr && item->name == name) {
                return true;
            }
        }
    }
    return false;
}

bool CodeGen::alias_base_ready(uint32_t file, const TypedefDecl &item) const {
    if (item.is_struct || item.is_enum || item.type_name.empty() || item.type_name == "int" || item.type_name == "char" ||
        item.type_name == "signed char" || item.type_name == "unsigned char" || item.type_name == "short" ||
        item.type_name == "unsigned short" || item.type_name == "unsigned" || item.type_name == "unsigned int" ||
        item.type_name == "long" || item.type_name == "unsigned long" || item.type_name == "long long" ||
        item.type_name == "unsigned long long" || item.type_name == "void" || item.type_name == "bool" ||
        item.type_name == "invalid" || item.type_name == "i8" || item.type_name == "i16" || item.type_name == "i32" ||
        item.type_name == "i64" || item.type_name == "u8" || item.type_name == "u16" || item.type_name == "u32" ||
        item.type_name == "u64" || item.type_name == "f32" || item.type_name == "f64" || item.type_name == "float" ||
        item.type_name == "double") {
        return true;
    }
    uint32_t target = file;
    if (!item.type_package.empty()) {
        target = package_file(file, item.type_package);
        if (target == 0) {
            return true;
        }
    }
    if (find_alias(target, item.type_name) != nullptr) {
        return true;
    }
    // `typedef Point Point` names the struct tag. The typedef is not waiting on itself.
    if (has_typedef(target, item.type_name) && !(item.type_package.empty() && item.name == item.type_name)) {
        return false;
    }
    if (item.type_package.empty()) {
        const PackageNode *node = find_node(file);
        if (node != nullptr && graph_ != nullptr) {
            for (const ImportDecl &import : node->unit.imports) {
                if (import.form != ImportDecl::Form::Selective) {
                    continue;
                }
                bool wanted = false;
                for (const std::string &imported : import.names) {
                    if (imported == item.type_name) {
                        wanted = true;
                    }
                }
                if (!wanted) {
                    continue;
                }
                for (const ImportEdge &edge : graph_->edges) {
                    if (edge.from_file != file || edge.spec != import.path || edge.loc.file != import.loc.file ||
                        edge.loc.offset != import.loc.offset) {
                        continue;
                    }
                    if (has_typedef(edge.to_file, item.type_name) && find_alias(edge.to_file, item.type_name) == nullptr) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

CodeGen::Lowered CodeGen::lower(uint32_t file, const std::string &name, const std::string &package, bool is_struct,
                                 int indirections, bool is_array, int array_len, bool parameter) const {
    Lowered out;
    Alias base;
    bool have = false;
    auto take = [&](const Alias &alias) {
        base = alias;
        have = true;
    };
    if (!package.empty()) {
        const uint32_t target = package_file(file, package);
        const int tag = target == 0 ? -1 : tag_id(target, name);
        const Alias *remote = target == 0 ? nullptr : find_alias(target, name);
        if (remote != nullptr && !is_struct) {
            take(*remote);
        } else if (tag >= 0) {
            base.struct_id = tag;
            have = true;
        } else if (const int eid = target == 0 ? -1 : enum_named(target, name); eid >= 0) {
            note_enum(base, eid);
            have = true;
        }
    } else if (is_struct) {
        int sid = tag_id(file, name);
        if (sid < 0) {
            sid = selective_tag(file, name);
        }
        if (sid >= 0) {
            base.struct_id = sid;
            have = true;
        } else if (const int eid = enum_named(file, name); eid >= 0) {
            note_enum(base, eid);
            have = true;
        } else if (const int imported = selective_enum(file, name); imported >= 0) {
            note_enum(base, imported);
            have = true;
        }
    } else if (name == "char" || name == "signed char" || name == "unsigned char" || name == "short" ||
               name == "unsigned short" || name == "int" || name == "unsigned" || name == "unsigned int" || name == "long" ||
               name == "unsigned long" || name == "long long" || name == "unsigned long long" || name == "i8" ||
               name == "i16" || name == "i32" || name == "i64" || name == "u8" || name == "u16" || name == "u32" ||
               name == "u64") {
        base.elem_char = name == "char";
        base.elem_unsigned = name.starts_with("unsigned") || name.starts_with("u");
        if (name == "char" || name == "signed char" || name == "unsigned char" || name == "i8" || name == "u8") {
            base.elem_bits = 8;
        } else if (name == "short" || name == "unsigned short" || name == "i16" || name == "u16") {
            base.elem_bits = 16;
        } else if (name == "long" || name == "unsigned long" || name == "long long" || name == "unsigned long long" ||
                   name == "i64" || name == "u64") {
            base.elem_bits = 64;
        } else {
            base.elem_bits = 32;
        }
        have = true;
    } else if (name == "float" || name == "f32") {
        base.elem_float = true;
        base.elem_bits = 32;
        have = true;
    } else if (name == "double" || name == "f64") {
        base.elem_float = true;
        base.elem_bits = 64;
        have = true;
    } else if (name == "void") {
        base.elem_void = true;
        have = true;
    } else if (name == "bool") {
        base.elem_bool = true;
        have = true;
    } else if (name != "int" && name != "invalid" && !name.empty()) {
        if (const Alias *local = find_alias(file, name)) {
            take(*local);
        } else if (const Alias *imported = selective_alias(file, name)) {
            take(*imported);
        } else {
            int sid = tag_id(file, name);
            if (sid < 0) {
                sid = selective_tag(file, name);
            }
            if (sid >= 0) {
                base.struct_id = sid;
                have = true;
            } else if (const int eid = enum_named(file, name); eid >= 0) {
                note_enum(base, eid);
                have = true;
            } else if (const int imported = selective_enum(file, name); imported >= 0) {
                note_enum(base, imported);
                have = true;
            } else if (const int tid = trait_named(file, name); tid >= 0) {
                base.trait_id = tid;
                have = true;
            }
        }
    }
    const int base_depth = have ? base.depth : 0;
    const bool elem_char = have && base.elem_char;
    const bool elem_void = have && base.elem_void;
    const bool elem_bool = have && base.elem_bool;
    const int elem_bits = have ? base.elem_bits : 0;
    const bool elem_unsigned = have && base.elem_unsigned;
    const bool elem_float = have && base.elem_float;
    const int sid = have ? base.struct_id : -1;
    const bool base_array = have && base.is_array;
    if (base_array && (indirections > 0 || is_array)) {
        out.type = i32();
        return out;
    }
    if (parameter && (is_array || base_array)) {
        out.depth = base_depth + indirections + 1;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = ptr_type();
        return out;
    }
    if (is_array && array_len > 0) {
        out.is_array = true;
        out.array_len = array_len;
        out.depth = base_depth + indirections;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = llvm::ArrayType::get(leaf_type(out.depth, elem_char, sid, elem_bool, elem_bits, elem_float),
                                        static_cast<uint64_t>(array_len));
        return out;
    }
    if (base_array && base.array_len > 0) {
        out.is_array = true;
        out.array_len = base.array_len;
        out.depth = base_depth;
        out.elem_char = elem_char;
        out.elem_void = elem_void;
        out.elem_bool = elem_bool;
        out.elem_bits = elem_bits;
        out.elem_unsigned = elem_unsigned;
        out.elem_float = elem_float;
        out.struct_id = sid;
        out.enum_id = have ? base.enum_id : -1;
        out.type = llvm::ArrayType::get(leaf_type(base_depth, elem_char, sid, elem_bool, elem_bits, elem_float),
                                        static_cast<uint64_t>(base.array_len));
        return out;
    }
    out.depth = base_depth + indirections;
    out.elem_char = elem_char;
    out.elem_void = elem_void;
    out.elem_bool = elem_bool;
    out.elem_bits = elem_bits;
    out.elem_unsigned = elem_unsigned;
    out.elem_float = elem_float;
    out.struct_id = sid;
    out.enum_id = have ? base.enum_id : -1;
    if (indirections == 0 && !is_array) {
        out.fn_ty = have ? base.fn_ty : nullptr;
        out.fn_ret_depth = have ? base.fn_ret_depth : 0;
        out.fn_ret_float = have && base.fn_ret_float;
        out.fn_ret_bits = have ? base.fn_ret_bits : 0;
        out.fn_ret_unsigned = have && base.fn_ret_unsigned;
        out.fn_ret_char = have && base.fn_ret_char;
        out.fn_ret_bool = have && base.fn_ret_bool;
        out.fn_ret_struct = have ? base.fn_ret_struct : -1;
        out.fn_results = have ? base.fn_results : std::vector<FnParam>{};
    }
    out.type = leaf_type(out.depth, elem_char, sid, elem_bool, elem_bits, elem_float);
    return out;
}

CodeGen::Lowered CodeGen::lower_fnptr(uint32_t file, const std::string &ret_name, const std::string &ret_package,
                                      bool ret_struct, bool ret_enum, int ret_indirections, int fn_depth,
                                      const std::vector<FnParam> &params, bool variadic,
                                      const std::vector<FnParam> &results) const {
    (void)ret_enum;
    std::vector<llvm::Type *> args;
    args.reserve(params.size());
    for (const FnParam &param : params) {
        const Lowered arg = param.is_fn_ptr
                                ? lower_fnptr(file, param.type_name, param.type_package, param.is_struct, param.is_enum,
                                              param.ret_indirections, param.indirections, param.params, param.variadic,
                                              param.results)
                                : lower(file, param.type_name, param.type_package, param.is_struct, param.indirections, false,
                                        0, true);
        args.push_back(stored_type(arg));
    }
    Lowered out;
    out.depth = fn_depth < 1 ? 1 : fn_depth;
    out.type = ptr_type();
    if (results.size() > 1) {
        std::vector<llvm::Type *> fields;
        fields.reserve(results.size());
        for (const FnParam &part : results) {
            fields.push_back(stored_type(
                lower(file, part.type_name, part.type_package, part.is_struct, part.indirections, false, 0, false)));
        }
        out.fn_ty = llvm::FunctionType::get(llvm::StructType::get(context_, fields), args, variadic);
        out.fn_results = results;
        return out;
    }
    const Lowered ret = lower(file, ret_name, ret_package, ret_struct, ret_indirections, false, 0, false);
    llvm::Type *ret_ty = ret.depth == 0 && ret.elem_void ? llvm::Type::getVoidTy(context_) : stored_type(ret);
    out.fn_ty = llvm::FunctionType::get(ret_ty, args, variadic);
    out.fn_ret_depth = ret.depth;
    out.fn_ret_float = ret.elem_float;
    out.fn_ret_bits = ret.elem_bits;
    out.fn_ret_unsigned = ret.elem_unsigned;
    out.fn_ret_char = ret.elem_char;
    out.fn_ret_bool = ret.elem_bool;
    out.fn_ret_struct = ret.struct_id;
    return out;
}

CodeGen::Lowered CodeGen::lower_param_type(uint32_t file, const Param &param) const {
    if (param.is_fn_ptr) {
        return lower_fnptr(file, param.type_name, param.type_package, param.is_struct, param.is_enum, param.ret_indirections,
                           param.indirections, param.fn_params, param.fn_variadic, param.fn_results);
    }
    return lower(file, param.type_name, param.type_package, param.is_struct, param.indirections, param.is_array,
                 param.array_len, true);
}

void CodeGen::apply_low(Binding &binding, const Lowered &low) const {
    binding.depth = low.depth;
    binding.elem_char = low.elem_char;
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
}

const CodeGen::FieldLayout *CodeGen::find_field(int struct_id, const std::string &name, int &index) const {
    index = -1;
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    const std::vector<FieldLayout> &fields = structs_[static_cast<std::size_t>(struct_id)].fields;
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        if (fields[static_cast<std::size_t>(i)].name == name) {
            index = i;
            return &fields[static_cast<std::size_t>(i)];
        }
    }
    return nullptr;
}

TypedValue CodeGen::field_addr(int struct_id, llvm::Value *pointer, const std::string &field, SourceLoc loc) {
    int field_index = -1;
    const FieldLayout *slot = find_field(struct_id, field, field_index);
    if (pointer == nullptr || slot == nullptr || field_index < 0 || struct_id < 0 ||
        struct_id >= static_cast<int>(structs_.size()) ||
        structs_[static_cast<std::size_t>(struct_id)].type == nullptr) {
        error(loc, slot == nullptr ? "undeclared identifier" : "type mismatch");
        return {};
    }
    llvm::Value *zero_index = const_i32(0);
    llvm::Value *member = const_i32(field_index);
    llvm::Value *gep = builder_.CreateInBoundsGEP(structs_[static_cast<std::size_t>(struct_id)].type, pointer,
                                                   llvm::ArrayRef<llvm::Value *>{zero_index, member});
    return {gep, slot->depth, slot->elem_char, slot->struct_id, slot->is_array, slot->elem_void, slot->elem_bool,
            slot->elem_bits, slot->elem_unsigned, -1, slot->elem_float};
}

bool CodeGen::addressable(const Expr &expr) const {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(name->text);
        return binding != nullptr && binding->value != nullptr && !binding->is_function && !binding->is_array;
    }
    if (const auto *star = std::get_if<Expr::Unary>(&expr.data)) {
        return star->op == TokenKind::Star && star->expr != nullptr;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        return index->base != nullptr && index->index != nullptr;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return false;
        }
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            const auto package = packages_.find(base->text);
            if (package != packages_.end()) {
                const Binding *found = visible(package->second, selector->field, false);
                return found != nullptr && !found->is_function && !found->is_array;
            }
        }
        return addressable(*selector->base);
    }
    return false;
}

int CodeGen::trait_named(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = trait_names_.find(id);
        if (found == trait_names_.end()) {
            continue;
        }
        const auto trait = found->second.find(name);
        if (trait != found->second.end()) {
            return trait->second;
        }
    }
    return -1;
}

void CodeGen::prepare_traits() {
    if (graph_ == nullptr) {
        return;
    }
    int next = 0;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TraitDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || trait_named(node.file_id, item->name) >= 0) {
                continue;
            }
            trait_names_[node.file_id][item->name] = next;
            ++next;
        }
    }
}

void CodeGen::prepare_structs() {
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<StructDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || tag_id(node.file_id, item->name) >= 0) {
                continue;
            }
            const int id = static_cast<int>(structs_.size());
            StructLayout layout;
            layout.name = item->name;
            layout.type = llvm::StructType::create(context_, node.qualifier + "." + item->name);
            structs_.push_back(std::move(layout));
            tags_[node.file_id][item->name] = id;
        }
    }
    std::vector<const StructDecl *> decls(structs_.size(), nullptr);
    std::vector<uint32_t> owners(structs_.size(), 0);
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<StructDecl>(&decl.data);
            if (item == nullptr || item->name.empty()) {
                continue;
            }
            const int id = tag_id(node.file_id, item->name);
            if (id >= 0 && (decls[static_cast<std::size_t>(id)] == nullptr ||
                            (decls[static_cast<std::size_t>(id)]->forward && !item->forward))) {
                decls[static_cast<std::size_t>(id)] = item;
                owners[static_cast<std::size_t>(id)] = node.file_id;
            }
        }
    }
    prepare_aliases();
    bool progress = true;
    while (progress) {
        progress = false;
        for (int id = 0; id < static_cast<int>(structs_.size()); ++id) {
            if (structs_[static_cast<std::size_t>(id)].ready) {
                continue;
            }
            const StructDecl *item = decls[static_cast<std::size_t>(id)];
            if (item == nullptr) {
                continue;
            }
            const uint32_t file = owners[static_cast<std::size_t>(id)];
            bool waiting = false;
            for (const FieldDecl &field : item->fields) {
                if (field.indirections > 0) {
                    continue;
                }
                const Lowered low = lower(file, field.type_name, field.type_package, field.is_struct, field.indirections,
                                          field.is_array, field.array_len, false);
                if (low.depth == 0 && !low.is_array && low.struct_id >= 0 &&
                    !structs_[static_cast<std::size_t>(low.struct_id)].ready) {
                    waiting = true;
                    break;
                }
            }
            if (waiting) {
                continue;
            }
            structs_[static_cast<std::size_t>(id)].is_extern = item->is_extern;
            auto c_memory = [&](auto &&self, llvm::Type *ty) -> llvm::Type * {
                if (ty == nullptr) {
                    return ty;
                }
                if (ty->isIntegerTy(1)) {
                    return i8();
                }
                if (auto *array = llvm::dyn_cast<llvm::ArrayType>(ty)) {
                    llvm::Type *elem = self(self, array->getElementType());
                    if (elem != array->getElementType()) {
                        return llvm::ArrayType::get(elem, array->getNumElements());
                    }
                }
                return ty;
            };
            std::vector<llvm::Type *> body;
            std::vector<FieldLayout> fields;
            body.reserve(item->fields.size());
            for (const FieldDecl &field : item->fields) {
                const Lowered low =
                    field.is_fn_ptr ? lower_fnptr(file, field.type_name, field.type_package, field.is_struct, field.is_enum,
                                                  field.ret_indirections, field.indirections, field.fn_params,
                                                  field.fn_variadic, field.fn_results)
                                   : lower(file, field.type_name, field.type_package, field.is_struct, field.indirections,
                                           field.is_array, field.array_len, false);
                FieldLayout slot;
                slot.name = field.name;
                slot.type = stored_type(low);
                slot.depth = low.depth;
                slot.elem_char = low.elem_char;
                slot.is_array = low.is_array;
                slot.array_len = low.array_len;
                slot.struct_id = low.struct_id;
                slot.elem_void = low.elem_void;
                slot.elem_bool = low.elem_bool;
                slot.elem_bits = low.elem_bits;
                slot.elem_unsigned = low.elem_unsigned;
                slot.elem_float = low.elem_float;
                // C _Bool is one byte. An extern struct keeps that layout.
                if (item->is_extern && low.elem_bool && low.depth == 0) {
                    slot.type = c_memory(c_memory, slot.type);
                    slot.elem_bool = false;
                    slot.elem_bits = 8;
                    slot.elem_unsigned = true;
                    slot.elem_char = false;
                }
                body.push_back(slot.type);
                fields.push_back(std::move(slot));
            }
            if (body.empty()) {
                body.push_back(i8());
            }
            structs_[static_cast<std::size_t>(id)].type->setBody(body);
            structs_[static_cast<std::size_t>(id)].fields = std::move(fields);
            structs_[static_cast<std::size_t>(id)].ready = true;
            progress = true;
        }
    }
    for (StructLayout &layout : structs_) {
        if (!layout.ready && layout.type != nullptr && layout.type->isOpaque()) {
            layout.type->setBody(llvm::ArrayRef<llvm::Type *>{static_cast<llvm::Type *>(i8())});
            layout.ready = true;
        }
    }
}

void CodeGen::prepare_aliases() {
    if (graph_ == nullptr) {
        return;
    }
    struct Pending {
        uint32_t file = 0;
        const TypedefDecl *item = nullptr;
    };
    std::vector<Pending> pending;
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || aliases_[node.file_id].contains(item->name)) {
                continue;
            }
            pending.push_back(Pending{node.file_id, item});
        }
    }
    bool progress = true;
    while (progress) {
        progress = false;
        std::vector<Pending> later;
        for (const Pending &item : pending) {
            if (item.item == nullptr || aliases_[item.file].contains(item.item->name)) {
                continue;
            }
            if (!alias_base_ready(item.file, *item.item)) {
                later.push_back(item);
                continue;
            }
            const Lowered low =
                item.item->is_fn_ptr
                    ? lower_fnptr(item.file, item.item->type_name, item.item->type_package, item.item->is_struct,
                                  item.item->is_enum, item.item->ret_indirections, item.item->indirections,
                                  item.item->fn_params, item.item->fn_variadic, item.item->fn_results)
                    : lower(item.file, item.item->type_name, item.item->type_package, item.item->is_struct,
                            item.item->indirections, item.item->is_array, item.item->array_len, false);
            Alias alias;
            alias.depth = low.depth;
            alias.elem_char = low.elem_char;
            alias.is_array = low.is_array;
            alias.array_len = low.array_len;
            alias.struct_id = low.struct_id;
            alias.elem_void = low.elem_void;
            alias.elem_bool = low.elem_bool;
            alias.elem_bits = low.elem_bits;
            alias.elem_unsigned = low.elem_unsigned;
            alias.elem_float = low.elem_float;
            alias.fn_ty = low.fn_ty;
            alias.fn_ret_depth = low.fn_ret_depth;
            alias.fn_ret_float = low.fn_ret_float;
            alias.fn_ret_bits = low.fn_ret_bits;
            alias.fn_ret_unsigned = low.fn_ret_unsigned;
            alias.fn_ret_char = low.fn_ret_char;
            alias.fn_ret_bool = low.fn_ret_bool;
            alias.fn_ret_struct = low.fn_ret_struct;
            alias.fn_results = low.fn_results;
            aliases_[item.file].emplace(item.item->name, alias);
            progress = true;
        }
        pending = std::move(later);
    }
    for (const Pending &item : pending) {
        if (item.item == nullptr || aliases_[item.file].contains(item.item->name)) {
            continue;
        }
        aliases_[item.file].emplace(item.item->name, Alias{});
    }
}

void CodeGen::describe_enum(const std::string &underlying, EnumLayout &layout) const {
    const std::string &name = underlying.empty() ? std::string("int") : underlying;
    layout.is_bool = name == "bool";
    layout.is_char = name == "char";
    layout.is_unsigned = name == "unsigned" || name == "unsigned int" || name == "unsigned char" ||
                         name == "unsigned short" || name == "unsigned long" || name == "unsigned long long" ||
                         name == "u8" || name == "u16" || name == "u32" || name == "u64";
    if (layout.is_bool) {
        layout.bits = 1;
    } else if (name == "char" || name == "signed char" || name == "unsigned char" || name == "i8" || name == "u8") {
        layout.bits = 8;
    } else if (name == "short" || name == "signed short" || name == "unsigned short" || name == "i16" || name == "u16") {
        layout.bits = 16;
    } else if (name == "long" || name == "signed long" || name == "long long" || name == "signed long long" ||
               name == "unsigned long" || name == "unsigned long long" || name == "i64" || name == "u64") {
        layout.bits = 64;
    } else {
        layout.bits = 32;
    }
}

void CodeGen::note_enum(Alias &base, int id) const {
    if (id < 0 || id >= static_cast<int>(enums_.size())) {
        return;
    }
    const EnumLayout &layout = enums_[static_cast<std::size_t>(id)];
    base.enum_id = id;
    base.elem_bool = layout.is_bool;
    base.elem_char = layout.is_char;
    base.elem_unsigned = layout.is_unsigned;
    base.elem_bits = layout.is_bool ? 0 : layout.bits;
}

int CodeGen::enum_named(uint32_t file, const std::string &name) const {
    for (const uint32_t id : package_files(file)) {
        const auto found = enum_ids_.find(id);
        if (found == enum_ids_.end()) {
            continue;
        }
        const auto tag = found->second.find(name);
        if (tag != found->second.end()) {
            return tag->second;
        }
    }
    return -1;
}

int CodeGen::selective_enum(uint32_t file, const std::string &name) const {
    const PackageNode *node = find_node(file);
    if (node == nullptr || graph_ == nullptr) {
        return -1;
    }
    for (const ImportDecl &import : node->unit.imports) {
        if (import.form != ImportDecl::Form::Selective) {
            continue;
        }
        bool wanted = false;
        for (const std::string &item : import.names) {
            if (item == name) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        for (const ImportEdge &edge : graph_->edges) {
            if (edge.from_file != file || edge.spec != import.path || edge.loc.file != import.loc.file ||
                edge.loc.offset != import.loc.offset) {
                continue;
            }
            const int id = enum_named(edge.to_file, name);
            if (id >= 0) {
                return id;
            }
        }
    }
    return -1;
}

void CodeGen::prepare_enums() {
    if (graph_ == nullptr) {
        return;
    }
    for (const PackageNode &node : graph_->nodes) {
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<EnumDecl>(&decl.data);
            if (item == nullptr || item->name.empty() || enum_named(node.file_id, item->name) >= 0) {
                continue;
            }
            EnumLayout layout;
            layout.name = item->name;
            describe_enum(item->underlying, layout);
            int64_t next = 0;
            bool next_ok = true;
            const bool wide = layout.bits >= 63 && !layout.is_bool;
            for (const Enumerator &enumerator : item->enumerators) {
                int64_t value = 0;
                if (enumerator.has_value) {
                    value = enumerator.value;
                } else if (next_ok) {
                    value = next;
                }
                if (!enumerator.name.empty() && !layout.values.contains(enumerator.name)) {
                    layout.values.emplace(enumerator.name, value);
                }
                bool full = false;
                if (layout.is_bool) {
                    full = value >= 1;
                } else if (layout.is_unsigned || layout.is_char) {
                    full = !wide && value >= ((1LL << layout.bits) - 1);
                } else if (wide) {
                    full = value == INT64_MAX;
                } else {
                    full = value >= ((1LL << (layout.bits - 1)) - 1);
                }
                if (full) {
                    next_ok = false;
                } else {
                    next = value + 1;
                    next_ok = true;
                }
            }
            enum_ids_[node.file_id][item->name] = static_cast<int>(enums_.size());
            enums_.push_back(std::move(layout));
        }
    }
}

int CodeGen::enum_type_designator(const Expr &expr) const {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        if (lookup(named->text) != nullptr) {
            return -1;
        }
        const int local = enum_named(current_file_, named->text);
        return local >= 0 ? local : selective_enum(current_file_, named->text);
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return -1;
        }
        if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
            if (packages_.contains(base->text)) {
                const uint32_t target = package_file(current_file_, base->text);
                return target == 0 ? -1 : enum_named(target, selector->field);
            }
        }
    }
    return -1;
}

int CodeGen::enum_value_id(const Expr &expr) const {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        const Binding *binding = lookup(named->text);
        return binding == nullptr ? -1 : binding->enum_id;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector->base == nullptr) {
            return -1;
        }
        const int id = enum_type_designator(*selector->base);
        if (id < 0 || id >= static_cast<int>(enums_.size())) {
            return -1;
        }
        if (enums_[static_cast<std::size_t>(id)].values.contains(selector->field)) {
            return id;
        }
    }
    return -1;
}

llvm::Constant *CodeGen::enum_constant(int enum_id, int64_t value) const {
    if (enum_id < 0 || enum_id >= static_cast<int>(enums_.size())) {
        return const_i32(0);
    }
    const EnumLayout &layout = enums_[static_cast<std::size_t>(enum_id)];
    if (layout.is_bool) {
        return llvm::ConstantInt::get(i1(), value == 0 ? 0 : 1, false);
    }
    if (layout.bits == 8) {
        return llvm::ConstantInt::get(i8(), static_cast<uint64_t>(value) & 0xFFULL, false);
    }
    if (layout.bits == 16) {
        return llvm::ConstantInt::get(i16(), static_cast<uint64_t>(value) & 0xFFFFULL, false);
    }
    if (layout.bits == 64) {
        return llvm::ConstantInt::get(i64(), static_cast<uint64_t>(value), true);
    }
    return const_i32(static_cast<int32_t>(value));
}

CodeGen::SymbolForm CodeGen::symbol_form(const PackageNode &node, const std::string &name, bool is_extern,
                                          bool is_static, bool exported, bool entry_main) const {
    SymbolForm form;
    if (is_extern) {
        form.name = name;
        form.linkage = llvm::GlobalValue::ExternalLinkage;
        return form;
    }
    if (entry_main) {
        form.name = "main";
        form.linkage = llvm::GlobalValue::ExternalLinkage;
        return form;
    }
    if (is_static && package_files(node.file_id).size() > 1) {
        form.name = node.qualifier + "." + source_stem(node.path) + "." + name;
    } else {
        form.name = node.qualifier + "." + name;
    }
    form.linkage = exported && !is_static ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;
    return form;
}

const PackageNode *CodeGen::find_node(uint32_t file_id) const {
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.file_id == file_id) {
            return &node;
        }
    }
    return nullptr;
}

std::string function_key(const FunctionDecl &fn) {
    if (fn.method_struct.empty()) {
        return fn.name;
    }
    return fn.method_struct + "." + fn.name;
}

void CodeGen::declare_file(const PackageNode &node) {
    struct Acc {
        const FunctionDecl *fn = nullptr;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
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
        const SymbolForm form = symbol_form(node, name, acc.is_extern, acc.is_static, acc.exported, entry && name == "main");
        llvm::Function *function = acc.is_extern ? module_.getFunction(form.name) : nullptr;
        const ExternLayout layout = acc.is_extern ? extern_layout(*acc.fn, node.file_id) : ExternLayout{};
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
        const SymbolForm form = symbol_form(node, var->name, var->is_extern, var->is_static, var->exported, false);
        const Lowered low = var->is_fn_ptr
                                ? lower_fnptr(node.file_id, var->type_name, var->type_package, var->is_struct, var->is_enum,
                                              var->ret_indirections, var->indirections, var->fn_params, var->fn_variadic,
                                              var->fn_results)
                                : lower(node.file_id, var->type_name, var->type_package, var->is_struct, var->indirections,
                                        var->is_array, var->array_len, false);
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
                case TokenKind::Bang: return *inner == 0 ? 1 : 0;
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

void CodeGen::apply_initializer(const VarDecl &var) {
    FileSymbols &file = files_[current_file_];
    if (var.is_extern && !var.init) {
        return;
    }
    if (!var.init) {
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
    if (var.is_struct || !var.type_package.empty()) {
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
        const int depth = base.depth - 1;
        llvm::Value *offset = subscript.value;
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
                    return load_object(inner.value, inner.depth - 1, inner.elem_char, inner.struct_id, inner.elem_void,
                                       inner.elem_bool, inner.elem_bits, inner.elem_unsigned, inner.elem_float);
                }
                if (node.op == TokenKind::Amp) {
                    const TypedValue addr = emit_addr(*node.expr);
                    if (addr.value == nullptr) {
                        return {zero(), 0};
                    }
                    return {addr.value, addr.depth + 1, addr.elem_char, addr.struct_id, false, addr.elem_void, addr.elem_bool,
                            addr.elem_bits, addr.elem_unsigned};
                }
                const TypedValue inner_value = emit_rvalue(*node.expr);
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
                const TypedValue slot = emit_addr(*node.lhs);
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
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    if (var.name.empty()) {
                        continue;
                    }
                    const Lowered low = var.is_fn_ptr
                                            ? lower_fnptr(current_file_, var.type_name, var.type_package, var.is_struct,
                                                          var.is_enum, var.ret_indirections, var.indirections, var.fn_params,
                                                          var.fn_variadic, var.fn_results)
                                            : lower(current_file_, var.type_name, var.type_package, var.is_struct,
                                                    var.indirections, var.is_array, var.array_len, false);
                    if (var.is_array || low.is_array) {
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
    prepare_structs();
    prepare_traits();
    prepare_enums();
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

bool write_object(llvm::Module &module, llvm::TargetMachine &machine, const std::string &path, std::string &error) {
    std::error_code ec;
    llvm::raw_fd_ostream dest(path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        error = ec.message();
        return false;
    }
    llvm::legacy::PassManager pass;
    if (machine.addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        error = "object emission is not supported for the host target";
        return false;
    }
    pass.run(module);
    dest.flush();
    return true;
}

} // namespace

CodegenOutput codegen(const PackageGraph &graph, DiagnosticEngine &diags, const std::string &object_path) {
    CodegenOutput output;
    TargetState target = prepare_target();
    if (!target.machine) {
        output.error = target.error;
        diags.error(SourceLoc{}, output.error);
        return output;
    }
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc", *context);
    module->setTargetTriple(target.triple);
    module->setDataLayout(target.machine->createDataLayout());
    CodeGen gen(*context, *module, diags);
    gen.emit(graph);
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        verify_stream.flush();
        diags.error(SourceLoc{}, "invalid llvm module: " + verify_error);
    }
    llvm::raw_string_ostream ir_stream(output.ir);
    module->print(ir_stream, nullptr);
    ir_stream.flush();
    if (!diags.ok()) {
        output.ok = false;
        output.error = diags.render();
        return output;
    }
    if (!object_path.empty() && !write_object(*module, *target.machine, object_path, output.error)) {
        diags.error(SourceLoc{}, output.error);
        output.ok = false;
        return output;
    }
    output.ok = true;
    return output;
}

} // namespace tlc
