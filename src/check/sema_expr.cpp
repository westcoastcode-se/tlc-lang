// Type checking calls, initializers, and the expressions around them.
#include "check/check.hpp"
#include "check/check_util.hpp"

#include "check/detail.hpp"

namespace tlc {

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

Checker::TraitHome::TraitHome(Checker &checker, uint32_t home) : checker(checker), saved(checker.trait_fn_home_) {
    if (home != 0) {
        checker.trait_fn_home_ = home;
    }
}

Checker::TraitHome::~TraitHome() {
    checker.trait_fn_home_ = saved;
}

uint32_t Checker::function_home(const Expr &callee) {
    // `pkg.name` is the defining package. A bare name in this package uses `file_id_`:
    // a string unit's source id is not that file, and the two ids are the same otherwise.
    if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->base == nullptr) {
            return 0;
        }
        const auto *base = std::get_if<Expr::Name>(&selector->base->data);
        if (base == nullptr) {
            return 0;
        }
        const Symbol *package = lookup(base->text);
        if (package == nullptr || !package->is_package) {
            return 0;
        }
        const Symbol *symbol = find_export(package->package_file, selector->field);
        if (symbol == nullptr) {
            return 0;
        }
        if (symbol->package_file != 0) {
            return symbol->package_file;
        }
        if (!symbol->lists.empty() && symbol->lists.front().loc.valid()) {
            return symbol->lists.front().loc.file;
        }
        return symbol->loc.valid() ? symbol->loc.file : 0;
    }
    const auto *named = std::get_if<Expr::Name>(&callee.data);
    if (named == nullptr) {
        return 0;
    }
    const Symbol *symbol = lookup(named->text);
    if (symbol == nullptr || !symbol->is_function) {
        return 0;
    }
    if (symbol->package_file != 0 && symbol->package_file != file_id_) {
        return symbol->package_file;
    }
    return file_id_;
}

void Checker::reject_trait_prototype(const Type &signature, bool has_body, SourceLoc loc) {
    if (has_body) {
        return;
    }
    for (const Type &param : signature.params) {
        if (mentions_trait(param)) {
            error(loc, "not supported yet");
            return;
        }
    }
}

bool Checker::callee_has_body(const Expr &callee) {
    const Symbol *symbol = nullptr;
    if (const auto *named = std::get_if<Expr::Name>(&callee.data)) {
        symbol = lookup(named->text);
    } else if (const auto *selector = std::get_if<Expr::Selector>(&callee.data)) {
        if (selector->base != nullptr) {
            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                const Symbol *package = lookup(base->text);
                if (package != nullptr && package->is_package) {
                    symbol = find_export(package->package_file, selector->field);
                }
            }
        }
    }
    if (symbol == nullptr || !symbol->is_function) {
        return true;
    }
    if (!symbol->lists.empty()) {
        return symbol->lists.front().has_body;
    }
    return symbol->has_body;
}

uint32_t Checker::trait_signature_home(const Type &signature, const Expr *callee) {
    if (callee == nullptr) {
        return 0;
    }
    for (const Type &param : signature.params) {
        if (mentions_trait(param)) {
            return function_home(*callee);
        }
    }
    return 0;
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
            // `Shape` reaches `Parent` through the stored parent link. No table.
            if (trait_is(got.trait_id, want.trait_id)) {
                return;
            }
            expect(got, want, loc);
            return;
        }
        if (got.kind == TypeKind::Struct) {
            require_satisfies(got.struct_id, want.trait_id, false, loc, trait_fn_home_);
            return;
        }
        if (accept_iteration(got, want, loc)) {
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
            if (trait_is(got.pointee->trait_id, want.pointee->trait_id)) {
                return;
            }
            expect(*got.pointee, *want.pointee, loc);
            return;
        }
        if (got.kind == TypeKind::Pointer && got.pointee && got.pointee->kind == TypeKind::Struct) {
            require_satisfies(got.pointee->struct_id, want.pointee->trait_id, true, loc, trait_fn_home_);
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
    // `kind const Type` accepts `typeof(i32)`. Dropping const still fails.
    if (allows_added_const(got, want)) {
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
    if (got.kind == TypeKind::Invalid || got.kind == TypeKind::Bool) {
        return;
    }
    if (got.kind == TypeKind::Pointer) {
        error(loc, "compare with null");
        return;
    }
    error(loc, "type mismatch: expected bool, found " + type_desc(got));
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
                            // The nested pointer is the one entry in `fn_results`. The flattened
                            // name is the innermost result, as on a written `func (i32) func (i32) i32`.
                            spelled.type_name = result.type_name;
                            spelled.type_package = result.type_package;
                            spelled.ret_indirections = result.ret_indirections;
                            spelled.ret_optional = result.ret_optional;
                            spelled.ret_const_pointee = result.ret_const_pointee;
                            spelled.fn_results.push_back(fn_param_from_spelling(result));
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
        error(fn.loc, "trait has no size");
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
        // A brace list is that row. A name, an index, or a call copies the array.
        check_against(*element, elem);
    }
    return type_array(std::move(elem), static_cast<int>(init.elements.size()));
}

Type Checker::infer_brace_struct(Expr::Init &init) {
    std::vector<StructInfo::Field> fields;
    std::unordered_set<std::string> seen;
    bool bad = false;
    for (const Expr::FieldInit &field : init.fields) {
        if (field.field.empty()) {
            if (field.value) {
                check_expr(*field.value, true);
            }
            bad = true;
            continue;
        }
        if (!seen.insert(field.field).second) {
            error(field.loc, "redefinition");
            bad = true;
        }
        if (field.value == nullptr) {
            error(field.loc, "type mismatch");
            bad = true;
            continue;
        }
        const Type field_type = check_expr(*field.value, true);
        if (field_type.kind == TypeKind::Invalid || field_type.kind == TypeKind::Void ||
            field_type.kind == TypeKind::Tuple || field_type.kind == TypeKind::Nullptr ||
            field_type.kind == TypeKind::Trait || field_type.kind == TypeKind::Meta) {
            error(field.loc, "type mismatch");
            bad = true;
            continue;
        }
        fields.push_back(StructInfo::Field{field.field, field_type, field.loc, false, false, nullptr});
    }
    if (bad || fields.empty()) {
        return type_invalid();
    }
    const int id = static_cast<int>(structs_.size());
    StructInfo info;
    info.name = ".struct." + std::to_string(id);
    info.file_id = file_id_;
    info.complete = true;
    info.loc = init.loc;
    info.anonymous = true;
    info.fields = std::move(fields);
    structs_.push_back(std::move(info));
    init.struct_id = id;
    return type_struct(id);
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
            // `type Pair (i32, i32)` is a different type from `(i32, i32)`. A cast names it.
            if (dest.alias_id != 0) {
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
            const int listed = static_cast<int>(init.elements.size());
            if (listed > dest.length) {
                error(init.loc, "type mismatch");
                check_elements();
                return type_invalid();
            }
            if (init.zero_rest && listed < dest.length && needs_nonnull_pointer(*dest.element)) {
                error(init.loc, "type mismatch");
            }
            if (!init.zero_rest && listed != dest.length && !init_allow_partial_) {
                // A short list leaves the tail uninitialized. A finished value cannot.
                error(init.loc, "uninitialized");
            }
            for (const auto &element : init.elements) {
                if (!element) {
                    continue;
                }
                // A brace list is that row. A name, an index, or a call copies the array.
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
    // `var s = struct{.value = 10}` has no destination type. A trait parameter accepts that struct.
    const bool infer = init.inferred && init.type_name.empty() && !init.fields.empty() &&
                       (dest.kind == TypeKind::Invalid || dest.kind == TypeKind::Trait) && !scopes_.empty() &&
                       scopes_.back().function_id != 0;
    if (infer) {
        return infer_brace_struct(init);
    }
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
        // `Holder{.raw = {4, 6}}` is that array. A short list leaves the tail unset.
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
            if (needs_nonnull_pointer(field.type) && !field.has_init) {
                error(init.loc, "type mismatch");
            }
        }
        if (!init_allow_partial_) {
            const int leaves = init_leaf_count(dest);
            std::vector<uint8_t> bits(static_cast<std::size_t>(std::max(leaves, 0)), 0);
            init_fill_brace(dest, &init, bits, 0);
            for (const uint8_t bit : bits) {
                if (bit == 0) {
                    error(init.loc, "uninitialized");
                    break;
                }
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

bool Checker::as_trait_cast(const std::string &package, const std::string &name, const std::vector<TypeArg> &written, Expr *value,
                           SourceLoc loc, Type &out) {
    out = type_invalid();
    if (name.empty()) {
        return false;
    }
    uint32_t root = file_id_;
    if (!package.empty()) {
        const Symbol *symbol = lookup(package);
        if (symbol == nullptr || !symbol->is_package) {
            return false;
        }
        root = symbol->package_file;
    }
    const TraitDecl *templ = nullptr;
    uint32_t owner = root;
    for (const uint32_t id : files_in_package(root)) {
        const auto file = trait_templates_.find(id);
        if (file == trait_templates_.end()) {
            continue;
        }
        const auto found = file->second.find(name);
        if (found == file->second.end()) {
            continue;
        }
        owner = id;
        templ = found->second;
        break;
    }
    int concrete = -1;
    if (templ == nullptr) {
        for (const uint32_t id : files_in_package(root)) {
            concrete = package_trait(id, name);
            if (concrete >= 0) {
                break;
            }
        }
        if (concrete < 0) {
            return false;
        }
    }
    if (templ != nullptr && !package.empty() && !templ->exported) {
        bool same = false;
        for (const uint32_t id : files_in_package(file_id_)) {
            if (id == owner) {
                same = true;
            }
        }
        if (!same) {
            error(loc, name + " is not exported");
            return true;
        }
    }
    const auto finish = [&](const Type &trait, const Type *known) -> bool {
        if (trait.kind != TypeKind::Trait) {
            return true;
        }
        if (value != nullptr && known == nullptr &&
            (accept_trait_named(*value, trait) || accept_trait_lambda(*value, trait))) {
            out = trait;
            return true;
        }
        if (value == nullptr) {
            error(loc, "type mismatch");
            return true;
        }
        Type source = known != nullptr ? *known : check_raw(*value, true);
        if (source.kind == TypeKind::Invalid) {
            return true;
        }
        if (source.kind == TypeKind::Function) {
            const Type *method = sole_trait_method(trait);
            if (method == nullptr || !same_type(source, *method)) {
                error(loc, "type mismatch");
                return true;
            }
            out = trait;
            return true;
        }
        const std::size_t before = diags_.size();
        expect_value(source, trait, *value, loc);
        if (diags_.size() == before) {
            out = trait;
        }
        return true;
    };
    if (templ == nullptr) {
        if (!written.empty()) {
            error(loc, "type mismatch");
            return true;
        }
        return finish(type_trait(concrete), nullptr);
    }
    std::vector<TypeArg> args = written;
    Type source;
    const Type *known = nullptr;
    if (args.empty()) {
        if (value == nullptr) {
            error(loc, "type mismatch");
            return true;
        }
        if (std::holds_alternative<Expr::Func>(value->data)) {
            error(loc, "type mismatch");
            return true;
        }
        source = check_raw(*value, true);
        known = &source;
        if (source.kind == TypeKind::Invalid) {
            return true;
        }
        const TypeParam &param = templ->type_params.front();
        Type bound;
        bool have = false;
        const auto agree = [&](const Type &got) {
            if (!have) {
                bound = got;
                have = true;
                return true;
            }
            return same_type(bound, got);
        };
        const bool iteration_value = templ->is_extern && package_is_iteration(owner) &&
                                     ((source.kind == TypeKind::Array && source.element != nullptr) ||
                                      (source.kind == TypeKind::Pointer && source.decayed_len > 0 && source.pointee != nullptr));
        if (iteration_value) {
            Type element = source.kind == TypeKind::Array ? *source.element : *source.pointee;
            element.is_const = false;
            if (!agree(element)) {
                error(loc, "type mismatch");
                return true;
            }
        } else if (source.kind == TypeKind::Struct) {
            const auto mention = [&](const std::string &written_name, const std::string &written_package, int indirections, bool is_array,
                                      const std::vector<TypeArg> &type_args) {
                return written_name == param.name && written_package.empty() && type_args.empty() && !is_array &&
                       (indirections == 0 || indirections == 1);
            };
            const auto take = [&](int indirections, const Type &got) {
                if (indirections == 0) {
                    return agree(got);
                }
                if (got.kind != TypeKind::Pointer || got.pointee == nullptr) {
                    return false;
                }
                return agree(*got.pointee);
            };
            if (source.struct_id >= 0 && source.struct_id < static_cast<int>(structs_.size())) {
                const StructInfo &info = structs_[static_cast<std::size_t>(source.struct_id)];
                for (const FunctionDecl &method : templ->methods) {
                    if (method.name.empty()) {
                        continue;
                    }
                    bool hidden = false;
                    const MethodInfo *head = find_visible_method(info.file_id, info.name, method.name, hidden);
                    if (head == nullptr) {
                        continue;
                    }
                    const bool result_t = mention(method.return_type, method.type_package, method.indirections, false, {});
                    bool param_t = false;
                    for (const Param &slot : method.params) {
                        if (!slot.is_receiver && mention(slot.type_name, slot.type_package, slot.indirections, slot.is_array, slot.type_args)) {
                            param_t = true;
                        }
                    }
                    if (!result_t && !param_t) {
                        continue;
                    }
                    const auto unify = [&](const MethodInfo &candidate) {
                        if (!candidate.has_body || candidate.is_static || candidate.type.kind != TypeKind::Function ||
                            candidate.type.result == nullptr || candidate.type.params.empty()) {
                            return false;
                        }
                        if (result_t && !take(method.indirections, *candidate.type.result)) {
                            return false;
                        }
                        std::size_t index = 1;
                        for (const Param &slot : method.params) {
                            if (slot.is_receiver) {
                                continue;
                            }
                            if (!mention(slot.type_name, slot.type_package, slot.indirections, slot.is_array, slot.type_args)) {
                                ++index;
                                continue;
                            }
                            if (index >= candidate.type.params.size() || !take(slot.indirections, candidate.type.params[index])) {
                                return false;
                            }
                            ++index;
                        }
                        return true;
                    };
                    bool matched = unify(*head);
                    for (const MethodInfo &extra : head->more) {
                        matched = unify(extra) || matched;
                    }
                    (void)matched;
                }
                const Type object = type_struct(source.struct_id);
                for (const FieldDecl &field : templ->fields) {
                    if (!mention(field.type_name, field.type_package, field.indirections, field.is_array, field.type_args)) {
                        continue;
                    }
                    Type got;
                    if (!lookup_field(object, field.name, got) || !take(field.indirections, got)) {
                        error(loc, "type mismatch");
                        return true;
                    }
                }
            }
        } else if (source.kind == TypeKind::Function) {
            if (templ->methods.size() == 1) {
                const FunctionDecl &method = templ->methods.front();
                const bool result_t = method.return_type == param.name && method.type_package.empty() && method.indirections == 0;
                if (result_t && source.result != nullptr) {
                    agree(*source.result);
                }
            }
        }
        if (!have) {
            error(loc, "type mismatch");
            return true;
        }
        TypeArg arg;
        arg.loc = loc;
        SpecArg spelled;
        if (!spell_type(bound, spelled)) {
            error(loc, "type mismatch");
            return true;
        }
        arg.type_name = spelled.type_name;
        arg.type_package = spelled.type_package;
        arg.is_struct = spelled.is_struct;
        arg.is_enum = spelled.is_enum;
        arg.indirections = spelled.indirections;
        arg.optional = spelled.optional;
        arg.const_pointee = spelled.const_pointee;
        arg.is_array = spelled.is_array;
        arg.elem_const = spelled.elem_const;
        arg.array_len = spelled.array_len;
        arg.shells = spelled.shells;
        args.push_back(std::move(arg));
    }
    const std::string spec = specialize_trait(*templ, owner, args, loc);
    if (spec.empty()) {
        return true;
    }
    const int id = package_trait(file_id_, spec);
    if (id < 0) {
        error(loc, "type mismatch");
        return true;
    }
    return finish(type_trait(id), known);
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
    const bool trait_result = signature.result != nullptr && mentions_trait(*signature.result);
    if (trait_result) {
        error(fn.loc, "trait has no size");
    }
    if (generic) {
        error(fn.loc, "not supported yet");
    } else if (!trait_result && (signature.kind != TypeKind::Function || !same_type(signature, *method))) {
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

bool Checker::accept_trait_named(const Expr &expr, const Type &want) {
    const Type *method = sole_trait_method(want);
    if (method == nullptr) {
        return false;
    }
    // One concrete function. An overload, a generic, and `unsafe func` keep their own diagnostics.
    // A prototype has no address to pass. `extern(abi="c")` is that address in another object.
    const auto matches = [&](const Symbol &symbol) {
        if (!symbol.is_function || symbol.is_template || symbol.unsafe_fn || symbol.fn_captures ||
            symbol.lists.size() != 1) {
            return false;
        }
        const FnList &list = symbol.lists.front();
        if (!list.has_body && !list.extern_c) {
            return false;
        }
        return symbol.type.kind == TypeKind::Function && same_type(symbol.type, *method);
    };
    if (const auto *name = std::get_if<Expr::Name>(&expr.data)) {
        const Symbol *symbol = lookup(name->text);
        if (symbol == nullptr || !matches(*symbol)) {
            return false;
        }
        check_raw(expr, true);
        return true;
    }
    const auto *selector = std::get_if<Expr::Selector>(&expr.data);
    if (selector == nullptr || selector->base == nullptr) {
        return false;
    }
    const auto *base = std::get_if<Expr::Name>(&selector->base->data);
    if (base == nullptr) {
        return false;
    }
    const Symbol *symbol = lookup(base->text);
    if (symbol != nullptr && symbol->is_package) {
        const Symbol *member = find_export(symbol->package_file, selector->field);
        if (member == nullptr || !matches(*member)) {
            return false;
        }
        check_raw(expr, true);
        return true;
    }
    // `Point.step` is the static function. A shadowed name is an ordinary value.
    if (symbol != nullptr && !symbol->is_type) {
        return false;
    }
    if (!static_type_owner(base->text)) {
        return false;
    }
    const MethodInfo *info = find_named_method(base->text, selector->field);
    if (info == nullptr || !info->is_static || !info->more.empty() || info->unsafe_fn || info->type.kind != TypeKind::Function ||
        !same_type(info->type, *method)) {
        return false;
    }
    if (!info->has_body && !info->extern_c) {
        return false;
    }
    check_raw(expr, true);
    return true;
}

void Checker::check_call_arg(const Expr &expr, const Type &want) {
    if (accept_trait_named(expr, want) || accept_trait_lambda(expr, want)) {
        return;
    }
    const std::size_t before = diags_.size();
    check_against(expr, want);
    // A type error is not a store into the argument.
    if (diags_.size() != before) {
        return;
    }
    // `*T` is assumed to store into the object. `*const T` may only read it.
    // A sized array passed as `*Elem`, including a parameter `[N]T`, is not that object.
    if (want.kind == TypeKind::Pointer && want.pointee != nullptr) {
        const bool writes = !want.pointee->is_const;
        const Type &pointee = *want.pointee;
        const bool array_pointer =
            want.decayed_len == 0 && pointee.kind == TypeKind::Array && pointee.length >= 0 && pointee.element != nullptr;
        init_address_span(expr, writes, expr_loc(expr), array_pointer);
    }
}
Type Checker::check_against(const Expr &expr, const Type &want) {
    struct PartialRestore {
        bool &flag;
        bool saved;
        ~PartialRestore() { flag = saved; }
    } restore{init_allow_partial_, init_allow_partial_};
    // A nested call sees a finished value. A nested brace keeps the partial initializer.
    if (!std::holds_alternative<Expr::Init>(expr.data)) {
        init_allow_partial_ = false;
    }
    // `[]const u8` gives C its `*const u8` address. The slice is still the string in TLC.
    if (const_u8_pointer(want)) {
        const Type got = check_expr(expr, true);
        if (slice_of_const_u8(got)) {
            return want;
        }
        // `[N]u8` is the address of the first byte. `*const u8` may read it.
        if (got.kind == TypeKind::Array && got.length >= 0 && !is_slice(got) && got.element != nullptr &&
            raw_pointee(*got.element)) {
            return want;
        }
        expect_value(got, want, expr, expr_loc(expr));
        return got;
    }
    // `[N]T` converts to `[]T` while the length is still known. A parameter `[N]T`
    // and `*[N]T` do too. A decayed `*T` with no length does not.
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
    // `var copy [2]i32 = row` and `var copy = row` copy the array.
    // `const` stays on the binding. The copy is a new array of the wanted type.
    // A parameter `[N]T` is already a pointer. The same written array copies those elements.
    if (want.kind == TypeKind::Array && want.length >= 0) {
        InitKeep keep(*this);
        Type got = check_raw(expr, true);
        got.is_const = false;
        if (got.kind == TypeKind::Invalid) {
            return got;
        }
        Type bare = want;
        bare.is_const = false;
        if (same_type(got, bare)) {
            return want;
        }
        if (decayed_array_value(got, want)) {
            return want;
        }
        mismatch(expr_loc(expr), want, got);
        return got;
    }
    // A parameter `[N]T` is a pointer that records N. A sized array of that length
    // passes its address and does not read the elements.
    // A written `*T` does the same when `T` is the element. `*[N]T` does not:
    // that argument is `&array`. The cast `*T(array)` is the same address.
    if (want.kind == TypeKind::Pointer && want.pointee != nullptr) {
        const bool decayed_param = !want.optional && want.decayed_len > 0;
        const bool element_pointer = want.decayed_len == 0 && want.pointee->kind != TypeKind::Array;
        if (decayed_param || element_pointer) {
            Type got = check_raw(expr, true);
            if (got.kind == TypeKind::Array && got.length >= 0 && got.element != nullptr && !is_slice(got)) {
                Type element = *got.element;
                if (got.is_const) {
                    element.is_const = true;
                }
                Type as_pointer = type_pointer(std::move(element));
                if (decayed_param) {
                    as_pointer.decayed_len = got.length;
                }
                const bool matched = decayed_param ? got.length == want.decayed_len && accepts_pointer(as_pointer, want)
                                                   : accepts_pointer(as_pointer, want);
                if (matched) {
                    return want;
                }
                mismatch(expr_loc(expr), want, got);
                return got;
            }
            Type value = value_type(std::move(got));
            expect_value(value, want, expr, expr_loc(expr));
            return value;
        }
    }
    // A parameter `[N]T` is a pointer that still records N. `value_type` drops that length.
    if (want.kind == TypeKind::Trait) {
        const Type got = check_raw(expr, true);
        if (got.kind != TypeKind::Invalid) {
            expect_value(got, want, expr, expr_loc(expr));
        }
        return got;
    }
    const Type got = check_expr(expr, true);
    expect_value(got, want, expr, expr_loc(expr));
    return got;
}

Type Checker::value_type(Type type) {
    // A loaded value is a copy. `const` stays on the source, not on this temporary.
    // A sized array stays `[N]T` until a written `*T` or `[]T` accepts it.
    // The cast `*T(array)` is that pointer. A slice is `array[:]`.
    // A parameter `[N]T` is already a pointer. Its length stays on that binding.
    type.is_const = false;
    type.decayed_len = 0;
    if (type.kind == TypeKind::Function) {
        return type_pointer(type);
    }
    return promote(type);
}

Type Checker::check_expr(const Expr &expr, bool used) {
    cond_split_ = false;
    const Type type = value_type(check_raw(expr, used));
    if (!cond_split_) {
        cond_true_ = assign_;
        cond_false_ = assign_;
    }
    // A constant condition makes the other arm unreachable. `&&` and `||` set both arms themselves.
    if (const std::optional<int> folded = fold_int(expr)) {
        if (*folded != 0) {
            cond_false_.unreachable = true;
        } else {
            cond_true_.unreachable = true;
        }
    }
    return type;
}

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

Type Checker::finish_overloaded(Expr &expr, const Symbol &symbol, bool deferring, bool used) {
    auto *call_ptr = std::get_if<Expr::Call>(&expr.data);
    if (call_ptr == nullptr) {
        return type_invalid();
    }
    Expr::Call &node = *call_ptr;
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
    if (winner->decl != nullptr && comptime_param_template(*winner->decl)) {
        const std::size_t before = diags_.size();
        require_comptime_params(*winner->decl, node);
        if (diags_.size() != before) {
            return type_invalid();
        }
        if (apply_comptime_template(node, *winner->decl, symbol.lists.size() > 1)) {
            return check_raw(expr, used);
        }
        if (diags_.size() != before) {
            return type_invalid();
        }
    } else if (winner->decl != nullptr) {
        require_comptime_params(*winner->decl, node);
    }
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
    const TraitHome home(*this, trait_signature_home(signature, node.callee.get()));
    reject_trait_prototype(signature, winner->has_body, node.loc);
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
    uint32_t trait_home = 0;
    for (const Type &param : method.type.params) {
        if (mentions_trait(param)) {
            trait_home = method.file_id;
            break;
        }
    }
    const TraitHome home(*this, trait_home);
    reject_trait_prototype(method.type, method.has_body, node.loc);
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
                } else if (selector.base != nullptr) {
                    const bool writes = receiver.pointee != nullptr && !receiver.pointee->is_const;
                    init_address_span(*selector.base, writes, expr_loc(*selector.base), false);
                }
            }
        } else if (selector.base != nullptr) {
            init_require_receiver(*selector.base);
            expect(object, receiver, expr_loc(*selector.base));
        }
    }
    uint32_t trait_home = 0;
    for (const Type &param : method.type.params) {
        if (mentions_trait(param)) {
            trait_home = method.file_id;
            break;
        }
    }
    const TraitHome home(*this, trait_home);
    reject_trait_prototype(method.type, method.has_body, node.loc);
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

} // namespace tlc
