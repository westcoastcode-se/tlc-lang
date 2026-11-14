#pragma once

#include "ast/ast.hpp"
#include "diag/diag.hpp"
#include "pkg/pkg.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tlc {

enum class TypeKind {
    Invalid, Void, Int, Char, Bool, Float, Nullptr, Pointer, Array, Struct, Enum, Function, Trait, Tuple, Meta
};

struct Type {
    TypeKind kind = TypeKind::Invalid;
    std::shared_ptr<Type> result;
    std::shared_ptr<Type> pointee;
    std::shared_ptr<Type> element;
    int length = 0;
    int struct_id = -1;
    int enum_id = -1;
    int trait_id = -1;
    bool variadic = false;
    std::vector<Type> params;
    std::vector<Type> results;
    int rank = 3;
    bool is_unsigned = false;
    bool plain_char = false;
    // `?*T` may be null. `*T` has a value.
    bool optional = false;
    // The value is constant. On a pointee, `*const T` cannot be stored through.
    bool is_const = false;
    // `type Name T` is a distinct type with the same representation as T. Zero means T itself.
    int alias_id = 0;
    // A parameter `[N]T` decays to a pointer. This is that `N`, including 0.
    // It is not part of the type. Where `[]T` is expected, the pointer becomes that slice.
    // When `[N]T` is expected, the pointer is that array and the elements are copied.
    // `decayed_array` is clear on an ordinary pointer, whose `decayed_len` is also 0.
    bool decayed_array = false;
    int decayed_len = 0;
    // A function type from an `extern(abi="c")` signature or field. Calls through it use the C convention.
    // A tlc function type is a different type.
    bool c_abi = false;
};

inline bool is_decayed_array(const Type &type) {
    return type.kind == TypeKind::Pointer && type.decayed_array;
}

Type type_invalid();
Type type_void();
Type type_meta();
Type type_int();
Type type_char();
Type type_bool();
Type type_nullptr();
Type type_pointer(Type pointee);
Type type_array(Type element, int length);
Type type_struct(int id);
Type type_enum(int id);
Type type_trait(int id);
Type type_function(Type result, std::vector<Type> params, bool variadic = false);
Type type_tuple(std::vector<Type> results);
Type type_rank(int rank, bool is_unsigned);
Type type_float(int bits);
bool uses_many_results(const Type &type);
bool mentions_trait(const Type &type);
bool same_type(const Type &lhs, const Type &rhs);
// Marks every function type in `type`, through pointers, arrays, and signatures, as a C function type.
void mark_c_abi(Type &type);
// True when the C and the tlc convention pass this function type the same way.
bool c_same_convention(const Type &signature);
// `got` may add `const` and still match `want`. `[]i32` matches `[]const i32`.
bool allows_added_const(const Type &got, const Type &want);
// Parameter lists used to tell two functions of one name apart. A method skips its receiver.
bool same_selection(const Type &lhs, bool skip_lhs, const Type &rhs, bool skip_rhs);
// `comptime n i32` and `n i32` are different lists. A missing declaration matches only a list with no `comptime`.
bool same_comptime_list(const FunctionDecl *lhs, bool skip_lhs, const FunctionDecl *rhs, bool skip_rhs);
// `[]T`: an array whose length is unknown. `[]const T` does not store an element.
bool is_slice(const Type &type);
// `[N]T`, a parameter `[N]T`, and `*[N]T` convert to `[]T`.
// A tuple does not, including `[N][M]Duo`. `const` may be added on the element. It may not be dropped.
// Two slices match only when they are the same type, apart from that added `const`.
bool accepts_slice(const Type &got, const Type &want);

void analyze(const TranslationUnit &unit, DiagnosticEngine &diags);
// `default_allocator` is the type bare `new` and `delete` use. Empty keeps `Malloc`.
void analyze(const PackageGraph &graph, DiagnosticEngine &diags, std::string_view default_allocator = {});

} // namespace tlc
