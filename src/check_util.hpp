#pragma once

#include "sema.hpp"

#include <string>
#include <utility>

namespace tlc {

inline bool fill_builtin(const std::string &name, Type &out) {
    if (name == "int") {
        out = type_int();
        return true;
    }
    if (name == "char") {
        out = type_char();
        return true;
    }
    if (name == "short") {
        out = type_rank(2, false);
        return true;
    }
    if (name == "uint" || name == "u32") {
        out = type_rank(3, true);
        return true;
    }
    if (name == "long") {
        out = type_rank(4, false);
        return true;
    }
    if (name == "ulong") {
        out = type_rank(4, true);
        return true;
    }
    if (name == "u64") {
        out = type_rank(5, true);
        return true;
    }
    if (name == "i8") {
        out = type_rank(1, false);
        return true;
    }
    if (name == "u8") {
        out = type_rank(1, true);
        return true;
    }
    if (name == "i16") {
        out = type_rank(2, false);
        return true;
    }
    if (name == "u16") {
        out = type_rank(2, true);
        return true;
    }
    if (name == "i32") {
        out = type_int();
        return true;
    }
    if (name == "i64") {
        out = type_rank(5, false);
        return true;
    }
    if (name == "bool") {
        out = type_bool();
        return true;
    }
    if (name == "float" || name == "f32") {
        out = type_float(32);
        return true;
    }
    if (name == "double" || name == "f64") {
        out = type_float(64);
        return true;
    }
    if (name == "void") {
        out = type_void();
        return true;
    }
    if (name == "Type") {
        out = type_meta();
        return true;
    }
    return false;
}

inline Type quiet_type(const std::string &name) {
    Type type;
    if (fill_builtin(name, type)) {
        return type;
    }
    return type_invalid();
}

inline bool is_integer(const Type &type) { return type.kind == TypeKind::Int || type.kind == TypeKind::Char; }

inline bool is_float(const Type &type) { return type.kind == TypeKind::Float; }

inline bool is_scalar(const Type &type) { return is_integer(type) || type.kind == TypeKind::Bool; }

inline bool is_numeric(const Type &type) { return is_scalar(type) || is_float(type); }

inline int rank_of(const Type &type) {
    if (type.kind == TypeKind::Bool) {
        return 0;
    }
    if (type.kind == TypeKind::Char) {
        return 1;
    }
    if (type.kind == TypeKind::Int) {
        return type.rank == 0 ? 3 : type.rank;
    }
    return -1;
}

inline int width_of(const Type &type) {
    switch (rank_of(type)) {
    case 0: return 1;
    case 1: return 8;
    case 2: return 16;
    case 3: return 32;
    case 4: return 64;
    case 5: return 64;
    default: return 0;
    }
}

inline Type promote(const Type &type) {
    if (type.kind == TypeKind::Bool || type.kind == TypeKind::Char) {
        return type_int();
    }
    if (type.kind == TypeKind::Int && rank_of(type) < 3) {
        return type_int();
    }
    return type;
}

inline Type usual(const Type &lhs, const Type &rhs) {
    if (lhs.kind == TypeKind::Invalid) {
        return lhs;
    }
    if (rhs.kind == TypeKind::Invalid) {
        return rhs;
    }
    if (!is_integer(lhs) || !is_integer(rhs)) {
        return type_int();
    }
    if (same_type(lhs, rhs)) {
        return lhs;
    }
    const int left_rank = rank_of(lhs);
    const int right_rank = rank_of(rhs);
    if (lhs.is_unsigned == rhs.is_unsigned) {
        return type_rank(left_rank > right_rank ? left_rank : right_rank, lhs.is_unsigned);
    }
    const Type &signed_type = lhs.is_unsigned ? rhs : lhs;
    const Type &unsigned_type = lhs.is_unsigned ? lhs : rhs;
    const int signed_rank = rank_of(signed_type);
    const int unsigned_rank = rank_of(unsigned_type);
    if (unsigned_rank >= signed_rank) {
        return type_rank(unsigned_rank, true);
    }
    if (width_of(signed_type) > width_of(unsigned_type)) {
        return type_rank(signed_rank, false);
    }
    return type_rank(signed_rank, true);
}

inline Type quiet_indirect(Type type, int count, uint32_t optional = 0, uint32_t const_pointee = 0) {
    for (int i = 0; i < count; ++i) {
        if (type.kind != TypeKind::Int && type.kind != TypeKind::Char && type.kind != TypeKind::Bool &&
            type.kind != TypeKind::Pointer && type.kind != TypeKind::Void && type.kind != TypeKind::Meta) {
            return type_invalid();
        }
        const int level = count - 1 - i;
        if ((const_pointee & (1u << level)) != 0) {
            type.is_const = true;
        }
        type = type_pointer(std::move(type));
        type.optional = (optional & (1u << level)) != 0;
    }
    return type;
}

inline Type quiet_object(const std::string &name, int indirections, bool is_array, int array_len, bool parameter,
                         uint32_t optional = 0, uint32_t const_pointee = 0) {
    Type type = quiet_indirect(quiet_type(name), indirections, optional, const_pointee);
    if (!is_array) {
        return type;
    }
    if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Void || type.kind == TypeKind::Function) {
        return type_invalid();
    }
    if (parameter) {
        return type_pointer(std::move(type));
    }
    if (array_len <= 0) {
        return type_invalid();
    }
    return type_array(std::move(type), array_len);
}

inline bool lvalue_form(const Expr &expr) {
    if (std::holds_alternative<Expr::Name>(expr.data)) {
        return true;
    }
    if (std::holds_alternative<Expr::Index>(expr.data)) {
        return true;
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data)) {
        return unary->op == TokenKind::Star;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        return selector->base &&
               (std::holds_alternative<Expr::Name>(selector->base->data) || lvalue_form(*selector->base));
    }
    return false;
}

} // namespace tlc
