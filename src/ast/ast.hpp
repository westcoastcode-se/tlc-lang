#pragma once

#include "lex/token.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tlc {

struct FunctionDecl;

// One constructor outside a leaf. `shells.front()` is the outermost.
// A slice is `{ ptr, i32 }`. An array is N of the inner type. A pointer points at the inner type.
struct TypeShell {
    enum class Kind : uint8_t { Pointer, Slice, Array };
    Kind kind = Kind::Pointer;
    bool optional = false;
    bool const_pointee = false;
    // Array length. Zero with `length_name` set is not folded yet. A slice leaves this at 0.
    int length = 0;
    std::string length_name;
};

// One slot of `func id[T type, N int]`. `T type` is a type. `T Shape` is a trait.
// `N int` is a compile-time integer. `[...U]` is a type pack and is the last parameter.
struct TypeParam {
    SourceLoc loc{};
    std::string name;
    bool is_type = false;
    bool is_value = false;
    bool is_trait = false;
    bool is_pack = false;
    std::string constraint;
    std::string constraint_package;
};

// One argument of `id[int](1)` or `Vec[int]`. A value argument is a folded integer or a constant name.
struct TypeArg {
    SourceLoc loc{};
    bool is_value = false;
    bool has_value = false;
    int value = 0;
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    int array_len = 0;
    std::string array_len_name;
    // Constructors outside the leaf, outermost first. Empty when the argument is only a name and pointers.
    std::vector<TypeShell> shells;
    std::vector<TypeArg> args;
};

// A parameter of a function-pointer type. Nested pointers use the same shape.
struct FnParam {
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    // `[]T` on a parameter or on one result of several. A sized array is not carried here.
    bool is_array = false;
    int array_len = 0;
    // `[][]T`. The element of this slice is one slice.
    bool elem_slice = false;
    // Constructors outside the leaf, outermost first.
    std::vector<TypeShell> shells;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
    std::vector<FnParam> params;
    bool variadic = false;
    // Set when this function pointer returns more than one result.
    std::vector<FnParam> results;
    // `Box[i32]` on this parameter, or on the result when `is_fn_ptr` is set.
    std::vector<TypeArg> type_args;
};

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
        // `pointer!`. Prefix `!` stays the integer operator.
        bool postfix = false;
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
        std::vector<TypeArg> type_args;
        std::vector<std::unique_ptr<Expr>> args;
        // Set while checking when the callee is `Type.name()`.
        mutable bool type_name = false;
        // The parameter list selected for this call. Null when the name has one list.
        mutable const FunctionDecl *selected = nullptr;
        // `a.malloc[i32]()` through a trait. The concrete struct's method has this name.
        mutable std::string trait_method;
    };
    struct Selector {
        SourceLoc loc{};
        std::unique_ptr<Expr> base;
        std::string field;
        // `Buf[4, i32].count`. A single argument stays an index, `Box[i32].extra`.
        std::vector<TypeArg> type_args;
        // Concrete owner after checking: `Entity` or `Box.spec.i32`.
        mutable std::string resolved_owner;
        // File that defines the method. Zero when the call is in that method's package.
        mutable uint32_t resolved_file = 0;
        // `name.len` on a slice or a sized array. The length is `i32` and is not a field.
        mutable bool slice_len = false;
        // A sized array's `.len`. Negative when the length is loaded from a slice.
        mutable int len_value = -1;
    };
    struct String {
        SourceLoc loc{};
        std::string text;
    };
    struct Index {
        SourceLoc loc{};
        std::unique_ptr<Expr> base;
        std::unique_ptr<Expr> index;
        // High bound of `base[index:end]`. Null when this reads one element, or when that bound is omitted.
        std::unique_ptr<Expr> end;
        // `base[index:end]`, including an omitted bound. An omitted low bound is 0. An omitted high bound is the length.
        bool is_range = false;
        // Set while checking a tuple element. Negative when this is not that index.
        mutable int tuple_index = -1;
        // The base is a slice. The element is readonly.
        mutable bool slice_element = false;
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
        // Bit 0 is the outermost `*`. A set bit means that pointer's pointee is `const`.
        uint32_t const_pointee = 0;
        bool is_array = false;
        // `[]const u8`. The element type is const.
        bool elem_const = false;
        // `[][]T` and `[N][]T`. The element is one slice.
        bool elem_slice = false;
        // Constructors outside the leaf, outermost first.
        std::vector<TypeShell> shells;
        int array_len = 0;
        std::unique_ptr<Expr> expr;
        // `alignof`. The operand is the same as `sizeof`. The value is the alignment in bytes.
        bool align = false;
    };
    struct Typeof {
        SourceLoc loc{};
        bool on_type = false;
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        uint32_t optional = 0;
        uint32_t const_pointee = 0;
        bool is_array = false;
        // `[]const u8`. The element type is const.
        bool elem_const = false;
        // `[][]T` and `[N][]T`. The element is one slice.
        bool elem_slice = false;
        std::vector<TypeShell> shells;
        int array_len = 0;
        std::unique_ptr<Expr> expr;
        // Filled while checking. Codegen interns this spelling, so equal types share one value.
        mutable std::string canonical;
    };
    struct Cast {
        SourceLoc loc{};
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        uint32_t optional = 0;
        uint32_t const_pointee = 0;
        // `func (i32) i32(value)`. `type_name` is the result. A named cast leaves this unset.
        bool is_fn_ptr = false;
        int ret_indirections = 0;
        uint32_t ret_optional = 0;
        uint32_t ret_const_pointee = 0;
        std::vector<FnParam> fn_params;
        bool fn_variadic = false;
        std::vector<FnParam> fn_results;
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
    // `{.field = value}` names struct fields. `{value, value}` fills an array.
    // `(value, value)` is a tuple. `Point{}`, `pkg.Point{}`, and `Vec[i32]{}` name the struct.
    struct Init {
        SourceLoc loc{};
        std::vector<FieldInit> fields;
        std::vector<std::unique_ptr<Expr>> elements;
        // `(value, value, ...)`. A brace list is not a tuple.
        bool is_tuple = false;
        // `{1, 2, 0...}` stores 0 in every element the list does not name.
        bool zero_rest = false;
        // Filled when the destination struct is known.
        int struct_id = -1;
        // `struct{.value = 10}`. The field types come from the values.
        bool inferred = false;
        // `Point{}`. Empty when the braces take their type from the destination.
        // For `[_]i32{1, 2}` this is the element type.
        std::string type_name;
        // `pkg` in `pkg.Point{}`. Empty for a name in this package.
        std::string type_package;
        // `[_]i32{1, 2, 3}`. The length is the number of elements.
        bool array_literal = false;
        // Element spelling of an array literal, besides `type_name` and `type_package`.
        bool elem_is_array = false;
        bool elem_ptr_to_array = false;
        bool elem_fn = false;
        // `[_][2]i32{...}`. The element is an array of this length.
        // Negative means the element is a slice, as in `[_][]i32{...}`.
        int elem_array_len = 0;
        // `[_][][]T`. The element slice is itself a slice. That form stays rejected.
        bool elem_slice = false;
        // Constructors of the element, outermost first. The inferred `[N]` is not one of them.
        std::vector<TypeShell> elem_shells;
        // `[_][]const u8`. The leaf is const. A sized row does not use this flag.
        bool elem_const = false;
        std::string elem_array_len_name;
        // `[_]func (i32) i32{add}`. The element is a function pointer.
        int elem_ret_indirections = 0;
        uint32_t elem_ret_optional = 0;
        uint32_t elem_ret_const_pointee = 0;
        std::vector<FnParam> elem_fn_params;
        bool elem_fn_variadic = false;
        std::vector<FnParam> elem_fn_results;
        bool is_struct = false;
        bool is_enum = false;
        int indirections = 0;
        uint32_t optional = 0;
        uint32_t const_pointee = 0;
        // `Vec[i32]{}`, or the element of `[_]Box[i32]{...}`.
        std::vector<TypeArg> type_args;
    };
    struct Unsupported {
        SourceLoc loc{};
    };
    // `new T` and `new(allocator) T`. The result is `*T`.
    // `new T{.field = expr}` writes those fields after the block is reserved.
    struct New {
        SourceLoc loc{};
        // Null uses `builtin.allocator`.
        std::unique_ptr<Expr> allocator;
        std::string type_name;
        std::string type_package;
        bool is_struct = false;
        int indirections = 0;
        uint32_t optional = 0;
        uint32_t const_pointee = 0;
        // `new Box[i32]`. Cleared once the name is the specialization.
        std::vector<TypeArg> type_args;
        // `new [count]T`. The result is `*T`, a pointer to the first element.
        std::unique_ptr<Expr> count;
        // `new [_]T{...}`. The length is the number of element values. The result is `*[N]T`.
        bool count_inferred = false;
        // `new [n]T{value...}`. That one value is stored in every element.
        bool repeat_init = false;
        // `{.field = expr}`. Null when the block is only zeroed.
        std::unique_ptr<Expr> init;
        // Filled while checking. `builtin_alloc` calls libc. Otherwise `alloc_struct` is the allocator.
        mutable bool builtin_alloc = false;
        // File and struct name of a custom allocator. Empty when `builtin_alloc` is set.
        mutable uint32_t alloc_file = 0;
        mutable std::string alloc_name;
        // The allocator expression is a pointer. A struct value is rejected.
        mutable bool alloc_is_pointer = false;
    };
    // `delete p` and `delete(allocator) p`.
    struct Delete {
        SourceLoc loc{};
        std::unique_ptr<Expr> allocator;
        std::unique_ptr<Expr> expr;
        mutable bool builtin_alloc = false;
        mutable uint32_t alloc_file = 0;
        mutable std::string alloc_name;
        mutable bool alloc_is_pointer = false;
    };

    using Data = std::variant<Name, Int, Unary, Binary, Assign, Call, Selector, String, Index, Sizeof, Typeof, Cast, Bool,
                              Nullptr, Char, Float, Func, Init, New, Delete, Unsupported>;

    Data data;

    Expr();
    ~Expr();
    Expr(const Expr &) = delete;
    Expr &operator=(const Expr &) = delete;
    Expr(Expr &&) noexcept;
    Expr &operator=(Expr &&) noexcept;
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
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    // `[][]T` and `[N][]T`. The element is one slice.
    bool elem_slice = false;
    std::vector<TypeShell> shells;
    int array_len = 0;
    // `[N]int` when N is a name. Folded into `array_len` before code generation.
    std::string array_len_name;
    std::vector<TypeArg> type_args;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
    // `export name` on a struct member. Another package can read and assign it.
    bool exported = false;
    // `name type = expr`. The value of a field that a struct value or `new` does not name.
    std::unique_ptr<Expr> init;
    // Fields of `Name struct { ... }` inside a tlc union. Empty when the variant names a declared struct.
    std::vector<FieldDecl> inline_fields;

    FieldDecl() = default;
    FieldDecl(const FieldDecl &other);
    FieldDecl &operator=(const FieldDecl &other);
    FieldDecl(FieldDecl &&) noexcept = default;
    FieldDecl &operator=(FieldDecl &&) noexcept = default;
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
    // `union(pkg.Kind)` stores the qualifier in `tag_package`. A bare name leaves it empty.
    std::string tag_name;
    std::string tag_package;
    std::vector<TypeParam> type_params;
    // The struct synthesized for an inline variant. Its name is not a type the program can write.
    bool anonymous = false;
    // Inline payload of a tlc union. A field value there is not a union form.
    bool union_payload = false;
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
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    // `[][]T` and `[N][]T`. The element is one slice.
    bool elem_slice = false;
    std::vector<TypeShell> shells;
    int array_len = 0;
    std::string array_len_name;
    std::vector<TypeArg> type_args;
    bool exported = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
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
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    // `[][]T` and `[N][]T`. The element is one slice.
    bool elem_slice = false;
    std::vector<TypeShell> shells;
    int array_len = 0;
    std::string array_len_name;
    std::vector<TypeArg> type_args;
    // The value is a pointer to the array, as in `*[4]i32`.
    bool ptr_to_array = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
    std::unique_ptr<Expr> init;
    bool exported = false;
    bool is_static = false;
    bool is_extern = false;
    // `extern(abi="c")` is a C symbol. Plain `extern` is a tlc library symbol.
    bool extern_c = false;
    // `extern(..., name="symbol")`. Empty keeps the usual object name.
    std::string link_name;
    // `name := expr` at file scope. The type comes from the initializer.
    bool inferred = false;
    // File-scope `const name = expr` or `const name Type = expr`.
    // A plain integer is folded and has no address. A struct, a function pointer, or a typedef keeps that type.
    bool is_const_decl = false;
    bool has_const = false;
    int const_value = 0;
    // File-scope `var` whose initializer is a call. The call runs before `main`.
    bool runtime_init = false;
    // `var Entity::next`. Empty for an ordinary variable. The variable is not a field.
    std::string owner;
    // Names in `var Box[T]::extra` or `var Buf[N, T]::count`. Empty on `var Entity::next`.
    std::vector<std::string> owner_params;
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
        // `const name = expr`. The name is immutable.
        bool is_const = false;
    };
    struct Assert {
        SourceLoc loc{};
        std::unique_ptr<Expr> cond;
        std::string message;
    };

    using Data = std::variant<Block, Return, Expression, If, While, For, DoWhile, Switch, Case, Break, Continue, Decl,
                              Func, Defer, Bind, Assert>;

    Data data;

    Stmt();
    ~Stmt();
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;
    Stmt(Stmt &&) noexcept;
    Stmt &operator=(Stmt &&) noexcept;
};

struct ResultSlot {
    std::string type_name;
    std::string type_package;
    bool is_struct = false;
    bool is_enum = false;
    int indirections = 0;
    uint32_t optional = 0;
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    // `[][]T` and `[N][]T`. The element is one slice.
    bool elem_slice = false;
    std::vector<TypeShell> shells;
    int array_len = 0;
    std::string array_len_name;
    std::vector<TypeArg> type_args;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
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
    uint32_t const_pointee = 0;
    bool is_array = false;
    // `[]const u8`. The element type is const.
    bool elem_const = false;
    // `[][]T` and `[N][]T`. The element is one slice.
    bool elem_slice = false;
    std::vector<TypeShell> shells;
    int array_len = 0;
    std::string array_len_name;
    std::vector<TypeArg> type_args;
    bool is_const = false;
    bool is_receiver = false;
    bool is_fn_ptr = false;
    int ret_indirections = 0;
    uint32_t ret_optional = 0;
    uint32_t ret_const_pointee = 0;
    std::vector<FnParam> fn_params;
    bool fn_variadic = false;
    std::vector<FnParam> fn_results;
    // A pack of several types. `tuple_parts` are the element types, in order.
    bool is_tuple = false;
    std::vector<ResultSlot> tuple_parts;
};

// A function pointer read from an enclosing function. `function_id` is the function that owns the name.
struct FnCapture {
    std::string name;
    int function_id = 0;
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
    uint32_t const_pointee = 0;
    std::vector<ResultSlot> results;
    std::vector<Param> params;
    std::unique_ptr<Stmt> body;
    bool exported = false;
    // The body is an unsafe context. A call must be written `unsafe name(...)`.
    bool unsafe = false;
    bool is_static = false;
    bool is_extern = false;
    // `extern(abi="c")` uses the C ABI. The object name is `link_name` when that is set.
    bool extern_c = false;
    // `extern(..., name="symbol")`. Empty keeps the C name, or `qualifier.name` for tlc.
    std::string link_name;
    bool variadic = false;
    std::string method_struct;
    // A function on the struct with no `this` parameter. Called as `Type.name(...)`.
    bool static_method = false;
    // Names written in `func Vec[T]::get`. Empty for `func Point::sum`.
    std::vector<std::string> method_params;
    std::vector<TypeParam> type_params;
    // Parallel to `results`. A non-null entry is `typeof(expr)` and is resolved while checking.
    std::vector<std::unique_ptr<Expr>> result_typeof;
    // `(a, b) { ... }` takes its parameter and result types from the expected function pointer.
    bool inferred = false;
    bool elaborated = false;
    // Scope id of this function while it is checked. Zero before that.
    int function_id = 0;
    // Function pointers read from an enclosing function, in first-use order.
    // A direct call passes them after the written arguments. The function's type does not list them.
    std::vector<FnCapture> captures;
    // Names this function owns that an inner function reads.
    std::vector<std::string> captured_here;
    // Specialized method of the iteration trait. 1 is length, 2 is the element, 3 is the address.
    // Zero on a declaration the program wrote.
    int iteration_kind = 0;
};

struct TraitDecl {
    SourceLoc loc{};
    std::string name;
    // `export` publishes the template. Another package names `pkg.Seq[i32]`.
    bool exported = false;
    // `extern` or `extern(abi="tlc")`. The trait has no symbol. `extern(abi="c")` is rejected.
    bool is_extern = false;
    // `type Seq[T type] trait`. Empty on `type Shape trait`. One `name type` parameter.
    std::vector<TypeParam> type_params;
    // `type Shape trait(Parent)`. Empty when the trait has no parent.
    std::string parent_name;
    std::string parent_package;
    SourceLoc parent_loc{};
    // A field the struct must have. It is not stored on the trait.
    std::vector<FieldDecl> fields;
    std::vector<FunctionDecl> methods;
};

// `test "name" { ... }` at file scope. The body returns `int`. Zero passes.
struct TestDecl {
    SourceLoc loc{};
    std::string name;
    std::unique_ptr<Stmt> body;
};

// `assert(condition, "message");` at file scope. The condition is checked while compiling.
struct AssertDecl {
    SourceLoc loc{};
    std::unique_ptr<Expr> cond;
    std::string message;
};

struct TopDecl {
    std::variant<FunctionDecl, VarDecl, StructDecl, EnumDecl, TypedefDecl, TraitDecl, AssertDecl, TestDecl> data;
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
    // A specialization is appended after sema has taken pointers into this list.
    // The move swaps that block map, so those pointers stay valid. It is noexcept
    // because a throwing move makes vector copy the unit, and TopDecl is not copyable.
    std::deque<TopDecl> decls;

    TranslationUnit() = default;
    TranslationUnit(const TranslationUnit &) = delete;
    TranslationUnit &operator=(const TranslationUnit &) = delete;
    TranslationUnit(TranslationUnit &&other) noexcept
        : package(std::move(other.package)), imports(std::move(other.imports)), decls(std::move(other.decls)) {}
    TranslationUnit &operator=(TranslationUnit &&other) noexcept {
        package = std::move(other.package);
        imports = std::move(other.imports);
        decls = std::move(other.decls);
        return *this;
    }
};

std::unique_ptr<Expr> clone_expr(const Expr &expr);
std::unique_ptr<Stmt> clone_stmt(const Stmt &stmt);
VarDecl clone_var(const VarDecl &var);
FunctionDecl clone_function(const FunctionDecl &fn);
StructDecl clone_struct(const StructDecl &item);

SourceLoc expr_loc(const Expr &expr);
std::string dump_expr(const Expr &expr);
std::string dump_translation_unit(const TranslationUnit &unit);

} // namespace tlc
