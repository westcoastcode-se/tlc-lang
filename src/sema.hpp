#pragma once

#include "ast.hpp"
#include "diag.hpp"
#include "pkg.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tlc {

enum class TypeKind { Invalid, Void, Int, Char, Bool, Float, Nullptr, Pointer, Array, Struct, Enum, Function, Trait, Tuple };

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
};

Type type_invalid();
Type type_void();
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

void analyze(const TranslationUnit &unit, DiagnosticEngine &diags);
void analyze(const PackageGraph &graph, DiagnosticEngine &diags);

} // namespace tlc
