# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

[Bugs](#bugs) are wrong behavior. They are fixed before every section below. A new bug is added at the top of that list. [Inconsistencies](#inconsistencies) are accepted forms whose rules do not match. Each bullet there is one change. [Documentation](#documentation) is pages that already disagree with the compiler. Those do not change the language.

## Bugs

No open bugs. A new bug is inserted here, before every other section, and fixed before those sections.

## Static functions

A function on a struct or enum with no `this` parameter is called as `Type.name(...)`. `export` publishes `pkg.Type.name(...)` and `pkg.Vec[i32].name(...)`. A constant is not a struct member. It is `const Entity::next = 1` or `const Box[T]::limit = 1`, written beside the type, at file scope. `const next` inside the braces stays `not supported yet`. `const Entity::next` inside a function stays `not supported yet`. A member is already a variable, written `name type`. `var` is not written inside the braces, and that keyword there stays `not supported yet`. The variable on the type is `var Entity::next` or `export var Box[T]::extra`, beside the type. A member is defined inside the struct as `name type`. It is not defined in a function, and `Entity::next := 1` is not a declaration. `var Entity::next` inside a function stays `not supported yet`. `Entity::next := 1` is `expected a declaration`.

## Language server

`tlc lsp` publishes diagnostics and answers completion, hover, definition, document symbols, and semantic tokens. Keyword and literal colors stay in the tree-sitter grammar.

## `const if`

Compile-time branching was left out on purpose. `const if` is not a form yet. It stays that way until the syntax of a `macro` function is chosen. `const if (1 == 1)` is `not supported yet`.

## Generics and compile-time

`...` is not added to a generic. It is only legal on `extern(abi="c")`. `extern(abi="c")` on a generic is `extern(abi="c") on a trait or a generic`, so a generic does not take `...`. `func id[T type](value T, ...) T` is `not supported yet`. Other use cases use other forms. Those spellings are not chosen. `any` is not a keyword. `extern` and `extern(abi="tlc")` on a generic function are accepted. Each specialization is its own symbol. A prototype followed by a body is accepted. A call of a generic that has no body is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, stays `not supported yet`. A type parameter is `T type`, not `...T`. `...` in that list is `expected an identifier`. `[...U]`, `[T type, ...U]`, and `[...U, T type]` are that error. `T` is the type of one value, a tuple of several types, or a struct with several fields.

- A tuple index that is not a constant. `t[i]` is `not supported yet`. The field is chosen while compiling, so `t[0]` and `t[0] = 1` are accepted. The element type and size are known then, including when the elements differ.
- `count(U)` and `const for`.
- A method template that is static or exported, and a method template on a type alias: `func Integer::id[T type]`. They are `not supported yet`. An instance method with a body, `func Vec[T]::get[U type](self *this, value U) U`, is accepted. The call is `value.get[i32](value)`. `func Vec[T]::get` still names the struct's parameters. `...` on a method template is not a form. `...` is only legal on `extern(abi="c")`, and that ABI on the template is rejected. `extern` and `extern(abi="tlc")` on a method template are accepted. Each specialization is its own symbol. The call stays `value.id[i32](...)`. A body defines that symbol. A prototype with no body is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, stays `not supported yet`. `extern(abi="c")` on that template is rejected.
- Type parameters on an alias, an enum, or a union. Only `type Name[T type] struct` takes the list. A type parameter on a trait is specified under [for each](#for-each).
- An alias of a tuple, including a generic one: `type Something (i32, f32);` and `type Something[K,V] (K,V);`. `type Something (i32, f32);` is `expected a type` on `(`. `type Something[K,V] (K,V);` is `expected ']'` on the comma. A bracket list on `type` is read as type parameters only when `struct`, `enum`, `trait`, or `union` follows, so `[K,V]` is parsed as an array length. `type Something[K type, V type] (K, V);` is that same `expected ']'`.
- A tuple as a parameter of a generic function or method: `func SomeFunc[P1, P2](value i32, other (P1, P2))`. The same parameter on a method is `func Point::take[P1, P2](self *this, other (P1, P2))` and `func Vec[T]::take[P1, P2](self *this, other (P1, P2))`. `[P1, P2]` is `expected a type` on the comma, because a type parameter is written `P1 type`. With `[P1 type, P2 type]`, `(P1, P2)` is `expected a type` on `(`. A parameter `(i32, f32)` without generics is that same `expected a type`. Passing a tuple as one `T`, `id((40, 2))`, already typechecks.
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

## Reflection

Type information for generating code while compiling. The goal is a `macro` keyword whose body can use `typeof` to read methods, struct fields, and the types of those fields. The syntax of a macro function is not chosen, and it has to be chosen before [`const if`](#const-if) is specified. The compiler has no `macro` form.

## Types and operators

- `?` on a type that is not a pointer: `?i32`.
- A slice field on `extern(abi="c")`. It is `not supported yet`. A tlc union arm that is not a struct stays `type mismatch`.
- A file-scope slice whose initializer is not the name of an array, a slice whose own initializer is one of these constants, a string, a constant range of one of those, or a call. `var mid = numbers[1:3]`, `var all = numbers[:]`, `const v []i32 = numbers[0:2]`, and `var rows = [_][]i32{a[:], b[:]}` store that header. A bound that does not fold is `global initializer is not a constant`. An array literal is `global initializer is not a constant`. A call used as the whole initializer runs before `main`. A range, a literal, or a call used as the initializer of a `const` slice is `global initializer is not a constant` when it is not one of those constant ranges.
- A cast to `void`.
- `++`, `--`, compound assignment (`+=` and the rest), `?:`, and `->`.
- `goto`.

## Declarations

- A variable named `_` is a hint that the value may be ignored, and the compiler may use that hint when optimizing. `var _ = give()` is the spelling. `_` is already a name: that declaration typechecks, and a later `_` reads the binding. The hint is not implemented.
- `var a, b =` at file scope. Inside a function, several names unpack one tuple.
- `extern` on an inferred variable, `var name = expr` or `name := expr`. `export var name = expr` and `export name := expr` are accepted. Another package uses `pkg.name` or `import { name }`.
- A local `type`, including a local struct or enum.
- A file-scope array whose written length is `[0]i32`. An element may be an integer, `bool`, a float, a pointer, a struct, an enum, a typedef, a function pointer, an empty `[_]T{}`, a nested array `[_][2][3]i32`, a pointer to an array `[_]*[4]i32`, or an array value `[_][2]i32{inner}`.
- A file-scope array element that is an expression such as `n + 1` when `n` is a `var`, or `give() + 1`. That is `global initializer is not a constant`. A call element runs before `main`: `var row = [_]i32{give(), 2}`, `var row [2]i32 = {give(), 2}`, `[_]Point{give()}`, and `var row = [_][2]i32{give()}`. The same call in a `const`, and `var n = give(); var row = [_]i32{n}`, are `global initializer is not a constant`. `var row = [_]i32{1u32}` folds. `var row = [_]i32{n}` copies the initializer of `n` when that initializer is a constant. `const row = [_]i32{n}` when `n` is a `var` is `global initializer is not a constant`.
- Another abi than `"c"` and `"tlc"`.
- `export type Name trait` with no type parameter. Export of the iteration trait is specified under [for each](#for-each).

## Functions

- An overloaded name used as a value, `var f = show` when `show` has several parameter lists, is `not supported yet`. A call selects the list.
- A struct argument to `testing.error`.
- `builtin.va` outside a variadic function.
- Calling `builtin.panic` on a fatal failure other than an allocation that returned `null`. `new` calls `builtin.panic("out of memory")` in that case. A non-positive element count and a size that does not fit abort without that call.
- `...` is only legal on `extern(abi="c")`. A function with a body may use `...` today, and `builtin.va()` reads the extra arguments. That body is not the TLC form. A variadic `extern`, `extern(abi="tlc")`, and a variadic prototype with no body stay `not supported yet`. Further TLC arguments are a generic, not `...`. Other use cases use other forms. Those spellings are not chosen. `any` is not a keyword.
- An inner function that uses a local or a parameter from an enclosing function, other than a function pointer it returns or calls. The inner function is called directly. `var f = pass` and passing that function as a value are the same gap. A prototype written before the body is the same gap when the body reads that pointer. It can call functions already in scope and use globals. `export`, `extern`, and a method on a function in a block are the same gap. A short anonymous function follows that function. A full anonymous function called directly can read the function pointer.

## Methods and traits

- The pointer receiver `self *this` is fixed. `self = ...` is rejected. `*self = ...` and a store such as `self.x = ...` stay, so the compiler can keep the pointer instead of reloading it for each use. Today `self = other`, when `other` is also `*Point`, typechecks. A value receiver `self this` is unchanged.
- A trait used as a type alias, or `**Shape`. A parameter is `Shape` or `*Shape`. `var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, `[]Shape`, and a field `item Shape` in a struct or a trait are `trait has no size`.
- `...` is not added to a function or a method that takes a trait. `func use(s Shape, ...)`, `func use(s *Shape, ...)`, `func Rect::paint(self *this, s Shape, ...)`, `extern func use(s Shape, ...)`, and a prototype `func use(s Shape, ...) i32;` are `not supported yet`. `extern(abi="c")` on that function, with or without `...`, is `extern(abi="c") on a trait or a generic`. Further TLC arguments are a generic, not `...`. Other use cases use other forms. Those spellings are not chosen. `any` is not a keyword. `extern` and `extern(abi="tlc")` without `...` are accepted. Each struct is its own symbol. The call stays `use(r)` or `value.paint(s)`. A body defines that symbol. A prototype with no body is accepted. A call that specializes it is `not supported yet`. `export` publishes that function when the ABI is not C.

## Inconsistencies

Accepted forms whose rules do not match. Each bullet is one change. The first sentence is what `tlc --check` does. Closing a bullet updates the pages it names. A gap that is already listed above stays there. This section records the mismatch.

### Spellings

- `[` is type parameters, type arguments, an array length, an index, a range, a `new` count, and `_`. `name[i32](` is a generic call, already listed under Parsing. A call of an indexed value is `(ops[i])()`. `Vec[i32]{}` is a struct value. `Buf[4, i32].count` is the constant because of the comma. `arr[i].field` is an index. Pages: `docs/grammar.md`.

### Documentation

Pages that disagree with the compiler. Fixing one does not change the language.

- `docs/types/literal.md` lists `"hi"` and a raw string as `*const u8`. Both are `[]const u8`. `docs/types/string.md` and `docs/grammar.md` already say that.
- `docs/types/pointer.md` says `opt![i]` is `(opt[i])!` and `type mismatch`. Postfix operators bind left to right, so `opt![2]` is `(opt!)[2]` and typechecks. `(opt!)[2]` is the same expression.
- `docs/grammar.md` prose accepts forms the EBNF does not generate. `new i32` and `new [n]u8` use an `integer-name`, and `new-expr` only names an identifier. `[N][M]T`, `[][N]T`, and `[][][]T` are another array or slice around the inner type, and the `go-type` alternatives do not generate them. `?*i32(p)` and a cast that writes `const` are casts, and the `cast` rule is `{ "*" }` plus a scalar or an identifier. `Vec[*const u8]` is a type argument, and the `type-arg` pointer alternative has no `const`.
