#pragma once

#include "diag/diag.hpp"
#include "pkg/pkg.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tlc {

// What LLVM runs after the module is built.
// Default matches the reference: a dead store is removed, and nothing else runs.
enum class OptLevel {
    Default,
    // `-O0`. Every store stays.
    None,
    // `-O` and `-O1`.
    O1,
    O2,
    O3,
};

struct CodegenOutput {
    bool ok = false;
    std::string ir;
    std::string error;
};

CodegenOutput codegen(const PackageGraph &graph, DiagnosticEngine &diags, const std::string &object_path,
                      bool testing, bool debug_info, OptLevel opt_level);

// Compile `fn` in `graph` and call it. `fn` returns `i32` and takes no parameters.
// `file` is the file whose names the body resolves. Nullopt when emission or the call fails.
std::optional<int32_t> eval_comptime_i32(const PackageGraph &graph, const FunctionDecl &fn, uint32_t file);

// One slot of a struct a comptime func returned. Offsets are relative to that slot.
// Plain data: the module that measured them is already destroyed.
struct ComptimeSlot {
    enum class Kind { Int, Bool, F32, F64, Enum, Struct, Array, SliceU8, Func, Pointer, Reject };
    Kind kind = Kind::Reject;
    // Field name when this slot is a struct member. Empty on the struct itself and on an element.
    std::string name;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t stride = 0;
    uint64_t count = 0;
    // Byte offset of the `i32` length inside a `SliceU8`, relative to that slice.
    uint64_t len_offset = 0;
    bool is_unsigned = false;
    int bits = 0;
    std::string type_name;
    // Source order. The first member with the stored value is the one the literal names.
    std::vector<std::pair<std::string, int64_t>> enumerators;
    std::vector<ComptimeSlot> children;
};

struct ComptimeStructImage {
    bool not_constant = false;
    bool little = true;
    uint64_t size = 0;
    uint64_t align = 1;
    ComptimeSlot root;
    // LLVM symbol, then the source name. The address is filled in after the call.
    std::vector<std::pair<std::string, std::string>> functions;
};

// What running a struct-returning comptime func produced.
struct ComptimeStructValue {
    enum class Status { Unsupported, NotConstant, Value };
    Status status = Status::Unsupported;
    std::unique_ptr<Expr> value;
};

// Compile `fn` and call it. `fn` returns a struct and takes no parameters.
// The literal is the result. `NotConstant` is a field that is not a constant.
ComptimeStructValue eval_comptime_struct(const PackageGraph &graph, const FunctionDecl &fn, uint32_t file);

} // namespace tlc
