// Types and the checker entry points.
#include "check.hpp"
#include "check_util.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tlc {

Type type_invalid() { return Type{}; }

Type type_void() {
    Type type;
    type.kind = TypeKind::Void;
    return type;
}

Type type_meta() {
    Type type;
    type.kind = TypeKind::Meta;
    return type;
}

Type type_int() {
    Type type;
    type.kind = TypeKind::Int;
    type.rank = 3;
    return type;
}

Type type_char() {
    Type type;
    type.kind = TypeKind::Char;
    type.rank = 1;
    type.plain_char = true;
    return type;
}

Type type_rank(int rank, bool is_unsigned) {
    Type type;
    type.kind = rank <= 1 ? TypeKind::Char : TypeKind::Int;
    type.rank = rank;
    type.is_unsigned = is_unsigned;
    type.plain_char = false;
    return type;
}

Type type_bool() {
    Type type;
    type.kind = TypeKind::Bool;
    return type;
}

Type type_float(int bits) {
    Type type;
    type.kind = TypeKind::Float;
    type.rank = bits >= 64 ? 64 : 32;
    return type;
}

Type type_nullptr() {
    Type type;
    type.kind = TypeKind::Nullptr;
    return type;
}

Type type_pointer(Type pointee) {
    Type type;
    type.kind = TypeKind::Pointer;
    type.pointee = std::make_shared<Type>(std::move(pointee));
    return type;
}

Type type_array(Type element, int length) {
    Type type;
    type.kind = TypeKind::Array;
    type.element = std::make_shared<Type>(std::move(element));
    type.length = length;
    return type;
}

Type type_struct(int id) {
    Type type;
    type.kind = TypeKind::Struct;
    type.struct_id = id;
    return type;
}

Type type_enum(int id) {
    Type type;
    type.kind = TypeKind::Enum;
    type.enum_id = id;
    return type;
}

Type type_trait(int id) {
    Type type;
    type.kind = TypeKind::Trait;
    type.trait_id = id;
    return type;
}

bool uses_many_results(const Type &type) {
    if (type.kind == TypeKind::Pointer && type.pointee != nullptr) {
        return uses_many_results(*type.pointee);
    }
    if (type.kind == TypeKind::Function) {
        if (type.result != nullptr && type.result->kind == TypeKind::Tuple) {
            return true;
        }
        for (const Type &param : type.params) {
            if (uses_many_results(param)) {
                return true;
            }
        }
        return false;
    }
    if (type.kind == TypeKind::Tuple) {
        for (const Type &part : type.results) {
            if (uses_many_results(part)) {
                return true;
            }
        }
    }
    return false;
}

bool mentions_trait(const Type &type) {
    if (type.kind == TypeKind::Trait) {
        return true;
    }
    if (type.kind == TypeKind::Pointer && type.pointee && type.pointee->kind == TypeKind::Trait) {
        return true;
    }
    if (type.kind == TypeKind::Tuple) {
        for (const Type &part : type.results) {
            if (mentions_trait(part)) {
                return true;
            }
        }
    }
    return false;
}

Type type_function(Type result, std::vector<Type> params, bool variadic) {
    Type type;
    type.kind = TypeKind::Function;
    type.result = std::make_shared<Type>(std::move(result));
    type.params = std::move(params);
    type.variadic = variadic;
    return type;
}

Type type_tuple(std::vector<Type> results) {
    Type type;
    type.kind = TypeKind::Tuple;
    type.results = std::move(results);
    return type;
}

bool same_type(const Type &lhs, const Type &rhs) {
    if (lhs.alias_id != rhs.alias_id) {
        return false;
    }
    if (lhs.kind != rhs.kind || lhs.is_const != rhs.is_const) {
        return false;
    }
    if (lhs.kind == TypeKind::Pointer) {
        return lhs.optional == rhs.optional && lhs.pointee && rhs.pointee && same_type(*lhs.pointee, *rhs.pointee);
    }
    if (lhs.kind == TypeKind::Array) {
        return lhs.length == rhs.length && lhs.element && rhs.element && same_type(*lhs.element, *rhs.element);
    }
    if (lhs.kind == TypeKind::Struct) {
        return lhs.struct_id >= 0 && lhs.struct_id == rhs.struct_id;
    }
    if (lhs.kind == TypeKind::Enum) {
        return lhs.enum_id >= 0 && lhs.enum_id == rhs.enum_id;
    }
    if (lhs.kind == TypeKind::Trait) {
        return lhs.trait_id >= 0 && lhs.trait_id == rhs.trait_id;
    }
    if (lhs.kind == TypeKind::Int || lhs.kind == TypeKind::Char) {
        return lhs.rank == rhs.rank && lhs.is_unsigned == rhs.is_unsigned && lhs.plain_char == rhs.plain_char;
    }
    if (lhs.kind == TypeKind::Float) {
        return lhs.rank == rhs.rank;
    }
    if (lhs.kind == TypeKind::Tuple) {
        if (lhs.results.size() != rhs.results.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.results.size(); ++i) {
            if (!same_type(lhs.results[i], rhs.results[i])) {
                return false;
            }
        }
        return true;
    }
    if (lhs.kind != TypeKind::Function) {
        return true;
    }
    if (lhs.params.size() != rhs.params.size()) {
        return false;
    }
    if (!lhs.result || !rhs.result || !same_type(*lhs.result, *rhs.result)) {
        return false;
    }
    if (lhs.variadic != rhs.variadic) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.params.size(); ++i) {
        if (!same_type(lhs.params[i], rhs.params[i])) {
            return false;
        }
    }
    return true;
}

bool is_slice(const Type &type) {
    return type.kind == TypeKind::Array && type.length < 0 && type.element != nullptr;
}

bool accepts_slice(const Type &got, const Type &want) {
    if (!is_slice(want) || got.kind != TypeKind::Array || got.element == nullptr || want.element == nullptr) {
        return false;
    }
    if (got.alias_id != 0 && got.alias_id != want.alias_id) {
        return false;
    }
    if (!same_type(*got.element, *want.element)) {
        return false;
    }
    if (is_slice(got)) {
        // `[]T` converts to `type Name []T`. The alias does not convert back.
        if (got.alias_id == 0 && want.alias_id != 0) {
            return true;
        }
        return same_type(got, want);
    }
    return got.length >= 0;
}

void analyze(const TranslationUnit &unit, DiagnosticEngine &diags) {
    Checker checker(diags);
    checker.analyze(unit);
}

void analyze(const PackageGraph &graph, DiagnosticEngine &diags) {
    Checker checker(diags);
    checker.analyze_graph(graph);
}

} // namespace tlc
