# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

[Inconsistencies](#inconsistencies) are accepted forms whose rules do not match. Each bullet there is one change. [Documentation](#documentation) is pages that already disagree with the compiler. Those do not change the language.

## Static functions

A function on a struct or enum with no `this` parameter is called as `Type.name(...)`. `export` publishes `pkg.Type.name(...)` and `pkg.Vec[i32].name(...)`. These forms are not supported yet:

- `const next` inside a struct. The constant is `const Entity::next = 1` or `const Box[T]::limit = 1`, written beside the type.
- `const Entity::next` inside a function. The constant is file scope.
- `extern` on `const Entity::next`.
- `var next` inside a struct. The variable is `var Entity::next` or `export var Box[T]::extra`, written beside the type. Another package reads and assigns `pkg.Box[i32].extra`.
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
- A generic that is also `...`. `extern` and `extern(abi="tlc")` on a generic function are accepted. Each specialization is its own symbol. `extern(abi="c")` on a trait or a generic is rejected. A prototype followed by a body is accepted. A call of a generic that has no body is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, stays `not supported yet`. `[...U]` is a separate parameter and is accepted when it is the last type parameter, as in `[T type, ...U]`.
- A pack that is not the last type parameter, `[...U, T type]`.
- A pack on a struct or a union.
- An empty pack.
- A pack of several types used as `*U`, `[N]U`, a type argument, or a field.
- `typeof` inside a parenthesized result list. A bare result `typeof(expression)` is accepted.
- A tuple index that is not a constant, and assignment to a tuple element.
- `count(U)` and `const for`.
- A type-parameter list after the method name on a generic struct: `func Vec[T]::get[U type]`. `func Type::name[T type](self *this)` is a method template when the owner is not a generic struct. `func Vec[T]::get` names the struct's parameters.
- A method template that is static, exported, or `...`, and a method template on a type alias: `func Integer::id[T type]`. They are `not supported yet`. `extern` and `extern(abi="tlc")` on a method template are accepted. Each specialization is its own symbol. The call stays `value.id[i32](...)`. A body defines that symbol. A prototype with no body is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, stays `not supported yet`. `extern(abi="c")` on that template is rejected.
- Type parameters on an alias, an enum, or a union. Only `type Name[T type] struct` takes the list. A type parameter on a trait is specified under [for each](#for-each).
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
- `export type Name trait` with no type parameter. Export of the iteration trait is specified under [for each](#for-each).

## Functions

- An overloaded name used as a value, `var f = show` when `show` has several parameter lists, is `not supported yet`. A call selects the list.
- A `test` inside a function. `test` is file scope.
- A struct argument to `testing.error`.
- `builtin.va` outside a variadic function.
- Calling `builtin.panic` on a fatal failure other than an allocation that returned `null`. `new` calls `builtin.panic("out of memory")` in that case. A non-positive element count and a size that does not fit abort without that call.
- A function pointer whose result is itself a function pointer: `func (i32) func (i32) i32`.
- A variadic `extern` or `extern(abi="tlc")`, and a variadic prototype with no body. `extern(abi="c")` may declare `...`. A function with a body may use `...`, and `builtin.va()` reads the extra arguments.
- An inner function that uses a local or a parameter from an enclosing function, other than a function pointer it returns or calls. The inner function is called directly. `var f = pass` and passing that function as a value are the same gap. A prototype written before the body is the same gap when the body reads that pointer. It can call functions already in scope and use globals. `export`, `extern`, and a method on a function in a block are the same gap. A short anonymous function follows that function. A full anonymous function called directly can read the function pointer.

## Methods and traits

- A trait used as a type alias, or `**Shape`. A parameter is `Shape` or `*Shape`. `var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, `[]Shape`, and a field `item Shape` in a struct or a trait are `trait has no size`.
- `...` on a function that takes a trait parameter. `extern` and `extern(abi="tlc")` on that function are accepted. Each struct is its own symbol. The call stays `use(r)` or `value.paint(s)`. A body defines that symbol. A prototype with no body is accepted. A call that specializes it is `not supported yet`. `export` publishes that function when the ABI is not C. `extern(abi="c")` on that function is rejected.
- A named function as the argument of a single-method trait. The argument is an anonymous function.
- `(*a).apply(n)` when `a` is a trait pointer passed as an anonymous function. The call is `a.apply(n)`.
- A value on a trait field, such as `w i32 = 1`. It is `not supported yet`.

## for each

`for (item := range items)` is `not supported yet`. The three-clause `for` stays as it is. `range` stays an identifier. It is recognized only in this clause, so this form adds no token. `&range` in that same place binds a pointer to the element. It is not the address of a variable named `range`. The binding is written with `:=`. The name is new in the loop and hides an outer name of the same spelling. It does not assign that outer name. `break`, `continue`, and `defer` are the ones the three-clause `for` already has.

`item := range items` binds the element. Assigning that name does not write the collection. `item := &range items` binds `*E`, where `E` is the element type. The pointer is never `?*E`. A const element, such as `[]const E` or a const array, binds `*const E`, and a store through it is `type mismatch`. `item.field` and `*item` write the element when the pointer is `*E`. `item = &other` reseats the local pointer. The next iteration stores the next address.

There is one iteration trait. It is parsed from `lib/tlc/std/seq`. The compiler does not build a second trait in the checker the way it builds `Allocator`. Arrays and slices satisfy that trait, and a struct that satisfies it is accepted here. Both decisions ask the same question. A file does not import the package to iterate. Importing it is how another file names the trait.

`type Seq[T type] trait` takes one type parameter. `T type` is a type, as on a struct. `Seq[i32]` and `Seq[u8]` are different traits. A method may use `T` as a parameter or a result, and so may a field. A concrete parameter is `items Seq[i32]` or `items *Seq[i32]`. `Seq` without arguments is `type mismatch`. An integer parameter, a pack, several parameters, and `trait(Parent)` on that list stay `not supported yet`. An alias, an enum, and a union stay as they are. `func Seq::name` stays `not supported yet`. `func use[T type](items Seq[T])` stays the existing gap for a trait parameter beside `[T type]`.

The bullets below are the order to land the rest. Each bullet is one change.

- `extern` and `extern(abi="tlc")` on that trait. `extern(abi="c")` and `extern(abi="c", name="symbol")` stay `extern(abi="c") on a trait or a generic`. `export` publishes it. Another package names `seq.Seq[i32]` after `import "std/seq"`. The trait still has no size. `var s Seq[i32]`, a result, an array, a slice, and a field of the trait stay `trait has no size`.
- The package `lib/tlc/std/seq` declares that trait, and its `link` file names a library this repository builds and installs under the compiler prefix. The compiler treats the `extern` trait in the package that links that library as the iteration trait. It reads the methods from that declaration. It does not keep a second list. A copy of the source that does not link that library is an ordinary trait: a struct may satisfy the copy, an array does not, and `for` does not select it. `Allocator` stays synthesized for `new` and `delete`.

```c
package seq;

export extern type Seq[T type] trait {
    func len() i32;
    func at(i i32) T;
    func ref(i i32) *T;
};
```

The length method has no parameter after the receiver and its result is `i32`. The element method has one `i32` parameter and its result is `T`. `ref` has one `i32` parameter and its result is `*T`. The names in that file are the names a struct writes. `len` and `at` have value receivers. `ref` has a pointer receiver, `self *this`, because its result points into the struct. A value parameter `Seq[T]` checks `len` and `at`. The pointer receiver on `ref` does not reject that value. `*Seq[T]` checks `ref` too. A field, a further method, or a default on this trait stays `not supported yet` until the array side grows the same method.

- `[N]T` and `[]T` satisfy `Seq[E]`, where `E` is the type of an index. That includes a parameter written `[N]T`, a string `[]const u8`, `[N][M]T`, and `[N][]T`. The element of `[N][M]T` is `[M]T`. `len` is `.len`. `at` is the index. `ref` is the address of that element, `&items[i]`. `range` does not copy the collection on each call beyond what `.len` and `[i]` already do. `&range` requires an lvalue. A temporary is `not an lvalue`. A slice alias such as `type Row []i32` does not satisfy the trait unless it has the methods. A pointer to an array or a slice, `*[N]T` or `*[]T`, is `type mismatch`, and so is a number. A struct is not given this implementation. It satisfies the trait only by its own methods. An array or a slice satisfies no other trait.
- The same trait the other way. `func use(items Seq[i32])` accepts `[N]i32`, `[]i32`, and a struct that has the methods. Inside that function the calls are `items.len()` and `items.at(i)`, specialized per value, as a trait parameter already is. `[4]i32` and `[8]i32` are different copies. `[]i32` is one. A missing method is `undeclared identifier`. A wrong signature or a pointer receiver is `type mismatch`. The address is not taken automatically.
- A struct satisfies it by `len` and `at`, with value receivers, and no `impl`. `&range` also calls `func Bag::ref(self *this, i i32) *T`. A missing `ref` leaves `range` working and reports `&range` as `undeclared identifier`. A generic struct uses its own parameters: `func Bag::at(self this, i i32) T` on `type Bag[T type] struct` satisfies `Seq[T]` for that specialization. `item := range bag` is then the same loop as an array. The file that defines `Bag` does not import `std/seq`.
- The statement `for (name := range expression) { ... }`, and `for (name := &range expression) { ... }`. The expression's type must satisfy the iteration trait. The body is a block. The name is visible only in the loop. `range` walks an index from `0` to `len()` and binds `at` of the index. `&range` binds `ref` of the index. These spellings stay `not supported yet`: `for (var name := range items)`, `for (name = range items)`, `for (name in items)`, `for (i, name := range items)`, `for (name := range items)` without parentheses, and `for (const name := range items)`.
- The name is a binding in the language-server index. Completion, hover, and definition see it. Rename, formatting, and the other missing answers stay as they are.

When a bullet lands, the behavior goes on [for](docs/statements/for.md), the syntax on [grammar](docs/grammar.md), and the trait on a page under `docs/std/` linked from [the index](docs/README.md). The cost is measured then, from the instructions the three-clause `for` and an index already run.

## Inconsistencies

Accepted forms whose rules do not match. Each bullet is one change. The first sentence is what `tlc --check` does. Closing a bullet updates the pages it names. A gap that is already listed above stays there. This section records the mismatch.

### Spellings

- `[` is type parameters, type arguments, an array length, an index, a range, a `new` count, and `_`. `name[i32](` is a generic call, already listed under Parsing. A call of an indexed value is `(ops[i])()`. `Vec[i32]{}` is a struct value. `Buf[4, i32].count` is the constant because of the comma. `arr[i].field` is an index. Pages: `docs/grammar.md`.

### Documentation

Pages that disagree with the compiler. Fixing one does not change the language.

- `docs/types/literal.md` lists `"hi"` and a raw string as `*const u8`. Both are `[]const u8`. `docs/types/string.md` and `docs/grammar.md` already say that.
- `docs/types/pointer.md` says `opt![i]` is `(opt[i])!` and `type mismatch`. Postfix operators bind left to right, so `opt![2]` is `(opt!)[2]` and typechecks. `(opt!)[2]` is the same expression.
- `docs/grammar.md` prose accepts forms the EBNF does not generate. `new i32` and `new [n]u8` use an `integer-name`, and `new-expr` only names an identifier. `[N][M]T`, `[][N]T`, and `[][][]T` are another array or slice around the inner type, and the `go-type` alternatives do not generate them. `?*i32(p)` and a cast that writes `const` are casts, and the `cast` rule is `{ "*" }` plus a scalar or an identifier. `Vec[*const u8]` is a type argument, and the `type-arg` pointer alternative has no `const`.
