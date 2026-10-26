#pragma once

// Internal type checker. The public entry points are analyze() in sema.hpp.

#include "check/sema.hpp"

#include <cstdint>
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
    // A constant `[]const u8` bound to a comptime parameter. Not a type.
    bool is_text = false;
    std::string text;
    // An enumerator bound to a comptime parameter. The use is `Type.member`, not the integer.
    bool is_enum_value = false;
    std::string enum_member;
    // A struct, float, alias, or function bound to a comptime parameter. The use is this expression.
    bool is_constant_expr = false;
    std::shared_ptr<Expr> constant_expr;
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
    // A tuple bound to `T`. One type is stored in the fields above instead.
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
    // The initializer of that local `const` folded to an `i32`. A tuple index may use it.
    // It is not a file-scope constant, so it is not an array length.
    bool folded_local = false;
    // Pointer receiver `self *this`. The name is not assigned. `*self` and `self.x` are.
    bool fixed_receiver = false;
    // A parameter marked `comptime`. A method or a generic call may forward the name.
    bool comptime_param = false;
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
    // 0 when this local is not tracked. A tracked local stays open until every leaf is assigned.
    int init_id = 0;
    // Leaves of the object a tracked pointer refers to. 0 when the pointee is not tracked.
    int pointee_id = 0;
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
            // `name type = expr`. An omitted field uses that value.
            bool has_init = false;
            // The expression written on the field. Null when `has_init` is clear.
            const Expr *init = nullptr;
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
        // `union(pkg.Kind)`. Empty when the enum is in this package.
        std::string tag_package;
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
        // `extern(abi="c")`. Two such lists on one name are `redefinition`.
        bool extern_c = false;
        // `name="symbol"`. Two different names on one list are `redefinition`.
        std::string link_name;
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
            // The call is `unsafe value.name(...)`. The struct method repeats `unsafe`.
            bool unsafe_fn = false;
            SourceLoc loc{};
            // Trait that declared this method. A copy on a child keeps the parent's id.
            int home = -1;
            // The iteration trait's pointer-result method. A value parameter does not require it.
            bool iteration_ref = false;
            // The iteration trait's element method. Its result is the element type.
            bool iteration_element = false;
        };
        struct Field {
            std::string name;
            Type type;
            SourceLoc loc{};
            // Trait that declared this field. A copy on a child keeps the parent's id.
            int home = -1;
        };
        std::vector<Method> methods;
        std::vector<Field> fields;
        // `type Name trait(Parent)`. Stays set after methods and fields are copied.
        // Negative when this trait has no parent.
        int parent_id = -1;
        std::string parent_name;
        std::string parent_package;
        SourceLoc parent_loc{};
        // `func Trait::name`. A struct that satisfies the trait gains these. The struct's own
        // method with the same parameters after the receiver wins. The decl stays in the unit,
        // or in `owned_defaults_` for the built-in `Allocator.malloc[T]`.
        std::vector<const FunctionDecl *> defaults;
        // Field declarations, typed after structs are complete.
        std::vector<FieldDecl> pending;
        // The extern trait in `<std_root>/std/seq`, after it is specialized.
        bool iteration = false;
        // `export type Shape trait`. The name is published. There is no symbol.
        bool exported = false;
        // Result of the element method. Null when that method is missing.
        std::shared_ptr<Type> iteration_elem;
    };

    std::vector<Scope> scopes_;
    int function_seq_ = 0;
    std::vector<StructInfo> structs_;
    // `fields()` yields `[]const Field`: `name []const u8` and `kind *const Type`.
    // Negative until `ensure_field_info` runs. `builtin.Field` replaces it when that struct is declared.
    int field_info_id_ = -1;
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
        // The subject when it is an enum. Invalid means the subject is `i32`.
        Type subject;
        // `extern(abi="c")` converts where an integer is expected. A case may be that enum or an `i32`.
        bool c_enum = false;
    };

    uint32_t file_id_ = 0;
    // File that defines the function whose trait arguments are being checked. Zero keeps the caller's view.
    uint32_t trait_fn_home_ = 0;
    Type return_type_ = type_invalid();
    // Set while folding `sizeof`, so a second lookup does not repeat a diagnostic.
    bool quiet_ = false;
    // A generic specialization may learn an omitted result. File signatures stay pending until
    // every function in the file is registered.
    bool allow_lazy_infer_ = false;
    // The walk is recording `return` types for an omitted result. Nested functions do not record.
    bool collect_returns_ = false;
    struct InferredReturn {
        bool bare = false;
        // `return f()` while checking `f`. It does not choose the result type.
        bool self = false;
        // `return g()` names this function. Null when the return is not that call.
        const FunctionDecl *callee = nullptr;
        SourceLoc loc{};
        std::vector<Type> parts;
    };
    std::vector<InferredReturn> inferred_returns_;
    std::unordered_set<FunctionDecl *> infer_running_;
    // The omitted result was already diagnosed, so a later pass must not report it again.
    std::unordered_set<FunctionDecl *> infer_mismatch_;
    // Every reached `return` calls this function. The real check reports `infinite loop`.
    std::unordered_set<FunctionDecl *> self_loop_;
    struct ReachedReturn {
        SourceLoc loc{};
        bool self = false;
        const FunctionDecl *callee = nullptr;
    };
    std::vector<ReachedReturn> reached_returns_;
    // Reached returns of each function, so a cycle of calls can be reported after the file.
    struct LoopInfo {
        bool falls_through = false;
        bool value = false;
        bool reported = false;
        SourceLoc loc{};
        std::vector<const FunctionDecl *> callees;
    };
    std::unordered_map<const FunctionDecl *, LoopInfo> loop_info_;
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
    // Fill `fn.results` from the body's `return` expressions. No `return`, a bare
    // `return;`, and `return` of a `void` value are `void`. Returns that name different
    // types are an error. A call whose result is not known yet does not choose the type.
    // A trait is not a result. The body is checked again later.
    bool infer_omitted_result(FunctionDecl &fn);
    void infer_file_results(const TranslationUnit &unit);
    void publish_inferred(FunctionDecl &fn);
    bool commit_inferred(FunctionDecl &fn);
    // The first `return` is the type. Each later `return` that differs is reported.
    bool report_inferred_mismatch();
    // A trait has no size, so it is not an omitted result.
    bool report_inferred_trait(const FunctionDecl &fn);
    // The function a direct call names. Null when the expression is not that call.
    const FunctionDecl *call_target(const Expr &expr);
    // The return expression is a call of `fn`.
    bool returns_self(const FunctionDecl &fn, const Expr &expr);
    // Every reached `return` calls this function, and the body does not fall through.
    void report_self_loop();
    void remember_loop(const FunctionDecl &fn);
    // A cycle of calls with no reached value never finishes.
    void report_call_cycles(const TranslationUnit &unit);
    bool install_inferred_type(const Type &type, ResultSlot &slot);
    bool spell_inferred_component(const Type &type, FnParam &out, bool allow_void);
    bool spell_inferred_fn_param(const Type &type, FnParam &out);
    void clear_inferred_result(FunctionDecl &fn);
    void check_local_function(const FunctionDecl &fn);
    Symbol *lookup(const std::string &name);
    Symbol *lookup_scope(const std::string &name);
    bool declare(Symbol symbol, SourceLoc loc);
    // Replaces a local in the current block. The initializer has already seen the previous binding.
    bool shadow(Symbol symbol, SourceLoc loc);
    bool remember_shared(const Symbol &symbol, SourceLoc loc);
    void note_body(const std::string &name);
    bool shared_has(const std::string &name) const;
    std::vector<uint32_t> files_in_package(uint32_t file_id) const;
    int find_tag(uint32_t file, const std::string &name) const;
    int find_enum(uint32_t file, const std::string &name) const;
    // File that defines this struct, enum, or typedef. The current file when the type is missing.
    uint32_t defined_in(const std::string &name) const;
    // A struct, an enum, a primitive alias, or a slice alias.
    bool named_type_owner(const std::string &name) const;
    // `named_type_owner`, plus a pointer alias, a sized-array alias, or a struct alias.
    // Those three own a static function. They do not own a method or `var Type::name`.
    bool static_type_owner(const std::string &name) const;
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
    void check_field_defaults(const TranslationUnit &unit);
    void check_field_default(const Expr &expr, const Type &type, SourceLoc loc);
    void prepare_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);
    void check_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);

    Type type_of_name(const std::string &name, SourceLoc loc, bool allow_incomplete = false);
    Type with_indirections(Type type, int count, SourceLoc loc, uint32_t optional = 0, uint32_t const_pointee = 0);
    void note_struct_use(bool is_struct, const std::string &name, SourceLoc loc);
    Type object_type(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                     bool parameter, const std::string &package = {}, bool is_struct = false, bool is_enum = false,
                     bool receiver = false, uint32_t optional = 0, uint32_t const_pointee = 0,
                     bool allow_incomplete = false, bool elem_const = false, bool elem_slice = false,
                     const std::vector<TypeShell> &shells = {}, bool variable = false);
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
    // `parameter` decays a top-level `[N]T`. An element of a tuple stays that array.
    Type type_of_fn_param(const FnParam &param, SourceLoc loc, bool parameter = true);
    Type type_of_fn_ptr(const std::string &ret_name, const std::string &ret_package, bool ret_struct, bool ret_enum,
                        int ret_indirections, int fn_depth, const std::vector<FnParam> &params, bool variadic,
                        const std::vector<FnParam> &results, SourceLoc loc, uint32_t ret_optional = 0,
                        uint32_t ret_const = 0);
    Type type_of_param(const Param &param);
    Type type_of_var(const VarDecl &var);
    // `(i32, i32)` written on a variable or a field. A shell around that spelling stays rejected.
    Type type_of_written_tuple(const std::vector<ResultSlot> &parts, int indirections, bool is_array,
                               const std::vector<TypeShell> &shells, SourceLoc loc);
    bool indexed_type(const Type &base, Type &out, bool &pointee_void) const;
    // `allow_incomplete` is a tuple alias registered before struct bodies exist.
    Type type_of_result(const ResultSlot &slot, SourceLoc loc, bool allow_incomplete = false);
    std::string type_desc(const Type &type) const;
    // The slice `Type.fields()` returns. Invalid before `ensure_field_info`.
    Type type_field_slice() const;
    void ensure_field_info();
    // `builtin.Field` when that struct is `name []const u8` and `kind *const Type`. Otherwise -1.
    int builtin_field_struct() const;
    // Writes `reflected_fields` on the package graph after every struct is known.
    void publish_reflected_fields();
    // `typeof` of a tuple. Element `i` is a field named `"i"` whose kind is that element's type.
    void note_tuple_fields(const Type &type);
    bool is_builtin_fmt(const Expr::Call &call);
    std::optional<std::string> format_call(const Expr::Call &call);
    void fold_format_expr(std::unique_ptr<Expr> &slot);
    void expand_generic_macro(const Expr::Call &call, const FunctionDecl &fn, std::vector<std::unique_ptr<Stmt>> &out);
    std::unordered_map<std::string, std::vector<ReflectedField>> tuple_fields_;
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
    // `extern func use[T type](s Shape, n T)` still takes a trait. `T Shape` does not.
    bool extern_generic_trait_param(uint32_t file, const FunctionDecl &fn) const;
    // A parameter of a generic function whose type is a trait or `Seq[T]`.
    // `value T` on `[T type]` is not one. That parameter stays `not supported yet`.
    bool generic_trait_parameter(const FunctionDecl &fn) const;
    FnList function_list(const FunctionDecl &fn, Type signature) const;
    // Picks the parameter list a call uses. Null when none matches or more than one matches.
    const MethodInfo *choose_method(const Expr::Call &node, const MethodInfo &head, bool want_static, bool foreign,
                                    const std::string &name);
    Type finish_overloaded(Expr &expr, const Symbol &symbol, bool deferring, bool used);
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
    // `Drawable(value)` selects that trait. A generic trait takes its type argument from the value.
    // False when `name` is not a trait. `out` is invalid when the value does not satisfy it.
    bool as_trait_cast(const std::string &package, const std::string &name, const std::vector<TypeArg> &written, Expr *value,
                       SourceLoc loc, Type &out);
    // `[N]T` and `[]T` satisfy the iteration trait when an index yields its element.
    // A slice alias is accepted only when it has the methods. True when the value is
    // accepted, or when the failure was already reported.
    bool accept_iteration(const Type &got, const Type &want, SourceLoc loc);
    void alias_satisfies(const Type &alias, int trait_id, SourceLoc loc);
    // The file that defines `callee`, when it is a name or `pkg.name`. Zero when that file is unknown.
    uint32_t function_home(const Expr &callee);
    // Non-zero when `signature` takes a trait and `callee` names that function.
    uint32_t trait_signature_home(const Type &signature, const Expr *callee);
    // A trait parameter is specialized from a body. A prototype alone cannot be called.
    void reject_trait_prototype(const Type &signature, bool has_body, SourceLoc loc);
    bool callee_has_body(const Expr &callee);
    // Restores the caller's trait-function file when checking one call's arguments.
    struct TraitHome {
        Checker &checker;
        uint32_t saved;
        TraitHome(Checker &checker, uint32_t home);
        ~TraitHome();
        TraitHome(const TraitHome &) = delete;
        TraitHome &operator=(const TraitHome &) = delete;
    };
    // The one method of a trait or `*Trait`. Null when the trait has a field or any other method count.
    const Type *sole_trait_method(const Type &want) const;
    // An anonymous function used as a single-method trait argument. The body is checked
    // against that method, with no receiver.
    bool accept_trait_lambda(const Expr &expr, const Type &want);
    // A named function with that method's signature. A prototype, an overload, an
    // `unsafe func`, and a function-pointer variable stay on the ordinary path.
    bool accept_trait_named(const Expr &expr, const Type &want);
    void check_call_arg(const Expr &expr, const Type &want);
    // Receiver checks shared by an enum value and a primitive alias.
    Type finish_value_method(Expr &expr, const Expr::Selector &selector, const MethodInfo &method, const Type &base,
                             bool through_pointer, bool used);
    void register_structs(const TranslationUnit &unit, uint32_t file_id);
    void require_extern_struct(const Type &type, SourceLoc loc);
    // A tagged union, or a pointer or array of one. An alias keeps the union's `struct_id`.
    bool mentions_tagged_union(const Type &type) const;
    void register_enums(const TranslationUnit &unit, uint32_t file_id);
    void check_unions();
    // Resolve `union(Kind)` on one struct, including a specialization built later.
    void bind_union_tag(StructInfo &info);
    // `pkg.Kind` on a union. Reports the diagnostic and returns -1 when the enum is not usable.
    int imported_enum(uint32_t file, const std::string &package, const std::string &name, SourceLoc loc);
    // `T pkg.Shape`. The package is an import of `file`, which is the template's file.
    // Reports the diagnostic and returns -1 when that trait is not usable.
    int imported_trait(uint32_t file, const std::string &package, const std::string &name, SourceLoc loc);
    void register_traits(const TranslationUnit &unit, uint32_t file_id);
    // The package directory is `<std_root>/std/seq`. The extern trait there is the iteration trait.
    bool package_is_iteration(uint32_t file) const;
    enum class IterationMethod { Other, Length, Element, Ref };
    // Classifies one method of the iteration trait from the declaration. Names are not a second list.
    IterationMethod iteration_method(const FunctionDecl &method, const std::string &type_param) const;
    void reject_iteration_extras(const TraitDecl &trait);
    // Copy a parent's methods, fields, and defaults into each `trait(Parent)`.
    void inherit_traits();
    void define_trait_fields();
    // Copy typed fields from the parent. Each copy keeps `home` pointing at that trait.
    void link_parent_fields();
    // True when `got` is `want`, or `got`'s parent chain reaches `want`.
    bool trait_is(int got, int want) const;
    void register_typedefs(const TranslationUnit &unit, uint32_t file_id);
    // `type Something[K type, V type] (K, V)` and `type MyAlias[T type] Vec[T]`.
    // The decl is a template, not a concrete alias.
    void register_tuple_alias_template(const TypedefDecl &item);
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
    // `{.value = 10}` with no destination type. Each field takes the type of its value.
    Type infer_brace_struct(Expr::Init &init);
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
    // `(value)` becomes a tuple of one element when `want` is that tuple.
    bool adopt_one_tuple(Expr &expr, const Type &want);
    void check_stmt(const Stmt &stmt);
    // Checks each statement. A macro call is replaced by the statements it emits.
    void check_statements(std::vector<std::unique_ptr<Stmt>> &statements);
    void append_statement(std::unique_ptr<Stmt> stmt, std::vector<std::unique_ptr<Stmt>> &out);
    void expand_macro_call(const Expr::Call &call, const FunctionDecl &fn, std::vector<std::unique_ptr<Stmt>> &out);
    void expand_macro_stmt(std::unique_ptr<Stmt> stmt, const FunctionDecl &fn, const std::vector<std::string> &names,
                           const std::vector<const Expr *> &exprs, std::vector<std::unique_ptr<Stmt>> &out, bool substitute);
    void unroll_inline_for(const Stmt &stmt, const FunctionDecl &fn, const std::vector<std::string> &names,
                           const std::vector<const Expr *> &exprs, std::vector<std::unique_ptr<Stmt>> &out);
    // The constant a `comptime` parameter denotes. Null when the argument is not that constant.
    std::unique_ptr<Expr> comptime_argument(const Expr &arg, const Param &param);
    const FunctionDecl *comptime_callee(const Expr::Call &call);
    const FunctionDecl *macro_callee(const Expr::Call &call);
    const FunctionDecl *generic_macro(const Expr::Call &call);
    // An ordinary function whose every parameter is `comptime`. A `comptime func` is not this.
    const FunctionDecl *baked_callee(const Expr::Call &call);
    // A `comptime` parameter of an ordinary function requires a comptime argument.
    // `skip` is 1 for a method call. The receiver is not an argument.
    void require_comptime_params(const FunctionDecl &fn, const Expr::Call &call, std::size_t skip = 0);
    // `builtin.atomic_*` and `builtin.fence`. True when the call is rejected.
    // `bound` is the type argument. Null on `fence` and on a specialization.
    bool atomic_builtin_rejected(const FunctionDecl &fn, const Expr::Call &call, const SpecArg *bound);
    std::optional<int> fold_comptime_call(const Expr::Call &call);
    // The comptime slice overload of `builtin.memeql` or `builtin.memcmp` when both arguments are
    // constant `[]const u8`. Null when that overload is not the one to fold.
    const FunctionDecl *mem_compare_callee(const Expr::Call &call);
    // The folded `[]const u8` slice call. Null when an argument is not that slice, or `T` is not `u8`.
    std::optional<int> fold_mem_compare(const Expr::Call &call);
    // A folded integer, or a constant `[]const u8` in this file. A `var` and a local `const` are not.
    bool argument_is_comptime(const Expr &arg);
    // A struct, float, alias, or function that a file-scope `const` can store. An enumerator is not one.
    // A `var`, a local `const`, and a tuple are not.
    bool comptime_constant(const Expr &expr);
    // That constant, with a file-scope name replaced by its initializer.
    std::unique_ptr<Expr> comptime_substitute(const Expr &expr);
    // The piece of a spec name for that expression. Empty when the value has none.
    std::optional<std::string> comptime_spec_piece(const Expr &expr);
    // An enumerator, or the name of a `comptime` parameter. A method passes it. A generic copy keeps it.
    bool comptime_kept_argument(const Expr &arg);
    // Every `comptime` parameter has a comptime argument. A function with none passes.
    bool comptime_arguments(const FunctionDecl &fn, const Expr::Call &call);
    std::unordered_set<const FunctionDecl *> comptime_expanding_;
    std::unordered_set<const FunctionDecl *> comptime_folding_;
    // Nonzero while checking a `comptime func` or a `macro`. Nested calls are not expanded there.
    int comptime_depth_ = 0;
    // Nonzero while checking a `macro`. `emit` is legal only then, and not inside a runtime loop.
    int macro_depth_ = 0;
    // `while`, `do`, and a `for` outside a macro. A `for` in a macro is unrolled.
    int plain_loop_depth_ = 0;
    void check_for_each(const Stmt::For &node);
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
    // A condition whose values are comptime, including an enumerator compare. A `var` does not fold.
    std::optional<int> fold_condition(const Expr &expr);
    // True when every value is comptime, including a `comptime` parameter that this copy still receives.
    bool comptime_value(const Expr &expr);
    // `fold_int`, and a local `const` whose initializer folded. A `var` does not.
    std::optional<int> constant_index(const Expr &expr);
    // `.len` when the number of elements is known before this selector is typechecked.
    std::optional<int> known_len(const Expr &base);
    bool folding_fields_len_ = false;
    // A string literal, a constant range of one, or a file-scope const in this file
    // whose initializer is one. A pointer const is not that slice. A `var` is not a constant.
    std::optional<std::string> constant_string(const Expr &expr, std::unordered_set<std::string> &seen);
    // An `i32` case, or an enumerator such as `Color.Blue`. Other expressions are not case constants.
    std::optional<int> fold_switch_case(const Expr &expr);
    bool switch_case_matches(const SwitchFrame &frame, const Type &got) const;
    std::optional<FoldedNumber> fold_number(const Expr &expr);
    std::optional<FoldedNumber> fold_number(const Expr &expr, std::unordered_set<std::string> &seen);
    // `typeof(...)` after checking. Empty when this expression is not that constant.
    std::optional<std::string> type_constant(const Expr &expr) const;

    // One step from a local toward the use. The stack is outer-first.
    struct InitStep {
        enum class Kind { Field, Index, Range, Deref };
        Kind kind = Kind::Field;
        std::string field;
        int index = 0;
        bool known = false;
    };
    // Absent from `open` means every leaf is assigned. A present byte is 1 when that leaf is assigned.
    struct AssignState {
        bool unreachable = false;
        std::unordered_map<int, std::vector<uint8_t>> open;
    };
    struct FlowFrame {
        enum class Kind { Loop, Switch };
        Kind kind = Kind::Loop;
        AssignState entry;
        std::vector<AssignState> breaks;
        std::vector<AssignState> continues;
        struct Local {
            int id = 0;
            int leaves = 0;
        };
        // Locals declared in this switch. A later case enters them directly, so they are open again.
        std::vector<Local> locals;
    };
    enum class InitUse { Read, Address, Method };

    class InitUseGuard {
      public:
        InitUseGuard(Checker &self, InitUse use);
        ~InitUseGuard();
        InitUseGuard(const InitUseGuard &) = delete;
        InitUseGuard &operator=(const InitUseGuard &) = delete;

      private:
        Checker *self_;
        InitUse saved_;
    };
    class InitPush {
      public:
        InitPush(Checker &self, InitStep step);
        ~InitPush();
        InitPush(const InitPush &) = delete;
        InitPush &operator=(const InitPush &) = delete;

      private:
        Checker *self_;
    };
    class InitKeep {
      public:
        explicit InitKeep(Checker &self);
        ~InitKeep();
        InitKeep(const InitKeep &) = delete;
        InitKeep &operator=(const InitKeep &) = delete;

      private:
        Checker *self_;
    };
    class InitQuiet {
      public:
        explicit InitQuiet(Checker &self);
        ~InitQuiet();
        InitQuiet(const InitQuiet &) = delete;
        InitQuiet &operator=(const InitQuiet &) = delete;

      private:
        Checker *self_;
        bool saved_;
    };
    class UnreachableAfter {
      public:
        explicit UnreachableAfter(Checker &self);
        ~UnreachableAfter();
        UnreachableAfter(const UnreachableAfter &) = delete;
        UnreachableAfter &operator=(const UnreachableAfter &) = delete;

      private:
        Checker *self_;
    };
    class InitSuspend {
      public:
        explicit InitSuspend(Checker &self);
        ~InitSuspend();
        InitSuspend(const InitSuspend &) = delete;
        InitSuspend &operator=(const InitSuspend &) = delete;

        Checker *self_ = nullptr;
        AssignState assign;
        std::vector<FlowFrame> frames;
        std::vector<InitStep> stack;
        InitUse use = InitUse::Read;
        bool quiet = false;
        int keep = 0;
        bool split = false;
        AssignState cond_true;
        AssignState cond_false;
    };

    struct InitTouch {
        bool read = false;
        bool mark = false;
        // The span is a sized array used as an address. The elements were not read.
        // `*[N]T` may still store into that span. `*T` and a parameter `[N]T` do not.
        bool array_addr = false;
        int offset = 0;
        int count = 0;
    };

    int init_leaf_count(const Type &type);
    int init_leaf_count(const Type &type, std::unordered_set<int> &seen);
    // The name under indexes, fields, and `unsafe`. `peel_deref` also walks `*`.
    const Expr::Name *init_root(const Expr &expr, bool peel_deref) const;
    void init_set_span(std::vector<uint8_t> &bits, int offset, int count);
    void init_fill(const Type &type, const Expr *init, std::vector<uint8_t> &bits, int offset);
    void init_fill_brace(const Type &type, const Expr::Init *brace, std::vector<uint8_t> &bits, int offset);
    void init_cover(Symbol &symbol, const Expr *init);
    void init_track_pointee(Symbol &symbol, const Type &object, const Expr *init, bool fresh);
    void init_address_span(const Expr &expr, bool mark, SourceLoc loc, bool array_pointer);
    bool init_pointee_steps(const Symbol &symbol, std::vector<InitStep> steps) const;
    void init_open(Symbol &symbol);
    void init_note_local(int id, int leaves);
    void init_mark_span(int id, int offset, int count);
    void init_mark_all(int id);
    void init_require_span(int id, int offset, int count, SourceLoc loc);
    void init_touch(const Symbol &symbol, SourceLoc loc);
    InitTouch init_resolve(Type type, int offset, int count, const std::vector<InitStep> &steps, bool storing);
    bool init_collect(const Expr &expr, std::vector<InitStep> &steps);
    void init_replace(const Expr &lhs, const Expr &rhs);
    void init_store(const Expr &lhs);
    void init_store_if(const Expr &lhs, std::size_t before);
    // A `self this` call copies the receiver. A `self *this` call does not.
    void init_require_receiver(const Expr &expr);
    AssignState init_merge(const AssignState &lhs, const AssignState &rhs) const;
    AssignState init_loop_exit(const std::vector<AssignState> &breaks, const AssignState &fail, bool can_fail) const;
    bool init_has_default(const Stmt *stmt) const;
    void init_break();
    void init_continue();
    void init_suspend(InitSuspend &saved);
    void init_resume(InitSuspend &saved);
    FlowFrame *init_switch_frame();
    void check_switch(const Stmt::Switch &node);
    void check_case_label(const Stmt::Case &node);
    // `can_skip` merges the entry state. A folded value cannot skip the taken path.
    void finish_switch(bool can_skip);
    // The first case whose label is `value`, else the first `default` left in `fallback`.
    bool find_switch_arm(const Stmt &stmt, int value, const Stmt::Case *&match, const Stmt::Case *&fallback);
    bool switch_reaches_case(const Stmt &stmt, const Stmt::Case *arm) const;
    bool check_switch_from(const Stmt &stmt, const Stmt::Case *arm, bool &active);
    bool check_switch_block(Stmt::Block &block, const Stmt::Case *arm, bool &active);

    // Definite assignment. Only a function-local `var` with no settled initializer is tracked.
    AssignState assign_;
    AssignState cond_true_;
    AssignState cond_false_;
    bool cond_split_ = false;
    std::vector<InitStep> init_stack_;
    InitUse init_use_ = InitUse::Read;
    bool init_quiet_ = false;
    int init_keep_array_ = 0;
    // A brace used as a local initializer may leave fields unset.
    bool init_allow_partial_ = false;
    // `var p = new T{...}` keeps the object's holes on `p`.
    bool init_new_partial_ = false;
    // Set while checking an assignment whose right side is a brace list.
    const Expr *init_cover_rhs_ = nullptr;
    int init_seq_ = 0;
    std::unordered_map<int, int> init_leaves_;
    std::unordered_map<int, int> init_struct_leaves_;
    std::vector<FlowFrame> flow_frames_;

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
    std::deque<TypedefDecl> owned_typedefs_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::vector<FunctionDecl *>>> fn_templates_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, StructDecl *>> struct_templates_;
    // `type Something[K type, V type] (K, V)`. A use `Something[i32, i32]` builds one alias.
    std::unordered_map<uint32_t, std::unordered_map<std::string, const TypedefDecl *>> typedef_templates_;
    // `type Seq[T type] trait`. The decl stays in the unit. A use `Seq[i32]` builds one trait.
    std::unordered_map<uint32_t, std::unordered_map<std::string, const TraitDecl *>> trait_templates_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, ConstBinding>> const_values_;
    std::deque<OwnedFn> deferred_methods_;
    // `func Vec[T]::get[U type]` after `Vec` is specialized. `U` is still open.
    // Pointers into this deque stay valid. Codegen does not emit these copies.
    std::deque<OwnedFn> held_method_templates_;
    // The built-in `Allocator.malloc[T type]`. Pointers into this deque stay valid.
    std::deque<FunctionDecl> owned_defaults_;
    // file, owner, name. `func Arena::malloc[T type]`. The decl stays in the unit.
    std::unordered_map<uint32_t,
                       std::unordered_map<std::string, std::unordered_map<std::string, std::vector<const FunctionDecl *>>>>
        method_templates_;
    // `file\nstruct\ntrait` once that struct has this trait's plain defaults.
    std::unordered_set<std::string> defaults_ready_;
    std::unordered_set<std::string> granting_;
    // Instantiated copy -> the default or method template it was copied from.
    std::unordered_map<const FunctionDecl *, const FunctionDecl *> default_origin_;
    // `a.malloc[i32]()` on a trait. Installed on each struct that satisfies the trait.
    struct TraitGenericUse {
        int trait_id = -1;
        const FunctionDecl *templ = nullptr;
        std::vector<SpecArg> bounds;
        std::string spec_name;
        bool through_pointer = false;
    };
    std::vector<TraitGenericUse> trait_generic_uses_;
    std::vector<PendingAppend> pending_appends_;
    std::unordered_set<std::string> spec_checking_;
    std::unordered_set<std::string> spec_ready_;
    // Nested comptime-parameter specializations. The cap matches a macro `for`.
    int comptime_spec_depth_ = 0;
    // `file\nSpec` while that specialization's variables are being built.
    std::unordered_set<std::string> assoc_busy_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> spec_symbols_;
    std::unordered_set<std::string> static_spec_keys_;

    void collect_templates(TranslationUnit &unit, uint32_t file_id);
    // A generic parameter the declaration does not name is `unused parameter`.
    // A struct, union, trait, or alias also counts a method or an associated variable.
    // Repeating `Vec[T]` on a method does not use `T`. An unused alias is cleared.
    void reject_unused_generics(TranslationUnit &unit);
    bool mark_unused_parameters(const std::vector<TypeParam> &params, const std::unordered_set<std::string> &used);
    void reject_function_generics(const FunctionDecl &fn);
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
    // An array, a string, a slice whose initializer is one of those, or a range of one.
    // A missing bound is constant. A bound that does not fold is not. A call is not.
    bool constant_slice_expr(const Expr &expr, std::unordered_set<std::string> &seen);
    // A file-scope array name, or a constant index of one, whose initializer is constant.
    // A call is not. The name has to already be an array.
    bool constant_array_source(const Expr &expr, std::unordered_set<std::string> &seen);
    // A sized array copied from a name, an index, or a call. A list is already constant.
    // A call on a `var` runs before `main`. A `const` and any other source stay constant.
    bool accept_array_copy(VarDecl &var);
    bool constant_operand(const Expr &expr);
    std::optional<int> lookup_const_value(const std::string &name);
    void prepare_written(std::string &type_name, std::string &type_package, bool &is_struct, bool &is_enum,
                         int &indirections, uint32_t &optional, bool &is_array, int &array_len,
                         std::string &array_len_name, std::vector<TypeArg> &type_args, SourceLoc loc);
    const StructDecl *find_struct_template(const std::string &name, uint32_t &owner) const;
    const TraitDecl *find_trait_template(const std::string &name, uint32_t &owner) const;
    const TypedefDecl *find_tuple_alias_template(const std::string &name, uint32_t &owner) const;
    std::optional<Type> maybe_generic_call(Expr &expr, bool used);
    // `func scale(n comptime i32, x i32)`. One copy per constant. `n` is inlined.
    std::optional<Type> maybe_comptime_param_call(Expr &expr, bool used);
    // `skip` is the receiver, which is not an argument. `owner_file` is the function's file
    // when the decl is a clone that `comptime_decl_file` cannot see.
    bool apply_comptime_template(Expr::Call &call, const FunctionDecl &fn, bool several, std::size_t skip = 0,
                                 std::optional<uint32_t> owner_file = std::nullopt);
    // Specialize when every comptime argument is a constant. A forwarded name leaves the call.
    std::optional<Type> call_comptime_spec(Expr &expr, const FunctionDecl *fn, std::size_t skip, uint32_t owner,
                                           bool used);
    uint32_t comptime_decl_file(const FunctionDecl &fn) const;
    void register_trait_default(const FunctionDecl &fn);
    void register_method_template(const FunctionDecl &fn);
    // The function type of a method with the receiver left out. Trait signatures have no receiver.
    Type method_tail(const FunctionDecl &fn);
    bool plain_default_supplies(int trait_id, const TraitInfo::Method &method, bool through_pointer);
    void grant_trait_defaults(uint32_t struct_file, const std::string &struct_name, int trait_id);
    void install_trait_generics(uint32_t struct_file, const std::string &struct_name, int trait_id);
    void grant_covered_defaults(int struct_id);
    const MethodInfo *find_method_head(uint32_t file, const std::string &owner, const std::string &name) const;
    // `name="symbol"` is one object name. A function or a variable already using it is `redefinition`.
    bool link_name_taken(const std::string &link_name) const;
    const FunctionDecl *install_default(const FunctionDecl &templ, uint32_t struct_file, const std::string &struct_name,
                                        const std::string &spec_name, const std::vector<SpecArg> &bounds);
    // 0: no template of either staticness has this type-argument count.
    // 1: one template of the requested staticness. 2: type mismatch.
    struct MethodTemplateChoice {
        const FunctionDecl *templ = nullptr;
        int kind = 0;
    };
    MethodTemplateChoice pick_method_template(uint32_t home, const std::string &owner, const std::string &name,
                                              const Expr::Call &call, bool want_static) const;
    // Installs one specialization and rewrites the call to that concrete method.
    // `value_base` checks the receiver when the call is rejected. A type name is not a value.
    std::optional<Type> apply_method_template(Expr &expr, const FunctionDecl &templ, uint32_t home,
                                              const std::string &owner, bool value_base, bool used);
    std::optional<Type> try_method_template(Expr &expr, bool used);
    void drain_deferred();
    void flush_appends();
    TranslationUnit *unit_for(uint32_t file_id);
    bool spell_type(const Type &type, SpecArg &out) const;
    std::string mangle_spec(const std::string &base, const std::vector<SpecArg> &args) const;
    std::string specialize_type_app(const std::string &package, const std::string &name, const std::vector<TypeArg> &args,
                                    SourceLoc loc);
    // `Seq[i32]`. The result is the specialized trait's name. Empty when the arguments do not bind.
    std::string specialize_trait(const TraitDecl &templ, uint32_t owner, const std::vector<TypeArg> &args,
                                 SourceLoc loc);
    // `Something[i32, i32]`. The result is the specialized alias's name. Empty when the arguments do not bind.
    std::string specialize_tuple_alias(const TypedefDecl &templ, uint32_t owner, const std::vector<TypeArg> &args,
                                       SourceLoc loc);
    bool bind_type_arg(const TypeParam &param, const TypeArg &arg, SpecArg &out, SourceLoc loc);
    void expose_bounds(uint32_t owner, const std::vector<SpecArg> &bounds);
    // A struct name that already means a different struct in `owner`, or that exists in two packages,
    // is spelled `Name.file.N` so both checker and codegen resolve that struct.
    void disambiguate_bounds(uint32_t owner, std::vector<SpecArg> &bounds);
    // `(T, i32)` on a generic field. Each element is substituted, including a nested tuple.
    void substitute_slot(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, ResultSlot &slot,
                         SourceLoc loc);
    void substitute_type(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, std::string &type_name,
                         std::string &type_package, bool &is_struct, bool &is_enum, int &indirections, uint32_t &optional,
                         bool &is_array, int &array_len, std::string &array_len_name, std::vector<TypeArg> &type_args,
                         SourceLoc loc, std::vector<TypeShell> *shells = nullptr, bool *elem_const = nullptr,
                         uint32_t *const_pointee = nullptr);
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
    // `Vec[i32].get`, `Buf[4, i32].get`, `pkg.Vec[i32].get`, and `pkg.Buf[4, i32].get`.
    // True when that form was recognized. An empty `owner` means the error is already reported.
    bool generic_spec_base(const Expr::Selector &node, uint32_t &home, std::string &owner);
    // `pkg.Type.name(...)` and `pkg.Vec[i32].name(...)`. True when that call was handled.
    bool package_static_call(Expr &expr, const Expr::Selector &selector, bool deferring, bool used, Type &out);
    // A static function used as a value on a type from another package.
    // True when the name is a method. False leaves the caller's diagnostic in place.
    bool static_method_value(const Expr::Selector &node, uint32_t home, const std::string &owner, Type &out);
    Type finish_static_call(Expr &expr, const MethodInfo &method, bool deferring, bool used);
    // True when `node` is `Type.name` or `Type[args].name`. The diagnostic is already reported.
    bool assoc_from_selector(const Expr::Selector &node, Type &out);
};

} // namespace tlc
