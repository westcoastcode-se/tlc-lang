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
    int array_len = 0;
    int struct_id = -1;
    int enum_id = -1;
    // Several types bound to `[...U]`. One type is stored in the fields above instead.
    bool is_pack = false;
    std::vector<SpecArg> parts;
};

struct Symbol {
    std::string name;
    Type type;
    bool has_body = false;
    bool is_function = false;
    bool is_local_function = false;
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
    const FunctionDecl *fn_template = nullptr;
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
        };
        std::vector<Field> fields;
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
    };

    struct TraitInfo {
        std::string name;
        struct Method {
            std::string name;
            Type type;
        };
        std::vector<Method> methods;
    };

    std::vector<Scope> scopes_;
    int function_seq_ = 0;
    std::vector<StructInfo> structs_;
    std::vector<TraitInfo> traits_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>>> methods_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> tags_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> trait_names_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Type>> typedefs_;
    // alias_names_[id] is the source name of a `type Name T`. Index 0 is unused.
    std::vector<std::string> alias_names_{""};
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
    // The callee of a call is being checked, and that callee is a name or a function expression.
    bool direct_callee_ = false;
    int loop_depth_ = 0;
    bool checking_defer_ = false;
    std::vector<SwitchFrame> switches_;
    // Functions whose bodies are on the stack, innermost last.
    std::vector<FunctionDecl *> checking_;

    void error(SourceLoc loc, const std::string &message);
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
    // File that defines this struct or enum. The current file when the type is missing.
    uint32_t defined_in(const std::string &name) const;
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
                     bool allow_incomplete = false);
    Type spelled_type(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                      int indirections, SourceLoc loc, uint32_t optional = 0, uint32_t const_pointee = 0);
    // A store through this lvalue would change a `const` binding or a `*const` pointee.
    bool const_path(const Expr &expr);
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
    bool dot_object(const Type &base, Type &object, bool &through_pointer) const;
    const MethodInfo *find_method(int struct_id, const std::string &name) const;
    const MethodInfo *find_named_method(const std::string &type_name, const std::string &name) const;
    const TraitInfo::Method *find_trait_method(int trait_id, const std::string &name) const;
    bool trait_method_matches(const Type &trait_fn, const Type &concrete) const;
    void require_satisfies(int struct_id, int trait_id, bool through_pointer, SourceLoc loc);
    // The one method of a trait or `*Trait`. Null when the trait has any other count.
    const Type *sole_trait_method(const Type &want) const;
    // An anonymous function used as a single-method trait argument. The body is checked
    // against that method, with no receiver.
    bool accept_trait_lambda(const Expr &expr, const Type &want);
    void check_call_arg(const Expr &expr, const Type &want);
    void register_structs(const TranslationUnit &unit, uint32_t file_id);
    void require_extern_struct(const Type &type, SourceLoc loc);
    void register_enums(const TranslationUnit &unit, uint32_t file_id);
    void check_unions();
    void register_traits(const TranslationUnit &unit, uint32_t file_id);
    void register_typedefs(const TranslationUnit &unit, uint32_t file_id);
    bool name_taken(const std::string &name) const;
    Type published_object(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                          bool parameter, const std::string &package, bool is_struct, bool is_enum,
                          uint32_t optional = 0);
    Type published_result(const std::string &name, int indirections, SourceLoc loc, const std::string &package,
                          bool is_struct, bool is_enum, uint32_t optional = 0);
    void expect(const Type &got, const Type &want, SourceLoc loc);
    void expect_value(const Type &got, const Type &want, const Expr &expr, SourceLoc loc);
    Type check_init(Expr::Init &init, const Type &want);
    void expect_integer(const Type &got, SourceLoc loc);
    void expect_condition(const Type &got, SourceLoc loc);
    bool is_lvalue_form(const Expr &expr) const;
    Type check_raw(const Expr &expr, bool used);
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
                      uint32_t const_pointee = 0);
    Type sizeof_operand(const Expr::Sizeof &node);
    std::optional<int> fold_int(const Expr &expr);
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
    std::deque<StructDecl> owned_structs_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, FunctionDecl *>> fn_templates_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, StructDecl *>> struct_templates_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, ConstBinding>> const_values_;
    std::deque<OwnedFn> deferred_methods_;
    std::vector<PendingAppend> pending_appends_;
    std::unordered_set<std::string> spec_checking_;
    std::unordered_set<std::string> spec_ready_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> spec_symbols_;
    std::unordered_set<std::string> static_spec_keys_;

    void collect_templates(TranslationUnit &unit, uint32_t file_id);
    void fold_early_consts(TranslationUnit &unit, uint32_t file_id);
    void bind_file_const(VarDecl &var);
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
    void substitute_type(const std::vector<TypeParam> &params, const std::vector<SpecArg> &bounds, std::string &type_name,
                         std::string &type_package, bool &is_struct, bool &is_enum, int &indirections, uint32_t &optional,
                         bool &is_array, int &array_len, std::string &array_len_name, std::vector<TypeArg> &type_args,
                         SourceLoc loc);
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
};

} // namespace tlc
