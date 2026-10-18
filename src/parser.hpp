#pragma once

#include "ast.hpp"
#include "diag.hpp"

#include <string_view>
#include <unordered_set>
#include <vector>

namespace tlc {

class Parser {
  public:
    Parser(std::vector<Token> tokens, DiagnosticEngine &diags);

    TranslationUnit parse();

  private:
    struct Specifiers {
        SourceLoc loc{};
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool struct_definition = false;
        bool is_union = false;
        bool union_definition = false;
        std::vector<FieldDecl> fields;
        bool is_enum = false;
        bool enum_definition = false;
        std::string underlying;
        std::vector<Enumerator> enumerators;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
        bool extern_c = false;
        // `extern(..., name="symbol")`. Empty keeps the usual object name.
        std::string link_name;
        // `struct(align=N)`. Zero means the natural alignment.
        int align = 0;
        bool is_const = false;
        bool saw_type = false;
    };

    struct Declarator {
        SourceLoc loc{};
        std::string name;
        bool is_function = false;
        int indirections = 0;
        bool is_array = false;
        int array_len = 0;
        std::vector<Param> params;
        bool variadic = false;
        std::string method_struct;
        bool is_fn_ptr = false;
        int ret_indirections = 0;
        std::vector<FnParam> fn_params;
        bool fn_variadic = false;
    };

    struct ArrayMark {
        bool is_array = false;
        std::string len_name;
        int length = 0;
        // One empty `[]` after the outer brackets: `[][]T` and `[N][]T`.
        bool elem_slice = false;
    };

    std::vector<Token> tokens_;
    DiagnosticEngine &diags_;
    std::size_t pos_ = 0;
    bool saw_decl_ = false;

    bool eof() const;
    const Token &peek(std::size_t ahead = 0) const;
    bool check(TokenKind kind) const;
    bool eat(TokenKind kind);
    bool expect(TokenKind kind, const std::string &message);
    void error_here(const std::string &message);
    void error_at(const Token &token, const std::string &message);

    struct PlacedType {
        SourceLoc loc{};
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool is_enum = false;
        bool is_const = false;
        bool struct_definition = false;
        bool enum_definition = false;
        int indirections = 0;
        // Bit 0 is the outermost `*`. A set bit is `?*`.
        uint32_t optional = 0;
        // Bit 0 is the outermost `*`. A set bit means that pointer's pointee is `const`.
        uint32_t const_pointee = 0;
        bool is_array = false;
        // `[]const u8` and `[4]const u8`. The element type is const.
        bool elem_const = false;
        // `[][]T` and `[N][]T`. The element is one slice. A third pair stays rejected.
        bool elem_slice = false;
        // Constructors outside the leaf, outermost first. This is the shape `[]`, `*[]`, and `[N][M]` share.
        std::vector<TypeShell> shells;
        int array_len = 0;
        std::string array_len_name;
        std::vector<TypeArg> type_args;
        // `*[4]i32`: one pointer outside the array.
        bool ptr_to_array = false;
        bool is_fn_ptr = false;
        int ret_indirections = 0;
        uint32_t ret_optional = 0;
        uint32_t ret_const_pointee = 0;
        std::vector<FnParam> fn_params;
        bool fn_variadic = false;
        std::vector<FnParam> fn_results;
    };

    bool starts_declaration() const;
    // `unsafe`, `export`, and `extern` in any order, then `func`.
    bool starts_unsafe_func() const;
    // `unsafe var` and the other non-function forms. The diagnostic is `not supported yet`.
    bool unsupported_unsafe_decl() const;
    void skip_decl_specifiers(std::size_t &ahead, bool &saw_unsafe) const;
    void reject_unsupported_unsafe();
    bool continues_declarator() const;
    bool starts_qualified_type() const;
    bool is_type_keyword(TokenKind kind) const;

    Specifiers parse_specifiers(bool file_scope, bool allow_const = false, bool type_only = false);
    // `extern` is `abi="tlc"`. `extern(abi="c")` is a C library. `name="symbol"` is the object symbol.
    bool take_extern(bool &is_extern, bool &extern_c, std::string &link_name);
    int parse_struct_align();
    // `enum(i32)`. Empty means `int` when the caller saw no parentheses.
    std::string parse_enum_underlying();
    std::vector<Enumerator> parse_enumerators();
    // One `name type;` field. False when the next token is not a name.
    bool parse_field(FieldDecl &field);
    std::vector<FieldDecl> parse_fields();
    struct StructMembers {
        std::vector<FieldDecl> fields;
        std::vector<StructDecl> anonymous;
    };
    // Members are `name type;` or `export name type;`. `name export struct { ... }` is an inline struct.
    // `var` and `const` inside the struct are rejected. `publish` marks every member exported,
    // which is what an exported inline struct does to the members inside it.
    StructMembers parse_struct_members(const std::string &owner, bool publish = false);
    struct UnionBody {
        std::vector<FieldDecl> fields;
        std::vector<StructDecl> anonymous;
    };
    // `Circle struct { ... };` or `Circle Circle;`. The enum name was already parsed.
    UnionBody parse_union_variants(const std::string &union_name);
    Declarator parse_declarator();
    void parse_fn_pointer(int &fn_depth, std::string &name, SourceLoc &loc, std::vector<FnParam> &params, bool &variadic);
    FnParam fn_param_from(const PlacedType &type) const;
    bool at_func_type(std::size_t ahead = 0) const;
    // `as_cast` is `func (i32) i32(value)`. The `(` after the result is the operand,
    // so the result does not take the C declarator `i32 (*name)(`.
    PlacedType parse_func_type(bool as_cast = false);
    void parse_func_vars(std::vector<VarDecl> &vars, bool exported, bool is_static, bool is_extern, bool extern_c,
                         std::string link_name = {});
    // `allow_slice` accepts `[]T`. A C declarator such as `name[]` stays unsupported.
    ArrayMark parse_array_suffix(bool allow_slice = false);
    bool bracket_then_paren() const;
    // `Vec[i32]{}`. True when `[...]` is followed by `{`.
    bool bracket_then_brace() const;
    // `func Vec[T]::get`. True when `[...]` is followed by `::`.
    bool bracket_then_colon_colon() const;
    // `Buf[4, i32].count`. True when a comma inside `[...]` is followed by `.`.
    bool bracket_comma_then_dot() const;
    bool looks_like_type_params() const;
    std::vector<TypeParam> parse_type_params();
    // `Vec[T, N]` on a method. The names refer to the struct's parameters.
    std::vector<std::string> parse_method_params();
    std::vector<TypeArg> parse_type_args();
    TypeArg parse_one_type_arg();
    bool primitive_type_name(const std::string &name) const;
    bool integer_constraint_name(const std::string &name) const;
    // `allow_fn_declarator` is the C form `i32 (*name)(`. A cast turns it off
    // so `func (i32) i32(*void(p))` keeps `*void(p)` as the operand.
    PlacedType parse_placed_type(bool allow_const, bool allow_fn_declarator = true);
    // `var` and field types use Go order: `*i32`, `**Point`, `[32]u8`, `[2]*int`.
    // A pointer is prefix in every other position too: `*int`, `*const char`.
    PlacedType parse_go_type();
    // `method_struct` is empty for a free function. A method receiver is `name this` or `name *this`.
    std::vector<Param> parse_params(bool allow_const, bool &variadic, const std::string &method_struct = {});
    void take_result(FunctionDecl &fn, const PlacedType &type);
    void parse_results(FunctionDecl &fn);
    FunctionDecl parse_function(bool exported, bool is_static, bool is_extern, bool extern_c, bool allow_body,
                                std::string link_name = {});
    // `type Name struct`, `type Name enum`, `type Name trait`, `type Name union`,
    // or `type Name go-type` for an alias. `link_name` is rejected: a type has no object symbol.
    void parse_type_decl(TranslationUnit &unit, bool exported, bool is_extern, bool extern_c,
                         std::string link_name = {});
    void parse_type_def(TranslationUnit &unit, bool exported, bool is_extern, bool extern_c, std::string name,
                        SourceLoc name_loc, std::vector<TypeParam> type_params);
    void parse_top_declaration(TranslationUnit &unit, bool file_scope_export_ok);
    std::unique_ptr<Stmt> parse_decl_stmt(bool file_scope);
    std::unique_ptr<Stmt> parse_statement();
    AssertDecl parse_assert();
    TestDecl parse_test();
    std::unique_ptr<Stmt> parse_block();
    // The body of if, while, and for. A missing '{' is an error.
    std::unique_ptr<Stmt> parse_required_block();
    std::unique_ptr<Stmt> parse_if();
    std::unique_ptr<Stmt> parse_while();
    std::unique_ptr<Stmt> parse_do();
    std::unique_ptr<Stmt> parse_for();
    std::unique_ptr<Stmt> parse_switch();
    std::unique_ptr<Stmt> parse_case();
    std::unique_ptr<Stmt> parse_return();
    std::unique_ptr<Stmt> parse_bind(bool is_var);
    std::unique_ptr<Stmt> parse_const_local();
    bool var_is_bind() const;
    bool var_is_inferred() const;
    // `const Type::name` or `const Type[T]::name`. A local one is rejected.
    bool const_on_type() const;
    std::unique_ptr<Stmt> parse_var_or_bind();
    // `var Type::name` and `var Type[T]::name`. The first identifier is the owner.
    void take_var_owner(VarDecl &var);
    // `allow_bare_inferred` accepts `var name = expr` before a type is written.
    // A struct body uses that for `var next = 1`, the same variable as `var Type::next = 1`.
    VarDecl parse_typed_var(bool exported, bool is_static, bool is_extern, bool extern_c,
                            bool allow_bare_inferred = false, std::string link_name = {});
    // `const name = expr`, `const name Type = expr`, and `const Type::name`.
    VarDecl parse_const_decl(bool exported);
    void parse_inferred_global(TranslationUnit &unit, bool is_static, bool from_var, bool exported);
    void reject_c_var(SourceLoc loc);

    std::unique_ptr<Expr> parse_expr();
    std::unique_ptr<Expr> parse_assignment();
    std::unique_ptr<Expr> parse_conditional();
    std::unique_ptr<Expr> parse_or();
    std::unique_ptr<Expr> parse_and();
    std::unique_ptr<Expr> parse_bit_or();
    std::unique_ptr<Expr> parse_bit_xor();
    std::unique_ptr<Expr> parse_bit_and();
    std::unique_ptr<Expr> parse_equality();
    std::unique_ptr<Expr> parse_relational();
    std::unique_ptr<Expr> parse_shift();
    std::unique_ptr<Expr> parse_additive();
    std::unique_ptr<Expr> parse_multiplicative();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_alloc();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> finish_postfix(std::unique_ptr<Expr> expr);
    std::unique_ptr<Expr> parse_primary();
    // `[_]i32{1, 2, 3}`. `_` is the length, taken from the values, not an unsized type.
    std::unique_ptr<Expr> parse_array_literal();
    std::unique_ptr<Expr> parse_init();
    std::unique_ptr<Expr> parse_anon_function();
    bool starts_short_function() const;
    std::unique_ptr<Expr> parse_short_function();
    bool starts_type_paren() const;
    bool starts_cast_type() const;
    // `func (i32) i32(value)`, not an anonymous function `func (n i32) i32 { ... }`.
    bool starts_func_cast() const;
    bool skip_balanced(std::size_t &ahead, TokenKind open, TokenKind close) const;
    bool skip_placed_type(std::size_t &ahead) const;
    // `cast` is a cast or a parenthesized sizeof type. A qualified name is a
    // sizeof type only. A bare name is a pointer cast only after it has been
    // declared, so `*f()` stays a dereference.
    bool skip_type_base(std::size_t &index, int leading, bool cast) const;
    struct ParenType {
        SourceLoc loc{};
        std::string name;
        std::string package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        uint32_t optional = 0;
        uint32_t const_pointee = 0;
        // `func (i32) i32` written as the cast type. `name` is the result.
        bool is_fn_ptr = false;
        int ret_indirections = 0;
        uint32_t ret_optional = 0;
        uint32_t ret_const_pointee = 0;
        std::vector<FnParam> fn_params;
        bool fn_variadic = false;
        std::vector<FnParam> fn_results;
    };
    struct PointerPrefix {
        int count = 0;
        uint32_t optional = 0;
        uint32_t const_pointee = 0;
    };
    // `*` and `?*`. `?` before any other type is `not supported yet`.
    PointerPrefix parse_pointer_prefix();
    ParenType parse_paren_type();
    ParenType parse_cast_type();
    std::unique_ptr<Expr> parse_cast_expr();

    void parse_import(TranslationUnit &unit);
    bool at_contextual(std::string_view spelling) const;

    // Struct, enum, union, and type-alias names seen in this file.
    std::unordered_set<std::string> type_names_;
};

} // namespace tlc
