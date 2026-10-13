// Type checking expressions, statements, and function bodies.
#include "check.hpp"
#include "check_util.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tlc {

void Checker::mismatch(SourceLoc loc, const Type &want, const Type &got) {
    error(loc, "type mismatch: expected " + type_desc(want) + ", found " + type_desc(got));
}

void Checker::expect(const Type &got, const Type &want, SourceLoc loc) {
    if (got.kind == TypeKind::Invalid || want.kind == TypeKind::Invalid) {
        return;
    }
    if (!same_type(got, want)) {
        mismatch(loc, want, got);
    }
}

void Checker::expect_value(const Type &got, const Type &want, const Expr &, SourceLoc loc) {
    if (want.kind == TypeKind::Trait && got.kind != TypeKind::Invalid) {
        if (got.kind == TypeKind::Trait) {
            expect(got, want, loc);
            return;
        }
        if (got.kind == TypeKind::Struct) {
            require_satisfies(got.struct_id, want.trait_id, false, loc);
            return;
        }
        mismatch(loc, want, got);
        return;
    }
    if (want.kind == TypeKind::Pointer && want.pointee && want.pointee->kind == TypeKind::Trait &&
        got.kind != TypeKind::Invalid) {
        if (got.kind == TypeKind::Pointer && got.optional && !want.optional) {
            mismatch(loc, want, got);
            return;
        }
        if (got.kind == TypeKind::Pointer && got.pointee && got.pointee->kind == TypeKind::Trait) {
            expect(*got.pointee, *want.pointee, loc);
            return;
        }
        if (got.kind == TypeKind::Pointer && got.pointee && got.pointee->kind == TypeKind::Struct) {
            require_satisfies(got.pointee->struct_id, want.pointee->trait_id, true, loc);
            return;
        }
        mismatch(loc, want, got);
        return;
    }
    if (want.kind == TypeKind::Pointer && got.kind == TypeKind::Nullptr) {
        if (!want.optional) {
            mismatch(loc, want, got);
        }
        return;
    }
    if (accepts_pointer(got, want)) {
        return;
    }
    if (is_c_enum(got) && is_integer(want)) {
        return;
    }
    if (is_numeric(got) && is_numeric(want)) {
        return;
    }
    expect(got, want, loc);
}

void Checker::expect_integer(const Type &got, SourceLoc loc) {
    if (got.kind == TypeKind::Invalid || is_integer(got) || is_c_enum(got)) {
        return;
    }
    error(loc, "type mismatch: expected an integer, found " + type_desc(got));
}

void Checker::expect_condition(const Type &got, SourceLoc loc) {
    if (got.kind == TypeKind::Pointer) {
        error(loc, "compare with null");
        return;
    }
    expect_integer(got, loc);
}

FunctionDecl *Checker::inferred_function(const Expr &expr) const {
    const auto *node = std::get_if<Expr::Func>(&expr.data);
    if (node == nullptr || node->decl == nullptr || !node->decl->inferred || node->decl->elaborated) {
        return nullptr;
    }
    return const_cast<FunctionDecl *>(node->decl.get());
}

namespace {

struct Spelling {
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
};

FnParam fn_param_from_spelling(const Spelling &spelling) {
    FnParam param;
    param.type_name = spelling.type_name;
    param.type_package = spelling.type_package;
    param.is_struct = spelling.is_struct;
    param.is_enum = spelling.is_enum;
    param.indirections = spelling.indirections;
    param.optional = spelling.optional;
    param.const_pointee = spelling.const_pointee;
    param.is_fn_ptr = spelling.is_fn_ptr;
    param.ret_indirections = spelling.ret_indirections;
    param.ret_optional = spelling.ret_optional;
    param.ret_const_pointee = spelling.ret_const_pointee;
    param.params = spelling.fn_params;
    param.variadic = spelling.fn_variadic;
    param.results = spelling.fn_results;
    return param;
}

void apply_spelling(Param &param, const Spelling &spelling) {
    param.type_name = spelling.type_name;
    param.type_package = spelling.type_package;
    param.is_struct = spelling.is_struct;
    param.is_enum = spelling.is_enum;
    param.indirections = spelling.indirections;
    param.optional = spelling.optional;
    param.const_pointee = spelling.const_pointee;
    param.is_fn_ptr = spelling.is_fn_ptr;
    param.ret_indirections = spelling.ret_indirections;
    param.ret_optional = spelling.ret_optional;
    param.ret_const_pointee = spelling.ret_const_pointee;
    param.fn_params = spelling.fn_params;
    param.fn_variadic = spelling.fn_variadic;
    param.fn_results = spelling.fn_results;
}

ResultSlot slot_from_spelling(const Spelling &spelling) {
    ResultSlot slot;
    slot.type_name = spelling.type_name;
    slot.type_package = spelling.type_package;
    slot.is_struct = spelling.is_struct;
    slot.is_enum = spelling.is_enum;
    slot.indirections = spelling.indirections;
    slot.optional = spelling.optional;
    slot.const_pointee = spelling.const_pointee;
    slot.is_fn_ptr = spelling.is_fn_ptr;
    slot.ret_indirections = spelling.ret_indirections;
    slot.ret_optional = spelling.ret_optional;
    slot.ret_const_pointee = spelling.ret_const_pointee;
    slot.fn_params = spelling.fn_params;
    slot.fn_variadic = spelling.fn_variadic;
    slot.fn_results = spelling.fn_results;
    return slot;
}

} // namespace

Type Checker::elaborate_inferred(FunctionDecl &fn, const Type &want) {
    const Type *signature = nullptr;
    if (want.kind == TypeKind::Pointer && want.pointee != nullptr && want.pointee->kind == TypeKind::Function) {
        signature = want.pointee.get();
    } else if (want.kind == TypeKind::Function) {
        signature = &want;
    }
    if (signature == nullptr || signature->result == nullptr || signature->variadic ||
        signature->params.size() != fn.params.size()) {
        error(fn.loc, signature != nullptr && signature->variadic ? "not supported yet" : "type mismatch");
        return type_invalid();
    }
    const auto spell = [&](const auto &self, const Type &type) -> Spelling {
        if (type.kind == TypeKind::Pointer && type.pointee != nullptr) {
            if (type.pointee->kind == TypeKind::Function) {
                const Type &target = *type.pointee;
                Spelling spelled;
                spelled.is_fn_ptr = true;
                spelled.indirections = 1;
                spelled.optional = type.optional ? 1u : 0u;
                spelled.fn_variadic = target.variadic;
                if (target.result != nullptr) {
                    if (target.result->kind == TypeKind::Tuple) {
                        for (const Type &part : target.result->results) {
                            spelled.fn_results.push_back(fn_param_from_spelling(self(self, part)));
                        }
                        if (!spelled.fn_results.empty()) {
                            const FnParam &first = spelled.fn_results.front();
                            spelled.type_name = first.type_name;
                            spelled.type_package = first.type_package;
                            spelled.is_struct = first.is_struct;
                            spelled.is_enum = first.is_enum;
                            spelled.ret_indirections = first.indirections;
                            spelled.ret_optional = first.optional;
                            spelled.ret_const_pointee = first.const_pointee;
                        }
                    } else {
                        const Spelling result = self(self, *target.result);
                        if (result.is_fn_ptr) {
                            spelled.type_name = "invalid";
                        } else {
                            spelled.type_name = result.type_name;
                            spelled.type_package = result.type_package;
                            spelled.is_struct = result.is_struct;
                            spelled.is_enum = result.is_enum;
                            spelled.ret_indirections = result.indirections;
                            spelled.ret_optional = result.optional;
                            spelled.ret_const_pointee = result.const_pointee;
                        }
                    }
                }
                for (const Type &param : target.params) {
                    spelled.fn_params.push_back(fn_param_from_spelling(self(self, param)));
                }
                return spelled;
            }
            Spelling inner = self(self, *type.pointee);
            if (!inner.is_fn_ptr) {
                inner.const_pointee <<= 1;
                if (type.pointee->is_const) {
                    inner.const_pointee |= 1u;
                }
                inner.optional <<= 1;
                if (type.optional) {
                    inner.optional |= 1u;
                }
                inner.indirections += 1;
            }
            return inner;
        }
        Spelling spelled;
        if (type.kind == TypeKind::Struct && type.struct_id >= 0 &&
            type.struct_id < static_cast<int>(structs_.size())) {
            spelled.type_name = structs_[static_cast<std::size_t>(type.struct_id)].name;
            return spelled;
        }
        if (type.kind == TypeKind::Enum && type.enum_id >= 0 && type.enum_id < static_cast<int>(enum_types_.size())) {
            spelled.type_name = enum_types_[static_cast<std::size_t>(type.enum_id)].name;
            return spelled;
        }
        spelled.type_name = type_desc(type);
        return spelled;
    };
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        apply_spelling(fn.params[i], spell(spell, signature->params[i]));
    }
    fn.results.clear();
    if (signature->result->kind == TypeKind::Tuple) {
        for (const Type &part : signature->result->results) {
            fn.results.push_back(slot_from_spelling(spell(spell, part)));
        }
    } else {
        fn.results.push_back(slot_from_spelling(spell(spell, *signature->result)));
    }
    if (!fn.results.empty()) {
        const ResultSlot &first = fn.results.front();
        fn.return_type = first.type_name;
        fn.type_package = first.type_package;
        fn.is_struct = first.is_struct;
        fn.is_enum = first.is_enum;
        fn.indirections = first.is_fn_ptr ? 0 : first.indirections;
        fn.optional = first.is_fn_ptr ? 0 : first.optional;
        fn.const_pointee = first.is_fn_ptr ? 0 : first.const_pointee;
    }
    fn.elaborated = true;
    bool generic = false;
    for (const Param &param : fn.params) {
        if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
            package_trait(file_id_, param.type_name) >= 0) {
            generic = true;
        }
    }
    Type concrete = function_signature(fn);
    if (concrete.result != nullptr && mentions_trait(*concrete.result)) {
        generic = true;
    }
    if (generic) {
        error(fn.loc, "not supported yet");
    }
    if (fn.body != nullptr) {
        check_function(fn);
    }
    return type_pointer(std::move(concrete));
}

Type Checker::check_init(Expr::Init &init, const Type &want) {
    if (!init.elements.empty()) {
        auto check_elements = [&]() {
            for (const auto &element : init.elements) {
                if (element) {
                    check_expr(*element, true);
                }
            }
        };
        if (!init.fields.empty()) {
            error(init.loc, "type mismatch");
            check_elements();
            return type_invalid();
        }
        if (init.is_tuple) {
            if (want.kind != TypeKind::Tuple) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            if (init.elements.size() != want.results.size()) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            for (std::size_t i = 0; i < init.elements.size(); ++i) {
                if (init.elements[i]) {
                    check_against(*init.elements[i], want.results[i]);
                }
            }
            return want;
        }
        if (want.kind == TypeKind::Array && want.element != nullptr) {
            if (static_cast<int>(init.elements.size()) != want.length) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            for (const auto &element : init.elements) {
                if (element) {
                    check_against(*element, *want.element);
                }
            }
            return want;
        }
        if (want.kind == TypeKind::Struct) {
            error(init.loc, "type mismatch");
            check_elements();
            return type_invalid();
        }
        if (init.elements.size() == 1 && init.elements.front() && want.kind != TypeKind::Invalid &&
            want.kind != TypeKind::Void) {
            return check_against(*init.elements.front(), want);
        }
        error(init.loc, "type mismatch");
        check_elements();
        return type_invalid();
    }
    auto check_values = [&]() {
        for (const Expr::FieldInit &field : init.fields) {
            if (field.value) {
                check_expr(*field.value, true);
            }
        }
    };
    if (want.kind != TypeKind::Struct || want.struct_id < 0 ||
        want.struct_id >= static_cast<int>(structs_.size())) {
        error(init.loc, "type mismatch");
        check_values();
        return type_invalid();
    }
    if (!structs_[static_cast<std::size_t>(want.struct_id)].complete) {
        error(init.loc, "not supported yet");
        check_values();
        return type_invalid();
    }
    init.struct_id = want.struct_id;
    std::unordered_set<std::string> seen;
    for (const Expr::FieldInit &field : init.fields) {
        if (field.field.empty()) {
            if (field.value) {
                check_expr(*field.value, true);
            }
            continue;
        }
        if (!seen.insert(field.field).second) {
            error(field.loc, "redefinition");
        }
        Type field_type;
        if (!lookup_field(want, field.field, field_type)) {
            error(field.loc, "undeclared identifier");
            if (field.value) {
                check_expr(*field.value, true);
            }
            continue;
        }
        if (field_type.kind == TypeKind::Array) {
            error(field.loc, "not supported yet");
            if (field.value) {
                check_expr(*field.value, true);
            }
            continue;
        }
        if (field.value) {
            check_against(*field.value, field_type);
        }
    }
    const StructInfo &info = structs_[static_cast<std::size_t>(want.struct_id)];
    if (!info.is_union) {
        for (const StructInfo::Field &field : info.fields) {
            if (seen.contains(field.name)) {
                continue;
            }
            if (needs_nonnull_pointer(field.type)) {
                error(init.loc, "type mismatch");
            }
        }
    }
    return want;
}

Type Checker::check_against(const Expr &expr, const Type &want) {
    if (FunctionDecl *fn = inferred_function(expr)) {
        const Type got = elaborate_inferred(*fn, want);
        if (got.kind != TypeKind::Invalid) {
            expect_value(got, want, expr, expr_loc(expr));
        }
        return got;
    }
    if (const auto *init = std::get_if<Expr::Init>(&expr.data)) {
        const Type got = check_init(const_cast<Expr::Init &>(*init), want);
        if (got.kind != TypeKind::Invalid) {
            expect_value(got, want, expr, expr_loc(expr));
        }
        return got;
    }
    const Type got = check_expr(expr, true);
    expect_value(got, want, expr, expr_loc(expr));
    return got;
}

Type Checker::check_expr(const Expr &expr, bool used) {
    Type type = check_raw(expr, used);
    // A loaded value is a copy. `const` stays on a pointee, not on this temporary.
    type.is_const = false;
    if (type.kind == TypeKind::Array && type.element) {
        return type_pointer(*type.element);
    }
    if (type.kind == TypeKind::Function) {
        return type_pointer(type);
    }
    return promote(type);
}

Type Checker::check_raw(const Expr &expr, bool used) {
    return std::visit(
        [&](const auto &node) -> Type {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                (void)used;
                const Symbol *symbol = lookup(node.text);
                if (!symbol) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                if (captured(*symbol)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (symbol->is_package) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (symbol->is_template) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (symbol->is_function && symbol->name == "va" && is_std_builtin_file(symbol->package_file)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                return symbol->type;
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                const int bits = node.suffix.bits;
                const bool uns = node.suffix.is_unsigned;
                const auto fits = [&](uint64_t limit) {
                    if (node.value > limit) {
                        error(node.loc, "type mismatch");
                        return false;
                    }
                    return true;
                };
                if (bits == 0) {
                    if (!fits(2147483647ULL)) {
                        return type_invalid();
                    }
                    return type_int();
                }
                if (bits == 8) {
                    if (!fits(uns ? 255ULL : 127ULL)) {
                        return type_invalid();
                    }
                    return type_rank(1, uns);
                }
                if (bits == 16) {
                    if (!fits(uns ? 65535ULL : 32767ULL)) {
                        return type_invalid();
                    }
                    return type_rank(2, uns);
                }
                if (bits == 32) {
                    if (!fits(uns ? 4294967295ULL : 2147483647ULL)) {
                        return type_invalid();
                    }
                    return uns ? type_rank(3, true) : type_int();
                }
                if (bits == 64) {
                    if (!uns && !fits(9223372036854775807ULL)) {
                        return type_invalid();
                    }
                    return type_rank(5, uns);
                }
                error(node.loc, "type mismatch");
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                (void)used;
                if (node.value > 255ULL) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                return type_char();
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                (void)used;
                return type_float(node.is_f32 ? 32 : 64);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::Bang && node.postfix) {
                    const Type inner = check_expr(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind != TypeKind::Pointer || !inner.optional || inner.pointee == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    Type asserted = inner;
                    asserted.optional = false;
                    return asserted;
                }
                if (node.op == TokenKind::Star) {
                    const Type inner = check_expr(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind != TypeKind::Pointer || !inner.pointee || inner.optional) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    if (inner.pointee->kind == TypeKind::Void) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return *inner.pointee;
                }
                if (node.op == TokenKind::Amp) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Symbol *symbol = lookup(named->text); symbol != nullptr && symbol->is_constant) {
                            error(node.loc, "not an lvalue");
                            return type_invalid();
                        }
                    }
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.expr->data)) {
                        if (selector->base != nullptr && enum_designator(*selector->base) >= 0) {
                            error(node.loc, "not an lvalue");
                            return type_invalid();
                        }
                        if (selector->base != nullptr) {
                            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                                if (const Symbol *symbol = lookup(base->text); symbol != nullptr && symbol->is_package) {
                                    const SelectorTarget target = resolve_selector(*selector);
                                    if (target.kind == SelectorTarget::Kind::Failed) {
                                        return type_invalid();
                                    }
                                    if (target.kind == SelectorTarget::Kind::Package && target.symbol != nullptr &&
                                        target.symbol->is_constant) {
                                        error(node.loc, "not an lvalue");
                                        return type_invalid();
                                    }
                                }
                            }
                        }
                    }
                    if (!is_lvalue_form(*node.expr)) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.expr, true);
                        return type_invalid();
                    }
                    Type inner = check_raw(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind == TypeKind::Function) {
                        return type_pointer(inner);
                    }
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Symbol *symbol = lookup(named->text); symbol != nullptr && symbol->is_immutable) {
                            inner.is_const = true;
                        }
                    }
                    return type_pointer(inner);
                }
                const Type inner = check_expr(*node.expr, true);
                if (node.op == TokenKind::Bang) {
                    if (is_float(inner)) {
                        return type_int();
                    }
                    expect_integer(inner, node.loc);
                    return inner.kind == TypeKind::Invalid ? inner : type_int();
                }
                if (is_float(inner) && (node.op == TokenKind::Plus || node.op == TokenKind::Minus)) {
                    return inner;
                }
                expect_integer(inner, node.loc);
                if (inner.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (is_c_enum(inner) || is_integer(inner)) {
                    return is_c_enum(inner) ? type_int() : inner;
                }
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                Type lhs = check_expr(*node.lhs, true);
                Type rhs = check_expr(*node.rhs, true);
                if (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) {
                    const bool lhs_ptr = lhs.kind == TypeKind::Pointer || lhs.kind == TypeKind::Nullptr;
                    const bool rhs_ptr = rhs.kind == TypeKind::Pointer || rhs.kind == TypeKind::Nullptr;
                    if (lhs_ptr || rhs_ptr) {
                        if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                            return type_invalid();
                        }
                        if (lhs.kind == TypeKind::Pointer && rhs.kind == TypeKind::Pointer) {
                            if (lhs.pointee == nullptr || rhs.pointee == nullptr ||
                                !same_type(*lhs.pointee, *rhs.pointee)) {
                                error(node.loc, "type mismatch");
                                return type_invalid();
                            }
                            return type_int();
                        }
                        if (!lhs_ptr || !rhs_ptr) {
                            error(node.loc, "type mismatch");
                            return type_invalid();
                        }
                        return type_int();
                    }
                }
                if (lhs.kind == TypeKind::Meta || rhs.kind == TypeKind::Meta) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (lhs.kind == TypeKind::Meta && rhs.kind == TypeKind::Meta &&
                        (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq)) {
                        return type_int();
                    }
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (lhs.kind == TypeKind::Enum || rhs.kind == TypeKind::Enum) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (lhs.kind == TypeKind::Enum && rhs.kind == TypeKind::Enum && same_type(lhs, rhs) &&
                        (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq)) {
                        return type_int();
                    }
                    if (is_c_enum(lhs)) {
                        lhs = type_int();
                    }
                    if (is_c_enum(rhs)) {
                        rhs = type_int();
                    }
                    if (lhs.kind == TypeKind::Enum || rhs.kind == TypeKind::Enum) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                }
                if (is_float(lhs) || is_float(rhs)) {
                    if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (!is_numeric(lhs) || !is_numeric(rhs)) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    const bool wide = (is_float(lhs) && lhs.rank >= 64) || (is_float(rhs) && rhs.rank >= 64);
                    switch (node.op) {
                    case TokenKind::Percent:
                    case TokenKind::Amp:
                    case TokenKind::Pipe:
                    case TokenKind::Caret:
                    case TokenKind::LtLt:
                    case TokenKind::GtGt:
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    case TokenKind::EqEq:
                    case TokenKind::BangEq:
                    case TokenKind::Lt:
                    case TokenKind::Gt:
                    case TokenKind::Le:
                    case TokenKind::Ge:
                        return type_int();
                    default:
                        return type_float(wide ? 64 : 32);
                    }
                }
                expect_integer(lhs, expr_loc(*node.lhs));
                expect_integer(rhs, expr_loc(*node.rhs));
                if (!is_integer(lhs) || !is_integer(rhs)) {
                    return type_invalid();
                }
                const Type common = usual(lhs, rhs);
                switch (node.op) {
                case TokenKind::EqEq:
                case TokenKind::BangEq:
                case TokenKind::Lt:
                case TokenKind::Gt:
                case TokenKind::Le:
                case TokenKind::Ge:
                    return type_int();
                default:
                    return common;
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                if (const auto *star = std::get_if<Expr::Unary>(&node.lhs->data); star != nullptr && star->op == TokenKind::Star) {
                    const Type pointer = check_expr(*star->expr, true);
                    if (pointer.kind != TypeKind::Pointer || !pointer.pointee) {
                        if (pointer.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (pointer.optional || pointer.pointee->kind == TypeKind::Void || pointer.pointee->is_const) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    check_against(*node.rhs, *pointer.pointee);
                    return *pointer.pointee;
                }
                if (const auto *index = std::get_if<Expr::Index>(&node.lhs->data)) {
                    const Type base = check_expr(*index->base, true);
                    const Type subscript = check_expr(*index->index, true);
                    expect(subscript, type_int(), expr_loc(*index->index));
                    if (base.kind == TypeKind::Tuple) {
                        error(node.loc, "not supported yet");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (base.kind == TypeKind::Pointer && base.optional) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type element;
                    bool pointee_void = false;
                    if (!indexed_type(base, element, pointee_void)) {
                        if (base.kind != TypeKind::Invalid) {
                            error(node.loc, pointee_void ? "type mismatch" : "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (element.is_const) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    check_against(*node.rhs, element);
                    return element;
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&node.lhs->data)) {
                    if (selector->base != nullptr && enum_designator(*selector->base) >= 0) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (target.kind == SelectorTarget::Kind::Package) {
                        if (target.symbol->is_function || target.symbol->is_type || target.symbol->is_constant ||
                            target.symbol->type.kind == TypeKind::Array) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        check_against(*node.rhs, target.symbol->type);
                        return target.symbol->type;
                    }
                    const Type base = check_expr(*selector->base, true);
                    if (base.kind == TypeKind::Trait ||
                        (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                        error(node.loc, base.kind == TypeKind::Trait ? "undeclared identifier" : "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (base.kind == TypeKind::Pointer && base.optional) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type object;
                    bool through_pointer = false;
                    if (!dot_object(base, object, through_pointer)) {
                        if (base.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (!through_pointer && !is_lvalue_form(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type field;
                    if (!lookup_field(object, selector->field, field)) {
                        error(node.loc, "undeclared identifier");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if ((base.kind == TypeKind::Pointer && base.pointee != nullptr && base.pointee->is_const) ||
                        const_path(*node.lhs)) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    check_against(*node.rhs, field);
                    return field;
                }
                const bool name = std::holds_alternative<Expr::Name>(node.lhs->data);
                const Symbol *symbol = nullptr;
                if (name) {
                    symbol = lookup(std::get<Expr::Name>(node.lhs->data).text);
                }
                if (name && symbol != nullptr && captured(*symbol)) {
                    error(expr_loc(*node.lhs), "not supported yet");
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                if (name && symbol != nullptr && symbol->is_immutable) {
                    error(node.loc, "type mismatch");
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                if (!name || !symbol || symbol->is_function || symbol->is_package || symbol->is_constant ||
                    symbol->type.kind == TypeKind::Array) {
                    if (name && !symbol) {
                        error(expr_loc(*node.lhs), "undeclared identifier");
                    } else {
                        error(node.loc, "not an lvalue");
                    }
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                check_against(*node.rhs, symbol->type);
                return symbol->type;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                const bool deferring = checking_defer_;
                if (deferring) {
                    checking_defer_ = false;
                }
                if (node.callee != nullptr) {
                    if (const std::optional<Type> generic = maybe_generic_call(const_cast<Expr &>(expr), used)) {
                        return *generic;
                    }
                }
                if (node.callee != nullptr && is_builtin_va_call(*node.callee)) {
                    if (deferring || !variadic_function_) {
                        error(node.loc, "not supported yet");
                    }
                    if (!node.args.empty()) {
                        error(node.loc, "argument count");
                        for (const auto &arg : node.args) {
                            if (arg) {
                                check_expr(*arg, true);
                            }
                        }
                    }
                    return type_pointer(type_char());
                }
                // Another file in the package does not see the enum as a cast spelling.
                // `ShapeType(shape)` is still the tag read.
                if (node.callee != nullptr && node.args.size() == 1 && node.args.front() != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (lookup(named->text) == nullptr) {
                            const int enum_id = find_enum(file_id_, named->text);
                            if (enum_id >= 0) {
                                const Type source = check_expr(*node.args.front(), true);
                                if (source.kind == TypeKind::Struct && source.struct_id >= 0 &&
                                    source.struct_id < static_cast<int>(structs_.size()) &&
                                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == enum_id) {
                                    return type_enum(enum_id);
                                }
                                if (source.kind != TypeKind::Invalid) {
                                    error(node.loc, "type mismatch");
                                }
                                return type_invalid();
                            }
                        }
                    }
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                    // `Point` is a type, not a value, so `lookup` does not see it. `Point.make(...)` is the static call.
                    if (selector->base != nullptr) {
                        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
                            const Symbol *symbol = lookup(named->text);
                            const bool shadowed = symbol != nullptr && !symbol->is_type;
                            if (!shadowed &&
                                (find_tag(file_id_, named->text) >= 0 || find_enum(file_id_, named->text) >= 0)) {
                                const MethodInfo *method = find_named_method(named->text, selector->field);
                                if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function ||
                                    method->type.result == nullptr) {
                                    error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                            : "undeclared identifier");
                                    for (const auto &arg : node.args) {
                                        check_expr(*arg, true);
                                    }
                                    return type_invalid();
                                }
                                if (deferring && method->type.variadic) {
                                    error(node.loc, "not supported yet");
                                }
                                const bool count_ok = method->type.variadic
                                                          ? node.args.size() >= method->type.params.size()
                                                          : node.args.size() == method->type.params.size();
                                if (!count_ok) {
                                    error(node.loc, "argument count");
                                }
                                for (std::size_t i = 0; i < node.args.size(); ++i) {
                                    if (count_ok && i < method->type.params.size()) {
                                        check_against(*node.args[i], method->type.params[i]);
                                    } else {
                                        const Type arg = check_expr(*node.args[i], true);
                                        if (arg.kind != TypeKind::Invalid && !is_numeric(arg) &&
                                            arg.kind != TypeKind::Pointer && arg.kind != TypeKind::Enum &&
                                            arg.kind != TypeKind::Nullptr) {
                                            error(expr_loc(*node.args[i]), "not supported yet");
                                        }
                                    }
                                }
                                return *method->type.result;
                            }
                        }
                    }
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        for (const auto &arg : node.args) {
                            check_expr(*arg, true);
                        }
                        return type_invalid();
                    }
                    if (target.kind != SelectorTarget::Kind::Package && selector->base != nullptr) {
                        const Type base = check_expr(*selector->base, true);
                        if (base.kind == TypeKind::Invalid) {
                            for (const auto &arg : node.args) {
                                check_expr(*arg, true);
                            }
                            return type_invalid();
                        }
                        const bool type_value = base.kind == TypeKind::Meta;
                        const bool through_type = base.kind == TypeKind::Pointer && base.pointee &&
                                                  base.pointee->kind == TypeKind::Meta;
                        if (type_value || through_type) {
                            if (through_type && base.optional) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (selector->field != "name") {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (!node.args.empty()) {
                                error(node.loc, "argument count");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            node.type_name = true;
                            return type_pointer(type_char());
                        }
                        if (base.kind == TypeKind::Trait ||
                            (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                            const bool through_pointer = base.kind != TypeKind::Trait;
                            const int trait_id = through_pointer ? base.pointee->trait_id : base.trait_id;
                            const TraitInfo::Method *method = find_trait_method(trait_id, selector->field);
                            if (method == nullptr || method->type.kind != TypeKind::Function || method->type.result == nullptr) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (deferring) {
                                error(node.loc, "not supported yet");
                            }
                            const std::size_t arity = method->type.params.size();
                            const bool count_ok = node.args.size() == arity;
                            if (!count_ok) {
                                error(node.loc, "argument count");
                            }
                            for (std::size_t i = 0; i < node.args.size(); ++i) {
                                if (count_ok && i < arity) {
                                    check_against(*node.args[i], method->type.params[i]);
                                } else {
                                    check_expr(*node.args[i], true);
                                }
                            }
                            return *method->type.result;
                        }
                        if (base.kind == TypeKind::Enum ||
                            (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Enum)) {
                            const bool through_pointer = base.kind != TypeKind::Enum;
                            const int enum_id = through_pointer ? base.pointee->enum_id : base.enum_id;
                            const std::string enum_name =
                                enum_id >= 0 && enum_id < static_cast<int>(enum_types_.size())
                                    ? enum_types_[static_cast<std::size_t>(enum_id)].name
                                    : std::string{};
                            if (const MethodInfo *method = find_named_method(enum_name, selector->field)) {
                                if (method->is_static) {
                                    error(node.loc, "type mismatch");
                                    for (const auto &arg : node.args) {
                                        check_expr(*arg, true);
                                    }
                                    return type_invalid();
                                }
                                if (deferring && method->type.variadic) {
                                    error(node.loc, "not supported yet");
                                }
                                const std::size_t arity = method->type.params.size();
                                const bool count_ok = method->type.variadic ? node.args.size() + 1 >= arity
                                                                            : node.args.size() + 1 == arity;
                                if (!count_ok) {
                                    error(node.loc, "argument count");
                                }
                                if (!method->type.params.empty()) {
                                    const Type &receiver = method->type.params[0];
                                    const Type object = through_pointer ? *base.pointee : base;
                                    const auto *as_selector = std::get_if<Expr::Selector>(&selector->base->data);
                                    const bool enumerator =
                                        as_selector != nullptr && as_selector->base != nullptr &&
                                        enum_designator(*as_selector->base) >= 0;
                                    if (receiver.kind == TypeKind::Pointer) {
                                        if (!through_pointer && (!is_lvalue_form(*selector->base) || enumerator)) {
                                            error(node.loc, "not an lvalue");
                                        } else {
                                            const Type pointer = through_pointer ? base : type_pointer(object);
                                            if (!accepts_pointer(pointer, receiver)) {
                                                mismatch(expr_loc(*selector->base), receiver, pointer);
                                            }
                                        }
                                    } else {
                                        expect(object, receiver, expr_loc(*selector->base));
                                    }
                                }
                                for (std::size_t i = 0; i < node.args.size(); ++i) {
                                    if (count_ok && i + 1 < arity) {
                                        check_against(*node.args[i], method->type.params[i + 1]);
                                    } else {
                                        check_expr(*node.args[i], true);
                                    }
                                }
                                if (method->type.result == nullptr) {
                                    return type_invalid();
                                }
                                return *method->type.result;
                            }
                        }
                        Type object;
                        bool through_pointer = false;
                        if (dot_object(base, object, through_pointer)) {
                            if (const MethodInfo *method = find_method(object.struct_id, selector->field)) {
                                if (method->is_static) {
                                    error(node.loc, "type mismatch");
                                    for (const auto &arg : node.args) {
                                        check_expr(*arg, true);
                                    }
                                    return type_invalid();
                                }
                                if (deferring && method->type.variadic) {
                                    error(node.loc, "not supported yet");
                                }
                                const std::size_t arity = method->type.params.size();
                                const bool count_ok = method->type.variadic ? node.args.size() + 1 >= arity
                                                                            : node.args.size() + 1 == arity;
                                if (!count_ok) {
                                    error(node.loc, "argument count");
                                }
                                if (!method->type.params.empty()) {
                                    const Type &receiver = method->type.params[0];
                                    if (receiver.kind == TypeKind::Pointer) {
                                        if (!through_pointer && !is_lvalue_form(*selector->base)) {
                                            error(node.loc, "not an lvalue");
                                        } else {
                                            const Type pointer = through_pointer ? base : type_pointer(object);
                                            if (!accepts_pointer(pointer, receiver)) {
                                                mismatch(expr_loc(*selector->base), receiver, pointer);
                                            }
                                        }
                                    } else {
                                        expect(object, receiver, expr_loc(*selector->base));
                                    }
                                }
                                for (std::size_t i = 0; i < node.args.size(); ++i) {
                                    if (count_ok && i + 1 < arity) {
                                        check_against(*node.args[i], method->type.params[i + 1]);
                                    } else {
                                        check_expr(*node.args[i], true);
                                    }
                                }
                                if (method->type.result == nullptr) {
                                    return type_invalid();
                                }
                                return *method->type.result;
                            }
                            if (base.kind == TypeKind::Pointer && base.optional) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                        }
                    }
                }
                const Type callee = check_expr(*node.callee, true);
                if (callee.kind == TypeKind::Invalid) {
                    for (const auto &arg : node.args) {
                        check_expr(*arg, true);
                    }
                    return type_invalid();
                }
                const Type *signature = &callee;
                if (callee.kind == TypeKind::Pointer && callee.pointee && callee.pointee->kind == TypeKind::Function) {
                    signature = callee.pointee.get();
                } else if (callee.kind != TypeKind::Function || !callee.result) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (signature->result == nullptr) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (deferring) {
                    bool generic = signature->variadic;
                    for (const Type &param : signature->params) {
                        if (mentions_trait(param)) {
                            generic = true;
                        }
                    }
                    if (generic) {
                        error(node.loc, "not supported yet");
                    }
                }
                const bool count_ok = signature->variadic ? node.args.size() >= signature->params.size()
                                                         : node.args.size() == signature->params.size();
                if (!count_ok) {
                    error(node.loc, "argument count");
                }
                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    if (count_ok && i < signature->params.size()) {
                        check_against(*node.args[i], signature->params[i]);
                    } else {
                        const Type arg = check_expr(*node.args[i], true);
                        if (arg.kind != TypeKind::Invalid && !is_numeric(arg) && arg.kind != TypeKind::Pointer &&
                            arg.kind != TypeKind::Enum && arg.kind != TypeKind::Nullptr) {
                            error(expr_loc(*node.args[i]), "not supported yet");
                        }
                    }
                }
                return *signature->result;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base != nullptr) {
                    const int enum_id = enum_designator(*node.base);
                    if (enum_id >= 0) {
                        if (find_enum_member(enum_id, node.field) == nullptr) {
                            error(node.loc, "undeclared identifier");
                            return type_invalid();
                        }
                        return type_enum(enum_id);
                    }
                }
                const SelectorTarget target = resolve_selector(node);
                if (target.kind == SelectorTarget::Kind::Failed) {
                    return type_invalid();
                }
                if (target.kind == SelectorTarget::Kind::Package) {
                    const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                    const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                    if (node.field == "va" && package != nullptr && package->is_package &&
                        is_std_builtin_file(package->package_file)) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    if (target.symbol->is_type) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return target.symbol->type;
                }
                const Type base = check_expr(*node.base, true);
                if (base.kind == TypeKind::Trait ||
                    (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                if (base.kind == TypeKind::Pointer && base.optional) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (base.kind == TypeKind::Meta ||
                    (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Meta)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                Type object;
                bool through_pointer = false;
                if (!dot_object(base, object, through_pointer)) {
                    if (base.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    return type_invalid();
                }
                Type field;
                if (!lookup_field(object, node.field, field)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                return field;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                (void)used;
                return type_array(type_char(), static_cast<int>(node.text.size()) + 1);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                (void)used;
                const Type base = check_expr(*node.base, true);
                const Type subscript = check_expr(*node.index, true);
                expect(subscript, type_int(), expr_loc(*node.index));
                if (base.kind == TypeKind::Tuple) {
                    const std::optional<int> folded = node.index ? fold_int(*node.index) : std::nullopt;
                    if (!folded) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    if (*folded < 0 || static_cast<std::size_t>(*folded) >= base.results.size()) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    node.tuple_index = *folded;
                    return base.results[static_cast<std::size_t>(*folded)];
                }
                if (base.kind == TypeKind::Pointer && base.optional) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                Type element;
                bool pointee_void = false;
                if (!indexed_type(base, element, pointee_void)) {
                    if (base.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    return type_invalid();
                }
                return element;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                (void)used;
                return type_bool();
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                (void)used;
                return type_nullptr();
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                (void)used;
                const Type type = sizeof_operand(node);
                if (type.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (type.kind == TypeKind::Void || type.kind == TypeKind::Function || type.kind == TypeKind::Tuple ||
                    mentions_trait(type)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                return type_int();
            } else if constexpr (std::is_same_v<T, Expr::Typeof>) {
                (void)used;
                const Type type = operand_type(node.on_type, node.type_name, node.type_package, node.is_struct, node.is_enum,
                                               node.indirections, node.optional, node.expr.get(), node.loc,
                                               node.const_pointee);
                if (type.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (type.kind == TypeKind::Function || type.kind == TypeKind::Tuple || mentions_trait(type)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                node.canonical = type_desc(type);
                return type_meta();
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                (void)used;
                const Type target = spelled_type(node.type_name, node.type_package, node.is_struct, node.is_enum,
                                                  node.indirections, node.loc, node.optional, node.const_pointee);
                const Type source = node.expr ? check_expr(*node.expr, true) : type_invalid();
                if (target.kind == TypeKind::Invalid || source.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (target.kind == TypeKind::Pointer && source.kind == TypeKind::Nullptr && !target.optional) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (target.kind == TypeKind::Void) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if ((is_c_enum(source) && is_integer(target)) || (is_c_enum(target) && is_integer(source))) {
                    return target;
                }
                // `ShapeType(shape)` reads the tag of a tlc union.
                if (source.kind == TypeKind::Struct && target.kind == TypeKind::Enum && source.struct_id >= 0 &&
                    source.struct_id < static_cast<int>(structs_.size()) &&
                    structs_[static_cast<std::size_t>(source.struct_id)].tag_enum == target.enum_id &&
                    target.enum_id >= 0) {
                    return target;
                }
                const bool source_scalar = is_numeric(source);
                const bool target_scalar = is_numeric(target);
                const bool source_int = same_type(source, type_int());
                const bool target_int = same_type(target, type_int());
                if ((target.kind == TypeKind::Pointer && source.kind == TypeKind::Pointer) ||
                    (target.kind == TypeKind::Pointer && source_int) ||
                    (target.kind == TypeKind::Pointer && source.kind == TypeKind::Nullptr) ||
                    (target_int && source.kind == TypeKind::Pointer) ||
                    (target_scalar && source_scalar)) {
                    return target;
                }
                error(node.loc, "type mismatch");
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                (void)used;
                if (!node.elements.empty() && node.fields.empty()) {
                    if (!node.is_tuple) {
                        if (node.elements.size() == 1 && node.elements.front()) {
                            return check_raw(*node.elements.front(), true);
                        }
                        error(node.loc, "type mismatch");
                        for (const auto &element : node.elements) {
                            if (element) {
                                check_expr(*element, true);
                            }
                        }
                        return type_invalid();
                    }
                    std::vector<Type> parts;
                    parts.reserve(node.elements.size());
                    bool invalid = false;
                    for (const auto &element : node.elements) {
                        if (!element) {
                            invalid = true;
                            parts.push_back(type_invalid());
                            continue;
                        }
                        Type part = check_raw(*element, true);
                        if (part.kind == TypeKind::Invalid) {
                            invalid = true;
                        }
                        parts.push_back(std::move(part));
                    }
                    if (invalid) {
                        return type_invalid();
                    }
                    return type_tuple(std::move(parts));
                }
                error(node.loc, "type mismatch");
                for (const Expr::FieldInit &field : node.fields) {
                    if (field.value) {
                        check_expr(*field.value, true);
                    }
                }
                for (const auto &element : node.elements) {
                    if (element) {
                        check_expr(*element, true);
                    }
                }
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                (void)used;
                if (node.decl == nullptr) {
                    return type_invalid();
                }
                const FunctionDecl &fn = *node.decl;
                if (fn.inferred && !fn.elaborated) {
                    error(fn.loc, "type mismatch");
                    return type_invalid();
                }
                if (fn.inferred && fn.elaborated) {
                    return function_signature(fn);
                }
                if (!fn.method_struct.empty() || fn.exported || fn.is_static || fn.is_extern || fn.variadic) {
                    error(fn.loc, "not supported yet");
                }
                bool generic = false;
                for (const Param &param : fn.params) {
                    if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
                        package_trait(file_id_, param.type_name) >= 0) {
                        generic = true;
                    }
                }
                Type signature = function_signature(fn);
                if (signature.result != nullptr && mentions_trait(*signature.result)) {
                    generic = true;
                }
                if (generic) {
                    error(fn.loc, "not supported yet");
                }
                if (fn.body != nullptr) {
                    check_function(fn);
                }
                return signature;
            } else {
                (void)used;
                return type_invalid();
            }
        },
        expr.data);
}

Type Checker::operand_type(bool on_type, const std::string &type_name, const std::string &type_package, bool is_struct,
                           bool is_enum, int indirections, uint32_t optional, const Expr *expr, SourceLoc loc,
                           uint32_t const_pointee) {
    if (on_type) {
        return spelled_type(type_name, type_package, is_struct, is_enum, indirections, loc, optional, const_pointee);
    }
    if (expr == nullptr) {
        return type_invalid();
    }
    int stars = 0;
    const Expr *cursor = expr;
    while (cursor != nullptr) {
        const auto *unary = std::get_if<Expr::Unary>(&cursor->data);
        if (unary == nullptr || unary->op != TokenKind::Star || unary->postfix || unary->expr == nullptr) {
            break;
        }
        ++stars;
        cursor = unary->expr.get();
    }
    const auto *named = cursor == nullptr ? nullptr : std::get_if<Expr::Name>(&cursor->data);
    Type ignored;
    const bool alias = named != nullptr && package_typedef(file_id_, named->text, ignored);
    const bool tag = named != nullptr && find_tag(file_id_, named->text) >= 0;
    const bool enumeration = named != nullptr && find_enum(file_id_, named->text) >= 0;
    if (named != nullptr && lookup(named->text) == nullptr && (alias || tag || enumeration)) {
        // `sizeof(Point)` and `typeof(*Point)` when the name is a type.
        return with_indirections(type_of_name(named->text, loc, stars > 0), stars, loc);
    }
    return check_raw(*expr, true);
}

Type Checker::sizeof_operand(const Expr::Sizeof &node) {
    return operand_type(node.on_type, node.type_name, node.type_package, node.is_struct, node.is_enum, node.indirections,
                        node.optional, node.expr.get(), node.loc, node.const_pointee);
}

void Checker::check_assert(SourceLoc loc, const Expr *cond, const std::string &message) {
    if (cond == nullptr) {
        error(loc, "type mismatch");
        return;
    }
    const Type got = check_expr(*cond, true);
    if (got.kind == TypeKind::Invalid) {
        return;
    }
    if (!is_integer(got) && !is_c_enum(got)) {
        error(loc, "type mismatch");
        return;
    }
    const std::optional<int> value = fold_int(*cond);
    if (!value) {
        error(loc, "not supported yet");
        return;
    }
    if (*value == 0) {
        error(loc, message.empty() ? "assert" : message);
    }
}

bool Checker::is_std_builtin_file(uint32_t file_id) const {
    const PackageNode *node = find_node(file_id);
    return node != nullptr && is_std_builtin_path(node->path);
}

bool Checker::is_builtin_va_call(const Expr &callee) {
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->field != "va" || selector->base == nullptr) {
            return false;
        }
        const auto *base = std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return false;
        }
        const Symbol *symbol = lookup(base->text);
        return symbol != nullptr && symbol->is_package && is_std_builtin_file(symbol->package_file);
    }
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        const Symbol *symbol = lookup(named->text);
        return symbol != nullptr && symbol->is_function && symbol->name == "va" &&
               is_std_builtin_file(symbol->package_file);
    }
    return false;
}

void Checker::check_test(const TestDecl &test) {
    push_function();
    const Type saved = return_type_;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    loop_depth_ = 0;
    switches_.clear();
    return_type_ = type_int();
    if (test.body) {
        if (const auto *block = std::get_if<Stmt::Block>(&test.body->data)) {
            for (const auto &child : block->statements) {
                if (child) {
                    check_stmt(*child);
                }
            }
        } else {
            check_stmt(*test.body);
        }
    }
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    pop();
}

std::optional<int> Checker::fold_int(const Expr &expr) {
    auto wrap = [](int64_t value) { return static_cast<int>(static_cast<uint32_t>(value)); };
    return std::visit(
        [&](const auto &node) -> std::optional<int> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int>) {
                const bool plain = !node.suffix.is_unsigned && (node.suffix.bits == 0 || node.suffix.bits == 32);
                if (!plain || node.value > 2147483647ULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                if (node.value > 255ULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(node.text);
                if (symbol != nullptr && symbol->is_constant) {
                    return symbol->const_value;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base == nullptr) {
                    return std::nullopt;
                }
                const Symbol *symbol = lookup(base->text);
                if (symbol == nullptr || !symbol->is_package) {
                    return std::nullopt;
                }
                const Symbol *member = find_export(symbol->package_file, node.field);
                if (member == nullptr || !member->is_constant) {
                    return std::nullopt;
                }
                return member->const_value;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                const bool saved = quiet_;
                quiet_ = true;
                const Type type = sizeof_operand(node);
                quiet_ = saved;
                if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Void || type.kind == TypeKind::Function ||
                    type.kind == TypeKind::Tuple || mentions_trait(type)) {
                    return std::nullopt;
                }
                return type_size(type);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.postfix) {
                    return std::nullopt;
                }
                const std::optional<int> inner = node.expr ? fold_int(*node.expr) : std::nullopt;
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
                const std::optional<int> lhs = node.lhs ? fold_int(*node.lhs) : std::nullopt;
                const std::optional<int> rhs = node.rhs ? fold_int(*node.rhs) : std::nullopt;
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                const int64_t a = *lhs;
                const int64_t b = *rhs;
                switch (node.op) {
                case TokenKind::Plus: return wrap(a + b);
                case TokenKind::Minus: return wrap(a - b);
                case TokenKind::Star: return wrap(a * b);
                case TokenKind::Slash: return b == 0 ? std::nullopt : std::optional<int>(wrap(a / b));
                case TokenKind::Percent: return b == 0 ? std::nullopt : std::optional<int>(wrap(a % b));
                case TokenKind::Amp: return wrap(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
                case TokenKind::Pipe: return wrap(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
                case TokenKind::Caret: return wrap(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
                case TokenKind::LtLt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return wrap(static_cast<uint32_t>(a) << static_cast<uint32_t>(b));
                case TokenKind::GtGt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return static_cast<int>(static_cast<uint32_t>(static_cast<int32_t>(a) >> static_cast<uint32_t>(b)));
                case TokenKind::EqEq: return a == b ? 1 : 0;
                case TokenKind::BangEq: return a != b ? 1 : 0;
                case TokenKind::Lt: return a < b ? 1 : 0;
                case TokenKind::Gt: return a > b ? 1 : 0;
                case TokenKind::Le: return a <= b ? 1 : 0;
                case TokenKind::Ge: return a >= b ? 1 : 0;
                case TokenKind::AmpAmp: return (*lhs != 0 && *rhs != 0) ? 1 : 0;
                case TokenKind::PipePipe: return (*lhs != 0 || *rhs != 0) ? 1 : 0;
                default: return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

namespace {

struct NullGuard {
    std::string name;
    bool equal_null = false;
};

bool always_exits(const Stmt *stmt) {
    if (stmt == nullptr) {
        return false;
    }
    return std::visit(
        [](const auto &node) -> bool {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Return> || std::is_same_v<T, Stmt::Break> ||
                          std::is_same_v<T, Stmt::Continue>) {
                return true;
            } else if constexpr (std::is_same_v<T, Stmt::Block>) {
                if (node.statements.empty() || node.statements.back() == nullptr) {
                    return false;
                }
                return always_exits(node.statements.back().get());
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                return node.then_arm != nullptr && node.else_arm != nullptr && always_exits(node.then_arm.get()) &&
                       always_exits(node.else_arm.get());
            } else {
                return false;
            }
        },
        stmt->data);
}

const Expr::Name *null_side(const Expr *expr, bool &is_null) {
    is_null = false;
    if (expr == nullptr) {
        return nullptr;
    }
    if (std::holds_alternative<Expr::Nullptr>(expr->data)) {
        is_null = true;
        return nullptr;
    }
    return std::get_if<Expr::Name>(&expr->data);
}

std::optional<NullGuard> null_guard(const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }
    const auto *binary = std::get_if<Expr::Binary>(&expr->data);
    if (binary == nullptr || (binary->op != TokenKind::EqEq && binary->op != TokenKind::BangEq)) {
        return std::nullopt;
    }
    bool lhs_null = false;
    bool rhs_null = false;
    const Expr::Name *lhs = null_side(binary->lhs.get(), lhs_null);
    const Expr::Name *rhs = null_side(binary->rhs.get(), rhs_null);
    const Expr::Name *name = nullptr;
    if (lhs != nullptr && rhs_null) {
        name = lhs;
    } else if (rhs != nullptr && lhs_null) {
        name = rhs;
    } else {
        return std::nullopt;
    }
    return NullGuard{name->text, binary->op == TokenKind::EqEq};
}

} // namespace

bool Checker::narrow_optional(const std::string &name) {
    Symbol *symbol = lookup_scope(name);
    if (symbol == nullptr || scopes_.empty() || symbol->function_id != scopes_.back().function_id) {
        return false;
    }
    if (symbol->type.kind != TypeKind::Pointer || !symbol->type.optional) {
        return false;
    }
    symbol->type.optional = false;
    return true;
}

void Checker::restore_optional(const std::string &name) {
    Symbol *symbol = lookup_scope(name);
    if (symbol == nullptr) {
        return;
    }
    symbol->type.optional = true;
}

bool Checker::const_path(const Expr &expr) {
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Symbol *symbol = lookup(name->text);
        return symbol != nullptr && symbol->is_immutable && symbol->type.kind != TypeKind::Pointer;
    }
    const Expr *base = nullptr;
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        base = selector->base.get();
    } else if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        base = index->base.get();
    } else if (const auto *star = std::get_if<Expr::Unary>(&expr.data);
               star != nullptr && star->op == TokenKind::Star) {
        base = star->expr.get();
    }
    if (base == nullptr) {
        return false;
    }
    if (const auto *name = std::get_if<Expr::Name>(&base->data)) {
        const Symbol *symbol = lookup(name->text);
        if (symbol != nullptr && symbol->type.kind == TypeKind::Pointer && symbol->type.pointee != nullptr &&
            symbol->type.pointee->is_const) {
            return true;
        }
    }
    return const_path(*base);
}

void Checker::check_stmt(const Stmt &stmt) {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                push();
                for (const auto &child : node.statements) {
                    if (child) {
                        check_stmt(*child);
                    }
                }
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                auto match_return = [&](const Type &got, const Type &want) {
                    const bool null_pointer =
                        want.kind == TypeKind::Pointer && got.kind == TypeKind::Nullptr && want.optional;
                    const bool widened = accepts_pointer(got, want);
                    const bool scalar = is_scalar(got) && is_scalar(want);
                    if (!null_pointer && !widened && !scalar &&
                        (want.kind == TypeKind::Void ||
                         (got.kind != TypeKind::Invalid && want.kind != TypeKind::Invalid && !same_type(got, want)))) {
                        error(node.loc, "return type: expected " + type_desc(want) + ", found " + type_desc(got));
                    }
                };
                std::vector<const Expr *> exprs;
                if (node.value) {
                    exprs.push_back(node.value.get());
                }
                for (const auto &extra : node.extra) {
                    if (extra) {
                        exprs.push_back(extra.get());
                    }
                }
                auto supplied = [&](const Expr &value, const Type &want) {
                    if (const auto *init = std::get_if<Expr::Init>(&value.data)) {
                        return check_init(const_cast<Expr::Init &>(*init), want);
                    }
                    if (want.kind == TypeKind::Array) {
                        return check_raw(value, true);
                    }
                    return check_expr(value, true);
                };
                if (return_type_.kind == TypeKind::Tuple && exprs.size() == 1 && exprs.front() != nullptr) {
                    Type got = type_invalid();
                    if (const auto *init = std::get_if<Expr::Init>(&exprs.front()->data)) {
                        got = check_init(const_cast<Expr::Init &>(*init), return_type_);
                    } else {
                        got = check_raw(*exprs.front(), true);
                    }
                    if (got.kind == TypeKind::Tuple && same_type(got, return_type_)) {
                        return;
                    }
                    if (got.kind == TypeKind::Tuple) {
                        error(node.loc, "type mismatch");
                        return;
                    }
                }
                if (return_type_.kind == TypeKind::Tuple) {
                    if (exprs.size() != return_type_.results.size()) {
                        error(node.loc, "return type: expected " + std::to_string(return_type_.results.size()) +
                                            " results, found " + std::to_string(exprs.size()));
                    }
                    for (std::size_t i = 0; i < exprs.size(); ++i) {
                        const Type got = i < return_type_.results.size() ? supplied(*exprs[i], return_type_.results[i])
                                                                          : check_expr(*exprs[i], true);
                        if (i < return_type_.results.size()) {
                            match_return(got, return_type_.results[i]);
                        }
                    }
                } else if (!node.extra.empty()) {
                    error(node.loc, "return type: expected " + type_desc(return_type_) + ", found " +
                                        std::to_string(exprs.size()) + " results");
                    for (const Expr *expr : exprs) {
                        check_expr(*expr, true);
                    }
                } else if (!node.value) {
                    if (return_type_.kind != TypeKind::Void && return_type_.kind != TypeKind::Invalid) {
                        error(node.loc, "return type: expected " + type_desc(return_type_) + ", found void");
                    }
                } else {
                    match_return(supplied(*node.value, return_type_), return_type_);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    check_expr(*node.expr, false);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                const std::optional<NullGuard> guard = null_guard(node.cond.get());
                if (!guard) {
                    if (node.then_arm) {
                        check_stmt(*node.then_arm);
                    }
                    if (node.else_arm) {
                        check_stmt(*node.else_arm);
                    }
                } else if (guard->equal_null) {
                    if (node.then_arm) {
                        check_stmt(*node.then_arm);
                    }
                    if (node.else_arm) {
                        const bool narrowed = narrow_optional(guard->name);
                        check_stmt(*node.else_arm);
                        if (narrowed) {
                            restore_optional(guard->name);
                        }
                    }
                    if (node.then_arm && always_exits(node.then_arm.get())) {
                        narrow_optional(guard->name);
                    }
                } else {
                    const bool narrowed = narrow_optional(guard->name);
                    if (node.then_arm) {
                        check_stmt(*node.then_arm);
                    }
                    if (narrowed) {
                        restore_optional(guard->name);
                    }
                    if (node.else_arm) {
                        check_stmt(*node.else_arm);
                    }
                    if (node.else_arm && always_exits(node.else_arm.get())) {
                        narrow_optional(guard->name);
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                const std::optional<NullGuard> guard = null_guard(node.cond.get());
                ++loop_depth_;
                const bool narrowed = guard && !guard->equal_null && narrow_optional(guard->name);
                if (node.body) {
                    check_stmt(*node.body);
                }
                if (narrowed) {
                    restore_optional(guard->name);
                }
                --loop_depth_;
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                ++loop_depth_;
                if (node.body) {
                    check_stmt(*node.body);
                }
                --loop_depth_;
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                if (node.cond) {
                    expect(check_expr(*node.cond, true), type_int(), expr_loc(*node.cond));
                }
                switches_.push_back({});
                if (node.body) {
                    check_stmt(*node.body);
                }
                switches_.pop_back();
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                if (switches_.empty()) {
                    error(node.loc, node.is_default ? "default outside switch" : "case outside switch");
                } else if (node.is_default) {
                    if (switches_.back().has_default) {
                        error(node.loc, "redefinition");
                    } else {
                        switches_.back().has_default = true;
                    }
                } else if (node.value) {
                    const Type got = check_expr(*node.value, true);
                    if (got.kind != TypeKind::Invalid && !same_type(got, type_int())) {
                        error(expr_loc(*node.value), "type mismatch");
                    }
                    if (same_type(got, type_int())) {
                        const std::optional<int> folded = fold_int(*node.value);
                        if (!folded) {
                            error(node.loc, "not supported yet");
                        } else if (!switches_.back().values.insert(*folded).second) {
                            error(expr_loc(*node.value), "redefinition");
                        }
                    }
                }
                if (node.stmt) {
                    check_stmt(*node.stmt);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                push();
                ++loop_depth_;
                if (node.init) {
                    check_stmt(*node.init);
                }
                if (node.cond) {
                    expect_condition(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                if (node.step) {
                    check_expr(*node.step, false);
                }
                const std::optional<NullGuard> guard = null_guard(node.cond.get());
                const bool narrowed = guard && !guard->equal_null && narrow_optional(guard->name);
                if (node.body) {
                    check_stmt(*node.body);
                }
                if (narrowed) {
                    restore_optional(guard->name);
                }
                --loop_depth_;
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                if (loop_depth_ == 0 && switches_.empty()) {
                    error(node.loc, "break outside loop");
                }
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                if (loop_depth_ == 0) {
                    error(node.loc, "continue outside loop");
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    check_local_function(*node.decl);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call == nullptr || !std::holds_alternative<Expr::Call>(node.call->data)) {
                    if (node.call) {
                        check_expr(*node.call, false);
                    }
                    error(node.loc, "type mismatch");
                } else {
                    checking_defer_ = true;
                    check_expr(*node.call, false);
                    checking_defer_ = false;
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value == nullptr || node.names.empty()) {
                    error(node.loc, "type mismatch");
                    return;
                }
                const Type rhs = check_expr(*node.value, true);
                if (rhs.kind == TypeKind::Invalid) {
                    return;
                }
                std::vector<Type> parts;
                if (node.names.size() == 1) {
                    parts.push_back(rhs);
                } else if (rhs.kind != TypeKind::Tuple || rhs.results.size() != node.names.size()) {
                    const std::string found = rhs.kind == TypeKind::Tuple
                                                  ? std::to_string(rhs.results.size()) + " results"
                                                  : type_desc(rhs);
                    error(node.loc, "type mismatch: expected " + std::to_string(node.names.size()) + " results, found " +
                                        found);
                    return;
                } else {
                    parts = rhs.results;
                }
                bool duplicate = false;
                for (std::size_t i = 0; i < node.names.size(); ++i) {
                    for (std::size_t j = i + 1; j < node.names.size(); ++j) {
                        if (node.names[i] == node.names[j]) {
                            error(node.name_locs[i], "redefinition");
                            duplicate = true;
                        }
                    }
                }
                if (duplicate) {
                    return;
                }
                auto in_current = [&](const std::string &name) -> Symbol * {
                    if (scopes_.empty()) {
                        return nullptr;
                    }
                    const auto found = scopes_.back().symbols.find(name);
                    if (found == scopes_.back().symbols.end()) {
                        return nullptr;
                    }
                    return &found->second;
                };
                auto assignable = [&](const Symbol &symbol, SourceLoc loc, const Type &got) {
                    if (symbol.is_function || symbol.is_package || symbol.is_constant || symbol.type.kind == TypeKind::Array) {
                        error(loc, "not an lvalue");
                        return;
                    }
                    if (captured(symbol)) {
                        error(loc, "not supported yet");
                        return;
                    }
                    expect_value(got, symbol.type, *node.value, loc);
                };
                auto declare_new = [&](std::size_t index) {
                    const SourceLoc loc = index < node.name_locs.size() ? node.name_locs[index] : node.loc;
                    Symbol symbol;
                    symbol.name = node.names[index];
                    symbol.type = parts[index];
                    symbol.is_auto = true;
                    symbol.is_immutable = node.is_const;
                    if (node.is_const) {
                        symbol.type.is_const = true;
                    }
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    symbol.loc = loc;
                    declare(std::move(symbol), loc);
                };
                if (node.is_var || node.is_const) {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        if (parts[i].kind == TypeKind::Nullptr) {
                            error(loc, "type mismatch");
                        } else if (in_current(node.names[i]) != nullptr) {
                            error(loc, "redefinition");
                        } else {
                            declare_new(i);
                        }
                    }
                } else if (node.is_define) {
                    bool any_new = false;
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        if (const Symbol *existing = in_current(node.names[i])) {
                            assignable(*existing, loc, parts[i]);
                        } else {
                            any_new = true;
                        }
                    }
                    if (!any_new) {
                        error(node.loc, "type mismatch");
                    } else {
                        for (std::size_t i = 0; i < node.names.size(); ++i) {
                            if (in_current(node.names[i]) != nullptr) {
                                continue;
                            }
                            const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                            if (parts[i].kind == TypeKind::Nullptr) {
                                error(loc, "type mismatch");
                            } else {
                                declare_new(i);
                            }
                        }
                    }
                } else {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        const Symbol *existing = lookup(node.names[i]);
                        if (existing == nullptr) {
                            error(loc, "undeclared identifier");
                        } else {
                            assignable(*existing, loc, parts[i]);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Assert>) {
                check_assert(node.loc, node.cond.get(), node.message);
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    Type type = type_of_var(var);
                    Symbol symbol;
                    symbol.name = var.name;
                    symbol.type = type;
                    symbol.is_auto = true;
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    declare(std::move(symbol), var.loc);
                    const auto *list = var.init == nullptr ? nullptr : std::get_if<Expr::Init>(&var.init->data);
                    const bool array_list = var.is_array && !var.ptr_to_array && list != nullptr && !list->elements.empty();
                    if (var.is_array && !var.ptr_to_array && var.init && !array_list) {
                        error(var.loc, "not supported yet");
                    } else if (var.init) {
                        check_against(*var.init, type);
                    } else if (!var.is_extern && needs_nonnull_pointer(type)) {
                        error(var.loc, "type mismatch");
                    }
                }
            }
        },
        stmt.data);
}

void Checker::check_function(const FunctionDecl &fn) {
    if (fn.results.size() > 1) {
        for (const ResultSlot &slot : fn.results) {
            if (slot.is_fn_ptr) {
                error(fn.loc, "not supported yet");
                break;
            }
        }
    }
    push_function();
    const Type saved = return_type_;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    const bool saved_variadic = variadic_function_;
    loop_depth_ = 0;
    switches_.clear();
    variadic_function_ = fn.variadic;
    resolve_result_typeof(const_cast<FunctionDecl &>(fn));
    return_type_ = function_result(fn);
    for (const Param &param : fn.params) {
        Type type = type_of_param(param);
        Symbol symbol;
        symbol.name = param.name;
        symbol.type = std::move(type);
        symbol.is_auto = true;
        symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
        declare(std::move(symbol), param.loc);
    }
    if (fn.body) {
        if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
            for (const auto &child : block->statements) {
                if (child) {
                    check_stmt(*child);
                }
            }
        } else {
            check_stmt(*fn.body);
        }
    }
    variadic_function_ = saved_variadic;
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    pop();
}

} // namespace tlc
