#pragma once

#include "ast.hpp"
#include "diag.hpp"

#include <string_view>
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
        std::vector<FieldDecl> fields;
        bool is_enum = false;
        bool enum_definition = false;
        std::vector<Enumerator> enumerators;
        bool is_typedef = false;
        bool exported = false;
        bool is_static = false;
        bool is_extern = false;
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
    };

    struct ArrayMark {
        bool is_array = false;
        int length = 0;
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
        bool is_array = false;
        int array_len = 0;
    };

    bool starts_declaration() const;
    bool starts_qualified_type() const;
    bool is_type_keyword(TokenKind kind) const;

    Specifiers parse_specifiers(bool file_scope, bool allow_const = false, bool type_only = false);
    std::vector<Enumerator> parse_enumerators();
    std::vector<FieldDecl> parse_fields();
    Declarator parse_declarator();
    ArrayMark parse_array_suffix(bool allow_unsized);
    PlacedType parse_placed_type(bool allow_const, bool allow_array);
    std::vector<Param> parse_params(bool allow_const, bool &variadic, bool method = false);
    void take_result(FunctionDecl &fn, const PlacedType &type);
    void parse_results(FunctionDecl &fn);
    FunctionDecl parse_function(bool exported, bool is_static, bool is_extern, bool allow_body);
    void parse_top_declaration(TranslationUnit &unit, bool file_scope_export_ok);
    std::unique_ptr<Stmt> parse_decl_stmt(bool file_scope);
    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Stmt> parse_block();
    std::unique_ptr<Stmt> parse_if();
    std::unique_ptr<Stmt> parse_while();
    std::unique_ptr<Stmt> parse_do();
    std::unique_ptr<Stmt> parse_for();
    std::unique_ptr<Stmt> parse_switch();
    std::unique_ptr<Stmt> parse_case();
    std::unique_ptr<Stmt> parse_return();
    std::unique_ptr<Stmt> parse_bind(bool is_var);

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
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_anon_function();
    bool starts_type_paren() const;
    struct ParenType {
        SourceLoc loc{};
        std::string name;
        std::string package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
    };
    ParenType parse_paren_type();

    void parse_import(TranslationUnit &unit);
    void parse_trait(TranslationUnit &unit);
    bool at_contextual(std::string_view spelling) const;
};

} // namespace tlc
