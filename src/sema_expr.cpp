// Type checking expressions, statements, and function bodies.
#include "check.hpp"
#include "check_util.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tlc {
namespace {

Type bare_pointee(Type type) {
    type.alias_id = 0;
    type.is_const = false;
    type.optional = false;
    return type;
}

// `*void` and `*u8`, including aliases. `const` and `?` are ignored by the caller.
bool raw_pointee(const Type &type) {
    if (type.kind == TypeKind::Void) {
        return true;
    }
    return type.kind == TypeKind::Char && type.is_unsigned && !type.plain_char && type.rank <= 1;
}

// `"Hello"` is `[]const u8`. The length does not count the terminating null.
Type string_slice() {
    Type bytes = type_rank(1, true);
    bytes.is_const = true;
    return type_array(std::move(bytes), -1);
}

// `*const u8`, including `type Text *const u8`. `*u8` does not match.
bool const_u8_pointer(const Type &type) {
    if (type.kind != TypeKind::Pointer || type.optional || type.pointee == nullptr || !type.pointee->is_const) {
        return false;
    }
    return raw_pointee(*type.pointee);
}

// A constant string is this slice. C takes the address when it asks for `*const u8`.
bool slice_of_const_u8(const Type &type) {
    return is_slice(type) && type.element != nullptr && type.element->is_const && raw_pointee(*type.element);
}

bool primitive_pointee(const Type &type) {
    return type.kind == TypeKind::Bool || type.kind == TypeKind::Int || type.kind == TypeKind::Char ||
           type.kind == TypeKind::Float;
}

bool same_primitive(const Type &lhs, const Type &rhs) {
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
bool unsafe_pointer_cast(const Type &source, const Type &target) {
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

} // namespace

void Checker::require_unsafe_call(bool unsafe_fn, SourceLoc loc) {
    if (unsafe_fn && unsafe_depth_ == 0) {
        error(loc, "unsafe call");
    }
}

bool Checker::allow_unsafe_fn(bool unsafe_fn, SourceLoc loc) {
    if (!unsafe_fn) {
        return true;
    }
    if (!direct_callee_) {
        error(loc, "type mismatch");
        return false;
    }
    require_unsafe_call(true, loc);
    return true;
}

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
        // An alias is confirmed with a cast. Integer ranks still convert to each other.
        if (got.alias_id != 0 || want.alias_id != 0) {
            expect(got, want, loc);
        }
        return;
    }
    if (is_numeric(got) && is_numeric(want)) {
        if (got.alias_id != 0 || want.alias_id != 0) {
            expect(got, want, loc);
        }
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
    if (got.kind == TypeKind::Bool) {
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

// `v[i].x` still names the slice element. The element is readonly.
bool from_slice_element(const Expr *expr) {
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
        if (type.alias_id != 0) {
            Spelling spelled;
            spelled.type_name = type_desc(type);
            return spelled;
        }
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
    // The short form is a value. A capturing function cannot be passed around later.
    if (!fn.captures.empty()) {
        error(fn.loc, "not supported yet");
    }
    return type_pointer(std::move(concrete));
}

Type Checker::literal_struct_type(const Expr::Init &init) {
    Type type = type_invalid();
    // `type Spot Point` stays Spot. A struct tag is the struct itself.
    const bool alias = init.type_package.empty() && package_typedef(file_id_, init.type_name, type);
    if (!init.type_package.empty()) {
        type = lookup_qualified_type(init.type_package, init.type_name, init.loc);
    } else if (!alias && find_tag(file_id_, init.type_name) >= 0) {
        type = lookup_tag(init.type_name, init.loc, false);
    } else if (!alias) {
        error(init.loc, "type mismatch");
        return type_invalid();
    }
    if (type.kind != TypeKind::Struct) {
        if (type.kind != TypeKind::Invalid) {
            error(init.loc, "type mismatch");
        }
        return type_invalid();
    }
    return type;
}

Type Checker::check_array_literal(Expr::Init &init) {
    auto check_loose = [&]() {
        for (const auto &element : init.elements) {
            if (element) {
                check_expr(*element, true);
            }
        }
    };
    if (!init.fields.empty()) {
        error(init.loc, "type mismatch");
        check_loose();
        return type_invalid();
    }
    if (init.elem_ptr_to_array && init.elem_shells.empty()) {
        error(init.loc, "not supported yet");
        check_loose();
        return type_invalid();
    }
    Type elem = type_invalid();
    if (init.elem_fn) {
        elem = type_of_fn_ptr(init.type_name, init.type_package, init.is_struct, init.is_enum, init.elem_ret_indirections,
                              init.indirections < 1 ? 1 : init.indirections, init.elem_fn_params, init.elem_fn_variadic,
                              init.elem_fn_results, init.loc, init.elem_ret_optional, init.elem_ret_const_pointee);
    } else if (!init.elem_shells.empty()) {
        bool is_array = false;
        int array_len = 0;
        std::string array_len_name;
        prepare_written(init.type_name, init.type_package, init.is_struct, init.is_enum, init.indirections, init.optional,
                        is_array, array_len, array_len_name, init.type_args, init.loc);
        fold_shells(init.elem_shells, init.loc);
        elem = object_type(init.type_name, init.indirections, false, 0, init.loc, false, init.type_package, init.is_struct,
                           init.is_enum, false, init.optional, init.const_pointee, false, init.elem_const, false,
                           init.elem_shells);
        if (elem.kind == TypeKind::Invalid) {
            check_loose();
            return type_invalid();
        }
    } else if (init.elem_slice) {
        // `[_][][]T` is a third slice. The element of `[_][]T` is one slice.
        error(init.loc, "not supported yet");
        check_loose();
        return type_invalid();
    } else if (init.elem_is_array) {
        const bool element_slice = init.elem_array_len_name.empty() && init.elem_array_len < 0;
        int length = init.elem_array_len;
        if (!element_slice) {
            if (!init.elem_array_len_name.empty()) {
                const std::optional<int> folded = lookup_const_value(init.elem_array_len_name);
                if (!folded || *folded <= 0) {
                    error(init.loc, "not supported yet");
                    check_loose();
                    return type_invalid();
                }
                length = *folded;
                init.elem_array_len = length;
            }
            if (length <= 0) {
                error(init.loc, "not supported yet");
                check_loose();
                return type_invalid();
            }
        }
        elem = object_type(init.type_name, init.indirections, false, 0, init.loc, false, init.type_package, init.is_struct,
                           init.is_enum, false, init.optional, init.const_pointee);
        if (elem.kind == TypeKind::Invalid) {
            check_loose();
            return type_invalid();
        }
        if (elem.kind == TypeKind::Array || elem.kind == TypeKind::Function || elem.kind == TypeKind::Void ||
            elem.kind == TypeKind::Tuple || elem.kind == TypeKind::Meta) {
            error(init.loc, "not supported yet");
            check_loose();
            return type_invalid();
        }
        if (element_slice) {
            // `[_][]const u8`. `const` stays on the leaf, not on the header.
            if (init.elem_const) {
                elem.is_const = true;
            }
            elem = type_array(std::move(elem), -1);
        } else {
            elem = type_array(std::move(elem), length);
        }
    } else {
        bool is_array = false;
        int array_len = 0;
        std::string array_len_name;
        prepare_written(init.type_name, init.type_package, init.is_struct, init.is_enum, init.indirections, init.optional,
                        is_array, array_len, array_len_name, init.type_args, init.loc);
        elem = object_type(init.type_name, init.indirections, false, 0, init.loc, false, init.type_package, init.is_struct,
                           init.is_enum, false, init.optional, init.const_pointee);
        if (elem.kind == TypeKind::Invalid) {
            check_loose();
            return type_invalid();
        }
        if (elem.kind == TypeKind::Array || elem.kind == TypeKind::Function || elem.kind == TypeKind::Void ||
            elem.kind == TypeKind::Tuple || elem.kind == TypeKind::Meta) {
            error(init.loc, "not supported yet");
            check_loose();
            return type_invalid();
        }
    }
    for (const auto &element : init.elements) {
        if (!element) {
            continue;
        }
        // `var copy [2]i32 = row` copies. A name inside `[_][2]i32{inner}` does not.
        if (elem.kind == TypeKind::Array && elem.length >= 0 && std::get_if<Expr::Init>(&element->data) == nullptr) {
            const Type got = check_expr(*element, true);
            if (got.kind != TypeKind::Invalid) {
                mismatch(expr_loc(*element), elem, got);
            }
            continue;
        }
        check_against(*element, elem);
    }
    return type_array(std::move(elem), static_cast<int>(init.elements.size()));
}

Type Checker::check_init(Expr::Init &init, const Type &want) {
    if (init.array_literal) {
        return check_array_literal(init);
    }
    // `Other{}` keeps Other. The caller reports a destination that wanted Point.
    Type dest = want;
    // `Vec[i32]{}` is that specialization. The field rules stay those of `Point{}`.
    bool applied = false;
    if (!init.type_name.empty() && !init.type_args.empty()) {
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        uint32_t optional = 0;
        bool is_array = false;
        int array_len = 0;
        std::string array_len_name;
        prepare_written(init.type_name, init.type_package, is_struct, is_enum, indirections, optional, is_array,
                        array_len, array_len_name, init.type_args, init.loc);
        applied = true;
    }
    if (!init.type_name.empty()) {
        if (applied && init.type_name == "invalid") {
            for (const Expr::FieldInit &field : init.fields) {
                if (field.value) {
                    check_expr(*field.value, true);
                }
            }
            for (const auto &element : init.elements) {
                if (element) {
                    check_expr(*element, true);
                }
            }
            return type_invalid();
        }
        dest = literal_struct_type(init);
        if (dest.kind == TypeKind::Invalid) {
            for (const Expr::FieldInit &field : init.fields) {
                if (field.value) {
                    check_expr(*field.value, true);
                }
            }
            for (const auto &element : init.elements) {
                if (element) {
                    check_expr(*element, true);
                }
            }
            return type_invalid();
        }
    }
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
            if (dest.kind != TypeKind::Tuple) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            if (init.elements.size() != dest.results.size()) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            for (std::size_t i = 0; i < init.elements.size(); ++i) {
                if (init.elements[i]) {
                    check_against(*init.elements[i], dest.results[i]);
                }
            }
            return dest;
        }
        if (dest.kind == TypeKind::Array && dest.element != nullptr) {
            if (static_cast<int>(init.elements.size()) != dest.length) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            for (const auto &element : init.elements) {
                if (!element) {
                    continue;
                }
                // A nested row is a brace list. A name in that position stays a type mismatch.
                if (dest.element->kind == TypeKind::Array && dest.element->length >= 0 &&
                    std::get_if<Expr::Init>(&element->data) == nullptr) {
                    const Type got = check_expr(*element, true);
                    if (got.kind != TypeKind::Invalid) {
                        mismatch(expr_loc(*element), *dest.element, got);
                    }
                    continue;
                }
                check_against(*element, *dest.element);
            }
            return dest;
        }
        if (dest.kind == TypeKind::Struct) {
            error(init.loc, "type mismatch");
            check_elements();
            return type_invalid();
        }
        if (init.elements.size() == 1 && init.elements.front() && dest.kind != TypeKind::Invalid &&
            dest.kind != TypeKind::Void) {
            return check_against(*init.elements.front(), dest);
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
    if (dest.kind != TypeKind::Struct || dest.struct_id < 0 ||
        dest.struct_id >= static_cast<int>(structs_.size())) {
        error(init.loc, "type mismatch");
        check_values();
        return type_invalid();
    }
    if (!structs_[static_cast<std::size_t>(dest.struct_id)].complete) {
        error(init.loc, "not supported yet");
        check_values();
        return type_invalid();
    }
    init.struct_id = dest.struct_id;
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
        if (!lookup_field(dest, field.field, field_type)) {
            error(field.loc, "undeclared identifier");
            if (field.value) {
                check_expr(*field.value, true);
            }
            continue;
        }
        if (hidden_field(dest, field.field, field.loc)) {
            if (field.value) {
                check_expr(*field.value, true);
            }
            continue;
        }
        if (field_type.kind == TypeKind::Array && !is_slice(field_type)) {
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
    const StructInfo &info = structs_[static_cast<std::size_t>(dest.struct_id)];
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
    return dest;
}

const Type *Checker::sole_trait_method(const Type &want) const {
    const Type *trait = nullptr;
    if (want.kind == TypeKind::Trait) {
        trait = &want;
    } else if (want.kind == TypeKind::Pointer && !want.optional && want.pointee != nullptr &&
               want.pointee->kind == TypeKind::Trait) {
        trait = want.pointee.get();
    } else {
        return nullptr;
    }
    if (trait->trait_id < 0 || trait->trait_id >= static_cast<int>(traits_.size())) {
        return nullptr;
    }
    const TraitInfo &info = traits_[static_cast<std::size_t>(trait->trait_id)];
    if (!info.fields.empty()) {
        return nullptr;
    }
    const std::vector<TraitInfo::Method> &methods = info.methods;
    if (methods.size() != 1 || methods.front().type.kind != TypeKind::Function || methods.front().type.result == nullptr) {
        return nullptr;
    }
    return &methods.front().type;
}

bool Checker::accept_trait_lambda(const Expr &expr, const Type &want) {
    const Type *method = sole_trait_method(want);
    if (method == nullptr) {
        return false;
    }
    const auto *node = std::get_if<Expr::Func>(&expr.data);
    if (node == nullptr || node->decl == nullptr) {
        return false;
    }
    FunctionDecl &fn = *node->decl;
    if (fn.inferred && !fn.elaborated) {
        elaborate_inferred(fn, *method);
        return true;
    }
    if (!fn.method_struct.empty() || fn.exported || fn.is_static || fn.is_extern || fn.variadic) {
        error(fn.loc, "not supported yet");
        return true;
    }
    bool generic = false;
    for (const Param &param : fn.params) {
        if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
            package_trait(file_id_, param.type_name) >= 0) {
            generic = true;
        }
    }
    const Type signature = function_signature(fn);
    if (signature.result != nullptr && mentions_trait(*signature.result)) {
        generic = true;
    }
    if (generic) {
        error(fn.loc, "not supported yet");
    } else if (signature.kind != TypeKind::Function || !same_type(signature, *method)) {
        error(fn.loc, "type mismatch");
    }
    if (fn.body != nullptr) {
        check_function(fn);
    }
    if (!fn.captures.empty()) {
        error(fn.loc, "not supported yet");
    }
    return true;
}

void Checker::check_call_arg(const Expr &expr, const Type &want) {
    if (accept_trait_lambda(expr, want)) {
        return;
    }
    check_against(expr, want);
}

Type Checker::check_against(const Expr &expr, const Type &want) {
    // `[]const u8` gives C its `*const u8` address. The slice is still the string in TLC.
    if (const_u8_pointer(want)) {
        const Type got = check_expr(expr, true);
        if (slice_of_const_u8(got)) {
            return want;
        }
        expect_value(got, want, expr, expr_loc(expr));
        return got;
    }
    // `[N]T` converts to `[]T` while the length is still known. A decayed `*T` does not.
    if (is_slice(want)) {
        const Type got = check_raw(expr, true);
        if (got.kind != TypeKind::Invalid && !accepts_slice(got, want)) {
            mismatch(expr_loc(expr), want, got);
        }
        return got;
    }
    if (FunctionDecl *fn = inferred_function(expr)) {
        const Type got = elaborate_inferred(*fn, want);
        if (got.kind != TypeKind::Invalid) {
            // `(a, b) { ... }` is written for the expected alias, so it is that alias.
            Type bare = want;
            bare.alias_id = 0;
            if (want.alias_id != 0 && same_type(got, bare)) {
                return want;
            }
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
    // `var copy [2]i32 = row` copies the array. A read elsewhere still decays to `*T`.
    // `const` on the source is the binding. The copy is a new array of the written type.
    if (want.kind == TypeKind::Array && want.length >= 0) {
        Type got = check_raw(expr, true);
        got.is_const = false;
        if (got.kind != TypeKind::Invalid && !same_type(got, want)) {
            mismatch(expr_loc(expr), want, got);
        }
        return got;
    }
    const Type got = check_expr(expr, true);
    expect_value(got, want, expr, expr_loc(expr));
    return got;
}

Type Checker::value_type(Type type) {
    // A loaded value is a copy. `const` stays on a pointee, not on this temporary.
    // A const array decays to a pointer to a const element, so `row[0] = n` is rejected.
    // The length of a parameter `[N]T` stays on that binding. A copy is a plain pointer.
    const bool array_const = type.kind == TypeKind::Array && type.is_const;
    type.is_const = false;
    type.decayed_len = 0;
    // A slice stays a value. Only a sized array decays to a pointer.
    if (type.kind == TypeKind::Array && type.element && type.length >= 0) {
        Type pointer = type_pointer(*type.element);
        if (array_const && pointer.pointee) {
            pointer.pointee->is_const = true;
        }
        return pointer;
    }
    if (type.kind == TypeKind::Function) {
        return type_pointer(type);
    }
    return promote(type);
}

Type Checker::check_expr(const Expr &expr, bool used) { return value_type(check_raw(expr, used)); }

bool Checker::type_arg_from_expr(const Expr &expr, TypeArg &out) const {
    if (const auto *named = std::get_if<Expr::Name>(&expr.data)) {
        out.loc = named->loc;
        out.type_name = named->text;
        return true;
    }
    if (const auto *number = std::get_if<Expr::Int>(&expr.data)) {
        out.loc = number->loc;
        out.is_value = true;
        const bool wide = number->suffix.is_unsigned || number->suffix.bits == 8 || number->suffix.bits == 16 ||
                          number->suffix.bits == 64 || number->value > 2147483647ULL;
        if (wide) {
            return false;
        }
        out.has_value = true;
        out.value = static_cast<int>(number->value);
        return true;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        const auto *base = selector->base == nullptr ? nullptr : std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr || selector->field.empty()) {
            return false;
        }
        out.loc = selector->loc;
        out.type_package = base->text;
        out.type_name = selector->field;
        return true;
    }
    if (const auto *index = std::get_if<Expr::Index>(&expr.data)) {
        const auto *base = index->base == nullptr ? nullptr : std::get_if<Expr::Name>(&index->base->data);
        if (base == nullptr || index->index == nullptr) {
            return false;
        }
        TypeArg nested;
        if (!type_arg_from_expr(*index->index, nested)) {
            return false;
        }
        out.loc = index->loc;
        out.type_name = base->text;
        out.args.push_back(std::move(nested));
        return true;
    }
    return false;
}

uint32_t Checker::assoc_home_file(const Expr::Selector &node) {
    const Expr *cursor = node.base.get();
    if (cursor != nullptr) {
        if (const auto *index = std::get_if<Expr::Index>(&cursor->data)) {
            cursor = index->base.get();
        }
    }
    const auto *inner = cursor == nullptr ? nullptr : std::get_if<Expr::Selector>(&cursor->data);
    if (inner == nullptr || inner->base == nullptr) {
        return file_id_;
    }
    const auto *package_name = std::get_if<Expr::Name>(&inner->base->data);
    if (package_name == nullptr) {
        return file_id_;
    }
    const Symbol *package = lookup(package_name->text);
    if (package == nullptr || !package->is_package || package->package_file == 0) {
        return file_id_;
    }
    return package->package_file;
}

bool Checker::selector_is_const(const Expr::Selector &node) {
    if (node.field.empty()) {
        return false;
    }
    if (!node.resolved_owner.empty()) {
        const AssocVar *var = find_assoc_in(assoc_home_file(node), node.resolved_owner, node.field);
        return var != nullptr && var->is_const;
    }
    const auto *named = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
    if (named == nullptr) {
        return false;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol != nullptr && !symbol->is_type && !symbol->is_package) {
        return false;
    }
    const AssocVar *var = find_assoc(named->text, node.field);
    return var != nullptr && var->is_const;
}

std::string Checker::same_package_spec(const Expr::Selector &node, bool &failed) {
    failed = false;
    if (node.base == nullptr) {
        return {};
    }
    const auto shadowed = [&](const std::string &name) {
        const Symbol *symbol = lookup(name);
        return symbol != nullptr && !symbol->is_type;
    };
    // `Buf[4, i32].width`. A comma keeps the arguments on the selector.
    if (!node.type_args.empty()) {
        const auto *named = std::get_if<Expr::Name>(&node.base->data);
        if (named == nullptr || shadowed(named->text) || !generic_struct_name(file_id_, named->text)) {
            error(node.loc, "type mismatch");
            failed = true;
            return {};
        }
        const std::string spec = specialize_type_app("", named->text, node.type_args, node.loc);
        failed = spec.empty();
        return spec;
    }
    // `Box[i32].from`. One argument stays an index, the same spelling as `Box[i32].extra`.
    const auto *index = std::get_if<Expr::Index>(&node.base->data);
    if (index == nullptr) {
        return {};
    }
    const auto *named = index->base == nullptr ? nullptr : std::get_if<Expr::Name>(&index->base->data);
    if (named == nullptr || index->index == nullptr || shadowed(named->text) ||
        !generic_struct_name(file_id_, named->text)) {
        return {};
    }
    TypeArg arg;
    if (!type_arg_from_expr(*index->index, arg)) {
        error(node.loc, "type mismatch");
        failed = true;
        return {};
    }
    std::vector<TypeArg> args;
    args.push_back(std::move(arg));
    const std::string spec = specialize_type_app("", named->text, args, node.loc);
    failed = spec.empty();
    return spec;
}

bool Checker::static_method_value(const Expr::Selector &node, uint32_t home, const std::string &owner, Type &out) {
    bool hidden = false;
    const MethodInfo *method = find_visible_method(home, owner, node.field, hidden);
    if (method == nullptr) {
        if (!hidden) {
            return false;
        }
        error(node.loc, node.field + " is not exported");
        out = type_invalid();
        return true;
    }
    bool any_static = method->is_static;
    if (!any_static) {
        for (const MethodInfo &extra : method->more) {
            if (extra.is_static) {
                any_static = true;
                break;
            }
        }
    }
    // An instance method is `not supported yet`. Several lists are too, used as a value.
    if (!any_static || !method->more.empty() || method->type.kind != TypeKind::Function) {
        error(node.loc, !any_static || !method->more.empty() ? "not supported yet" : "type mismatch");
        out = type_invalid();
        return true;
    }
    if (!allow_unsafe_fn(method->unsafe_fn, node.loc)) {
        out = type_invalid();
        return true;
    }
    out = method->type;
    node.resolved_owner = owner;
    node.resolved_file = method->file_id;
    return true;
}

bool Checker::package_static_call(const Expr::Call &node, const Expr::Selector &selector, bool deferring, Type &out) {
    if (selector.base == nullptr || selector.field.empty()) {
        return false;
    }
    auto check_args = [&]() {
        for (const auto &arg : node.args) {
            if (arg) {
                check_expr(*arg, true);
            }
        }
    };
    auto reject = [&](const std::string &message) {
        error(node.loc, message);
        check_args();
        out = type_invalid();
        return true;
    };

    uint32_t home = 0;
    std::string owner;
    // `pkg.Vec[i32].name` and `pkg.Buf[4, i32].name`. A value such as `pkg.row[0].x` is not a template.
    {
        std::vector<TypeArg> args = selector.type_args;
        const Expr *type_expr = nullptr;
        const Expr::Index *index = nullptr;
        if (!args.empty()) {
            type_expr = selector.base.get();
        } else if (const auto *found = std::get_if<Expr::Index>(&selector.base->data)) {
            index = found;
            type_expr = found->base.get();
        }
        const auto *inner = type_expr == nullptr ? nullptr : std::get_if<Expr::Selector>(&type_expr->data);
        if (inner != nullptr && inner->base != nullptr && inner->type_args.empty()) {
            if (const auto *package_name = std::get_if<Expr::Name>(&inner->base->data)) {
                const Symbol *package = lookup(package_name->text);
                if (package != nullptr && package->is_package) {
                    uint32_t templ_file = package->package_file;
                    const StructDecl *templ = nullptr;
                    for (const uint32_t id : files_in_package(package->package_file)) {
                        const auto found = struct_templates_.find(id);
                        if (found == struct_templates_.end()) {
                            continue;
                        }
                        const auto have = found->second.find(inner->field);
                        if (have == found->second.end()) {
                            continue;
                        }
                        templ_file = id;
                        templ = have->second;
                        break;
                    }
                    if (templ != nullptr) {
                        if (index != nullptr) {
                            TypeArg arg;
                            if (index->index == nullptr || !type_arg_from_expr(*index->index, arg)) {
                                error(node.loc, "type mismatch");
                                check_args();
                                out = type_invalid();
                                return true;
                            }
                            args.push_back(std::move(arg));
                        }
                        if (!templ->exported) {
                            error(inner->loc, inner->field + " is not exported");
                            check_args();
                            out = type_invalid();
                            return true;
                        }
                        const uint32_t saved = file_id_;
                        file_id_ = templ_file;
                        const std::string spec = specialize_type_app("", inner->field, args, selector.loc);
                        file_id_ = saved;
                        if (spec.empty()) {
                            check_args();
                            out = type_invalid();
                            return true;
                        }
                        home = templ_file;
                        owner = spec;
                    }
                }
            }
        }
    }
    if (owner.empty()) {
        // The generic form did not match. An index that is not a template stays a value.
        if (!selector.type_args.empty() || std::holds_alternative<Expr::Index>(selector.base->data)) {
            return false;
        }
        const auto *inner = std::get_if<Expr::Selector>(&selector.base->data);
        if (inner == nullptr || inner->base == nullptr || !inner->type_args.empty()) {
            return false;
        }
        const auto *package_name = std::get_if<Expr::Name>(&inner->base->data);
        if (package_name == nullptr) {
            return false;
        }
        const Symbol *package = lookup(package_name->text);
        if (package == nullptr || !package->is_package) {
            return false;
        }
        const Symbol *type = find_export(package->package_file, inner->field);
        if (type == nullptr || !type->is_type) {
            return false;
        }
        // `math.Color.Red()` keeps the diagnostic it had. A static function on the enum does not.
        if (type->type.kind == TypeKind::Enum && find_enum_member(type->type.enum_id, selector.field) != nullptr) {
            return false;
        }
        home = package->package_file;
        owner = inner->field;
    }

    const AssocVar *var = find_assoc_in(home, owner, selector.field);
    if (var != nullptr) {
        return reject(var->exported ? "type mismatch" : selector.field + " is not exported");
    }
    bool hidden = false;
    const MethodInfo *method = find_visible_method(home, owner, selector.field, hidden);
    if (method == nullptr) {
        return reject(hidden ? selector.field + " is not exported" : "undeclared identifier");
    }
    bool any_static = method->is_static;
    if (!any_static) {
        for (const MethodInfo &extra : method->more) {
            if (extra.is_static) {
                any_static = true;
                break;
            }
        }
    }
    // `pkg.Point.sum` is an instance method. The call is not that method.
    if (!any_static) {
        return reject("not supported yet");
    }
    if (!method->more.empty()) {
        method = choose_method(node, *method, true, !package_owns(home), selector.field);
        if (method == nullptr) {
            out = type_invalid();
            return true;
        }
    }
    if (!method->is_static || method->type.kind != TypeKind::Function || method->type.result == nullptr) {
        return reject(method->is_static ? "undeclared identifier" : "type mismatch");
    }
    node.selected = method->decl;
    selector.resolved_owner = owner;
    selector.resolved_file = method->file_id;
    out = finish_static_call(node, *method, deferring);
    return true;
}

const Checker::MethodInfo *Checker::choose_method(const Expr::Call &node, const MethodInfo &head, bool want_static,
                                                  bool foreign, const std::string &name) {
    std::vector<const MethodInfo *> forms;
    int hidden = 0;
    auto consider = [&](const MethodInfo &info) {
        if (info.is_static != want_static || info.type.kind != TypeKind::Function) {
            return;
        }
        if (foreign && !info.exported) {
            ++hidden;
            return;
        }
        forms.push_back(&info);
    };
    consider(head);
    for (const MethodInfo &extra : head.more) {
        consider(extra);
    }
    if (forms.empty()) {
        error(node.loc, hidden > 0 ? name + " is not exported" : "type mismatch");
        for (const auto &arg : node.args) {
            if (arg) {
                check_expr(*arg, true);
            }
        }
        return nullptr;
    }
    if (forms.size() == 1) {
        node.selected = forms.front()->decl;
        return forms.front();
    }
    std::vector<Type> args;
    bool bad = false;
    args.reserve(node.args.size());
    for (const auto &arg : node.args) {
        if (!arg) {
            bad = true;
            args.push_back(type_invalid());
            continue;
        }
        const Type got = check_expr(*arg, true);
        if (got.kind == TypeKind::Invalid) {
            bad = true;
        }
        args.push_back(got);
    }
    if (bad) {
        return nullptr;
    }
    const auto fixed_count = [](const Type &signature, bool skip_receiver) {
        const std::size_t skip = skip_receiver && !signature.params.empty() ? 1 : 0;
        return signature.params.size() - skip;
    };
    const auto count_ok = [&](const Type &signature) {
        const std::size_t fixed = fixed_count(signature, !want_static);
        return signature.variadic ? args.size() >= fixed : args.size() == fixed;
    };
    const auto exact = [&](const Type &signature) {
        if (!count_ok(signature)) {
            return false;
        }
        const std::size_t skip = !want_static && !signature.params.empty() ? 1 : 0;
        const std::size_t fixed = signature.params.size() - skip;
        for (std::size_t i = 0; i < fixed && i < args.size(); ++i) {
            if (!same_type(args[i], signature.params[skip + i])) {
                return false;
            }
        }
        return true;
    };
    const MethodInfo *winner = nullptr;
    int matches = 0;
    int visible_counts = 0;
    for (const MethodInfo *form : forms) {
        if (!count_ok(form->type)) {
            continue;
        }
        ++visible_counts;
        if (exact(form->type)) {
            ++matches;
            winner = form;
        }
    }
    int hidden_counts = 0;
    if (foreign) {
        auto hidden_count = [&](const MethodInfo &info) {
            if (info.is_static != want_static || info.exported || info.type.kind != TypeKind::Function) {
                return;
            }
            if (count_ok(info.type)) {
                ++hidden_counts;
            }
        };
        hidden_count(head);
        for (const MethodInfo &extra : head.more) {
            hidden_count(extra);
        }
    }
    if (matches == 1 && winner != nullptr) {
        node.selected = winner->decl;
        return winner;
    }
    if (matches == 0 && visible_counts == 0 && hidden_counts > 0) {
        error(node.loc, name + " is not exported");
        return nullptr;
    }
    error(node.loc, visible_counts == 0 ? "argument count" : "type mismatch");
    return nullptr;
}

Type Checker::finish_overloaded(const Expr::Call &node, const Symbol &symbol, bool deferring) {
    std::vector<Type> args;
    bool bad = false;
    args.reserve(node.args.size());
    for (const auto &arg : node.args) {
        if (!arg) {
            bad = true;
            args.push_back(type_invalid());
            continue;
        }
        const Type got = check_expr(*arg, true);
        if (got.kind == TypeKind::Invalid) {
            bad = true;
        }
        args.push_back(got);
    }
    if (bad) {
        return type_invalid();
    }
    const FnList *winner = nullptr;
    int matches = 0;
    int counts = 0;
    for (const FnList &list : symbol.lists) {
        if (list.type.kind != TypeKind::Function || list.type.result == nullptr) {
            continue;
        }
        const bool count_ok = list.type.variadic ? args.size() >= list.type.params.size()
                                                 : args.size() == list.type.params.size();
        if (!count_ok) {
            continue;
        }
        ++counts;
        bool same = true;
        for (std::size_t i = 0; i < list.type.params.size() && i < args.size(); ++i) {
            if (!same_type(args[i], list.type.params[i])) {
                same = false;
                break;
            }
        }
        if (same) {
            ++matches;
            winner = &list;
        }
    }
    if (matches != 1 || winner == nullptr || winner->type.result == nullptr) {
        error(node.loc, counts == 0 ? "argument count" : "type mismatch");
        return type_invalid();
    }
    node.selected = winner->decl;
    require_unsafe_call(winner->unsafe_fn, node.loc);
    const Type &signature = winner->type;
    if (deferring) {
        bool generic = signature.variadic;
        for (const Type &param : signature.params) {
            if (mentions_trait(param)) {
                generic = true;
            }
        }
        if (generic) {
            error(node.loc, "not supported yet");
        }
    }
    const bool count_ok = signature.variadic ? node.args.size() >= signature.params.size()
                                             : node.args.size() == signature.params.size();
    for (std::size_t i = 0; i < node.args.size(); ++i) {
        if (count_ok && i < signature.params.size()) {
            check_call_arg(*node.args[i], signature.params[i]);
        } else if (node.args[i]) {
            const Type arg = check_expr(*node.args[i], true);
            if (slice_of_const_u8(arg)) {
                continue;
            }
            if (arg.alias_id != 0) {
                error(expr_loc(*node.args[i]), "type mismatch");
            } else if (arg.kind != TypeKind::Invalid && !is_numeric(arg) && arg.kind != TypeKind::Pointer &&
                       arg.kind != TypeKind::Enum && arg.kind != TypeKind::Nullptr) {
                error(expr_loc(*node.args[i]), "not supported yet");
            }
        }
    }
    return *signature.result;
}

Type Checker::finish_static_call(const Expr::Call &node, const MethodInfo &method, bool deferring) {
    require_unsafe_call(method.unsafe_fn, node.loc);
    if (deferring && method.type.variadic) {
        error(node.loc, "not supported yet");
    }
    const bool count_ok = method.type.variadic ? node.args.size() >= method.type.params.size()
                                                : node.args.size() == method.type.params.size();
    if (!count_ok) {
        error(node.loc, "argument count");
    }
    for (std::size_t i = 0; i < node.args.size(); ++i) {
        if (count_ok && i < method.type.params.size()) {
            check_call_arg(*node.args[i], method.type.params[i]);
        } else {
            const Type arg = check_expr(*node.args[i], true);
            // A variadic C argument has no written type. A constant string passes its address.
            if (slice_of_const_u8(arg)) {
                continue;
            }
            if (arg.alias_id != 0) {
                error(expr_loc(*node.args[i]), "type mismatch");
            } else if (arg.kind != TypeKind::Invalid && !is_numeric(arg) && arg.kind != TypeKind::Pointer &&
                       arg.kind != TypeKind::Enum && arg.kind != TypeKind::Nullptr) {
                error(expr_loc(*node.args[i]), "not supported yet");
            }
        }
    }
    if (method.type.result == nullptr) {
        return type_invalid();
    }
    return *method.type.result;
}

bool Checker::assoc_from_selector(const Expr::Selector &node, Type &out) {
    if (node.base == nullptr || node.field.empty()) {
        return false;
    }
    // `math.Entity.next`. The type is an export. The variable is too, or the name is not exported.
    if (node.type_args.empty()) {
        if (const auto *inner = std::get_if<Expr::Selector>(&node.base->data);
            inner != nullptr && inner->base != nullptr && inner->type_args.empty()) {
            if (const auto *package_name = std::get_if<Expr::Name>(&inner->base->data)) {
                const Symbol *package = lookup(package_name->text);
                if (package != nullptr && package->is_package) {
                    const Symbol *type = find_export(package->package_file, inner->field);
                    if (type != nullptr && type->is_type) {
                        const AssocVar *var = find_assoc_in(package->package_file, inner->field, node.field);
                        if (var != nullptr) {
                            if (!var->exported) {
                                error(node.loc, node.field + " is not exported");
                                out = type_invalid();
                                return true;
                            }
                            out = var->type;
                            node.resolved_owner = inner->field;
                            return true;
                        }
                        if (static_method_value(node, package->package_file, inner->field, out)) {
                            return true;
                        }
                        error(node.loc, node.field + " is not exported");
                        out = type_invalid();
                        return true;
                    }
                }
            }
        }
    }
    const auto shadowed = [&](const std::string &name) {
        const Symbol *symbol = lookup(name);
        return symbol != nullptr && !symbol->is_type;
    };
    const auto take = [&](const std::string &spec) {
        if (spec.empty()) {
            out = type_invalid();
            return true;
        }
        const AssocVar *var = find_assoc(spec, node.field);
        if (var != nullptr) {
            out = var->type;
            node.resolved_owner = spec;
            return true;
        }
        // `Box[i32].from` is the static function. An instance method on the type is `type mismatch`.
        const MethodInfo *method = find_named_method(spec, node.field);
        if (method == nullptr || method->type.kind != TypeKind::Function) {
            error(node.loc, "undeclared identifier");
            out = type_invalid();
            return true;
        }
        if (!method->more.empty()) {
            error(node.loc, "not supported yet");
            out = type_invalid();
            return true;
        }
        if (!method->is_static) {
            error(node.loc, "type mismatch");
            out = type_invalid();
            return true;
        }
        if (!allow_unsafe_fn(method->unsafe_fn, node.loc)) {
            out = type_invalid();
            return true;
        }
        out = method->type;
        node.resolved_owner = spec;
        return true;
    };
    // `pkg.Box[i32].limit` and `pkg.Buf[4, i32].count`. A value such as `pkg.row[0].x` is not a template.
    {
        std::vector<TypeArg> args = node.type_args;
        const Expr *type_expr = nullptr;
        const Expr::Index *index = nullptr;
        if (!args.empty()) {
            type_expr = node.base.get();
        } else if (const auto *found = std::get_if<Expr::Index>(&node.base->data)) {
            index = found;
            type_expr = found->base.get();
        }
        const auto *inner = type_expr == nullptr ? nullptr : std::get_if<Expr::Selector>(&type_expr->data);
        if (inner != nullptr && inner->base != nullptr && inner->type_args.empty()) {
            if (const auto *package_name = std::get_if<Expr::Name>(&inner->base->data)) {
                const Symbol *package = lookup(package_name->text);
                if (package != nullptr && package->is_package) {
                    uint32_t owner = package->package_file;
                    const StructDecl *templ = nullptr;
                    for (const uint32_t id : files_in_package(package->package_file)) {
                        const auto found = struct_templates_.find(id);
                        if (found == struct_templates_.end()) {
                            continue;
                        }
                        const auto have = found->second.find(inner->field);
                        if (have == found->second.end()) {
                            continue;
                        }
                        owner = id;
                        templ = have->second;
                        break;
                    }
                    if (templ != nullptr) {
                        if (index != nullptr) {
                            TypeArg arg;
                            if (index->index == nullptr || !type_arg_from_expr(*index->index, arg)) {
                                error(node.loc, "type mismatch");
                                out = type_invalid();
                                return true;
                            }
                            args.push_back(std::move(arg));
                        }
                        if (!templ->exported) {
                            error(inner->loc, inner->field + " is not exported");
                            out = type_invalid();
                            return true;
                        }
                        const uint32_t saved = file_id_;
                        file_id_ = owner;
                        const std::string spec = specialize_type_app("", inner->field, args, node.loc);
                        file_id_ = saved;
                        if (spec.empty()) {
                            out = type_invalid();
                            return true;
                        }
                        const AssocVar *var = find_assoc_in(package->package_file, spec, node.field);
                        if (var != nullptr) {
                            if (!var->exported) {
                                error(node.loc, node.field + " is not exported");
                                out = type_invalid();
                                return true;
                            }
                            out = var->type;
                            node.resolved_owner = spec;
                            return true;
                        }
                        if (static_method_value(node, owner, spec, out)) {
                            return true;
                        }
                        error(node.loc, node.field + " is not exported");
                        out = type_invalid();
                        return true;
                    }
                }
            }
        }
    }
    {
        bool failed = false;
        const std::string spec = same_package_spec(node, failed);
        if (failed) {
            out = type_invalid();
            return true;
        }
        if (!spec.empty()) {
            return take(spec);
        }
    }
    if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
        if (shadowed(named->text)) {
            return false;
        }
        if (!named_type_owner(named->text)) {
            return false;
        }
        const AssocVar *var = find_assoc(named->text, node.field);
        if (var == nullptr) {
            return false;
        }
        out = var->type;
        node.resolved_owner = named->text;
        return true;
    }
    return false;
}

Type Checker::finish_value_method(const Expr::Call &node, const Expr::Selector &selector, const Checker::MethodInfo &method,
                                  const Type &base, bool through_pointer) {
    if (method.is_static) {
        error(node.loc, "type mismatch");
        for (const auto &arg : node.args) {
            check_expr(*arg, true);
        }
        return type_invalid();
    }
    require_unsafe_call(method.unsafe_fn, node.loc);
    if (checking_defer_ && method.type.variadic) {
        error(node.loc, "not supported yet");
    }
    const std::size_t arity = method.type.params.size();
    const bool count_ok = method.type.variadic ? node.args.size() + 1 >= arity : node.args.size() + 1 == arity;
    if (!count_ok) {
        error(node.loc, "argument count");
    }
    if (!method.type.params.empty()) {
        const Type &receiver = method.type.params[0];
        const Type object = through_pointer ? *base.pointee : base;
        const auto *as_selector = selector.base == nullptr ? nullptr : std::get_if<Expr::Selector>(&selector.base->data);
        const bool enumerator =
            as_selector != nullptr && as_selector->base != nullptr && enum_designator(*as_selector->base) >= 0;
        if (receiver.kind == TypeKind::Pointer) {
            if (!through_pointer && (selector.base == nullptr || !is_lvalue_form(*selector.base) || enumerator)) {
                error(node.loc, "not an lvalue");
            } else {
                const Type pointer = method_pointer(base, object, through_pointer, *selector.base);
                if (!accepts_pointer(pointer, receiver)) {
                    mismatch(expr_loc(*selector.base), receiver, pointer);
                }
            }
        } else if (selector.base != nullptr) {
            expect(object, receiver, expr_loc(*selector.base));
        }
    }
    for (std::size_t i = 0; i < node.args.size(); ++i) {
        if (count_ok && i + 1 < arity) {
            check_call_arg(*node.args[i], method.type.params[i + 1]);
        } else {
            check_expr(*node.args[i], true);
        }
    }
    if (method.type.result == nullptr) {
        return type_invalid();
    }
    return *method.type.result;
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
                    // A function pointer is a pointer. A direct call of the inner function can pass it along.
                    if (!fn_pointer(symbol->type) || checking_.empty()) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    note_capture(*checking_.back(), node.text, symbol->function_id);
                    return symbol->type;
                }
                if (symbol->is_local_function && symbol->fn_captures && !direct_callee_) {
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
                if (symbol->is_function && symbol->lists.size() > 1) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (!allow_unsafe_fn(symbol->unsafe_fn, node.loc)) {
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
                return type_rank(1, true);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                (void)used;
                return type_float(node.is_f32 ? 32 : 64);
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::KwUnsafe) {
                    if (node.expr == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    ++unsafe_depth_;
                    const Type inner = check_expr(*node.expr, used);
                    --unsafe_depth_;
                    return inner;
                }
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
                            Type assoc;
                            if (assoc_from_selector(*selector, assoc)) {
                                if (assoc.kind == TypeKind::Invalid || selector_is_const(*selector)) {
                                    if (assoc.kind != TypeKind::Invalid) {
                                        error(node.loc, "not an lvalue");
                                    }
                                    return type_invalid();
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
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.expr->data);
                        selector != nullptr && selector->slice_len) {
                        error(node.loc, "not an lvalue");
                        return type_invalid();
                    }
                    if (const auto *index = std::get_if<Expr::Index>(&node.expr->data);
                        index != nullptr && index->slice_element) {
                        inner.is_const = true;
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
                    if (is_float(inner) || inner.kind == TypeKind::Bool) {
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
                            if (lhs.alias_id != rhs.alias_id || lhs.pointee == nullptr || rhs.pointee == nullptr ||
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
                if ((lhs.alias_id != 0 || rhs.alias_id != 0) && !same_type(lhs, rhs)) {
                    mismatch(node.loc, lhs, rhs);
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
                    case TokenKind::AmpAmp:
                    case TokenKind::PipePipe:
                        return type_int();
                    default:
                        if (lhs.alias_id != 0) {
                            return lhs;
                        }
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
                    if (index->is_range) {
                        if (index->base) {
                            check_expr(*index->base, true);
                        }
                        if (index->index) {
                            check_expr(*index->index, true);
                        }
                        if (index->end) {
                            check_expr(*index->end, true);
                        }
                        check_expr(*node.rhs, true);
                        error(node.loc, "not an lvalue");
                        return type_invalid();
                    }
                    const Type base = check_expr(*index->base, true);
                    const Type subscript = check_expr(*index->index, true);
                    expect(subscript, type_int(), expr_loc(*index->index));
                    if (is_slice(base)) {
                        error(node.loc, "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
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
                    Type assoc;
                    if (assoc_from_selector(*selector, assoc)) {
                        if (assoc.kind == TypeKind::Invalid) {
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        if (selector_is_const(*selector) || assoc.kind == TypeKind::Function) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        check_against(*node.rhs, assoc);
                        return assoc;
                    }
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (target.kind == SelectorTarget::Kind::Package) {
                        if (target.symbol->is_function || target.symbol->is_type || target.symbol->is_constant ||
                            (target.symbol->type.kind == TypeKind::Array && !is_slice(target.symbol->type))) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        check_against(*node.rhs, target.symbol->type);
                        return target.symbol->type;
                    }
                    const Type base = check_expr(*selector->base, true);
                    Type trait_field;
                    if (as_trait_field(base, selector->field, trait_field)) {
                        if (trait_field.kind == TypeKind::Invalid) {
                            error(node.loc, "undeclared identifier");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const bool through_pointer = base.kind != TypeKind::Trait;
                        if (!through_pointer && !is_lvalue_form(*selector->base)) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        if ((through_pointer && base.pointee != nullptr && base.pointee->is_const) ||
                            const_path(*node.lhs)) {
                            error(node.loc, "type mismatch");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        check_against(*node.rhs, trait_field);
                        return trait_field;
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
                    if (hidden_field(object, selector->field, node.loc)) {
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (from_slice_element(selector->base.get()) ||
                        (base.kind == TypeKind::Pointer && base.pointee != nullptr && base.pointee->is_const) ||
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
                    (symbol->type.kind == TypeKind::Array && !is_slice(symbol->type))) {
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
                    return type_pointer(type_rank(1, true));
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
                    // `Box[i32].from(...)` and `Buf[4, i32].width()` are the same call on a specialization.
                    if (selector->base != nullptr) {
                        auto check_args = [&]() {
                            for (const auto &arg : node.args) {
                                check_expr(*arg, true);
                            }
                        };
                        Type foreign;
                        if (package_static_call(node, *selector, deferring, foreign)) {
                            return foreign;
                        }
                        bool failed = false;
                        const std::string spec = same_package_spec(*selector, failed);
                        if (failed) {
                            check_args();
                            return type_invalid();
                        }
                        if (!spec.empty()) {
                            const AssocVar *var = find_assoc(spec, selector->field);
                            const MethodInfo *method = find_named_method(spec, selector->field);
                            if (var == nullptr || (method != nullptr && (method->is_static || !method->more.empty()))) {
                                if (method != nullptr && !method->more.empty()) {
                                    method = choose_method(node, *method, true, !package_owns(method->file_id),
                                                           selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function ||
                                    method->type.result == nullptr) {
                                    error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                            : "undeclared identifier");
                                    check_args();
                                    return type_invalid();
                                }
                                selector->resolved_owner = spec;
                                return finish_static_call(node, *method, deferring);
                            }
                        }
                        if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
                            const Symbol *symbol = lookup(named->text);
                            const bool shadowed = symbol != nullptr && !symbol->is_type;
                            if (!shadowed && named_type_owner(named->text)) {
                                const MethodInfo *method = find_named_method(named->text, selector->field);
                                if (method != nullptr && !method->more.empty()) {
                                    method = choose_method(node, *method, true, !package_owns(method->file_id),
                                                           selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function ||
                                    method->type.result == nullptr) {
                                    error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                            : "undeclared identifier");
                                    check_args();
                                    return type_invalid();
                                }
                                return finish_static_call(node, *method, deferring);
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
                            return type_pointer(type_rank(1, true));
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
                                    check_call_arg(*node.args[i], method->type.params[i]);
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
                            bool hidden = false;
                            uint32_t home = 0;
                            if (enum_id >= 0 && enum_id < static_cast<int>(enum_types_.size())) {
                                home = enum_types_[static_cast<std::size_t>(enum_id)].file_id;
                            }
                            if (const MethodInfo *method = find_visible_method(home, enum_name, selector->field, hidden)) {
                                if (!method->more.empty()) {
                                    method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                selector->resolved_file = method->file_id;
                                return finish_value_method(node, *selector, *method, base, through_pointer);
                            }
                            if (hidden) {
                                error(node.loc, selector->field + " is not exported");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                        }
                        bool alias_pointer = false;
                        if (const Type *alias = scalar_alias(base, alias_pointer)) {
                            bool hidden = false;
                            uint32_t home = 0;
                            if (alias->alias_id > 0 && static_cast<std::size_t>(alias->alias_id) < alias_file_.size()) {
                                home = alias_file_[static_cast<std::size_t>(alias->alias_id)];
                            }
                            const MethodInfo *method = find_visible_method(home, type_desc(*alias), selector->field, hidden);
                            if (method != nullptr && !method->more.empty()) {
                                method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                if (method == nullptr) {
                                    return type_invalid();
                                }
                            }
                            if (method == nullptr || method->type.kind != TypeKind::Function ||
                                method->type.result == nullptr) {
                                error(node.loc, hidden ? selector->field + " is not exported" : "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (!method->is_static) {
                                selector->resolved_owner = type_desc(*alias);
                                selector->resolved_file = method->file_id;
                            }
                            return finish_value_method(node, *selector, *method, base, alias_pointer);
                        }
                        bool slice_pointer = false;
                        if (const Type *alias = slice_alias(base, slice_pointer)) {
                            bool hidden = false;
                            uint32_t home = 0;
                            if (alias->alias_id > 0 && static_cast<std::size_t>(alias->alias_id) < alias_file_.size()) {
                                home = alias_file_[static_cast<std::size_t>(alias->alias_id)];
                            }
                            const MethodInfo *method = find_visible_method(home, type_desc(*alias), selector->field, hidden);
                            if (method != nullptr && !method->more.empty()) {
                                method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                if (method == nullptr) {
                                    return type_invalid();
                                }
                            }
                            if (method == nullptr || method->type.kind != TypeKind::Function ||
                                method->type.result == nullptr) {
                                error(node.loc, hidden ? selector->field + " is not exported" : "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (!method->is_static) {
                                selector->resolved_owner = type_desc(*alias);
                                selector->resolved_file = method->file_id;
                            }
                            return finish_value_method(node, *selector, *method, base, slice_pointer);
                        }
                        Type object;
                        bool through_pointer = false;
                        if (dot_object(base, object, through_pointer)) {
                            bool hidden = false;
                            uint32_t home = 0;
                            std::string owner;
                            if (object.struct_id >= 0 && object.struct_id < static_cast<int>(structs_.size())) {
                                home = structs_[static_cast<std::size_t>(object.struct_id)].file_id;
                                owner = structs_[static_cast<std::size_t>(object.struct_id)].name;
                            }
                            if (const MethodInfo *method = find_visible_method(home, owner, selector->field, hidden)) {
                                if (!method->more.empty()) {
                                    method = choose_method(node, *method, false, !package_owns(home), selector->field);
                                    if (method == nullptr) {
                                        return type_invalid();
                                    }
                                }
                                if (method->is_static) {
                                    error(node.loc, "type mismatch");
                                    for (const auto &arg : node.args) {
                                        check_expr(*arg, true);
                                    }
                                    return type_invalid();
                                }
                                selector->resolved_file = method->file_id;
                                require_unsafe_call(method->unsafe_fn, node.loc);
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
                                            const Type pointer =
                                                method_pointer(base, object, through_pointer, *selector->base);
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
                                        check_call_arg(*node.args[i], method->type.params[i + 1]);
                                    } else {
                                        check_expr(*node.args[i], true);
                                    }
                                }
                                if (method->type.result == nullptr) {
                                    return type_invalid();
                                }
                                return *method->type.result;
                            }
                            if (hidden) {
                                error(node.loc, selector->field + " is not exported");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
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
                if (node.callee != nullptr) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.callee->data)) {
                        if (const Symbol *symbol = lookup(named->text);
                            symbol != nullptr && symbol->is_function && !symbol->is_template && symbol->lists.size() > 1) {
                            return finish_overloaded(node, *symbol, deferring);
                        }
                    } else if (const auto *called = std::get_if<Expr::Selector>(&node.callee->data)) {
                        if (const auto *base = called->base == nullptr ? nullptr : std::get_if<Expr::Name>(&called->base->data)) {
                            const Symbol *package = base == nullptr ? nullptr : lookup(base->text);
                            if (package != nullptr && package->is_package) {
                                const Symbol *member = find_export(package->package_file, called->field);
                                if (member != nullptr && member->is_function && !member->is_template &&
                                    member->lists.size() > 1) {
                                    return finish_overloaded(node, *member, deferring);
                                }
                            }
                        }
                    }
                }
                const bool saved_direct = direct_callee_;
                direct_callee_ = std::holds_alternative<Expr::Name>(node.callee->data) ||
                                 std::holds_alternative<Expr::Func>(node.callee->data) ||
                                 std::holds_alternative<Expr::Selector>(node.callee->data);
                const Type callee = check_expr(*node.callee, true);
                direct_callee_ = saved_direct;
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
                        check_call_arg(*node.args[i], signature->params[i]);
                    } else {
                        const Type arg = check_expr(*node.args[i], true);
                        // A variadic C argument has no written type. A constant string passes its address.
                        if (slice_of_const_u8(arg)) {
                            continue;
                        }
                        if (arg.alias_id != 0) {
                            error(expr_loc(*node.args[i]), "type mismatch");
                        } else if (arg.kind != TypeKind::Invalid && !is_numeric(arg) && arg.kind != TypeKind::Pointer &&
                                   arg.kind != TypeKind::Enum && arg.kind != TypeKind::Nullptr) {
                            error(expr_loc(*node.args[i]), "not supported yet");
                        }
                    }
                }
                return *signature->result;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.base != nullptr) {
                    const int enum_id = enum_designator(*node.base);
                    if (enum_id >= 0 && find_enum_member(enum_id, node.field) != nullptr) {
                        return type_enum(enum_id);
                    }
                    Type assoc;
                    if (assoc_from_selector(node, assoc)) {
                        return assoc;
                    }
                    // `Point.make` names the static function. An enumerator such as `Color.Red` was returned above.
                    if (const auto *named = std::get_if<Expr::Name>(&node.base->data)) {
                        const Symbol *symbol = lookup(named->text);
                        const bool shadowed = symbol != nullptr && !symbol->is_type;
                        if (!shadowed && named_type_owner(named->text)) {
                            const MethodInfo *method = find_named_method(named->text, node.field);
                            if (method != nullptr && !method->more.empty()) {
                                error(node.loc, "not supported yet");
                                return type_invalid();
                            }
                            if (method == nullptr || !method->is_static || method->type.kind != TypeKind::Function) {
                                error(node.loc, method != nullptr && !method->is_static ? "type mismatch"
                                                                                        : "undeclared identifier");
                                return type_invalid();
                            }
                            if (!allow_unsafe_fn(method->unsafe_fn, node.loc)) {
                                return type_invalid();
                            }
                            return method->type;
                        }
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
                    if (target.symbol->is_function && target.symbol->lists.size() > 1) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    if (!allow_unsafe_fn(target.symbol->unsafe_fn, node.loc)) {
                        return type_invalid();
                    }
                    return target.symbol->type;
                }
                const Type raw = check_raw(*node.base, true);
                // `name.len` is the number of elements, for a slice and for a sized array.
                // The array decays to a pointer only when the field is not `len`.
                if (raw.kind == TypeKind::Array && raw.element != nullptr && node.field == "len") {
                    node.slice_len = true;
                    if (raw.length >= 0) {
                        node.len_value = raw.length;
                    }
                    return type_int();
                }
                // `a.len` on a parameter `[N]T`. The value is a pointer. The length is the one that was written.
                if (raw.kind == TypeKind::Pointer && raw.decayed_len > 0 && node.field == "len") {
                    node.slice_len = true;
                    node.len_value = raw.decayed_len;
                    return type_int();
                }
                const Type base = value_type(raw);
                if (is_slice(base)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                Type trait_field;
                if (as_trait_field(base, node.field, trait_field)) {
                    if (trait_field.kind == TypeKind::Invalid) {
                        error(node.loc, "undeclared identifier");
                        return type_invalid();
                    }
                    return trait_field;
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
                if (hidden_field(object, node.field, node.loc)) {
                    return type_invalid();
                }
                if (from_slice_element(node.base.get())) {
                    field.is_const = true;
                }
                return field;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                (void)used;
                // `"Hello"` and a raw string are `[]const u8`. The bytes still end with a null.
                return string_slice();
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                (void)used;
                // `a[i:j]` is a slice of the elements from i up to, but not including, j.
                // A missing low bound is 0. A missing high bound is the length.
                // The base is an array, a slice, or a parameter `[N]T`. Any other pointer is not.
                if (node.is_range) {
                    const Type base = node.base ? check_raw(*node.base, true) : type_invalid();
                    if (node.index) {
                        const Type low = check_expr(*node.index, true);
                        expect(low, type_int(), expr_loc(*node.index));
                    }
                    if (node.end) {
                        const Type high = check_expr(*node.end, true);
                        expect(high, type_int(), expr_loc(*node.end));
                    }
                    if (base.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    // `row[0:2]` on `func open(row [4]i32)`. The pointer keeps the written length.
                    if (base.kind == TypeKind::Pointer && base.decayed_len > 0 && base.pointee != nullptr &&
                        !base.optional) {
                        return type_array(*base.pointee, -1);
                    }
                    if (base.kind != TypeKind::Array || base.element == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    Type slice = type_array(*base.element, -1);
                    // `type Row []i32` stays `Row`. A sized array becomes the plain slice `[]T`.
                    if (is_slice(base)) {
                        slice.alias_id = base.alias_id;
                    }
                    return slice;
                }
                // An array keeps its element, including when that element is an array.
                // A pointer to an array still means `(*p)[i]`.
                const Type base = check_raw(*node.base, true);
                const Type subscript = check_expr(*node.index, true);
                expect(subscript, type_int(), expr_loc(*node.index));
                if (base.kind == TypeKind::Array && base.element != nullptr) {
                    Type element = *base.element;
                    if (base.is_const || is_slice(base)) {
                        element.is_const = true;
                    }
                    if (is_slice(base)) {
                        node.slice_element = true;
                    }
                    return element;
                }
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
                                               node.const_pointee, node.is_array, node.array_len, node.elem_const,
                                               node.elem_slice, node.shells);
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
                if (node.is_fn_ptr && node.type_name == "invalid") {
                    if (node.expr) {
                        check_expr(*node.expr, true);
                    }
                    return type_invalid();
                }
                const Type target =
                    node.is_fn_ptr
                        ? type_of_fn_ptr(node.type_name, node.type_package, node.is_struct, node.is_enum,
                                         node.ret_indirections, node.indirections < 1 ? 1 : node.indirections,
                                         node.fn_params, node.fn_variadic, node.fn_results, node.loc, node.ret_optional,
                                         node.ret_const_pointee)
                        : spelled_type(node.type_name, node.type_package, node.is_struct, node.is_enum, node.indirections,
                                       node.loc, node.optional, node.const_pointee);
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
                Type source_rep = source;
                Type target_rep = target;
                source_rep.alias_id = 0;
                target_rep.alias_id = 0;
                const bool source_scalar = is_numeric(source_rep);
                const bool target_scalar = is_numeric(target_rep);
                const bool source_int = same_type(source_rep, type_int());
                const bool target_int = same_type(target_rep, type_int());
                if ((target.kind == TypeKind::Pointer && source.kind == TypeKind::Pointer) ||
                    (target.kind == TypeKind::Pointer && source_int) ||
                    (target.kind == TypeKind::Pointer && source.kind == TypeKind::Nullptr) ||
                    (target_int && source.kind == TypeKind::Pointer) ||
                    (target_scalar && source_scalar) || same_type(source_rep, target_rep)) {
                    if (target.kind == TypeKind::Pointer && source.kind == TypeKind::Pointer &&
                        unsafe_pointer_cast(source, target) && unsafe_depth_ == 0) {
                        error(node.loc, "unsafe cast");
                    }
                    return target;
                }
                error(node.loc, "type mismatch");
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Init>) {
                (void)used;
                if (node.array_literal || !node.type_name.empty()) {
                    return check_init(const_cast<Expr::Init &>(node), type_invalid());
                }
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
                const bool as_callee = direct_callee_;
                direct_callee_ = false;
                if (node.decl == nullptr) {
                    direct_callee_ = as_callee;
                    return type_invalid();
                }
                const FunctionDecl &fn = *node.decl;
                if (fn.inferred && !fn.elaborated) {
                    direct_callee_ = as_callee;
                    error(fn.loc, "type mismatch");
                    return type_invalid();
                }
                if (fn.inferred && fn.elaborated) {
                    direct_callee_ = as_callee;
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
                // Hidden arguments are passed at a direct call. The function cannot escape.
                if (!fn.captures.empty() && !as_callee) {
                    error(fn.loc, "not supported yet");
                }
                direct_callee_ = as_callee;
                return signature;
            } else if constexpr (std::is_same_v<T, Expr::New> || std::is_same_v<T, Expr::Delete>) {
                (void)used;
                if (scopes_.size() <= 1) {
                    error(node.loc, "global initializer is not a constant");
                    return type_invalid();
                }
                const auto resolve_allocator = [&](Expr *allocator, bool &builtin_alloc, uint32_t &alloc_file,
                                                   std::string &alloc_name, bool &alloc_is_pointer) {
                    builtin_alloc = allocator == nullptr;
                    alloc_file = 0;
                    alloc_name.clear();
                    alloc_is_pointer = false;
                    if (allocator == nullptr) {
                        return;
                    }
                    const Type got = check_expr(*allocator, true);
                    int struct_id = -1;
                    // The argument is `*T` where `T` has `Allocator`'s methods. `T` is the pointee.
                    if (got.kind == TypeKind::Pointer && got.pointee != nullptr &&
                        got.pointee->kind == TypeKind::Struct) {
                        struct_id = got.pointee->struct_id;
                        alloc_is_pointer = true;
                    } else if (got.kind == TypeKind::Struct) {
                        mismatch(expr_loc(*allocator), type_pointer(got), got);
                        return;
                    } else if (got.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
                        return;
                    }
                    const StructInfo &info = structs_[static_cast<std::size_t>(struct_id)];
                    alloc_file = info.file_id;
                    alloc_name = info.name;
                    if (info.name == "Malloc" && is_std_builtin_file(info.file_id)) {
                        builtin_alloc = true;
                        return;
                    }
                    ensure_allocator_trait();
                    require_satisfies(struct_id, allocator_trait_id_, true, node.loc);
                    auto trait_form = [&](const char *name) -> const MethodInfo * {
                        const MethodInfo *method = find_method(struct_id, name);
                        if (method == nullptr || method->more.empty()) {
                            return method;
                        }
                        const TraitInfo::Method *wanted = find_trait_method(allocator_trait_id_, name);
                        if (wanted == nullptr) {
                            return method;
                        }
                        const MethodInfo *matched = nullptr;
                        auto consider = [&](const MethodInfo &info) {
                            if (trait_method_matches(wanted->type, info.type)) {
                                matched = &info;
                            }
                        };
                        consider(*method);
                        for (const MethodInfo &extra : method->more) {
                            consider(extra);
                        }
                        return matched != nullptr ? matched : method;
                    };
                    for (const char *name : {"malloc", "free", "realloc"}) {
                        const MethodInfo *method = trait_form(name);
                        const bool pointer = method != nullptr && method->type.kind == TypeKind::Function &&
                                             !method->type.params.empty() &&
                                             method->type.params[0].kind == TypeKind::Pointer;
                        if (method != nullptr && !pointer) {
                            error(node.loc, "type mismatch");
                        }
                    }
                    // `malloc` takes `*this`. A const allocator is rejected the same way as `arena.malloc(...)`.
                    const MethodInfo *malloc_method = trait_form("malloc");
                    if (malloc_method != nullptr && malloc_method->type.kind == TypeKind::Function &&
                        !malloc_method->type.params.empty() &&
                        malloc_method->type.params[0].kind == TypeKind::Pointer) {
                        const Type object =
                            got.kind == TypeKind::Pointer && got.pointee != nullptr ? *got.pointee : got;
                        const Type passed = method_pointer(got, object, alloc_is_pointer, *allocator);
                        if (!accepts_pointer(passed, malloc_method->type.params[0])) {
                            mismatch(expr_loc(*allocator), malloc_method->type.params[0], passed);
                        }
                    }
                };
                if constexpr (std::is_same_v<T, Expr::Delete>) {
                    resolve_allocator(node.allocator.get(), node.builtin_alloc, node.alloc_file, node.alloc_name,
                                      node.alloc_is_pointer);
                    if (node.expr == nullptr) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    const Type got = check_expr(*node.expr, true);
                    if (got.kind != TypeKind::Invalid && got.kind != TypeKind::Pointer) {
                        error(node.loc, "type mismatch");
                    }
                    return type_void();
                } else {
                    resolve_allocator(node.allocator.get(), node.builtin_alloc, node.alloc_file, node.alloc_name,
                                      node.alloc_is_pointer);
                    if (node.type_name.empty()) {
                        error(node.loc, "expected a type");
                        return type_invalid();
                    }
                    bool is_enum = false;
                    bool is_array = false;
                    int array_len = 0;
                    std::string array_len_name;
                    // Specializing `Box[i32]` rewrites the name on the expression.
                    Expr::New &written = const_cast<Expr::New &>(node);
                    prepare_written(written.type_name, written.type_package, written.is_struct, is_enum,
                                    written.indirections, written.optional, is_array, array_len, array_len_name,
                                    written.type_args, written.loc);
                    if (node.count) {
                        const Type got = check_expr(*node.count, true);
                        if (got.kind != TypeKind::Invalid) {
                            expect(got, type_int(), expr_loc(*node.count));
                        }
                        if (const std::optional<int> folded = fold_int(*node.count); folded && *folded <= 0) {
                            error(expr_loc(*node.count), "type mismatch");
                        }
                    }
                    const Type object = spelled_type(node.type_name, node.type_package, node.is_struct, false,
                                                     node.indirections, node.loc, node.optional, node.const_pointee);
                    if (object.kind == TypeKind::Invalid) {
                        return type_invalid();
                    }
                    if (object.kind == TypeKind::Void || object.kind == TypeKind::Trait ||
                        object.kind == TypeKind::Function) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    // A non-nullable pointer has to be written. A buffer has no place to name each field.
                    auto loose_init = [&]() {
                        const auto *init = node.init ? std::get_if<Expr::Init>(&node.init->data) : nullptr;
                        if (init == nullptr) {
                            return;
                        }
                        for (const Expr::FieldInit &field : init->fields) {
                            if (field.value) {
                                check_expr(*field.value, true);
                            }
                        }
                        for (const auto &element : init->elements) {
                            if (element) {
                                check_expr(*element, true);
                            }
                        }
                    };
                    if (node.count || node.count_inferred) {
                        const auto *list = node.init == nullptr ? nullptr : std::get_if<Expr::Init>(&node.init->data);
                        if (node.init != nullptr && (list == nullptr || !list->fields.empty())) {
                            error(expr_loc(*node.init), "type mismatch");
                            loose_init();
                            return type_pointer(object);
                        }
                        auto check_element = [&](const Expr &element) { check_against(element, object); };
                        if (node.count_inferred) {
                            // `_` takes its length from the list. `...` has no length to repeat.
                            if (list == nullptr || node.repeat_init || list->elements.empty()) {
                                error(node.loc, "type mismatch");
                                return type_invalid();
                            }
                            for (const auto &element : list->elements) {
                                if (element) {
                                    check_element(*element);
                                }
                            }
                            return type_pointer(type_array(object, static_cast<int>(list->elements.size())));
                        }
                        if (list != nullptr) {
                            if (node.repeat_init) {
                                if (list->elements.size() != 1 || list->elements.front() == nullptr) {
                                    error(expr_loc(*node.init), "type mismatch");
                                } else {
                                    check_element(*list->elements.front());
                                }
                            } else if (list->elements.empty()) {
                                error(expr_loc(*node.init), "type mismatch");
                            } else {
                                const std::optional<int> folded = node.count ? fold_int(*node.count) : std::nullopt;
                                if (!folded || *folded != static_cast<int>(list->elements.size())) {
                                    error(expr_loc(*node.init), "type mismatch");
                                }
                                for (const auto &element : list->elements) {
                                    if (element) {
                                        check_element(*element);
                                    }
                                }
                            }
                        } else if (needs_nonnull_pointer(object)) {
                            error(node.loc, "type mismatch");
                        }
                        return type_pointer(object);
                    }
                    // The fields are checked against the reserved object. A non-struct has no fields.
                    if (node.init) {
                        auto *init = std::get_if<Expr::Init>(&node.init->data);
                        if (init == nullptr || object.kind != TypeKind::Struct) {
                            error(expr_loc(*node.init), "type mismatch");
                            loose_init();
                        } else {
                            check_init(*init, object);
                        }
                    } else if (needs_nonnull_pointer(object)) {
                        error(node.loc, "type mismatch");
                    }
                    return type_pointer(object);
                }
            } else {
                (void)used;
                return type_invalid();
            }
        },
        expr.data);
}

Type Checker::operand_type(bool on_type, const std::string &type_name, const std::string &type_package, bool is_struct,
                           bool is_enum, int indirections, uint32_t optional, const Expr *expr, SourceLoc loc,
                           uint32_t const_pointee, bool is_array, int array_len, bool elem_const, bool elem_slice,
                           const std::vector<TypeShell> &shells) {
    if (on_type) {
        if (is_array || elem_slice || !shells.empty()) {
            std::vector<TypeShell> folded = shells;
            fold_shells(folded, loc);
            return object_type(type_name, indirections, is_array, array_len, loc, false, type_package, is_struct, is_enum,
                               false, optional, const_pointee, false, elem_const, elem_slice, folded);
        }
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
                        node.optional, node.expr.get(), node.loc, node.const_pointee, node.is_array, node.array_len,
                        node.elem_const, node.elem_slice, node.shells);
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

namespace {

double round_float(double value, bool wide) {
    if (wide) {
        return value;
    }
    return static_cast<double>(static_cast<float>(value));
}

// f32 arithmetic rounds each step. f64 arithmetic uses the wider value.
double float_arith(TokenKind op, double lhs, double rhs, bool wide) {
    if (wide) {
        switch (op) {
        case TokenKind::Plus: return lhs + rhs;
        case TokenKind::Minus: return lhs - rhs;
        case TokenKind::Star: return lhs * rhs;
        case TokenKind::Slash: return lhs / rhs;
        default: return 0;
        }
    }
    const float left = static_cast<float>(lhs);
    const float right = static_cast<float>(rhs);
    float value = 0;
    switch (op) {
    case TokenKind::Plus: value = left + right; break;
    case TokenKind::Minus: value = left - right; break;
    case TokenKind::Star: value = left * right; break;
    case TokenKind::Slash: value = left / right; break;
    default: break;
    }
    return static_cast<double>(value);
}

std::optional<int> eval_int_op(TokenKind op, int lhs, int rhs) {
    auto wrap = [](int64_t value) { return static_cast<int>(static_cast<uint32_t>(value)); };
    const int64_t a = lhs;
    const int64_t b = rhs;
    switch (op) {
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
    case TokenKind::AmpAmp: return (lhs != 0 && rhs != 0) ? 1 : 0;
    case TokenKind::PipePipe: return (lhs != 0 || rhs != 0) ? 1 : 0;
    default: return std::nullopt;
    }
}

bool is_float_logic(TokenKind op) {
    switch (op) {
    case TokenKind::EqEq:
    case TokenKind::BangEq:
    case TokenKind::Lt:
    case TokenKind::Gt:
    case TokenKind::Le:
    case TokenKind::Ge:
    case TokenKind::AmpAmp:
    case TokenKind::PipePipe:
        return true;
    default:
        return false;
    }
}

} // namespace

std::optional<std::string> Checker::type_constant(const Expr &expr) const {
    const auto *kind = std::get_if<Expr::Typeof>(&expr.data);
    if (kind == nullptr || kind->canonical.empty()) {
        return std::nullopt;
    }
    return kind->canonical;
}

std::optional<Checker::FoldedNumber> Checker::fold_number(const Expr &expr) {
    std::unordered_set<std::string> seen;
    return fold_number(expr, seen);
}

std::optional<Checker::FoldedNumber> Checker::fold_number(const Expr &expr, std::unordered_set<std::string> &seen) {
    const auto from_int = [](int value) {
        return FoldedNumber{static_cast<double>(value), false, false};
    };
    return std::visit(
        [&](const auto &node) -> std::optional<FoldedNumber> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int> || std::is_same_v<T, Expr::Char> || std::is_same_v<T, Expr::Bool> ||
                          std::is_same_v<T, Expr::Sizeof>) {
                const std::optional<int> value = fold_int(expr);
                if (!value) {
                    return std::nullopt;
                }
                return from_int(*value);
            } else if constexpr (std::is_same_v<T, Expr::Float>) {
                const double parsed = node.text.empty() ? 0.0 : std::strtod(node.text.c_str(), nullptr);
                if (node.is_f32) {
                    return FoldedNumber{round_float(parsed, false), true, false};
                }
                return FoldedNumber{parsed, true, true};
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                if (const std::optional<int> value = fold_int(expr)) {
                    return from_int(*value);
                }
                const Symbol *symbol = lookup(node.text);
                if (symbol == nullptr || !symbol->is_constant || symbol->type.kind != TypeKind::Float) {
                    return std::nullopt;
                }
                const VarDecl *var = global_var(node.text);
                if (var == nullptr || var->init == nullptr || !seen.insert(node.text).second) {
                    return std::nullopt;
                }
                const std::optional<FoldedNumber> inner = fold_number(*var->init, seen);
                seen.erase(node.text);
                return inner;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::KwUnsafe && node.expr != nullptr) {
                    return fold_number(*node.expr, seen);
                }
                if (node.postfix || node.expr == nullptr ||
                    (node.op != TokenKind::Plus && node.op != TokenKind::Minus)) {
                    return std::nullopt;
                }
                std::optional<FoldedNumber> inner = fold_number(*node.expr, seen);
                if (!inner) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::Minus) {
                    inner->value = -inner->value;
                }
                return inner;
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_fn_ptr || node.expr == nullptr) {
                    return std::nullopt;
                }
                std::optional<FoldedNumber> inner = fold_number(*node.expr, seen);
                if (!inner) {
                    return std::nullopt;
                }
                if (node.type_name == "f32" || node.type_name == "float") {
                    inner->value = round_float(inner->value, false);
                    inner->is_float = true;
                    inner->wide = false;
                } else if (node.type_name == "f64" || node.type_name == "double") {
                    inner->is_float = true;
                    inner->wide = true;
                }
                return inner;
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return std::nullopt;
                }
                const std::optional<int> left_int = fold_int(*node.lhs);
                const std::optional<int> right_int = fold_int(*node.rhs);
                if (left_int && right_int) {
                    const std::optional<int> value = eval_int_op(node.op, *left_int, *right_int);
                    if (!value) {
                        return std::nullopt;
                    }
                    return from_int(*value);
                }
                const std::optional<FoldedNumber> left = fold_number(*node.lhs, seen);
                const std::optional<FoldedNumber> right = fold_number(*node.rhs, seen);
                if (!left || !right || (!left->is_float && !right->is_float)) {
                    return std::nullopt;
                }
                const bool wide = left->wide || right->wide;
                const double a = round_float(left->value, wide);
                const double b = round_float(right->value, wide);
                double value = 0;
                switch (node.op) {
                case TokenKind::Plus:
                case TokenKind::Minus:
                case TokenKind::Star:
                    value = float_arith(node.op, a, b, wide);
                    break;
                case TokenKind::Slash:
                    if (b == 0.0) {
                        return std::nullopt;
                    }
                    value = float_arith(node.op, a, b, wide);
                    break;
                case TokenKind::EqEq: value = a == b ? 1 : 0; break;
                case TokenKind::BangEq: value = a != b ? 1 : 0; break;
                case TokenKind::Lt: value = a < b ? 1 : 0; break;
                case TokenKind::Gt: value = a > b ? 1 : 0; break;
                case TokenKind::Le: value = a <= b ? 1 : 0; break;
                case TokenKind::Ge: value = a >= b ? 1 : 0; break;
                case TokenKind::AmpAmp: value = (a != 0.0 && b != 0.0) ? 1 : 0; break;
                case TokenKind::PipePipe: value = (a != 0.0 || b != 0.0) ? 1 : 0; break;
                default: return std::nullopt;
                }
                const bool logic = is_float_logic(node.op);
                if (logic) {
                    return from_int(value == 0.0 ? 0 : 1);
                }
                return FoldedNumber{value, true, wide};
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

std::optional<int> Checker::fold_int(const Expr &expr) {
    auto wrap = [](int64_t value) { return static_cast<int>(static_cast<uint32_t>(value)); };
    return std::visit(
        [&](const auto &node) -> std::optional<int> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int>) {
                // `1u32` fits in the compile-time integer. A value past i32 does not.
                if (node.value > 2147483647ULL) {
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
                if (symbol != nullptr && symbol->is_constant && plain_int_constant(symbol->type)) {
                    return symbol->const_value;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                if (node.len_value >= 0) {
                    return node.len_value;
                }
                const auto folded_assoc = [&](uint32_t file, const std::string &owner) -> std::optional<int> {
                    const AssocVar *var = find_assoc_in(file, owner, node.field);
                    if (var == nullptr || !var->is_const || !var->has_const) {
                        return std::nullopt;
                    }
                    return var->const_value;
                };
                const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base != nullptr) {
                    const Symbol *symbol = lookup(base->text);
                    if (symbol != nullptr && symbol->is_package) {
                        const Symbol *member = find_export(symbol->package_file, node.field);
                        if (member == nullptr || !member->is_constant || !plain_int_constant(member->type)) {
                            return std::nullopt;
                        }
                        return member->const_value;
                    }
                    if (symbol == nullptr || symbol->is_type) {
                        return folded_assoc(file_id_, base->text);
                    }
                    return std::nullopt;
                }
                const auto *inner = node.base == nullptr ? nullptr : std::get_if<Expr::Selector>(&node.base->data);
                if (inner != nullptr && inner->base != nullptr && inner->type_args.empty()) {
                    if (const auto *package_name = std::get_if<Expr::Name>(&inner->base->data)) {
                        const Symbol *package = lookup(package_name->text);
                        if (package != nullptr && package->is_package) {
                            const Symbol *type = find_export(package->package_file, inner->field);
                            if (type != nullptr && type->is_type) {
                                return folded_assoc(package->package_file, inner->field);
                            }
                        }
                    }
                }
                return std::nullopt;
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
                if (node.postfix || node.expr == nullptr) {
                    return std::nullopt;
                }
                if (node.op == TokenKind::KwUnsafe) {
                    return fold_int(*node.expr);
                }
                if (node.op == TokenKind::Bang) {
                    if (const std::optional<int> inner = fold_int(*node.expr)) {
                        return *inner == 0 ? 1 : 0;
                    }
                    const std::optional<FoldedNumber> inner = fold_number(*node.expr);
                    if (!inner) {
                        return std::nullopt;
                    }
                    return inner->value == 0.0 ? 1 : 0;
                }
                const std::optional<int> inner = fold_int(*node.expr);
                if (!inner) {
                    return std::nullopt;
                }
                switch (node.op) {
                case TokenKind::Plus: return *inner;
                case TokenKind::Minus: return wrap(-static_cast<int64_t>(*inner));
                case TokenKind::Tilde: return wrap(~static_cast<uint32_t>(*inner));
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                if (node.indirections != 0 || node.is_struct || node.is_enum || node.expr == nullptr ||
                    !node.type_package.empty()) {
                    return std::nullopt;
                }
                Type named;
                if (!fill_builtin(node.type_name, named) && !package_typedef(file_id_, node.type_name, named)) {
                    return std::nullopt;
                }
                if (!is_integer(named) && named.kind != TypeKind::Bool && !is_c_enum(named)) {
                    return std::nullopt;
                }
                return fold_int(*node.expr);
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                if (node.lhs == nullptr || node.rhs == nullptr) {
                    return std::nullopt;
                }
                if (const std::optional<std::string> left_type = type_constant(*node.lhs)) {
                    if (const std::optional<std::string> right_type = type_constant(*node.rhs)) {
                        const bool same = *left_type == *right_type;
                        if (node.op == TokenKind::EqEq) {
                            return same ? 1 : 0;
                        }
                        if (node.op == TokenKind::BangEq) {
                            return same ? 0 : 1;
                        }
                        return std::nullopt;
                    }
                }
                const std::optional<int> lhs = fold_int(*node.lhs);
                const std::optional<int> rhs = fold_int(*node.rhs);
                if (lhs && rhs) {
                    return eval_int_op(node.op, *lhs, *rhs);
                }
                if (!is_float_logic(node.op)) {
                    return std::nullopt;
                }
                const std::optional<FoldedNumber> left = fold_number(*node.lhs);
                const std::optional<FoldedNumber> right = fold_number(*node.rhs);
                if (!left || !right || (!left->is_float && !right->is_float)) {
                    return std::nullopt;
                }
                const bool wide = left->wide || right->wide;
                const double a = round_float(left->value, wide);
                const double b = round_float(right->value, wide);
                switch (node.op) {
                case TokenKind::EqEq: return a == b ? 1 : 0;
                case TokenKind::BangEq: return a != b ? 1 : 0;
                case TokenKind::Lt: return a < b ? 1 : 0;
                case TokenKind::Gt: return a > b ? 1 : 0;
                case TokenKind::Le: return a <= b ? 1 : 0;
                case TokenKind::Ge: return a >= b ? 1 : 0;
                case TokenKind::AmpAmp: return (a != 0.0 && b != 0.0) ? 1 : 0;
                case TokenKind::PipePipe: return (a != 0.0 || b != 0.0) ? 1 : 0;
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
        return symbol != nullptr && (symbol->is_immutable || symbol->is_constant) &&
               symbol->type.kind != TypeKind::Pointer;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        if (selector_is_const(*selector)) {
            return true;
        }
        if (selector->base != nullptr) {
            if (const auto *named = std::get_if<Expr::Name>(&selector->base->data)) {
                const Symbol *symbol = lookup(named->text);
                if (symbol != nullptr && symbol->is_package) {
                    const Symbol *member = find_export(symbol->package_file, selector->field);
                    if (member == nullptr) {
                        return false;
                    }
                    if (member->type.kind == TypeKind::Pointer) {
                        return member->type.pointee != nullptr && member->type.pointee->is_const;
                    }
                    return member->is_constant;
                }
            }
        }
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

Type Checker::method_pointer(const Type &base, const Type &object, bool through_pointer, const Expr &source) {
    if (through_pointer) {
        return base;
    }
    Type value = object;
    // `const origin` and `*const Point` both pass `*const Point` into the method.
    if (const_path(source)) {
        value.is_const = true;
    }
    return type_pointer(std::move(value));
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
                    const bool scalar =
                        is_scalar(got) && is_scalar(want) && got.alias_id == 0 && want.alias_id == 0;
                    if (!null_pointer && !widened && !scalar && !accepts_slice(got, want) &&
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
                // A short anonymous function, `(n) { ... }`, takes its types from the result.
                auto supplied = [&](const Expr &value, const Type &want) {
                    if (inferred_function(value) != nullptr) {
                        return check_against(value, want);
                    }
                    if (const auto *init = std::get_if<Expr::Init>(&value.data)) {
                        Type got = check_init(const_cast<Expr::Init &>(*init), want);
                        // `return {4, 6}` has no array address. `return [_]i32{4, 6}` does.
                        if (want.kind == TypeKind::Array && want.length >= 0 && !init->array_literal &&
                            got.kind != TypeKind::Invalid) {
                            error(expr_loc(value), "not supported yet");
                            return type_invalid();
                        }
                        return got;
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
                const Expr *call = node.call.get();
                int extra = 0;
                while (call != nullptr) {
                    const auto *unary = std::get_if<Expr::Unary>(&call->data);
                    if (unary == nullptr || unary->op != TokenKind::KwUnsafe || unary->expr == nullptr) {
                        break;
                    }
                    call = unary->expr.get();
                    ++extra;
                }
                if (call == nullptr || !std::holds_alternative<Expr::Call>(call->data)) {
                    if (node.call) {
                        check_expr(*node.call, false);
                    }
                    error(node.loc, "type mismatch");
                } else {
                    unsafe_depth_ += extra;
                    checking_defer_ = true;
                    check_expr(*call, false);
                    checking_defer_ = false;
                    unsafe_depth_ -= extra;
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value == nullptr || node.names.empty()) {
                    error(node.loc, "type mismatch");
                    return;
                }
                Type rhs = type_invalid();
                // `var row = [_]i32{1, 2}` keeps `[2]i32`. A read of the name still decays to `*i32`.
                if (node.names.size() == 1) {
                    if (auto *init = std::get_if<Expr::Init>(&node.value->data); init != nullptr && init->array_literal) {
                        rhs = check_array_literal(*init);
                    } else {
                        rhs = check_expr(*node.value, true);
                    }
                } else {
                    rhs = check_expr(*node.value, true);
                }
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
                    if (symbol.is_function || symbol.is_package || symbol.is_constant ||
                        (symbol.type.kind == TypeKind::Array && !is_slice(symbol.type))) {
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
                    if (!var.owner.empty()) {
                        error(var.loc, "not supported yet");
                        continue;
                    }
                    Type type = type_of_var(var);
                    Symbol symbol;
                    symbol.name = var.name;
                    symbol.type = type;
                    symbol.is_auto = true;
                    symbol.is_immutable = var.is_const_decl;
                    if (var.is_const_decl) {
                        symbol.type.is_const = true;
                    }
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    declare(std::move(symbol), var.loc);
                    const auto *list = var.init == nullptr ? nullptr : std::get_if<Expr::Init>(&var.init->data);
                    const bool array_list = var.is_array && !var.ptr_to_array && list != nullptr && !list->elements.empty();
                    // `var v []i32 = row` converts. `var copy [2]i32 = row` copies that array.
                    // A brace list is the other initializer. A file-scope name stays unsupported.
                    const bool slice = var.is_array && !var.ptr_to_array && var.array_len < 0;
                    const bool array_value = var.is_array && !var.ptr_to_array && var.array_len > 0 &&
                                             var.init != nullptr && list == nullptr;
                    if (var.is_const_decl && var.init == nullptr) {
                        error(var.loc, "type mismatch");
                    } else if (var.is_array && !var.ptr_to_array && var.init && !array_list && !slice && !array_value) {
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
    const int saved_unsafe = unsafe_depth_;
    unsafe_depth_ = fn.unsafe ? 1 : 0;
    if (fn.results.size() > 1) {
        for (const ResultSlot &slot : fn.results) {
            if (slot.is_fn_ptr) {
                error(fn.loc, "not supported yet");
                break;
            }
        }
    }
    checking_.push_back(const_cast<FunctionDecl *>(&fn));
    push_function();
    const_cast<FunctionDecl &>(fn).function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
    const Type saved = return_type_;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    const bool saved_variadic = variadic_function_;
    loop_depth_ = 0;
    switches_.clear();
    variadic_function_ = fn.variadic;
    resolve_result_typeof(const_cast<FunctionDecl &>(fn));
    return_type_ = function_result(fn);
    // Codegen reads `return_type`. Preparing the result slot rewrites `Vec[i32]` to the specialization.
    if (fn.results.size() == 1 && !fn.results.front().is_fn_ptr) {
        FunctionDecl &written = const_cast<FunctionDecl &>(fn);
        const ResultSlot &first = written.results.front();
        written.return_type = first.type_name;
        written.type_package = first.type_package;
        written.is_struct = first.is_struct;
        written.is_enum = first.is_enum;
        written.indirections = first.indirections;
        written.optional = first.optional;
        written.const_pointee = first.const_pointee;
    }
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
    adopt_captures(fn);
    variadic_function_ = saved_variadic;
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    unsafe_depth_ = saved_unsafe;
    pop();
    checking_.pop_back();
}

} // namespace tlc
