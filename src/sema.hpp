#pragma once

#include "ast.hpp"
#include "diag.hpp"
#include "pkg.hpp"

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
};

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
// `[]T`: an array whose length is unknown. It is a readonly view.
bool is_view(const Type &type);
// `[N]T` converts to `[]T` of the same element. Two views match only when they are the same type.
bool accepts_view(const Type &got, const Type &want);

void analyze(const TranslationUnit &unit, DiagnosticEngine &diags);
void analyze(const PackageGraph &graph, DiagnosticEngine &diags);

} // namespace tlc
