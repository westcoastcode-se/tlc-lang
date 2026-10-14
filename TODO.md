# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

## Static functions

A function on a struct or enum with no `this` parameter is called as `Type.name(...)`. These forms are not supported yet:

- `pkg.Type.name(...)`.
- A static function on a generic struct in another package, `pkg.Vec[i32].name(...)`. The same package calls `Vec[i32].name(...)` and `Buf[4, i32].name()`.
- A call through a type alias that is not a primitive, such as `type Ptr *Point` or `type Spot Point`. `type Integer i32` can use `Integer.name(...)`.
- `const next` inside a struct. The constant is `const Entity::next = 1` or `const Box[T]::limit = 1`, written beside the type.
- `const Entity::next` inside a function. The constant is file scope.
- `extern` on `const Entity::next`.
- `export var Box[T]::extra`. `var` inside a struct is not a form. A published member is `export name type`. `export const Box[T]::limit` is accepted. Another package reads `pkg.Box[i32].limit`.
- `var Entity::next` inside a function. The variable is file scope.
- `Entity::next := 1`. The form is `var Entity::next i32 = 1` or `var Entity::next = 1`.

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
- `extern` on a generic, a generic with no body, and a generic that is also `...`. `[...U]` is a separate parameter and is accepted when it is the last type parameter, as in `[T type, ...U]`.
- A pack that is not the last type parameter, `[...U, T type]`.
- A pack on a struct or a union.
- An empty pack.
- A pack of several types used as `*U`, `[N]U`, a type argument, or a field.
- A tuple element that is itself a tuple.
- `typeof` inside a parenthesized result list. A bare result `typeof(expression)` is accepted.
- A tuple index that is not a constant, and assignment to a tuple element.
- `count(U)` and `const for`.
- A type-parameter list after the method name: `func Vec[T]::get[U type]`. `func Vec[T]::get` names the struct's parameters.
- Type parameters on an alias, an enum, a trait, or a union. Only `type Name[T type] struct` takes the list.
- A qualified type application: `pkg.Vec[i32]`.
- A qualified trait constraint: `T pkg.Shape`.
- A type argument that is `sizeof(...)`.
- An array type parameter under a pointer, or a pointer to an array, as in `*T` when `T` is `[4]i32`.
- `sizeof(T)` when `T` is an array.
- `typeof(T)` when `T` is an array.
- `typeof` of a function, a tuple, or a trait.
- The C qualifier spelling `const i32 x` and `var x const i32`. Inside a function, `const name = expr` is an immutable binding, and `*const T` is a pointer to constant `T`.
- A file-scope `const` of a pointer, other than a string literal. `const label *const u8 = "unix"` and `const label = "hi"` are `*const u8`. `const p *i32 = &n` is `type mismatch`. A string kept as an array is the same gap. A float, a struct, a function pointer, a typedef, and an array of a constant element are constants. `const bias = 0.5f` is not an array length. `const N = 4` remains the compile-time integer used as an array length. An alias is not an array length. An array constant is not an array length. `const row = [_]i32{n}` when `n` is a `var` is not a constant.
- A non-positive compile-time length, including `const N = 0` used as `[N]i32`.
- Returning an array from anything other than a local array name. `return out` works for `[N]T`. Another return expression does not.

### Inference

- A compile-time integer is written at the call: `repeat[4](7)`. It is not inferred from a value argument.
- A type is inferred from `T` and from `*T`. `Vec[T]` is inferred when the argument is already that specialization.
- There is no partial specialization and no SFINAE.

### Parsing

- `name[args](` is a generic call. A call of an indexed value is `(ops[i])()`.
- `Vec[i32]{}` and `pkg.Vec[i32]{}`. `Point{}` is a struct value. A type application before `{}` is not.
- An array length may be a name, `[N]i32`. The name has to be a file-scope `const` already folded. A length that needs `sizeof` of a struct that is not complete yet does not fold.

### Constant evaluation

`assert`, `const`, and a compile-time integer parameter share `fold_int`. That folds integers, names of those constants, and `sizeof`. It does not fold `typeof` or a comparison of two `Type` values, so `assert(typeof(i32) == typeof(i64))` is `not supported yet`. A general constant evaluator is not in place. An `assert` whose condition is not a constant integer is the same gap. Float arithmetic in a file-scope initializer, such as `var x = 1.5f + 0.5f` or `const x = 1.5f + 0.5f`, is `global initializer is not a constant`. `var x f32 = 1u32` is `not supported yet`. `const x f32 = n` when `n` is a `var` is `global initializer is not a constant`.

## Types and operators

- `?` on a type that is not a pointer: `?i32`.
- An optional pointer to an array: `?*[4]i32`.
- Two stars outside an array, `**[4]i32`, and a nested array.
- A pointer to an array as a field. `var p *[4]i32` is fine. `p *[4]i32` inside a struct is not.
- A pointer to a slice, `*[]T`. It is `not supported yet`.
- A nested slice, `[][]T` or `[N][]T`. It is `not supported yet`.
- A slice taken from an array parameter. The parameter is already a pointer, so `return row` from `func open(row [4]i32) []i32` is `return type: expected []i32, found *i32`. A range on that pointer, `row[0:2]`, is `type mismatch`.
- Omitted range bounds. `a[i:]`, `a[:j]`, and `a[:]` are not forms. The parser reports `expected an expression`.
- A range whose element is an array. `grid[0:1]` on `[_][2]i32` is `not supported yet`.
- A slice field on `extern(abi="c")`. It is `not supported yet`. A tlc union arm that is not a struct stays `type mismatch`.
- A file-scope slice whose initializer is not the name of an array or another slice. A literal, a call, or `a[i:j]` is `global initializer is not a constant`.
- `const` on a slice. `const v []i32 = row` is `not supported yet`.
- A cast to `void`.
- `++`, `--`, compound assignment (`+=` and the rest), `?:`, and `->`.
- `goto`.

## Declarations

- `var a, b =` at file scope. Inside a function, several names unpack one tuple.
- `extern` on an inferred variable, `var name = expr` or `name := expr`. `export var name = expr` and `export name := expr` are accepted. Another package uses `pkg.name` or `import { name }`.
- A local `type`, including a local struct or enum.
- A file-scope array whose element is a pointer to an array, `[_]*[4]i32`. An element may be an integer, `bool`, a float, a pointer, a struct, an enum, a typedef, a function pointer, an empty `[_]T{}`, or one nested level written as `[_][2]i32{{1, 2}}`. A written nested type `[2][3]i32` and a written length `[0]i32` stay `not supported yet`. An element that is an array name, `[_][2]i32{inner}`, is `type mismatch`.
- A file-scope array element that is a call or an expression such as `n + 1`. That is `global initializer is not a constant`. An integer suffix that does not fold, such as `1u32`, is `not supported yet`. `var row = [_]i32{n}` copies the initializer of `n`. `const row = [_]i32{n}` when `n` is a `var` is `global initializer is not a constant`.
- Another abi than `"c"` and `"tlc"`.
- `export type Name trait`.

## Functions

- A `test` inside a function. `test` is file scope.
- A struct argument to `testing.error`.
- `builtin.va` outside a variadic function.
- Calling `builtin.panic` when memory runs out, or on another fatal failure. `builtin.panic(message)` prints the message and the call stack, then aborts.
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

- `export` on a method. A method is visible in its package and is not exported.
- A receiver with more than one star: `self **this`.
- A body on a trait method. The trait lists the method and its type.
- A trait method that returns several results.
- A trait used as a variable, a return type, an array, a type alias, a trait field, or `**Shape`. A parameter is `Shape` or `*Shape`.
- `export`, `extern`, or `...` on a function that takes a trait parameter.
- A named function as the argument of a single-method trait. The argument is an anonymous function.
- `(*a).apply(n)` when `a` is a trait pointer passed as an anonymous function. The call is `a.apply(n)`.
