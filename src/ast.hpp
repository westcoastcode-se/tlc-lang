#pragma once

#include "token.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tlc {

struct FunctionDecl;

struct Expr {
    struct Name {
        SourceLoc loc{};
        std::string text;
    };
    struct Int {
        SourceLoc loc{};
        uint64_t value = 0;
        IntSuffix suffix{};
    };
    struct Unary {
        SourceLoc loc{};
        TokenKind op = TokenKind::End;
        std::unique_ptr<Expr> expr;
    };
    struct Binary {
        SourceLoc loc{};
        TokenKind op = TokenKind::End;
        std::unique_ptr<Expr> lhs;
        std::unique_ptr<Expr> rhs;
    };
    struct Assign {
        SourceLoc loc{};
        std::unique_ptr<Expr> lhs;
        std::unique_ptr<Expr> rhs;
    };
    struct Call {
        SourceLoc loc{};
        std::unique_ptr<Expr> callee;
        std::vector<std::unique_ptr<Expr>> args;
    };
    struct Selector {
        SourceLoc loc{};
        std::unique_ptr<Expr> base;
        std::string field;
    };
    struct String {
        SourceLoc loc{};
        std::string text;
    };
    struct Index {
        SourceLoc loc{};
        std::unique_ptr<Expr> base;
        std::unique_ptr<Expr> index;
    };
    struct Sizeof {
        SourceLoc loc{};
        bool on_type = false;
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        // Bit 0 is the outermost `*`. A set bit is `?*`.
        uint32_t optional = 0;
        std::unique_ptr<Expr> expr;
    };
    struct Cast {
        SourceLoc loc{};
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        uint32_t optional = 0;
        std::unique_ptr<Expr> expr;
    };
    struct Bool {
        SourceLoc loc{};
        bool value = false;
    };
    struct Nullptr {
        SourceLoc loc{};
    };
    struct Char {
        SourceLoc loc{};
        uint64_t value = 0;
    };
    struct Float {
        SourceLoc loc{};
        std::string text;
        bool is_f32 = false;
    };
    struct Func {
        SourceLoc loc{};
        std::unique_ptr<FunctionDecl> decl;
    };
    // One designated field in `{.field = value}`.
    struct FieldInit {
        SourceLoc loc{};
        std::string field;
        std::unique_ptr<Expr> value;
    };
    // `{.field = value, ...}`. The destination type supplies the struct. Omitted fields are zero.
    struct Init {
        SourceLoc loc{};
        std::vector<FieldInit> fields;
        // Filled when the destination struct is known.
        int struct_id = -1;
    };
    struct Unsupported {
        SourceLoc loc{};
    };

    using Data = std::variant<Name, Int, Unary, Binary, Assign, Call, Selector, String, Index, Sizeof, Cast, Bool,
                              Nullptr, Char, Float, Func, Init, Unsupported>;

    Data data;

    Expr();
    ~Expr();
    Expr(const Expr &) = delete;
    Expr &operator=(const Expr &) = delete;
    Expr(Expr &&) noexcept;
    Expr &operator=(Expr &&) noexcept;
};

// A parameter of a function-pointer type. Nested pointers use the same shape.
struct FnParam {
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    std::vector<FnParam> params;
    bool variadic = false;
    // Set when this function pointer returns more than one result.
    std::vector<FnParam> results;
};

struct FieldDecl {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    bool is_array = false;
    int array_len = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
    // Fields of `Name struct { ... }` inside a tlc union. Empty when the variant names a declared struct.
    std::vector<FieldDecl> inline_fields;
};

struct StructDecl {
    SourceLoc loc{};
    std::string name;
    std::vector<FieldDecl> fields;
    bool exported = false;
    bool is_extern = false;
    // `extern(abi="c")` uses C layout. Plain `extern` is abi "tlc".
    bool extern_c = false;
    // `struct(align=N)`. Zero means the natural alignment.
    int align = 0;
    // An `extern(abi="c") union` overlays its fields. A tlc union stores `tag_name` and one payload.
    bool is_union = false;
    // Enum that selects the active field. Empty on an `extern(abi="c")` union.
    std::string tag_name;
    // The struct synthesized for an inline variant. Its name is not a type the program can write.
    bool anonymous = false;
};

struct Enumerator {
    SourceLoc loc{};
    std::string name;
    bool has_value = false;
    int value = 0;
};

struct EnumDecl {
    SourceLoc loc{};
    std::string name;
    // Empty means the representation is int. The type is still the enum.
    std::string underlying;
    std::vector<Enumerator> enumerators;
    bool exported = false;
    bool is_extern = false;
    // `extern(abi="c")` is a C enum. Plain `extern` keeps the tlc enum rules.
    bool extern_c = false;
};

struct TypedefDecl {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    bool is_array = false;
    int array_len = 0;
    bool exported = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
};

struct VarDecl {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    bool is_array = false;
    int array_len = 0;
    // The value is a pointer to the array, as in `*[4]i32`.
    bool ptr_to_array = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
    std::unique_ptr<Expr> init;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
    // `extern(abi="c")` is a C symbol. Plain `extern` is a tlc library symbol.
    bool extern_c = false;
    // `name := expr` at file scope. The type comes from the initializer.
    bool inferred = false;
};

struct Stmt {
    struct Block {
        SourceLoc loc{};
        std::vector<std::unique_ptr<Stmt>> statements;
    };
    struct Return {
        SourceLoc loc{};
        std::unique_ptr<Expr> value;
        std::vector<std::unique_ptr<Expr>> extra;
    };
    struct Expression {
        SourceLoc loc{};
        std::unique_ptr<Expr> expr;
    };
    struct If {
        SourceLoc loc{};
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Stmt> then_arm;
        std::unique_ptr<Stmt> else_arm;
    };
    struct While {
        SourceLoc loc{};
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Stmt> body;
    };
    struct For {
        SourceLoc loc{};
        std::unique_ptr<Stmt> init;
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Expr> step;
        std::unique_ptr<Stmt> body;
    };
    struct DoWhile {
        SourceLoc loc{};
        std::unique_ptr<Stmt> body;
        std::unique_ptr<Expr> cond;
    };
    struct Switch {
        SourceLoc loc{};
        std::unique_ptr<Expr> cond;
        std::unique_ptr<Stmt> body;
    };
    struct Case {
        SourceLoc loc{};
        std::unique_ptr<Expr> value;
        std::unique_ptr<Stmt> stmt;
        bool is_default = false;
    };
    struct Break {
        SourceLoc loc{};
    };
    struct Continue {
        SourceLoc loc{};
    };
    struct Decl {
        SourceLoc loc{};
        std::vector<VarDecl> vars;
    };
    struct Func {
        std::unique_ptr<FunctionDecl> decl;
    };
    struct Defer {
        SourceLoc loc{};
        std::unique_ptr<Expr> call;
    };
    struct Bind {
        SourceLoc loc{};
        std::vector<std::string> names;
        std::vector<SourceLoc> name_locs;
        std::unique_ptr<Expr> value;
        bool is_var = false;
        bool is_define = false;
    };

    using Data = std::variant<Block, Return, Expression, If, While, For, DoWhile, Switch, Case, Break, Continue, Decl,
                              Func, Defer, Bind>;

    Data data;

    Stmt();
    ~Stmt();
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;
    Stmt(Stmt &&) noexcept;
    Stmt &operator=(Stmt &&) noexcept;
};

struct Param {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    bool is_array = false;
    int array_len = 0;
    bool is_const = false;
    bool is_receiver = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
};

struct ResultSlot {
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
};

struct FunctionDecl {
    SourceLoc loc{};
    std::string name;
    std::string return_type;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    std::vector<ResultSlot> results;
    std::vector<Param> params;
    std::unique_ptr<Stmt> body;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
    // `extern(abi="c")` uses the C ABI and the C symbol name.
    bool extern_c = false;
    bool variadic = false;
    std::string method_struct;
    // `(a, b) { ... }` takes its parameter and result types from the expected function pointer.
    bool inferred = false;
    bool elaborated = false;
};

struct TraitDecl {
    SourceLoc loc{};
    std::string name;
    std::vector<FunctionDecl> methods;
};

struct TopDecl {
    std::variant<FunctionDecl, VarDecl, StructDecl, EnumDecl, TypedefDecl, TraitDecl> data;
};

struct ImportDecl {
    enum class Form { Plain, Alias, Selective };
    SourceLoc loc{};
    Form form = Form::Plain;
    std::string path;
    std::string alias;
    std::vector<std::string> names;
};

struct PackageClause {
    SourceLoc loc{};
    std::string name;
};

struct TranslationUnit {
    std::optional<PackageClause> package;
    std::vector<ImportDecl> imports;
    std::vector<TopDecl> decls;
};

SourceLoc expr_loc(const Expr &expr);
std::string dump_expr(const Expr &expr);
std::string dump_translation_unit(const TranslationUnit &unit);

} // namespace tlc
