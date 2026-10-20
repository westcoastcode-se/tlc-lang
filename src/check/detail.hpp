#pragma once

#include "check/check.hpp"
#include "check/check_util.hpp"

namespace tlc {

inline Type bare_pointee(Type type) {
    type.alias_id = 0;
    type.is_const = false;
    type.optional = false;
    return type;
}

// `*void` and `*u8`, including aliases. `const` and `?` are ignored by the caller.
inline bool raw_pointee(const Type &type) {
    if (type.kind == TypeKind::Void) {
        return true;
    }
    return type.kind == TypeKind::Char && type.is_unsigned && !type.plain_char && type.rank <= 1;
}

// `"Hello"` is `[]const u8`. The length does not count the terminating null.
inline Type string_slice() {
    Type bytes = type_rank(1, true);
    bytes.is_const = true;
    return type_array(std::move(bytes), -1);
}

// `*const u8`, including `type Text *const u8`. `*u8` does not match.
inline bool const_u8_pointer(const Type &type) {
    if (type.kind != TypeKind::Pointer || type.optional || type.pointee == nullptr || !type.pointee->is_const) {
        return false;
    }
    return raw_pointee(*type.pointee);
}

// A constant string is this slice. C takes the address when it asks for `*const u8`.
inline bool slice_of_const_u8(const Type &type) {
    return is_slice(type) && type.element != nullptr && type.element->is_const && raw_pointee(*type.element);
}

// `[2]const i32` and `[2][2]const i32` store into a constant element. The array name is not a store.
inline bool const_array_elements(const Type &type) {
    const Type *cursor = &type;
    while (cursor->kind == TypeKind::Array && !is_slice(*cursor) && cursor->element != nullptr) {
        if (cursor->is_const) {
            return true;
        }
        cursor = cursor->element.get();
    }
    return cursor->is_const;
}

inline bool primitive_pointee(const Type &type) {
    return type.kind == TypeKind::Bool || type.kind == TypeKind::Int || type.kind == TypeKind::Char ||
           type.kind == TypeKind::Float;
}

inline bool same_primitive(const Type &lhs, const Type &rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }
    if (lhs.kind == TypeKind::Float) {
        return lhs.rank == rhs.rank;
    }
    if (lhs.kind == TypeKind::Bool || lhs.kind == TypeKind::Void) {
        return true;
    }
    return lhs.rank == rhs.rank && lhs.is_unsigned == rhs.is_unsigned && lhs.plain_char == rhs.plain_char;
}

// Both operands are pointers. One pointee level: a pointer pointee is not a primitive or a struct.
inline bool unsafe_pointer_cast(const Type &source, const Type &target) {
    if (source.pointee == nullptr || target.pointee == nullptr) {
        return false;
    }
    const Type from = bare_pointee(*source.pointee);
    const Type to = bare_pointee(*target.pointee);
    if (raw_pointee(from) && raw_pointee(to)) {
        return false;
    }
    if (to.kind == TypeKind::Void) {
        return false;
    }
    if (raw_pointee(from) && to.kind == TypeKind::Struct) {
        return true;
    }
    if (from.kind == TypeKind::Void && primitive_pointee(to) && !raw_pointee(to)) {
        return true;
    }
    if (primitive_pointee(from) && primitive_pointee(to) && !same_primitive(from, to)) {
        return true;
    }
    return false;
}

// `v[i].x` on `[]const Point` still names that const element.
inline bool from_slice_element(const Expr *expr) {
    while (expr != nullptr) {
        if (const auto *index = std::get_if<Expr::Index>(&expr->data)) {
            return index->slice_element;
        }
        if (const auto *selector = std::get_if<Expr::Selector>(&expr->data)) {
            expr = selector->base.get();
            continue;
        }
        break;
    }
    return false;
}

// A parameter `[N]T` is a pointer that still records N. When `[N]T` is expected, that
// pointer is the array. A read of a parameter stays that pointer.
inline bool decayed_array_value(const Type &got, const Type &want) {
    if (want.kind != TypeKind::Array || want.length <= 0 || want.element == nullptr) {
        return false;
    }
    if (got.kind != TypeKind::Pointer || got.optional || got.pointee == nullptr || got.decayed_len != want.length) {
        return false;
    }
    return same_type(*got.pointee, *want.element);
}

} // namespace tlc
