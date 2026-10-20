# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

[Inconsistencies](#inconsistencies) are accepted forms whose rules do not match. Each bullet there is one change. [Documentation](#documentation) is pages that already disagree with the compiler. Those do not change the language.

## Static functions

A function on a struct or enum with no `this` parameter is called as `Type.name(...)`. `export` publishes `pkg.Type.name(...)` and `pkg.Vec[i32].name(...)`. These forms are not supported yet:

- `const next` inside a struct. The constant is `const Entity::next = 1` or `const Box[T]::limit = 1`, written beside the type.
- `const Entity::next` inside a function. The constant is file scope.
- `extern` on `const Entity::next`.
- `export var Box[T]::extra`. `var` inside a struct is not a form. A published member is `export name type`. `export const Box[T]::limit` is accepted. Another package reads `pkg.Box[i32].limit`.
- `var Entity::next` inside a function. The variable is file scope.
- `Entity::next := 1`. The form is `var Entity::next i32 = 1` or `var Entity::next = 1`.

## Names

- A name may be declared again in the same block, as Rust does with `let`. `var n = 1` followed by `var n = n + 1` in that block is `redefinition`.

## Language server

`tlc lsp` publishes diagnostics and answers completion, hover, definition, and document symbols. These are not in place:

- Rename, formatting, code actions, and signature help.
- Incremental document sync. Each change replaces the whole buffer.
- Completion after a dot when the receiver is a call, an index, or a parenthesized expression. `{.field = ...}` completes the struct's fields. A name bound with `:=`, `var name = expression`, or `const name = expression` completes from that expression when it is a call, a name, a field, a cast, or a struct value `Name{}`.
- Semantic tokens from the server. Colors come from the tree-sitter grammar.

## `const if`

Compile-time branching was left out on purpose. `const if` is not a form yet.

## Generics and compile-time

- A generic used as a value: `var f = id`.
- `extern` on a generic, and a generic that is also `...`. A prototype followed by a body is accepted. A call of a generic that has no body is `not supported yet`. `[...U]` is a separate parameter and is accepted when it is the last type parameter, as in `[T type, ...U]`.
- A pack that is not the last type parameter, `[...U, T type]`.
- A pack on a struct or a union.
- An empty pack.
- A pack of several types used as `*U`, `[N]U`, a type argument, or a field.
- `typeof` inside a parenthesized result list. A bare result `typeof(expression)` is accepted.
- A tuple index that is not a constant, and assignment to a tuple element.
- `count(U)` and `const for`.
- A type-parameter list after the method name on a generic struct: `func Vec[T]::get[U type]`. `func Type::name[T type](self *this)` is a method template when the owner is not a generic struct. `func Vec[T]::get` names the struct's parameters.
- A method template that is static, exported, `extern`, or `...`, and a method template on a type alias: `func Integer::id[T type]`. They are `not supported yet`.
- Type parameters on an alias, an enum, a trait, or a union. Only `type Name[T type] struct` takes the list.
- A qualified trait constraint: `T pkg.Shape`.
- A type argument that is `sizeof(...)`.
- `sizeof(T)` when `T` is an array.
- `typeof(T)` when `T` is an array.
- `typeof` of a function, a tuple, or a trait.
- The C qualifier spelling `const i32 x` and `var x const i32`. Inside a function, `const name = expr` is an immutable binding, and `*const T` is a pointer to constant `T`.
- A file-scope `const` of a pointer, other than a string passed to C. `const label = "hi"` is `[]const u8`. `const c_text *const u8 = "unix"` is the C pointer. `const p *i32 = &n` is `type mismatch`. A string kept as an array is the same gap. A float, a struct, a function pointer, a typedef, and an array of a constant element are constants. `const bias = 0.5f` is not an array length. `const N = 4` remains the compile-time integer used as an array length. An alias is not an array length. An array constant is not an array length. `const row = [_]i32{n}` when `n` is a `var` is not a constant.
- A non-positive compile-time length, including `const N = 0` used as `[N]i32`.
### Inference

- A compile-time integer is written at the call: `repeat[4](7)`. It is not inferred from a value argument.
- A type is inferred from `T` and from `*T`. `Vec[T]` is inferred when the argument is already that specialization.
- There is no partial specialization and no SFINAE.

### Parsing

- `name[args](` is a generic call. A call of an indexed value is `(ops[i])()`.
- An array length may be a name, `[N]i32`. The name has to be a file-scope `const` already folded. A length that needs `sizeof` of a struct that is not complete yet does not fold.

### Constant evaluation

`assert`, `const`, and a compile-time integer share one evaluator. It folds integers, including a suffix whose value fits in `i32` such as `1u32`, names of those constants, `sizeof`, `alignof`, float arithmetic (`1.5f + 0.5f`), and `==` / `!=` of two `typeof` values. `var x = 1.5f + 0.5f`, `const x = 1.5f + 0.5f`, and `var x f32 = 1u32` are constant initializers.

An `assert` whose condition is not one of those constants is `not supported yet`. A call is not a constant. `const x f32 = n` when `n` is a `var` is `global initializer is not a constant`. A suffix whose value does not fit in `i32`, such as `3000000000u32`, is `not supported yet`.

## Types and operators

- `?` on a type that is not a pointer: `?i32`.
- A slice field on `extern(abi="c")`. It is `not supported yet`. A tlc union arm that is not a struct stays `type mismatch`.
- A file-scope slice whose initializer is not the name of an array, a slice whose own initializer is one of these constants, a string, a constant range of one of those, or a call. `var mid = numbers[1:3]`, `var all = numbers[:]`, `const v []i32 = numbers[0:2]`, and `var rows = [_][]i32{a[:], b[:]}` store that header. A bound that does not fold is `global initializer is not a constant`. An array literal is `global initializer is not a constant`. A call used as the whole initializer runs before `main`. A range, a literal, or a call used as the initializer of a `const` slice is `global initializer is not a constant` when it is not one of those constant ranges.
- A cast to `void`.
- `++`, `--`, compound assignment (`+=` and the rest), `?:`, and `->`.
- `goto`.

## Declarations

- `var a, b =` at file scope. Inside a function, several names unpack one tuple.
- `extern` on an inferred variable, `var name = expr` or `name := expr`. `export var name = expr` and `export name := expr` are accepted. Another package uses `pkg.name` or `import { name }`.
- A local `type`, including a local struct or enum.
- A file-scope array whose written length is `[0]i32`. An element may be an integer, `bool`, a float, a pointer, a struct, an enum, a typedef, a function pointer, an empty `[_]T{}`, a nested array `[_][2][3]i32`, a pointer to an array `[_]*[4]i32`, or an array value `[_][2]i32{inner}`.
- A file-scope array element that is an expression such as `n + 1` when `n` is a `var`, or `give() + 1`. That is `global initializer is not a constant`. A call element runs before `main`: `var row = [_]i32{give(), 2}`, `var row [2]i32 = {give(), 2}`, `[_]Point{give()}`, and `var row = [_][2]i32{give()}`. The same call in a `const`, and `var n = give(); var row = [_]i32{n}`, are `global initializer is not a constant`. `var row = [_]i32{1u32}` folds. `var row = [_]i32{n}` copies the initializer of `n` when that initializer is a constant. `const row = [_]i32{n}` when `n` is a `var` is `global initializer is not a constant`.
- Another abi than `"c"` and `"tlc"`.
- `export type Name trait`.

## Functions

- An overloaded name used as a value, `var f = show` when `show` has several parameter lists, is `not supported yet`. A call selects the list.
- A `test` inside a function. `test` is file scope.
- A struct argument to `testing.error`.
- `builtin.va` outside a variadic function.
- Calling `builtin.panic` on a fatal failure other than an allocation that returned `null`. `new` calls `builtin.panic("out of memory")` in that case. A non-positive element count and a size that does not fit abort without that call.
- A function pointer whose result is itself a function pointer: `func (i32) func (i32) i32`.
- A variadic `extern` or `extern(abi="tlc")`, and a variadic prototype with no body. `extern(abi="c")` may declare `...`. A function with a body may use `...`, and `builtin.va()` reads the extra arguments.
- An inner function that uses a local or a parameter from an enclosing function, other than a function pointer it returns or calls. The inner function is called directly. `var f = pass` and passing that function as a value are the same gap. A prototype written before the body is the same gap when the body reads that pointer. It can call functions already in scope and use globals. `export`, `extern`, and a method on a function in a block are the same gap. A short anonymous function follows that function. A full anonymous function called directly can read the function pointer.

## Structs, enums, and unions

- An empty enum.
- An empty union.
- A union that is not `extern(abi="c")` and does not name an enum, including `extern union`.
- `extern` or `extern(abi="c")` on `union(Enum)`.
- A qualified enum name in `union(Enum)`, such as `union(pkg.Kind)`.
- A by-value field of a struct that is still incomplete, including the struct being defined. A pointer to that struct is fine.

## Methods and traits

- `extern` on a method. A method may be exported, including `export func Vec[T]::get`. Another package calls `value.name(...)`. `export` on a static function publishes `pkg.Type.name(...)`. A method without `export` is reported as `name is not exported`.
- A receiver with more than one star: `self **this`.
- A body on a trait method. The trait lists the method and its type. A default is `func Trait::name` outside the braces.
- A trait used as a variable, a return type, an array, a type alias, a trait field, or `**Shape`. A parameter is `Shape` or `*Shape`.
- `export`, `extern`, or `...` on a function that takes a trait parameter.
- A named function as the argument of a single-method trait. The argument is an anonymous function.
- `(*a).apply(n)` when `a` is a trait pointer passed as an anonymous function. The call is `a.apply(n)`.
- A value on a trait field, such as `w i32 = 1`. It is `not supported yet`.

## Inconsistencies

Accepted forms whose rules do not match. Each bullet is one change. The first sentence is what `tlc --check` does. Closing a bullet updates the pages it names. A gap that is already listed above stays there. This section records the mismatch.

### Aggregates

- `new [n]Point{.x = 1}` is parsed as a field list and is `type mismatch`. The element list is `new [n]Point{{.x = 1}...}`. Pages: `docs/statements/new.md`, `docs/grammar.md`.

### Conditions

- `if` accepts an integer after promotion, including `i64` and `u32`, and a `bool`. A pointer is `compare with null`. An `f32` is `type mismatch`. `switch` accepts a value that promotes to `i32`, so `bool`, `i8`, and `i16` match. `u32` and `i64` do not. `case 1u32` is `type mismatch`. Pages: `docs/expressions/condition.md`, `docs/statements/switch.md`.
- A tlc enum is neither a condition nor a `switch` value. An `extern(abi="c")` enum converts to an integer and can be either. Pages: `docs/types/enum.md`, `docs/expressions/condition.md`, `docs/statements/switch.md`.
- Prefix `!` is logical not and yields `i32`. Postfix `!` strips `?` from a pointer and inserts no null check. `if (p != null)` narrows the name for the rest of the block. `p!` applies to that use. `opt![2]` parses as `(opt!)[2]` and typechecks. Pages: `docs/expressions/narrowing.md`, `docs/types/pointer.md`, `docs/grammar.md`. The sentence in `docs/types/pointer.md` that parses `opt![i]` as `(opt[i])!` is under [Documentation](#documentation).

### Spellings

- `sizeof(union ...)` and `sizeof(trait ...)` do not parse. Pages: `docs/grammar.md`, `docs/types/sizeof.md`.
- `[` is type parameters, type arguments, an array length, an index, a range, a `new` count, and `_`. `name[i32](` is a generic call, already listed under Parsing. A call of an indexed value is `(ops[i])()`. `Vec[i32]{}` is a struct value. `Buf[4, i32].count` is the constant because of the comma. `arr[i].field` is an index. Pages: `docs/grammar.md`.

### Documentation

Pages that disagree with the compiler. Fixing one does not change the language.

- `docs/types/literal.md` lists `"hi"` and a raw string as `*const u8`. Both are `[]const u8`. `docs/types/string.md` and `docs/grammar.md` already say that.
- `docs/types/pointer.md` says `opt![i]` is `(opt[i])!` and `type mismatch`. Postfix operators bind left to right, so `opt![2]` is `(opt!)[2]` and typechecks. `(opt!)[2]` is the same expression.
- `docs/grammar.md` prose accepts forms the EBNF does not generate. `new i32` and `new [n]u8` use an `integer-name`, and `new-expr` only names an identifier. `[N][M]T`, `[][N]T`, and `[][][]T` are another array or slice around the inner type, and the `go-type` alternatives do not generate them. `?*i32(p)` and a cast that writes `const` are casts, and the `cast` rule is `{ "*" }` plus a scalar or an identifier. `Vec[*const u8]` is a type argument, and the `type-arg` pointer alternative has no `const`.
