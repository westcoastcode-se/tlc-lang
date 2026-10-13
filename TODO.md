# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

## Static functions

A function on a struct or enum with no `this` parameter is called as `Type.name(...)`. These forms are not supported yet:

- `pkg.Type.name(...)`.
- A call through a type alias, `Alias.name(...)`.
- A static function on a generic struct, `Vec[int].name(...)`.

## Language server

`tlc lsp` publishes diagnostics and answers completion, hover, definition, and document symbols. These are not in place:

- Rename, formatting, code actions, and signature help.
- Incremental document sync. Each change replaces the whole buffer.
- Completion after a dot when the receiver is a call, an index, or a parenthesized expression. `{.field = ...}` completes the struct's fields. A name bound with `:=`, `var name = expression`, or `const name = expression` completes from that expression when it is a call, a name, a field, or a cast.
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
- A qualified type application: `pkg.Vec[int]`.
- A qualified trait constraint: `T pkg.Shape`.
- A type argument that is `sizeof(...)`.
- An array type parameter under a pointer, or a pointer to an array, as in `*T` when `T` is `[4]int`.
- `sizeof(T)` when `T` is an array.
- `typeof(T)` when `T` is an array.
- `typeof` of a function, a tuple, or a trait.
- The C qualifier spelling `const int x` and `var x const int`. Inside a function, `const name = expr` is an immutable binding, and `*const T` is a pointer to constant `T`.
- A file-scope `const` whose value is not an integer constant.
- A non-positive compile-time length, including `const N = 0` used as `[N]int`.
- Returning an array from anything other than a local array name. `return out` works for `[N]T`. Another return expression does not.

### Inference

- A compile-time integer is written at the call: `repeat[4](7)`. It is not inferred from a value argument.
- A type is inferred from `T` and from `*T`. `Vec[T]` is inferred when the argument is already that specialization.
- There is no partial specialization and no SFINAE.

### Parsing

- `name[args](` is a generic call. A call of an indexed value is `(ops[i])()`.
- An array length may be a name, `[N]int`. The name has to be a file-scope `const` already folded. A length that needs `sizeof` of a struct that is not complete yet does not fold.

### Constant evaluation

`assert`, `const`, and a compile-time integer parameter share `fold_int`. That folds integers, names of those constants, and `sizeof`. It does not fold `typeof` or a comparison of two `Type` values, so `assert(typeof(int) == typeof(i32))` is `not supported yet`. A general constant evaluator is not in place. An `assert` whose condition is not a constant integer is the same gap.

## Types and operators

- `?` on a type that is not a pointer: `?int`.
- An optional pointer to an array: `?*[4]int`.
- Two stars outside an array, `**[4]i32`, and a nested array.
- A pointer to an array as a field. `var p *[4]int` is fine. `p *[4]int` inside a struct is not.
- An unsized array on a `var`, `var a []int`. `int[]` is a parameter.
- A cast to `void`.
- `++`, `--`, compound assignment (`+=` and the rest), `?:`, and `->`.
- `goto`.

## Declarations

- `var a, b =` at file scope. Inside a function, several names unpack one tuple.
- `export` or `extern` on an inferred variable, `var name = expr` or `name := expr`. They go on a typed `var`.
- A local `type`, including a local struct or enum.
- A global array initializer. A local array accepts `var row [3]int = {10, 20, 12}`. Any other local array initializer is `not supported yet`.
- A `static` local. File-scope `static` is private to its file.
- Another abi than `"c"` and `"tlc"`.
- `export type Name trait`.

## Functions

- A `test` inside a function. `test` is file scope.
- A struct argument to `testing.error`.
- `builtin.va` outside a variadic function.
- A function pointer whose result is itself a function pointer: `func (i32) func (i32) i32`.
- A variadic `extern` or `extern(abi="tlc")`, and a variadic prototype with no body. `extern(abi="c")` may declare `...`. A function with a body may use `...`, and `builtin.va()` reads the extra arguments.
- An inner function that uses a local or a parameter from an enclosing function. It can call functions already in scope and use globals. `export`, `static`, `extern`, and a method on a function in a block are the same gap. An anonymous function follows that function.

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
- A trait used as a variable, a return type, an array, a type alias, or `**Shape`. A parameter is `Shape` or `*Shape`.
- `export`, `extern`, or `...` on a function that takes a trait parameter. A method cannot take a trait parameter.
