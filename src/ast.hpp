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
        std::unique_ptr<Expr> expr;
    };
    struct Cast {
        SourceLoc loc{};
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
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
    struct Unsupported {
        SourceLoc loc{};
    };

    using Data = std::variant<Name, Int, Unary, Binary, Assign, Call, Selector, String, Index, Sizeof, Cast, Bool,
                              Nullptr, Char, Float, Func, Unsupported>;

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
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    std::vector<FnParam> params;
    bool variadic = false;
};

struct FieldDecl {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    bool is_array = false;
    int array_len = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
};

struct StructDecl {
    SourceLoc loc{};
    std::string name;
    std::vector<FieldDecl> fields;
    bool exported = false;
    bool forward = false;
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
    std::vector<Enumerator> enumerators;
    bool exported = false;
};

struct TypedefDecl {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    bool is_array = false;
    int array_len = 0;
    bool exported = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
};

struct VarDecl {
    SourceLoc loc{};
    std::string name;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    bool is_array = false;
    int array_len = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::unique_ptr<Expr> init;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
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
    bool is_array = false;
    int array_len = 0;
    bool is_const = false;
    bool is_receiver = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
};

struct ResultSlot {
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
};

struct FunctionDecl {
    SourceLoc loc{};
    std::string name;
    std::string return_type;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    std::vector<ResultSlot> results;
    std::vector<Param> params;
    std::unique_ptr<Stmt> body;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
    bool variadic = false;
    std::string method_struct;
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
