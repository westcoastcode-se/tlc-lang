#pragma once

// Internal type checker. The public entry points are analyze() in sema.hpp.

#include "sema.hpp"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tlc {

struct SpecArg {
    bool is_value = false;
    int value = 0;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    // Bit 0 is the outermost `*`. A set bit means that pointer's pointee is `const`.
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    // `[][]T` and `[N][]T`. The element is one slice.
    bool elem_slice = false;
    // Every pointer, slice, and array outside the leaf. Empty when a plain pointer run is enough.
    std::vector<TypeShell> shells;
    int array_len = 0;
    int struct_id = -1;
    int enum_id = -1;
    // Several types bound to `[...U]`. One type is stored in the fields above instead.
    bool is_pack = false;
    std::vector<SpecArg> parts;
};

// One parameter list of a function name. The receiver is not part of the list.
struct FnList {
    Type type;
    bool has_body = false;
    bool unsafe_fn = false;
    bool extern_c = false;
    std::string link_name;
    const FunctionDecl *decl = nullptr;
    SourceLoc loc{};
};

struct Symbol {
    std::string name;
    Type type;
    bool has_body = false;
    bool is_function = false;
    // Every parameter list. Empty on a symbol that is not a function. One entry is the usual function.
    std::vector<FnList> lists;
    // Object symbol from `extern(..., name="...")`. Empty when that argument was omitted.
    std::string link_name;
    bool is_local_function = false;
    // `unsafe func`. A call outside an unsafe context is `unsafe call`. A value use is `type mismatch`.
    bool unsafe_fn = false;
    // The function reads a function pointer from an enclosing function. A direct call is fine.
    // Using the function as a value is not.
    bool fn_captures = false;
    bool is_auto = false;
    bool is_package = false;
    bool is_type = false;
    bool is_constant = false;
    // `const name = expr` inside a function. The binding has storage and cannot be assigned.
    bool is_immutable = false;
    bool is_enum = false;
    bool is_typedef = false;
    bool is_struct_tag = false;
    bool is_template = false;
    // The first template. One template keeps today's call path.
    const FunctionDecl *fn_template = nullptr;
    // Every value-parameter list of this generic name. One entry is the usual template.
    std::vector<const FunctionDecl *> fn_templates;
    int const_value = 0;
    int function_id = 0;
    uint32_t package_file = 0;
    SourceLoc loc{};
};

struct Scope {
    std::unordered_map<std::string, Symbol> symbols;
    int function_id = 0;
};

class Checker {
  public:
    explicit Checker(DiagnosticEngine &diags) : diags_(diags) {}

    void analyze(const TranslationUnit &unit);
    void analyze_graph(const PackageGraph &graph);

  private:
    struct SelectorTarget {
        enum class Kind { Failed, Package, Other };
        Kind kind = Kind::Other;
        const Symbol *symbol = nullptr;
    };

    DiagnosticEngine &diags_;
    const PackageGraph *graph_ = nullptr;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> exports_;
    struct StructInfo {
        std::string name;
        // File that wrote the struct. A method on it belongs in this file.
        uint32_t file_id = 0;
        bool complete = false;
        SourceLoc loc{};
        struct Field {
            std::string name;
            Type type;
            SourceLoc loc{};
            // `export name` in the struct. Extern structs and unions publish every field.
            bool exported = false;
        };
        std::vector<Field> fields;
        bool exported = false;
        bool anonymous = false;
        bool is_extern = false;
        bool extern_c = false;
        int align = 0;
        bool is_union = false;
        // Enum that tags a tlc union. Empty until check_unions resolves `tag_name`.
        std::string tag_name;
        int tag_enum = -1;
        // Set on a specialization. Empty on a struct written in source.
        std::string template_name;
        std::vector<SpecArg> spec_args;
    };

    struct MethodInfo {
        Type type;
        bool has_body = false;
        // No `this` parameter. The call is `Type.name(...)`.
        bool is_static = false;
        bool unsafe_fn = false;
        // `export func Type::name`. Another package calls `value.name`.
        bool exported = false;
        // File that wrote the method. Zero when the method has not been registered.
        uint32_t file_id = 0;
        const FunctionDecl *decl = nullptr;
        // Further parameter lists of this name. Empty when the fields above are the only list.
        std::vector<MethodInfo> more;
    };

    // `var Entity::next` or `const Entity::next`. One entry per owner and name.
    // A generic template is not stored here. Each specialization is.
    struct AssocVar {
        Type type;
        // Published as `pkg.Owner.name`. The rest of the package can use it either way.
        bool exported = false;
        // `const`. Assignment and `&` are rejected.
        bool is_const = false;
        // Folded integer or bool. Codegen uses the value and emits no global.
        bool has_const = false;
        int const_value = 0;
    };

    struct TraitInfo {
        std::string name;
        // File that wrote the trait. Field types are resolved in that file.
        uint32_t file_id = 0;
        struct Method {
            std::string name;
            Type type;
        };
        struct Field {
            std::string name;
            Type type;
            SourceLoc loc{};
        };
        std::vector<Method> methods;
        std::vector<Field> fields;
        // Field declarations, typed after structs are complete.
        std::vector<FieldDecl> pending;
    };

    std::vector<Scope> scopes_;
    int function_seq_ = 0;
    std::vector<StructInfo> structs_;
    std::vector<TraitInfo> traits_;
    // The built-in `Allocator` trait. Negative until the first unit is registered.
    int allocator_trait_id_ = -1;
    void ensure_allocator_trait();
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>>> methods_;
    // file, owner, field. The owner is the struct name or a specialization such as `Box.spec.i32`.
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::unordered_map<std::string, AssocVar>>> assoc_vars_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> tags_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> trait_names_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Type>> typedefs_;
    // alias_names_[id] is the source name of a `type Name T`. Index 0 is unused.
    std::vector<std::string> alias_names_{""};
    // alias_file_[id] is the file that wrote that alias. Index 0 is unused.
    std::vector<uint32_t> alias_file_{0};
    int next_alias_id_ = 1;
    struct EnumInfo {
        std::string name;
        // File that wrote the enum. A method on it belongs in this file.
        uint32_t file_id = 0;
        Type underlying;
        bool is_extern = false;
        struct Member {
            std::string name;
            int64_t value = 0;
            SourceLoc loc{};
        };
        std::vector<Member> members;
    };
    std::vector<EnumInfo> enum_types_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> enum_ids_;
    struct PendingStruct {
        uint32_t file_id = 0;
        const StructDecl *decl = nullptr;
        int id = -1;
    };
    std::vector<PendingStruct> pending_structs_;
    std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> package_syms_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> statics_;
    struct SwitchFrame {
        std::unordered_set<int> values;
        bool has_default = false;
    };

    uint32_t file_id_ = 0;
    Type return_type_ = type_invalid();
    // Set while folding `sizeof`, so a second lookup does not repeat a diagnostic.
    bool quiet_ = false;
    // Set while checking a variadic function body. `builtin.va` is valid only then.
    bool variadic_function_ = false;
    // The callee of a call is being checked. That callee is a name, a function expression, or a selector.
    bool direct_callee_ = false;
    // `unsafe` expressions and the body of an `unsafe func`. Nested functions do not inherit it.
    int unsafe_depth_ = 0;
    int loop_depth_ = 0;
    bool checking_defer_ = false;
    std::vector<SwitchFrame> switches_;
    // Functions whose bodies are on the stack, innermost last.
    std::vector<FunctionDecl *> checking_;

    void error(SourceLoc loc, const std::string &message);
    void require_unsafe_call(bool unsafe_fn, SourceLoc loc);
    // False when an unsafe function is used as a value. The caller returns an invalid type.
    bool allow_unsafe_fn(bool unsafe_fn, SourceLoc loc);
    void push();
    void push_function();
    void pop();
    bool captured(const Symbol &symbol) const;
    bool fn_pointer(const Type &type) const;
    void note_capture(FunctionDecl &fn, const std::string &name, int function_id);
    void note_owned(FunctionDecl &fn, const std::string &name);
    void adopt_captures(const FunctionDecl &fn);
    Type function_result(const FunctionDecl &fn);
    Type function_signature(const FunctionDecl &fn);
    void check_local_function(const FunctionDecl &fn);
    Symbol *lookup(const std::string &name);
    Symbol *lookup_scope(const std::string &name);
    bool declare(Symbol symbol, SourceLoc loc);
    bool remember_shared(const Symbol &symbol, SourceLoc loc);
    void note_body(const std::string &name);
    bool shared_has(const std::string &name) const;
    std::vector<uint32_t> files_in_package(uint32_t file_id) const;
    int find_tag(uint32_t file, const std::string &name) const;
    int find_enum(uint32_t file, const std::string &name) const;
    // File that defines this struct, enum, or typedef. The current file when the type is missing.
    uint32_t defined_in(const std::string &name) const;
    // A struct, an enum, or a primitive typedef such as `type Integer i32`.
    bool named_type_owner(const std::string &name) const;
    bool package_has_enum(uint32_t file, const std::string &name) const;
    bool is_c_enum(const Type &type) const;
    const EnumInfo::Member *find_enum_member(int enum_id, const std::string &name) const;
    int enum_designator(const Expr &expr);
    int package_trait(uint32_t file, const std::string &name) const;
    bool package_typedef(uint32_t file, const std::string &name, Type &out) const;
    bool exported_elsewhere(const std::string &name) const;
    bool typedef_target_ready(const TypedefDecl &item) const;
    void register_one_typedef(const TypedefDecl &item);
    bool field_waits(const FieldDecl &field) const;
    int min_align_of(const Type &type) const;
    std::optional<int> type_size(const Type &type) const;
    void declare_struct_tags(const TranslationUnit &unit, uint32_t file_id);
    void define_struct_fields();
    void prepare_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);
    void check_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);

    Type type_of_name(const std::string &name, SourceLoc loc, bool allow_incomplete = false);
    Type with_indirections(Type type, int count, SourceLoc loc, uint32_t optional = 0, uint32_t const_pointee = 0);
    void note_struct_use(bool is_struct, const std::string &name, SourceLoc loc);
    Type object_type(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                     bool parameter, const std::string &package = {}, bool is_struct = false, bool is_enum = false,
                     bool receiver = false, uint32_t optional = 0, uint32_t const_pointee = 0,
                     bool allow_incomplete = false, bool elem_const = false, bool elem_slice = false,
                     const std::vector<TypeShell> &shells = {});
    Type spelled_type(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                      int indirections, SourceLoc loc, uint32_t optional = 0, uint32_t const_pointee = 0);
    // A store through this lvalue would change a `const` binding or a `*const` pointee.
    bool const_path(const Expr &expr);
    // The pointer passed to a method. A const object is `*const T`.
    Type method_pointer(const Type &base, const Type &object, bool through_pointer, const Expr &source);
    bool accepts_pointer(const Type &got, const Type &want) const;
    bool needs_nonnull_pointer(const Type &type) const;
    // A `?*T` name in this function becomes `*T`. A package global is left alone.
    bool narrow_optional(const std::string &name);
    void restore_optional(const std::string &name);
    Type type_of_fn_param(const FnParam &param, SourceLoc loc);
    Type type_of_fn_ptr(const std::string &ret_name, const std::string &ret_package, bool ret_struct, bool ret_enum,
                        int ret_indirections, int fn_depth, const std::vector<FnParam> &params, bool variadic,
                        const std::vector<FnParam> &results, SourceLoc loc, uint32_t ret_optional = 0,
                        uint32_t ret_const = 0);
    Type type_of_param(const Param &param);
    Type type_of_var(const VarDecl &var);
    bool indexed_type(const Type &base, Type &out, bool &pointee_void) const;
    Type type_of_result(const ResultSlot &slot, SourceLoc loc);
    std::string type_desc(const Type &type) const;
    void mismatch(SourceLoc loc, const Type &want, const Type &got);
    Type lookup_tag(const std::string &name, SourceLoc loc, bool allow_incomplete);
    Type lookup_qualified_type(const std::string &package, const std::string &name, SourceLoc loc);
    bool lookup_field(const Type &object, const std::string &name, Type &out) const;
    // Another package sees `export` members. A member whose type is a struct that is not
    // visible stays hidden. The same package sees every member.
    bool field_visible(int struct_id, const std::string &name) const;
    bool struct_open(int struct_id, std::unordered_set<int> &seen) const;
    bool hidden_field(const Type &object, const std::string &name, SourceLoc loc);
    bool dot_object(const Type &base, Type &object, bool &through_pointer) const;
    const MethodInfo *find_method(int struct_id, const std::string &name) const;
    const MethodInfo *find_named_method(const std::string &type_name, const std::string &name) const;
    // A method on `owner` written in `home`'s package. The current package sees every method.
    // Another package sees an exported method. `hidden` is set when one exists and is not exported.
    const MethodInfo *find_visible_method(uint32_t home, const std::string &owner, const std::string &name,
                                          bool &hidden) const;
    bool package_owns(uint32_t home) const;
    // Another parameter list of an existing function. The same list merges, or is `redefinition`.
    void absorb_function(Symbol &existing, const FunctionDecl &fn, Type signature);
    void mirror_function(const Symbol &symbol);
    // Another value-parameter list of a generic function. The same list merges, or is `redefinition`.
    // `report` is false while publishing, so a clash is recorded once in declare_globals.
    void absorb_template(Symbol &existing, const FunctionDecl &fn, FunctionDecl *stable, bool report);
    void mirror_template(const Symbol &symbol);
    FunctionDecl *stable_template(uint32_t file, const FunctionDecl &fn) const;
    // `self this` beside `self *this`, or two bodies of the same remaining list.
    void generic_method_clash(const FunctionDecl &fn);
    std::optional<std::vector<SpecArg>> infer_generic_bounds(const FunctionDecl &templ, const Expr::Call &call,
                                                            const std::vector<Type> &arg_types);
    bool generic_candidate_matches(const FunctionDecl &templ, const Expr::Call &call, const std::vector<Type> &arg_types,
                                   uint32_t owner);
    FnList function_list(const FunctionDecl &fn, Type signature) const;
    // Picks the parameter list a call uses. Null when none matches or more than one matches.
    const MethodInfo *choose_method(const Expr::Call &node, const MethodInfo &head, bool want_static, bool foreign,
                                    const std::string &name);
    Type finish_overloaded(const Expr::Call &node, const Symbol &symbol, bool deferring);
    // `Integer` or `*Integer` when that alias is a primitive. Null otherwise.
    const Type *scalar_alias(const Type &base, bool &through_pointer) const;
    // A slice alias such as `type string_view []const u8`. A pointer to that alias sets `through_pointer`.
    const Type *slice_alias(const Type &base, bool &through_pointer) const;
    const AssocVar *find_assoc(const std::string &owner, const std::string &name) const;
    const AssocVar *find_assoc_in(uint32_t file, const std::string &owner, const std::string &name) const;
    bool assoc_defined(uint32_t file, const std::string &owner, const std::string &name) const;
    const TraitInfo::Method *find_trait_method(int trait_id, const std::string &name) const;
    const TraitInfo::Field *find_trait_field(int trait_id, const std::string &name) const;
    // True when `base` is a trait or `*Trait`. `out` is the field type, or invalid when the name is absent.
    bool as_trait_field(const Type &base, const std::string &name, Type &out) const;
    bool trait_method_matches(const Type &trait_fn, const Type &concrete) const;
    // `body_home` is the package that will compile the specialized body. Zero keeps the caller's view.
    // A method counts only when that body can call it, and when the caller can see it.
    bool require_satisfies(int struct_id, int trait_id, bool through_pointer, SourceLoc loc, uint32_t body_home = 0);
    // The one method of a trait or `*Trait`. Null when the trait has a field or any other method count.
    const Type *sole_trait_method(const Type &want) const;
    // An anonymous function used as a single-method trait argument. The body is checked
    // against that method, with no receiver.
    bool accept_trait_lambda(const Expr &expr, const Type &want);
    void check_call_arg(const Expr &expr, const Type &want);
    // Receiver checks shared by an enum value and a primitive alias.
    Type finish_value_method(const Expr::Call &node, const Expr::Selector &selector, const MethodInfo &method,
                             const Type &base, bool through_pointer);
    void register_structs(const TranslationUnit &unit, uint32_t file_id);
    void require_extern_struct(const Type &type, SourceLoc loc);
    void register_enums(const TranslationUnit &unit, uint32_t file_id);
    void check_unions();
    void register_traits(const TranslationUnit &unit, uint32_t file_id);
    void define_trait_fields();
    void register_typedefs(const TranslationUnit &unit, uint32_t file_id);
    bool name_taken(const std::string &name) const;
    Type published_object(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                          bool parameter, const std::string &package, bool is_struct, bool is_enum,
                          uint32_t optional = 0, uint32_t const_pointee = 0, bool elem_const = false,
                          bool elem_slice = false, const std::vector<TypeShell> &shells = {});
    Type published_result(const std::string &name, int indirections, SourceLoc loc, const std::string &package,
                          bool is_struct, bool is_enum, uint32_t optional = 0, uint32_t const_pointee = 0);
    void expect(const Type &got, const Type &want, SourceLoc loc);
    void expect_value(const Type &got, const Type &want, const Expr &expr, SourceLoc loc);
    // `Point{}` and `pkg.Point{}`. A typedef keeps its alias. Anything else is invalid.
    Type literal_struct_type(const Expr::Init &init);
    // `[_]T{v, v, v}` has type `[N]T`, where N is the number of values.
    Type check_array_literal(Expr::Init &init);
    Type check_init(Expr::Init &init, const Type &want);
    void expect_integer(const Type &got, SourceLoc loc);
    void expect_condition(const Type &got, SourceLoc loc);
    bool is_lvalue_form(const Expr &expr) const;
    Type check_raw(const Expr &expr, bool used);
    // The type of a loaded value. A sized array becomes a pointer. A slice stays a slice.
    Type value_type(Type type);
    Type check_expr(const Expr &expr, bool used);
    FunctionDecl *inferred_function(const Expr &expr) const;
    Type elaborate_inferred(FunctionDecl &fn, const Type &want);
    Type check_against(const Expr &expr, const Type &want);
    void check_stmt(const Stmt &stmt);
    void check_assert(SourceLoc loc, const Expr *cond, const std::string &message);
    void check_test(const TestDecl &test);
    bool is_std_builtin_file(uint32_t file_id) const;
    bool is_builtin_va_call(const Expr &callee);
    Type operand_type(bool on_type, const std::string &type_name, const std::string &type_package, bool is_struct,
                      bool is_enum, int indirections, uint32_t optional, const Expr *expr, SourceLoc loc,
                      uint32_t const_pointee = 0, bool is_array = false, int array_len = 0, bool elem_const = false,
                      bool elem_slice = false, const std::vector<TypeShell> &shells = {});
    // Fold each array shell's length name. A non-positive length is `not supported yet`.
    void fold_shells(std::vector<TypeShell> &shells, SourceLoc loc);
    Type sizeof_operand(const Expr::Sizeof &node);
    // A folded number. `wide` is f64. An integer is neither.
    struct FoldedNumber {
        double value = 0;
        bool is_float = false;
        bool wide = false;
    };
    std::optional<int> fold_int(const Expr &expr);
    std::optional<FoldedNumber> fold_number(const Expr &expr);
    std::optional<FoldedNumber> fold_number(const Expr &expr, std::unordered_set<std::string> &seen);
    // `typeof(...)` after checking. Empty when this expression is not that constant.
    std::optional<std::string> type_constant(const Expr &expr) const;
    void check_function(const FunctionDecl &fn);
    void declare_globals(const TranslationUnit &unit);
    void check_globals(const TranslationUnit &unit);
    void publish_exports(const TranslationUnit &unit, uint32_t file_id);
    void bind_imports(const PackageNode &node);
    void analyze_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);
    const PackageNode *find_node(uint32_t file_id) const;
    const Symbol *find_export(uint32_t file_id, const std::string &name) const;
    const PackageNode *target_of(const PackageNode &from, const ImportDecl &import) const;
    SelectorTarget resolve_selector(const Expr::Selector &selector);

    struct OwnedFn {
        uint32_t file_id = 0;
        FunctionDecl fn;
    };
    struct OwnedVar {
        uint32_t file_id = 0;
        VarDecl var;
    };
    struct PendingAppend {
        uint32_t file_id = 0;
        TopDecl decl;
    };
    struct ConstBinding {
        int value = 0;
        bool is_static = false;
    };

    TranslationUnit *primary_unit_ = nullptr;
    std::deque<OwnedFn> owned_fns_;
    std::deque<OwnedVar> owned_assoc_;
    std::deque<StructDecl> owned_structs_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::vector<FunctionDecl *>>> fn_templates_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, StructDecl *>> struct_templates_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, ConstBinding>> const_values_;
    std::deque<OwnedFn> deferred_methods_;
    std::vector<PendingAppend> pending_appends_;
    std::unordered_set<std::string> spec_checking_;
    std::unordered_set<std::string> spec_ready_;
    // `file\nSpec` while that specialization's variables are being built.
    std::unordered_set<std::string> assoc_busy_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> spec_symbols_;
    std::unordered_set<std::string> static_spec_keys_;

    void collect_templates(TranslationUnit &unit, uint32_t file_id);
    void fold_early_consts(TranslationUnit &unit, uint32_t file_id);
    void bind_file_const(VarDecl &var);
    // The type of a file-scope constant. Sets `has_const` when the value folds.
    // Does not declare the name. Invalid means the constant was rejected.
    Type check_file_const(VarDecl &var);
    // `var row = [_]i32{1, 2}`, `var row [3]i32 = {1, 2, 3}`, and `const row = [_]i32{1}`.
    // `declared` is the symbol type when it was already stored.
    Type file_array_initializer(VarDecl &var, const Type *declared);
    enum class FileConst { Yes, Unfolded, No };
    // A file-scope array element. `allow_var` copies another global's constant initializer.
    FileConst file_constant(const Expr &expr, std::unordered_set<std::string> &seen, bool allow_var);
    // A file-scope `f32` or `f64`. A `var` may copy another global's initializer.
    bool accept_file_scalar(const VarDecl &var, const Type &type);
    const VarDecl *global_var(const std::string &name);
    void define_constant(const VarDecl &var, Type type, int folded);
    bool integer_const_type(const Type &type) const;
    bool stored_const_type(const Type &type) const;
    bool constant_operand(const Expr &expr);
    std::optional<int> lookup_const_value(const std::string &name);
    void prepare_written(std::string &type_name, std::string &type_package, bool &is_struct, bool &is_enum,
                         int &indirections, uint32_t &optional, bool &is_array, int &array_len,
                         std::string &array_len_name, std::vector<TypeArg> &type_args, SourceLoc loc);
    const StructDecl *find_struct_template(const std::string &name, uint32_t &owner) const;
    std::optional<Type> maybe_generic_call(Expr &expr, bool used);
    void drain_deferred();
    void flush_appends();
    TranslationUnit *unit_for(uint32_t file_id);
    bool spell_type(const Type &type, SpecArg &out) const;
    std::string mangle_spec(const std::string &base, const std::vector<SpecArg> &args) const;
    std::string specialize_type_app(const std::string &package, const std::string &name, const std::vector<TypeArg> &args,
                                    SourceLoc loc);
    bool bind_type_arg(const TypeParam &param, const TypeArg &arg, SpecArg &out, SourceLoc loc);
    void expose_bounds(uint32_t owner, const std::vector<SpecArg> &bounds);
    // A struct name that already means a different struct in `owner`, or that exists in two packages,
    // is spelled `Name.file.N` so both checker and codegen resolve that struct.
    void disambiguate_bounds(uint32_t owner, std::vector<SpecArg> &bounds);
    void substitute_type(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, std::string &type_name,
                         std::string &type_package, bool &is_struct, bool &is_enum, int &indirections, uint32_t &optional,
                         bool &is_array, int &array_len, std::string &array_len_name, std::vector<TypeArg> &type_args,
                         SourceLoc loc, std::vector<TypeShell> *shells = nullptr, bool *elem_const = nullptr);
    void substitute_expr(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, Expr &expr);
    void substitute_stmt(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, Stmt &stmt);
    void substitute_function(FunctionDecl &fn, const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds);
    // Replace `typeof(expr)` results with the type of that expression. Parameters must typecheck.
    bool resolve_result_typeof(FunctionDecl &fn);
    void check_isolated(uint32_t file, const FunctionDecl &fn);
    void queue_spec(uint32_t file, FunctionDecl fn);
    bool generic_struct_name(uint32_t file, const std::string &name) const;
    // `func Vec[T]::get` must name the struct's parameters. A list on a plain struct is rejected.
    void require_method_params(const FunctionDecl &fn);
    // `var Vec[T]::count` uses the same names. A list on a plain struct is rejected.
    void require_owner_params(const VarDecl &var);
    void declare_assoc(const VarDecl &var);
    // Check `var` as a constant and store it on `var.owner` in `file`.
    void store_assoc_const(uint32_t file, VarDecl &var);
    // Package file that holds `pkg.Type.name`, or the current file.
    uint32_t assoc_home_file(const Expr::Selector &node);
    // True when the selector is a constant on a type. Does not report a diagnostic.
    bool selector_is_const(const Expr::Selector &node);
    // One variable for this specialization. Safe to call again when the struct already exists.
    void instantiate_assoc(uint32_t owner_file, const StructDecl &templ, const std::string &spec_name,
                           const std::vector<SpecArg> &bounds);
    bool type_arg_from_expr(const Expr &expr, TypeArg &out) const;
    // `Box[i32].from` and `Buf[4, i32].width` in this package. Empty when `node` is not that form.
    // `failed` means the form was recognized and the arguments did not specialize.
    std::string same_package_spec(const Expr::Selector &node, bool &failed);
    // `pkg.Type.name(...)` and `pkg.Vec[i32].name(...)`. True when that call was handled.
    bool package_static_call(const Expr::Call &node, const Expr::Selector &selector, bool deferring, Type &out);
    // A static function used as a value on a type from another package.
    // True when the name is a method. False leaves the caller's diagnostic in place.
    bool static_method_value(const Expr::Selector &node, uint32_t home, const std::string &owner, Type &out);
    Type finish_static_call(const Expr::Call &node, const MethodInfo &method, bool deferring);
    // True when `node` is `Type.name` or `Type[args].name`. The diagnostic is already reported.
    bool assoc_from_selector(const Expr::Selector &node, Type &out);
};

} // namespace tlc
