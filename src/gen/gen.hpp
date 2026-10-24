#pragma once

// Internal code generator. The public entry point is codegen() in codegen.hpp.

#include "gen/codegen.hpp"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <cstdint>
#include <memory>
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

inline void append_type_args_key(std::string &out, const std::vector<TypeArg> &args);

inline void append_fn_param_key(std::string &out, const FnParam &param) {
    if (param.is_array) {
        out.push_back('[');
        if (param.array_len >= 0) {
            out += std::to_string(param.array_len);
        }
        out.push_back(']');
        if (param.elem_const) {
            out += "const.";
        }
    }
    for (int bit = param.indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((param.optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((param.const_pointee & mask) != 0) {
            out += "const";
        }
    }
    if (param.is_fn_ptr) {
        out += "fn";
        for (int bit = param.ret_indirections - 1; bit >= 0; --bit) {
            const uint32_t mask = 1u << static_cast<unsigned>(bit);
            if ((param.ret_optional & mask) != 0) {
                out.push_back('?');
            }
            out.push_back('*');
            if ((param.ret_const_pointee & mask) != 0) {
                out += "const";
            }
        }
    }
    if (!param.type_package.empty()) {
        out += param.type_package;
        out.push_back('.');
    }
    out += param.type_name;
    append_type_args_key(out, param.type_args);
    if (!param.is_fn_ptr) {
        return;
    }
    out.push_back('(');
    for (std::size_t i = 0; i < param.params.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_fn_param_key(out, param.params[i]);
    }
    if (param.variadic) {
        out += "...";
    }
    out.push_back(')');
    if (param.results.empty()) {
        return;
    }
    out.push_back('(');
    for (std::size_t i = 0; i < param.results.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        append_fn_param_key(out, param.results[i]);
    }
    out.push_back(')');
}

inline void append_type_args_key(std::string &out, const std::vector<TypeArg> &args) {
    if (args.empty()) {
        return;
    }
    out.push_back('[');
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        const TypeArg &arg = args[i];
        if (arg.has_value || arg.is_value) {
            out += std::to_string(arg.value);
            continue;
        }
        if (arg.is_array) {
            out.push_back('[');
            if (!arg.array_len_name.empty()) {
                out += arg.array_len_name;
            } else if (arg.array_len >= 0) {
                out += std::to_string(arg.array_len);
            }
            out.push_back(']');
        }
        for (int bit = arg.indirections - 1; bit >= 0; --bit) {
            const uint32_t mask = 1u << static_cast<unsigned>(bit);
            if ((arg.optional & mask) != 0) {
                out.push_back('?');
            }
            out.push_back('*');
            if ((arg.const_pointee & mask) != 0) {
                out += "const";
            }
        }
        if (arg.elem_const) {
            out += "const.";
        }
        if (!arg.type_package.empty()) {
            out += arg.type_package;
            out.push_back('.');
        }
        out += arg.type_name;
        append_type_args_key(out, arg.args);
    }
    out.push_back(']');
}

inline void append_param_key(std::string &out, const Param &param) {
    if (param.is_tuple) {
        out.push_back('(');
        for (std::size_t i = 0; i < param.tuple_parts.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            const ResultSlot &slot = param.tuple_parts[i];
            FnParam part;
            part.type_name = slot.type_name;
            part.type_package = slot.type_package;
            part.is_struct = slot.is_struct;
            part.is_enum = slot.is_enum;
            part.indirections = slot.indirections;
            part.optional = slot.optional;
            part.const_pointee = slot.const_pointee;
            part.is_array = slot.is_array;
            part.array_len = slot.array_len;
            part.is_fn_ptr = slot.is_fn_ptr;
            part.ret_indirections = slot.ret_indirections;
            part.ret_optional = slot.ret_optional;
            part.ret_const_pointee = slot.ret_const_pointee;
            part.params = slot.fn_params;
            part.variadic = slot.fn_variadic;
            part.results = slot.fn_results;
            part.type_args = slot.type_args;
            append_fn_param_key(out, part);
        }
        out.push_back(')');
        return;
    }
    if (param.is_array) {
        out.push_back('[');
        if (!param.array_len_name.empty()) {
            out += param.array_len_name;
        } else if (param.array_len >= 0) {
            out += std::to_string(param.array_len);
        }
        out.push_back(']');
        if (param.elem_const) {
            out += "const.";
        }
    }
    for (int bit = param.indirections - 1; bit >= 0; --bit) {
        const uint32_t mask = 1u << static_cast<unsigned>(bit);
        if ((param.optional & mask) != 0) {
            out.push_back('?');
        }
        out.push_back('*');
        if ((param.const_pointee & mask) != 0) {
            out += "const";
        }
    }
    if (param.is_fn_ptr) {
        FnParam fn;
        fn.type_name = param.type_name;
        fn.type_package = param.type_package;
        fn.indirections = 0;
        fn.is_fn_ptr = true;
        fn.ret_indirections = param.ret_indirections;
        fn.ret_optional = param.ret_optional;
        fn.ret_const_pointee = param.ret_const_pointee;
        fn.params = param.fn_params;
        fn.variadic = param.fn_variadic;
        fn.results = param.fn_results;
        fn.type_args = param.type_args;
        append_fn_param_key(out, fn);
        return;
    }
    if (!param.type_package.empty()) {
        out += param.type_package;
        out.push_back('.');
    }
    out += param.type_name;
    append_type_args_key(out, param.type_args);
}

// The parameters a call uses. The receiver is omitted. One list keeps `function_key`.
inline std::string param_list_key(const FunctionDecl &fn) {
    std::string key;
    if (fn.variadic) {
        key.push_back('v');
    }
    std::size_t start = 0;
    if (!fn.method_struct.empty() && !fn.static_method && !fn.params.empty() && fn.params.front().is_receiver) {
        start = 1;
    }
    for (std::size_t i = start; i < fn.params.size(); ++i) {
        if (!key.empty()) {
            key.push_back(',');
        }
        append_param_key(key, fn.params[i]);
    }
    return key;
}

inline FnParam fn_param_from_slot(const ResultSlot &slot) {
    FnParam param;
    param.type_name = slot.type_name;
    param.type_package = slot.type_package;
    param.is_struct = slot.is_struct;
    param.is_enum = slot.is_enum;
    param.indirections = slot.indirections;
    param.optional = slot.optional;
    param.const_pointee = slot.const_pointee;
    param.is_array = slot.is_array && slot.array_len < 0;
    param.array_len = param.is_array ? slot.array_len : 0;
    param.elem_slice = param.is_array && slot.elem_slice;
    param.elem_const = slot.elem_const;
    param.is_fn_ptr = slot.is_fn_ptr;
    param.ret_indirections = slot.ret_indirections;
    param.ret_optional = slot.ret_optional;
    param.ret_const_pointee = slot.ret_const_pointee;
    param.params = slot.fn_params;
    param.variadic = slot.fn_variadic;
    param.results = slot.fn_results;
    return param;
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
    // A tlc `extern` on a trait parameter. The prototype may be the declaration that says it.
    bool is_extern = false;
    // `const name = expr`. `value` is null and `const_value` is the folded integer.
    bool immediate = false;
    int const_value = 0;
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
    // `[2][3]i32`: the element is an array of this length. Zero when the element is not an array.
    int inner_array_len = 0;
    // A pack of several types, stored as one anonymous struct.
    bool is_tuple = false;
    std::vector<ResultSlot> tuple_parts;
    // `[]T`. The slot holds `{ data, len }`. `depth` is the element, not the slice.
    bool is_slice = false;
    // The element of this slice or array is one `[]E`. A further slice stays rejected.
    bool elem_slice = false;
    std::vector<TypeShell> shells;
    // `type Row []i32`. Empty when this value is the slice itself.
    std::string alias_owner;
    uint32_t alias_file = 0;
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
    // Set on a decayed array whose element is itself an array. Indexing steps by that row.
    int inner_array_len = 0;
    // The address is a row of an array of arrays. The caller uses it as a pointer, not a load.
    bool row_decay = false;
    bool is_tuple = false;
    // Element types of a tuple value, in order. Empty when the value is not a tuple.
    std::vector<ResultSlot> tuple_parts{};
    // Set when this value is a pointer to an array (`*[4]i32`).
    bool ptr_to_array = false;
    int ptr_array_len = 0;
    // Length of a sized array that has not decayed. Negative for a slice.
    int array_len = 0;
    // Set when this rvalue is a sized array. A new binding copies that array.
    // The address is still the array, so an index or a `[N]T` argument uses the pointer.
    llvm::Type *array_type = nullptr;
    // `[]T`, stored as one `{ ptr, i32 }` value.
    bool is_slice = false;
    // The element is one `[]E`. Indexing loads a header, not `E`.
    bool elem_slice = false;
    std::vector<TypeShell> shells{};
    // `type Row []i32`. Empty when this value is the slice itself.
    std::string alias_owner;
    uint32_t alias_file = 0;
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
    void enable_debug() { debug_ = true; }

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
    // The function whose body is being emitted. Its `captured_here` names keep a stable slot.
    const FunctionDecl *emitting_fn_ = nullptr;
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
    struct DeclKey {
        uint32_t file_id = 0;
        std::string key;
    };
    // Every declaration of a function, including a prototype and its body.
    std::unordered_map<const FunctionDecl *, DeclKey> decl_keys_;
    // Local functions that do not own the scope name. The first list still does.
    std::unordered_map<const FunctionDecl *, Binding> local_fns_;
    uint32_t entry_file_ = 0;
    bool testing_ = false;
    // `lower` is const. The struct is created the first time a slice is lowered.
    mutable llvm::StructType *slice_ty_ = nullptr;
    // `-g`. Line numbers and variable locations for a debugger.
    bool debug_ = false;
    std::unique_ptr<llvm::DIBuilder> di_builder_;
    llvm::DICompileUnit *di_unit_ = nullptr;
    llvm::DISubprogram *di_subprogram_ = nullptr;
    std::vector<llvm::DIScope *> di_scopes_;
    std::unordered_map<uint32_t, llvm::DIFile *> di_files_;
    std::unordered_map<std::string, llvm::DIType *> di_basic_;
    std::unordered_map<int, llvm::DIType *> di_structs_;
    // Name recorded on the approximate call stack while a function or test is emitted.
    std::string trace_name_;
    llvm::StructType *trace_frame_ = nullptr;
    llvm::ArrayType *trace_stack_type_ = nullptr;
    llvm::GlobalVariable *trace_depth_ = nullptr;
    llvm::GlobalVariable *trace_stack_ = nullptr;
    llvm::GlobalVariable *trace_buffer_ = nullptr;
    uint32_t current_file_ = 0;
    // Comptime funcs on the fold stack. A cycle folds as not a constant.
    mutable std::unordered_set<const FunctionDecl *> comptime_folding_;
    std::vector<std::unordered_map<std::string, Binding>> scopes_;
    std::unordered_map<uint32_t, FileSymbols> files_;
    // Globals whose constant value is being copied into another initializer.
    std::unordered_set<std::string> const_names_;
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
        SourceLoc loc{};
        // Push a call-stack frame around this call. `builtin.trace` and `builtin.va` leave it
        // off, so those calls are not part of the text. `builtin.panic` keeps the frame.
        bool trace = false;
        // `items.len()` on an array or a slice. The value is the length, the element, or its address.
        bool yielded = false;
        TypedValue yield;
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
        // `[]T` stored in the field. The slot is the `{ ptr, i32 }` value.
        bool is_slice = false;
        // The element of this slice or array field is one `[]E`.
        bool elem_slice = false;
        std::vector<TypeShell> shells;
        // A function-pointer field. The address keeps this signature, so a store of a
        // function is that pointer. Without it the function's depth is 0 and the store is null.
        llvm::FunctionType *fn_ty = nullptr;
        int fn_ret_depth = 0;
        bool fn_ret_float = false;
        int fn_ret_bits = 0;
        bool fn_ret_unsigned = false;
        bool fn_ret_char = false;
        bool fn_ret_bool = false;
        int fn_ret_struct = -1;
        std::vector<FnParam> fn_results{};
        // `name type = expr`. Null when the field has no value, or when that value is rejected.
        const Expr *init = nullptr;
        // A field whose type is a tuple alias. `type` is that aggregate.
        bool is_tuple = false;
        std::vector<ResultSlot> tuple_parts;
    };
    struct StructLayout {
        std::string name;
        llvm::StructType *type = nullptr;
        uint32_t file_id = 0;
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
        int inner_array_len = 0;
        bool is_tuple = false;
        std::vector<ResultSlot> tuple_parts;
        bool is_slice = false;
        // The element is one `[]E`. `[N][]T` is `[N x tlc.slice]`.
        bool elem_slice = false;
        std::vector<TypeShell> shells;
        // `type Row []i32`. Empty when the lowered type is the slice itself.
        std::string alias_owner;
        uint32_t alias_file = 0;
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
        bool is_slice = false;
        // `type Rows [][]i32`. A use of the name keeps this on the header.
        bool elem_slice = false;
        std::vector<TypeShell> shells;
        // `type Pair (i32, i32)`. `tuple_ty` is the aggregate. The name stays a distinct type in sema.
        bool is_tuple = false;
        std::vector<ResultSlot> tuple_parts;
        llvm::Type *tuple_ty = nullptr;
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
        bool is_slice = false;
        // The element is one `[]E`. `sizeof` of `[N][]T` is `N` headers.
        bool elem_slice = false;
        std::vector<TypeShell> shells;
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
    // Parallel to the ids in `trait_names_`. The decl stays in the package AST.
    std::vector<const TraitDecl *> trait_decls_;
    std::unordered_map<std::string, llvm::Function *> specs_;
    std::unordered_map<std::string, llvm::GlobalVariable *> type_names_;
    // One descriptor per spelling. The value of `Type` is a pointer to it.
    std::unordered_map<std::string, llvm::GlobalVariable *> type_descs_;
    // One `*const Type` cell per spelling. `Field.kind` points at it.
    std::unordered_map<std::string, llvm::GlobalVariable *> type_kind_ptrs_;
    llvm::StructType *type_desc_ty_ = nullptr;
    // `fields()` element: `{ name []const u8, kind *const Type }`. Negative until `prepare_structs`.
    int field_struct_id_ = -1;
    std::unordered_set<std::string> emitting_specs_;
    std::vector<int> active_spec_;
    // One entry per parameter of the specialization being emitted. Parallel to `active_spec_`.
    // A slot is live when that parameter is `kIterSpec`.
    struct IterSlot {
        bool active = false;
        bool is_slice = false;
        int array_len = 0;
        Lowered param;
        std::string fragment;
        std::string alias_owner;
        uint32_t alias_file = 0;
    };
    std::vector<IterSlot> active_iter_;
    // Set while `spec_type` builds the signature of a specialization that is not yet `active_iter_`.
    std::vector<IterSlot> pending_iter_;
    bool pending_iter_on_ = false;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> tags_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Alias>> aliases_;

    llvm::Type *i32() const { return llvm::Type::getInt32Ty(context_); }
    llvm::Type *i16() const { return llvm::Type::getInt16Ty(context_); }
    llvm::Type *i64() const { return llvm::Type::getInt64Ty(context_); }
    llvm::Type *i8() const { return llvm::Type::getInt8Ty(context_); }
    llvm::Type *i1() const { return llvm::Type::getInt1Ty(context_); }
    llvm::PointerType *ptr_type() const { return llvm::PointerType::get(context_, 0); }
    // Every `[]T` is this one struct. The pointer addresses the first element.
    llvm::StructType *slice_type() const;
    llvm::Value *as_slice_value(const TypedValue &value, SourceLoc loc);
    llvm::Value *adapt(const TypedValue &value, llvm::Type *slot, SourceLoc loc);
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
        // `bool` is 0 or 1. Sign-extending `i1` would turn `true` into `-1`.
        if (value->getType()->isIntegerTy(1)) {
            return builder_.CreateZExt(value, dest);
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
        if (value->getType()->isFloatingPointTy()) {
            return builder_.CreateFCmpUNE(value, llvm::ConstantFP::get(value->getType(), 0.0));
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
        if (slot_type != nullptr && slot_type->isStructTy() && from_depth == 0 && value->getType()->isStructTy()) {
            if (value->getType() == slot_type) {
                return value;
            }
            auto *dest = llvm::cast<llvm::StructType>(slot_type);
            auto *src = llvm::cast<llvm::StructType>(value->getType());
            if (dest->getNumElements() == src->getNumElements()) {
                llvm::Value *aggregate = llvm::ConstantAggregateZero::get(dest);
                for (unsigned index = 0; index < dest->getNumElements(); ++index) {
                    llvm::Value *field = builder_.CreateExtractValue(value, index);
                    llvm::Type *want = dest->getElementType(index);
                    if (field->getType() != want) {
                        if (field->getType()->isIntegerTy() && want->isIntegerTy()) {
                            field = resize_int(field, want, source_unsigned);
                        } else if (field->getType()->isFloatingPointTy() && want->isFloatingPointTy()) {
                            field = field->getType()->getPrimitiveSizeInBits() < want->getPrimitiveSizeInBits()
                                        ? builder_.CreateFPExt(field, want)
                                        : builder_.CreateFPTrunc(field, want);
                        } else if (field->getType()->isPointerTy() && want->isArrayTy()) {
                            // `(nums, 4)` holds the address of `[N]T`. The tuple slot stores the array.
                            field = builder_.CreateLoad(want, field);
                        } else {
                            field = llvm::Constant::getNullValue(want);
                        }
                    }
                    aggregate = builder_.CreateInsertValue(aggregate, field, index);
                }
                return aggregate;
            }
        }
        // `[]const u8` passed where C asks for `*const u8` is the data address.
        if (slot_type != nullptr && slot_type->isPointerTy() && value->getType() == slice_type()) {
            return builder_.CreateExtractValue(value, 0);
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
            // The bits are 0 or 1 in an i32. The type stays bool so a later alias cast does not convert them again.
            return {builder_.CreateZExt(loaded, i32()), 0, false, -1, false, elem_void, true};
        }
        if (elem_char || elem_bits == 8 || elem_bits == 16) {
            const bool zero_extend = elem_unsigned || elem_char;
            llvm::Value *wide = zero_extend ? builder_.CreateZExt(loaded, i32()) : builder_.CreateSExt(loaded, i32());
            const int bits = elem_bits == 0 ? 8 : elem_bits;
            return {wide, 0, elem_char, -1, false, elem_void, false, bits, elem_unsigned};
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
        if (binding.immediate) {
            return {const_i32(binding.const_value), 0};
        }
        if (binding.is_function) {
            TypedValue value{binding.value, binding.depth, binding.elem_char, binding.struct_id, false, binding.elem_void,
                             binding.elem_bool, binding.elem_bits, binding.elem_unsigned, -1, binding.elem_float};
            value.enum_id = binding.enum_id;
            if (auto *function = llvm::dyn_cast_or_null<llvm::Function>(binding.value)) {
                value.fn_ty = function->getFunctionType();
            }
            if (binding.decl != nullptr && binding.decl->results.size() > 1) {
                value.fn_results.reserve(binding.decl->results.size());
                for (const ResultSlot &slot : binding.decl->results) {
                    value.fn_results.push_back(fn_param_from_slot(slot));
                }
            }
            return value;
        }
        if (binding.is_slice && binding.value != nullptr) {
            llvm::Type *ty = object_type(binding.value);
            if (ty == nullptr) {
                ty = slice_type();
            }
            TypedValue loaded;
            loaded.value = builder_.CreateLoad(ty, binding.value);
            loaded.is_slice = true;
            loaded.array_len = -1;
            loaded.depth = binding.depth;
            loaded.elem_char = binding.elem_char;
            loaded.struct_id = binding.struct_id;
            loaded.elem_void = binding.elem_void;
            loaded.elem_bool = binding.elem_bool;
            loaded.elem_bits = binding.elem_bits;
            loaded.elem_unsigned = binding.elem_unsigned;
            loaded.elem_float = binding.elem_float;
            loaded.enum_id = binding.enum_id;
            loaded.inner_array_len = binding.inner_array_len;
            loaded.elem_slice = binding.elem_slice;
            loaded.shells = binding.shells;
            loaded.alias_owner = binding.alias_owner;
            loaded.alias_file = binding.alias_file;
            return loaded;
        }
        if (binding.is_array) {
            TypedValue value{binding.value, binding.depth + 1, binding.elem_char, binding.struct_id, false, binding.elem_void,
                             binding.elem_bool, binding.elem_bits, binding.elem_unsigned, -1, binding.elem_float};
            value.array_len = binding.array_len;
            value.inner_array_len = binding.inner_array_len;
            value.elem_slice = binding.elem_slice;
            // The array decays to a pointer to its element. Indexing steps by that element.
            if (!binding.shells.empty() && binding.shells.front().kind == TypeShell::Kind::Array) {
                value.shells.assign(binding.shells.begin() + 1, binding.shells.end());
            }
            value.fn_ty = binding.fn_ty;
            value.fn_ret_depth = binding.fn_ret_depth;
            value.fn_ret_float = binding.fn_ret_float;
            value.fn_ret_bits = binding.fn_ret_bits;
            value.fn_ret_unsigned = binding.fn_ret_unsigned;
            value.fn_ret_char = binding.fn_ret_char;
            value.fn_ret_bool = binding.fn_ret_bool;
            value.fn_ret_struct = binding.fn_ret_struct;
            value.fn_results = binding.fn_results;
            value.enum_id = binding.enum_id;
            // A binding copies this array. Indexing still uses the address.
            if (llvm::Type *object = object_type(binding.value); object != nullptr && object->isArrayTy()) {
                value.array_type = object;
            }
            return value;
        }
        if (binding.is_tuple && binding.value != nullptr) {
            llvm::Type *ty = object_type(binding.value);
            if (ty == nullptr) {
                return {zero(), 0};
            }
            TypedValue loaded;
            loaded.value = builder_.CreateLoad(ty, binding.value);
            loaded.is_tuple = true;
            loaded.tuple_parts = binding.tuple_parts;
            return loaded;
        }
        if (!binding.shells.empty() && binding.shells.front().kind == TypeShell::Kind::Pointer && binding.value != nullptr) {
            TypedValue loaded;
            loaded.value = builder_.CreateLoad(ptr_type(), binding.value);
            loaded.shells = binding.shells;
            loaded.depth = binding.depth;
            loaded.elem_char = binding.elem_char;
            loaded.struct_id = binding.struct_id;
            loaded.elem_void = binding.elem_void;
            loaded.elem_bool = binding.elem_bool;
            loaded.elem_bits = binding.elem_bits;
            loaded.elem_unsigned = binding.elem_unsigned;
            loaded.elem_float = binding.elem_float;
            loaded.enum_id = binding.enum_id;
            loaded.ptr_to_array = binding.ptr_to_array;
            loaded.ptr_array_len = binding.ptr_array_len;
            loaded.elem_slice = binding.elem_slice;
            loaded.inner_array_len = binding.inner_array_len;
            // A parameter `[N][M]T` or `[N][]T` keeps `N` after the outer array decays.
            loaded.array_len = binding.array_len;
            return loaded;
        }
        TypedValue loaded = load_object(binding.value, binding.depth, binding.elem_char, binding.struct_id, binding.elem_void,
                                        binding.elem_bool, binding.elem_bits, binding.elem_unsigned, binding.elem_float);
        loaded.ptr_to_array = binding.ptr_to_array;
        loaded.ptr_array_len = binding.ptr_array_len;
        // A `[N][]T` parameter is a pointer that steps by one header.
        // A `[N]T` parameter keeps `N` in `array_len` so a range can build the slice.
        loaded.elem_slice = binding.elem_slice;
        loaded.shells = binding.shells;
        loaded.inner_array_len = binding.inner_array_len;
        loaded.array_len = binding.array_len;
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
    void open_debug();
    void finish_debug();
    llvm::DIFile *debug_file(SourceLoc loc);
    void set_loc(SourceLoc loc);
    void debug_enter(SourceLoc loc);
    void debug_leave();
    void debug_begin_function(const FunctionDecl &fn, llvm::Function *function);
    void debug_end_function();
    void debug_declare(const std::string &name, SourceLoc loc, const Binding &binding, unsigned param_index);
    void debug_global(const std::string &name, llvm::GlobalVariable *global, SourceLoc loc, const Lowered &low,
                      bool local);
    llvm::DIType *debug_type(const Lowered &low);
    void stamp_builder(llvm::IRBuilderBase &builder) const;
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
    // `[]T` among several results. A sized array stays the element type.
    Lowered lower_result(uint32_t file, const ResultSlot &slot) const;
    const FunctionDecl *callee_decl(const Expr &callee) const;
    SymbolForm symbol_form(const PackageNode &node, const std::string &name, bool is_extern, bool extern_c,
                           bool is_static, bool exported, bool entry_main, const std::string &link_name) const;
    const PackageNode *find_node(uint32_t file_id) const;
    std::vector<uint32_t> package_files(uint32_t file) const;
    const Binding *visible(uint32_t file, const std::string &name, bool function) const;
    const Binding *binding_for_decl(const FunctionDecl *decl) const;
    void bind_package_globals();
    int tag_id(uint32_t file, const std::string &name) const;
    // `Name.file.N` is the spelling `disambiguate_bounds` writes. `N` is the file that defines `Name`.
    int file_alias(const std::string &name) const;
    // A specialization is stored on the package that defines the generic struct.
    int spec_tag(const std::string &name) const;
    int unique_tag(const std::string &name) const;
    uint32_t package_file(uint32_t file, const std::string &package) const;
    // File whose package holds `Entity.next`. `math.Entity.next` is the imported package.
    uint32_t assoc_home(const Expr::Selector &node) const;
    int selective_tag(uint32_t file, const std::string &name) const;
    const Alias *find_alias(uint32_t file, const std::string &name) const;
    const Alias *selective_alias(uint32_t file, const std::string &name) const;
    bool has_typedef(uint32_t file, const std::string &name) const;
    bool alias_base_ready(uint32_t file, const TypedefDecl &item) const;
    // `builtin.Type`, or `Type` brought in by `import { Type } from "builtin"`.
    bool meta_type(uint32_t file, const std::string &package, const std::string &name) const;
    Lowered lower(uint32_t file, const std::string &name, const std::string &package, bool is_struct, int indirections,
                  bool is_array, int array_len, bool parameter, bool elem_slice = false,
                  const std::vector<TypeShell> &shells = {}) const;
    // LLVM type of `shells` around a leaf. `shells.front()` is the outermost constructor.
    llvm::Type *shell_type(const std::vector<TypeShell> &shells, int depth, bool elem_char, int struct_id, bool elem_bool,
                           int elem_bits, bool elem_float) const;
    Lowered compose_shells(Lowered leaf, std::vector<TypeShell> shells, bool parameter) const;
    // Index one shell. The base is an rvalue: a slice header, or a pointer whose shells start with `Pointer`.
    TypedValue index_shells(const TypedValue &base, llvm::Value *subscript);
    Lowered lower_declared(uint32_t file, const VarDecl &var) const;
    Lowered lower_fnptr(uint32_t file, const std::string &ret_name, const std::string &ret_package, bool ret_struct,
                        bool ret_enum, int ret_indirections, int fn_depth, const std::vector<FnParam> &params,
                        bool variadic, const std::vector<FnParam> &results) const;
    Lowered lower_param_type(uint32_t file, const Param &param) const;
    bool infer_global(uint32_t file, const Expr &expr, Lowered &out) const;
    void apply_low(Binding &binding, const Lowered &low) const;
    // Scalar fields only. An array or a slice return does not set those flags on a function.
    void apply_scalar_result(Binding &binding, const Lowered &ret, bool returns_void) const;
    const FieldLayout *find_field(int struct_id, const std::string &name, int &index) const;
    TypedValue field_addr(int struct_id, llvm::Value *pointer, const std::string &field, SourceLoc loc);
    void store_union_tag(int struct_id, llvm::Value *pointer, const std::string &field);
    llvm::Type *enum_mem_type(int enum_id) const;
    bool write_constant_bytes(llvm::Constant *value, uint64_t offset, std::vector<uint8_t> &bytes) const;
    bool addressable(const Expr &expr) const;
    void prepare_structs();
    // A specialized function is stored on the package that defines the generic.
    // A type argument written by the caller is not a name in that package.
    void publish_spec_types();
    void prepare_traits();
    int trait_named(uint32_t file, const std::string &name) const;
    const TraitDecl *trait_parent(uint32_t file, const TraitDecl *trait) const;
    bool param_is_trait(uint32_t file, const Param &param) const;
    bool function_is_generic(uint32_t file, const FunctionDecl &fn) const;
    // A trait parameter specialized to the function pointer of its one method.
    Lowered trait_fn_lowered(const std::string &trait_name) const;
    Lowered spec_param(const FunctionDecl &fn, std::size_t index, const std::vector<int> &spec) const;
    Lowered lower_param(const FunctionDecl &fn, std::size_t index) const;
    llvm::FunctionType *spec_type(const FunctionDecl &fn, const std::vector<int> &spec) const;
    void emit_function_body(const FunctionDecl &fn, llvm::Function *function, int return_depth,
                            const std::vector<Binding> *captures = nullptr);
    llvm::Function *specialize(const FunctionDecl &fn, const Binding &meta, const std::vector<TypedValue> &arg_values,
                               SourceLoc loc, std::vector<llvm::Value *> &out_args);
    void generic_method_call(const Binding &method, const TypedValue *receiver, const Expr::Call &node,
                             PreparedCall &prepared);
    void call_trait_fn(llvm::Value *fnptr, const TypedValue &meta, const Expr::Call &node, PreparedCall &prepared);
    TypedValue emit_generic_call(const Binding &binding, const Expr::Call &node);
    void prepare_aliases();
    bool make_tuple_alias(uint32_t file, const TypedefDecl &item, Alias &out) const;
    // Tuple aliases whose elements are sized now. A struct element waits for that body.
    bool register_tuple_aliases();
    void finish_tuple_aliases();
    void prepare_enums();
    int enum_named(uint32_t file, const std::string &name) const;
    // An enum in `file`'s package, or a typedef of one.
    int enum_id_of(uint32_t file, const std::string &name) const;
    int selective_enum(uint32_t file, const std::string &name) const;
    int enum_type_designator(const Expr &expr) const;
    int enum_value_id(const Expr &expr) const;
    llvm::Constant *enum_constant(int enum_id, int64_t value) const;
    bool type_known(const std::string &name) const;
    bool unshadowed_type_name(const std::string &name) const;
    const Binding *function_binding(uint32_t file, const std::string &name) const;
    const Binding *static_method_binding(uint32_t file, const std::string &type, const std::string &field) const;
    // `Type.name` or `pkg.Type.name`. `resolved_file` selects a method in another package.
    const Binding *static_selector_binding(uint32_t file, const Expr::Selector &selector) const;
    // A function in `home`'s package. A static function in another file of that package is skipped.
    const Binding *function_named(uint32_t home, const std::string &key, uint32_t caller) const;
    // A value method. A type defined in another package is found through `resolved_file`.
    const Binding *method_binding(uint32_t caller, uint32_t resolved_file, const std::string &key) const;
    // `s.twice()` inside a trait specialization. `selected` is empty, and overloads are not stored
    // under the plain name. The trait method, or the one default of this arity, names the list.
    const Binding *trait_spec_method(uint32_t caller, uint32_t resolved_file, const std::string &key,
                                     const Expr::Selector &selector, const Expr::Call &node) const;
    const Binding *function_designator(uint32_t file, const Expr &expr) const;
    // Function or method a file-scope call initializer names. Null when it is not one.
    const Binding *call_callee(uint32_t file, const Expr &callee) const;
    std::optional<Lowered> function_pointer_type(const Binding &fn) const;
    void describe_enum(const std::string &underlying, EnumLayout &layout) const;
    void note_enum(Alias &base, int id) const;
    void declare_functions(const PackageNode &node);
    void declare_globals(const PackageNode &node);
    void emit_dynamic_inits();
    std::optional<int32_t> fold_global(uint32_t file, const std::string &name, bool allow_local_static = true) const;
    void bind_imports(const PackageNode &node);
    void emit_bodies(const PackageNode &node);
    void emit_tests();
    bool is_std_builtin_call(const Expr &callee, const std::string &field) const;
    llvm::Function *builtin_function(const std::string &name) const;
    void ensure_trace();
    std::string display_path(SourceLoc loc) const;
    llvm::Value *push_trace(SourceLoc loc);
    void pop_trace(llvm::Value *pushed);
    TypedValue emit_builtin_va(SourceLoc loc);
    void emit_builtin_trace();
    void emit_builtin_panic();
    void emit_builtin_heap();
    llvm::Value *variadic_arg(const TypedValue &value);
    void apply_initializer(const VarDecl &var);
    // Pointer to the first element, the element type a further range steps by, and the length.
    struct SlicePieces {
        llvm::Constant *pointer = nullptr;
        llvm::Type *element = nullptr;
        int32_t length = 0;
    };
    std::optional<SlicePieces> slice_pieces(const Expr &expr, std::unordered_set<std::string> &seen);
    llvm::Constant *constant_slice(const Expr &expr, std::unordered_set<std::string> &seen);
    llvm::Constant *const_initializer(const Expr &expr, const Binding &dest);
    llvm::Constant *sized_int(const Binding &dest, int32_t value) const;
    llvm::Constant *intern_type_name(const std::string &text);
    llvm::StructType *type_desc_type();
    llvm::Constant *intern_type_desc(const std::string &text);
    llvm::Constant *type_kind_ptr(const std::string &text);
    // The descriptor pointer a `Type` or `*Type` receiver holds. Null on a bad receiver.
    llvm::Value *type_descriptor(const Expr &expr);
    void install_field_struct();
    llvm::Constant *const_struct(const Expr::Init &init, int struct_id);
    // Omitted fields take the value written on the field. `strict` drops the constant when a named field does not fold.
    llvm::Constant *const_struct_into(const Expr::Init *overlay, int struct_id, bool strict,
                                      std::unordered_set<int> &stack);
    llvm::Constant *default_struct_into(int struct_id, std::unordered_set<int> &stack);
    llvm::Constant *default_struct(int struct_id);
    llvm::Constant *init_image(int struct_id, const Expr::Init *named);
    llvm::Constant *default_array(llvm::Type *type, int struct_id, std::unordered_set<int> &stack);
    llvm::Constant *omitted_field(const FieldLayout &slot, std::unordered_set<int> &stack);
    llvm::Constant *bare_initializer(const Lowered &low, llvm::Type *ty);
    // `seen` stops a by-value cycle. A true result leaves ids in `seen`.
    bool struct_has_default(int struct_id, std::unordered_set<int> &seen) const;
    void store_struct_defaults(llvm::Value *slot, int struct_id, std::unordered_set<int> &seen,
                               const Expr::Init *overlay = nullptr);
    void paint_omitted_elements(llvm::Value *slot, llvm::ArrayType *array_ty, int struct_id, const Expr::Init *list,
                                std::unordered_set<int> &seen);
    void store_each_element(llvm::Value *raw, llvm::Value *count, llvm::Type *elem, llvm::Constant *image);
    std::optional<int32_t> folded_sizeof(const Expr::Sizeof &node) const;
    llvm::Constant *const_array(const Expr::Init &init, const Binding &dest);
    llvm::Constant *fill_array(const Expr::Init &init, llvm::ArrayType *array_ty, const Binding &flags);
    // The constant array stored by `expr`, using that array's own global when `expr` is a name.
    llvm::Constant *const_array_base(const Expr &expr);
    // `expr` copied into `dest`. A list is filled as `dest`. A name or an index must match `dest`.
    llvm::Constant *const_array_copied(const Expr &expr, const Binding &dest);
    const VarDecl *global_decl(const std::string &name) const;
    std::optional<Lowered> lower_array_literal(uint32_t file, const Expr::Init &init) const;
    llvm::Constant *const_field(const Expr &expr, const FieldLayout &slot);
    // A field default `{1}` or `{1, 0...}` on `[2]i32`. Missing elements are zero in this constant.
    llvm::Constant *const_default_array(const Expr::Init &init, llvm::ArrayType *array_ty, const Binding &flags);
    std::optional<int32_t> fold(const Expr &expr) const;
    // A string literal, a constant range of one, or a file-scope const in this file
    // whose initializer is one. A pointer const is not that slice. A `var` is not a constant.
    std::optional<std::string> constant_string(const Expr &expr, std::unordered_set<std::string> &seen) const;
    std::optional<int32_t> known_len(const Expr &base) const;
    bool is_builtin_fmt(const Expr::Call &call) const;
    const FunctionDecl *fold_call_target(const std::string &name, const Expr::Call &call) const;
    std::optional<int32_t> fold_substituted_call(const FunctionDecl &fn, const Expr::Call &call) const;
    // The `builtin.memeql` or `builtin.memcmp` declaration this call names.
    const FunctionDecl *mem_compare_decl(const Expr::Call &call) const;
    // A constant float or integer. `wide` is an f64 operation. Integers leave `is_float` clear.
    struct FoldedNumber {
        double value = 0;
        bool is_float = false;
        bool wide = false;
    };
    std::optional<FoldedNumber> fold_number(const Expr &expr) const;
    std::optional<FoldedNumber> fold_number(const Expr &expr, std::unordered_set<std::string> &seen) const;
    void emit_function(const FunctionDecl &fn);
    void emit_local_function(const FunctionDecl &fn);
    void emit_nested_body(const FunctionDecl &fn, llvm::Function *function, int depth, std::vector<Binding> captures);
    TypedValue emit_anon_function(const FunctionDecl &fn);
    llvm::FunctionType *function_type_with_captures(const FunctionDecl &fn) const;
    std::vector<Binding> capture_bindings(const FunctionDecl &fn) const;
    void alias_capture(const std::string &name, const Binding &binding);
    void append_captures(const FunctionDecl *decl, std::vector<llvm::Value *> &args, SourceLoc loc);
    // Logical arguments, receiver first, packed into the C struct ABI already stored for `function`.
    std::vector<llvm::Value *> pack_extern_args(llvm::Function *function, const ExternLayout &layout,
                                                const std::vector<TypedValue> &sources, PreparedCall &prepared);
    PreparedCall prepare_call(const Expr::Call &node);
    TypedValue emit_prepared(const PreparedCall &call);
    // A call whose one result is a function pointer keeps that pointer's type on the value.
    void attach_returned_fn(TypedValue &value, const FunctionDecl *decl) const;
    llvm::AllocaInst *defer_flag();
    void emit_one_defer(const DeferredCall &call);
    void emit_defer_frame(DeferFrame &frame);
    void emit_defers_from(int min_loop_index);
    void finish_function();
    void emit_stmt(const Stmt &stmt);
    void emit_for_each(const Stmt::For &node);
    void emit_bind(const Stmt::Bind &node);
    // A missing local initializer. One store per leaf, so a later store or an unread leaf can be deleted.
    void zero_slot(llvm::Value *slot, llvm::Type *ty);
    void zero_leaves(llvm::Value *slot, llvm::Type *ty);
    void zero_bytes(llvm::Value *slot, uint64_t offset, uint64_t size);
    void collect_cases(const Stmt &stmt, std::vector<CaseArm> &arms) const;
    void emit_case(const Stmt::Case &node);
    void emit_switch(const Stmt::Switch &node);
    TypedValue emit_rvalue(const Expr &expr);
    TypedValue emit_new(const Expr::New &node);
    // Stores one element of a `new [n]T` buffer. `index` is an i32.
    void store_new_element(llvm::Value *raw, llvm::Value *index, const Lowered &low, const TypedValue &value,
                           SourceLoc loc);
    void fill_new_buffer(llvm::Value *raw, llvm::Value *count, const Lowered &low, const Expr::Init &init, bool repeat);
    void emit_delete(const Expr::Delete &node);
    llvm::Value *allocator_receiver(const Expr &allocator, bool is_pointer);
    llvm::Function *allocator_method(uint32_t file, const std::string &type_name, const std::string &name);
    llvm::Value *call_malloc(const Expr::New &node, uint64_t size, uint64_t align);
    // `bytes` is an i64. `receiver` is null for `builtin.allocator`.
    llvm::Value *heap_alloc(const Expr::New &node, llvm::Value *receiver, llvm::Value *bytes, uint64_t align);
    void call_free(const Expr::Delete &node, llvm::Value *pointer, llvm::Value *receiver);
    void abort_now();
    llvm::Function *panic_function();
    void abort_out_of_memory(SourceLoc loc);
    // `builtin.panic(message)`, then unreachable. The frame names `loc`.
    void abort_panic(SourceLoc loc, const char *message);
    // `base[low:high]`. The result is a slice header.
    TypedValue emit_range(const Expr::Index &node);
    // Load one field. A slice field is the header. An array field decays to a pointer.
    TypedValue load_field(const TypedValue &addr);
    // Allocates `[_]T{...}` and stores each value. `low` is the array type.
    llvm::AllocaInst *emit_array_literal(const Expr::Init &init, Lowered &low);
    // The array in `value`. A pointer is the address of that array, the same shape a name has.
    llvm::Value *array_bits(llvm::Value *value, llvm::Type *array_ty, SourceLoc loc);
    // Stores those bits into `slot`. The slot is not cleared first.
    void store_array_bits(llvm::Value *slot, llvm::Type *array_ty, llvm::Value *value, SourceLoc loc);
    // Named elements of a brace list. `0...` zeros this array first. A short list does not.
    void store_array_list(llvm::Value *slot, llvm::ArrayType *array_ty, const Expr::Init &init);
    // A brace list stores its elements. Any other expression copies that array into `slot`.
    void store_array(llvm::Value *slot, llvm::ArrayType *array_ty, const Expr &expr, SourceLoc loc);
    // Writes `init` into `slot`. A nested array is another brace list, not one rvalue.
    void store_array_elements(llvm::Value *slot, llvm::ArrayType *array_ty, const Expr::Init &init);
    TypedValue emit_init(const Expr::Init &init);
    // `{.value = 10}` builds the struct when sema first numbers it. Later structs keep that number.
    TypedValue emit_inferred_init(const Expr::Init &init);
    // Writes the named fields of `init` through `slot`. Omitted fields are left as they are.
    void store_init_fields(int struct_id, llvm::Value *slot, const Expr::Init &init);
    TypedValue emit_addr(const Expr &expr);
    // Address of `base[subscript]`. `base` is the rvalue of the collection.
    TypedValue index_address(const TypedValue &base, llvm::Value *subscript, SourceLoc loc);
    IterSlot iter_slot(const TypedValue &arg) const;
    bool emit_iter_call(const Expr::Call &node, const Expr::Selector &selector, PreparedCall &prepared);
    llvm::Type *shape_type(const Shape &shape) const;
    Shape decay_shape(Shape shape) const;
    Shape shape_from_binding(const Binding &binding) const;
    Shape raw_shape(const Expr &expr) const;
    llvm::Value *emit_short_circuit(const Expr::Binary &expr, bool is_and);
};

} // namespace tlc
