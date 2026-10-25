# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

[Bugs](#bugs) are wrong behavior. They are fixed before every section below. A new bug is added at the top of that list. [Inconsistencies](#inconsistencies) are accepted forms whose rules do not match. Each bullet there is one change. [Optimization](#optimization) goes through the features, including tuples, so a form does not keep more stack or more registers than the values it moves. That pass does not change the language.

## Bugs

No open bugs. A new bug is inserted here, before every other section, and fixed before those sections.

## Static functions

`const` inside the braces, `const Entity::next` inside a function, `var` inside the braces, and `var Entity::next` inside a function stay `not supported yet`. `Entity::next := 1` is `expected a declaration`.

## `if` and `for` in a macro

In a `macro`, a condition or a bound that does not fold is `not supported yet`. `comptime if` and `comptime for` are `not supported yet`. `const for` is not a form.

## Generics and compile-time

`...` is only legal on `extern(abi="c")`. `func id[T type](value T, ...) T` is `not supported yet`. `extern(abi="c")` on a generic is `extern(abi="c") on a trait or a generic`. `any` is not a keyword. `...` in a type-parameter list is `expected an identifier`. `extern` and `extern(abi="tlc")` on a generic function are accepted. A call of a generic that has no body is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, stays `not supported yet`.

- Using a function that has both a comptime parameter and a runtime parameter as a value, unless it is `extern` or `extern(abi="tlc")`. `var f = scale` is `not supported yet`. An `extern` function used as a value is the unspecialized function.
- A comptime argument of that function that is neither a folded `i32`, a constant `[]const u8`, nor an enumerator. A tuple is `not supported yet`. An enumerator is accepted on a free function: `take(n, Order.acquire)` calls `take.spec.1` when `acquire` is `1`. A `var` of that enum is `type mismatch`. A method and a generic function do not take it.
- Every constant as a comptime value, including a struct. A bool folds as an integer. The values are the ones a file-scope `const` already accepts. `const origin = Point{.x = 1, .y = 2}` and `Point{.x = 1, .y = 2}` passed to `comptime func field(p Point)` are `not supported yet`. The same value on `func take(p comptime Point, n i32)` is `type mismatch`. `const scale = 0.5` on `comptime func as_f64(v f64)` is `not supported yet`, and on `v comptime f64` is `type mismatch`. `const n = Integer(4)` when `type Integer i32` is `not supported yet` on a `comptime func` and `type mismatch` on `v comptime Integer`. `const op = add` on a parameter `func (i32, i32) i32` is that same pair. An enumerator passed to a `comptime func` is `not supported yet`. A parameter `[2]i32` is a pointer, so `const row = [_]i32{1, 2}` passed to it is `type mismatch: expected *i32, found [2]i32`.
- More than 32 nested specializations of that function. A body that calls `scale(n + 1, x)` is `not supported yet`.
- A comptime parameter on a method or a generic function. `func Point::id(self *this, order comptime i32)` still passes `order`. `func load_at[T type](p *T, order comptime i32)` keeps `order` as an argument of the copy, so `if (order == Order.relaxed)` stays a runtime compare. A free function with a runtime parameter beside it drops that parameter, and each use is the constant.
- The other arm of a constant `if` in an ordinary function. Both arms are emitted. A `macro` keeps the arm whose condition folds. `comptime if` is `not supported yet`.

The `if` chain in `load_at`, `store_at`, `xchg_at`, and `rmw_at` in `lib/tlc/std/atomic` stays until the two bullets above hold. An enumerator is already a comptime argument of a free function. Each remaining arm passes an integer literal to `builtin.atomic_load`, `builtin.atomic_store`, or `builtin.atomic_rmw`. A `var` of `Order` is not specialized. When the parameter is comptime, that call is `type mismatch`.

- A tuple index that is not a constant. `t[i]` when `i` is a `var` is `index is not a compile-time constant`. `const i = 0` then `t[i]` and `t[i] = 1` choose that field while compiling, as `t[0]` does. A file-scope `const` and a folded expression do too.
- `count(U)`.
- On a generic struct, `export` and a static method on `func Vec[T]::get[U type]` stay `not supported yet`. `extern` and `extern(abi="tlc")` on a method template are accepted. `extern(abi="c")` on that template is rejected. `...` on a method template is not a form. A prototype with no body is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, stays `not supported yet`.
- Type parameters on an alias that is not a tuple, and on an enum or a union. `type Name[T type] i32` and `type Name[T type] *T` are `expected ']'`. `type Name[T type] (i32)` is `not supported yet`. `type Name[T type] enum` and `type Name[T type] union` are `not supported yet`. `type Something[K, V] (K, V)` is `expected a type` on the comma.
- A qualified trait constraint: `T pkg.Shape`.
- A type argument that is `sizeof(...)`.
- `sizeof(T)` when `T` is an array.
- `typeof(T)` when `T` is an array.
- `typeof` of a function. A tuple is `typeof(value)`, and `fields()[i].kind` is element `i`. `typeof(trait Name)` is `expected a type`.
- The C qualifier spelling `const i32 x` and `var x const i32`. Inside a function, `const name = expr` is an immutable binding, and `*const T` is a pointer to constant `T`.
- A file-scope `const` of a pointer, other than a string passed to C. `const c_text *const u8 = "unix"` is the C pointer. `const p *i32 = &n` is `type mismatch`. A string kept as an array is the same gap.
- A non-positive compile-time length, including `const N = 0` used as `[N]i32`.

### Inference

- A compile-time integer is written at the call: `repeat[4](7)`. It is not inferred from a value argument.
- There is no partial specialization and no SFINAE.

### Parsing

- An array length may be a name, `[N]i32`. The name has to be a file-scope `const` already folded. A length that needs `sizeof` of a struct that is not complete yet does not fold.

### Constant evaluation

`assert`, `const`, and a compile-time integer share one evaluator. An `assert` whose condition does not fold is `not supported yet`. A call is not a constant, except a call of a `comptime func` that returns a folded `i32`, a call of an ordinary function whose every parameter is `comptime` and whose body is that same `return`, and `builtin.memeql[u8]` or `builtin.memcmp[u8]` of two constant `[]const u8` values. An index outside the length does not fold, and neither does a bound past the string or a high bound below the low bound. A suffix whose value does not fit in `i32`, such as `3000000000u32`, is `not supported yet`.

## Reflection

`typeof(T).fields()` returns `[]const Field`. Methods are not read yet. A plain `while` or `do` in a `macro` is `not supported yet`. A `var` passed to a `comptime func`, `comptime { ... }` inside an ordinary function, `emit` outside a `macro`, `comptime for`, `comptime if`, a method, `extern`, `export`, a value call of a `macro`, a value call that does not fold, and `const for` are `not supported yet`.

## Types and operators

- `?` on a type that is not a pointer: `?i32`.
- A slice field on `extern(abi="c")`. It is `not supported yet`.
- A file-scope slice whose initializer is not the name of an array, a slice of one of those constants, a string, a constant range of one of those, or a call. A bound that does not fold is `global initializer is not a constant`. An array literal is `global initializer is not a constant`. A range, a literal, or a call used as the initializer of a `const` slice is `global initializer is not a constant` when it is not one of those constant ranges.
- A cast to `void`.
- An atomic `f32`, `f64`, or `bool`. `builtin.atomic_load` of those types is `not supported yet`. `atomic.Bool` stores a `u8`.
- Using `builtin.fence` as a value, and `defer builtin.fence(order)`. Both are `not supported yet`. A direct call is replaced with a fence.
- An atomic value wider than 64 bits.
- A generic `Atomic[T]` for a struct whose size is 1, 2, 4, or 8 bytes.
- `++`, `--`, compound assignment (`+=` and the rest), `?:`, and `->`.
- `??`. `value!.value2!.value3 ?? default_value` yields `default_value` when a pointer in that `!` chain is null, and that `!` does not call `builtin.panic("null pointer")`. `??` is not a form.
- `goto`.

## Declarations

- A variable named `_` is a hint that the value may be ignored, and the compiler may use that hint when optimizing. `var _ = give()` is the spelling. `_` is already a name: that declaration typechecks, and a later `_` reads the binding. The hint is not implemented.
- `var a, b =` at file scope. Inside a function, several names unpack one tuple.
- `extern` on an inferred variable, `var name = expr` or `name := expr`. `export var name = expr` and `export name := expr` are accepted.
- A local `type`, including a local struct or enum.
- A file-scope array whose written length is `[0]i32`.
- A file-scope array element that is not a constant, such as `n + 1` when `n` is a `var`, or `give() + 1`. That is `global initializer is not a constant`. A `const` with that call, and `var n = give(); var row = [_]i32{n}`, are that error.
- Another abi than `"c"` and `"tlc"`.
- `export type Name trait` with no type parameter.

## Functions

- An overloaded name used as a value, `var f = show` when `show` has several parameter lists, is `not supported yet`. A call selects the list.
- A struct argument to `testing.error`.
- `builtin.va` outside a variadic function.
- Calling `builtin.panic` on a fatal failure other than an allocation that returned `null`. `new` calls `builtin.panic("out of memory")` in that case. A non-positive element count and a size that does not fit abort without that call.
- `...` is only legal on `extern(abi="c")`. A function with a body may use `...` today, and `builtin.va()` reads the extra arguments. That body is not the TLC form. A variadic `extern`, `extern(abi="tlc")`, and a variadic prototype with no body stay `not supported yet`. Further TLC arguments are a generic, not `...`. `any` is not a keyword.
- An inner function that uses a local or a parameter from an enclosing function, other than a function pointer it returns or calls. The inner function is called directly. `var f = pass` and passing that function as a value are the same gap. `thread.spawn` takes a file-scope function for that reason. A prototype written before the body is the same gap when the body reads that pointer. It can call functions already in scope and use globals. `export`, `extern`, and a method on a function in a block are the same gap. A short anonymous function follows that function. A full anonymous function called directly can read the function pointer.

## Methods and traits

- A trait used as a type alias, or `**Shape`. A parameter is `Shape` or `*Shape`. `var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, `[]Shape`, and a field `item Shape` in a struct or a trait are `trait has no size`.
- `...` is not added to a function or a method that takes a trait. `func use(s Shape, ...)`, `func use(s *Shape, ...)`, `func Rect::paint(self *this, s Shape, ...)`, `extern func use(s Shape, ...)`, and a prototype `func use(s Shape, ...) i32;` are `not supported yet`. `extern(abi="c")` on that function, with or without `...`, is `extern(abi="c") on a trait or a generic`. Further TLC arguments are a generic, not `...`. `any` is not a keyword. `extern` and `extern(abi="tlc")` without `...` are accepted. A call that specializes a prototype with no body is `not supported yet`.

## Compiler

- A compiler argument names the type `new` and `delete` use when the call names no allocator. Today that type is `builtin.Malloc`, and those calls use the C functions `malloc` and `free` (`docs/builtin/allocator.md`, `docs/statements/new.md`). `new(&arena)` keeps the type of that pointer. The named type satisfies `Allocator`. The later use is a `DebugAllocator` for `tlc --test`, so a test reports a block that was reserved and not released.
- A race condition is not reported. `tlc --test` reports one, and so does a debug build. A release build that is not `tlc --test` does not. The compiler has no debug or release mode yet.

## Inconsistencies

An accepted form whose rule does not match its page is inserted here. Each bullet is one change. The first sentence is what `tlc --check` does. Closing a bullet updates the pages it names. A gap that is already listed above stays there.

- Postfix `!` on a `?*T` is `*T`. A null `p!` calls `builtin.panic("null pointer")` (`docs/expressions/narrowing.md`, `docs/builtin/panic.md`, `docs/grammar.md`). That call runs in a debug build and is left out of a release build. The compiler has no debug or release mode yet.

## Optimization

Go through the features, including tuples, so a form does not keep more stack or more registers than the values it moves. The measure is the x86-64 instructions that run, from `tlc --emit-obj` with no `-O` flag, as `docs/cost.md` describes. A feature page shows the assembly after the extra moves are gone.

A tuple parameter is one LLVM struct. Sending two to six `i32` values uses the same argument registers as those parameters. The same holds for two to four `i64`, two or three `f64`, a `u8`, a slice, a struct beside an `i32`, and a function pointer of two `i32`. The callee stores the whole struct and loads it again for each `t[i]`. Two `i32` is 6 instructions in the callee. Two parameters are 5. Eight `i32` is 35 in the callee. Eight parameters are 17: the registers are moved again before the fields are read. An element the body does not read is still stored. `func sum(t (i32, f64)) i32 { return t[0]; }` is 5 instructions and keeps the `movsd` of the `f64`. Two parameters that return the `i32` are 3. A pointer element is written into the struct and loaded back into the argument register, 2 instructions more in `main` than `sum(&n, b)`. Seven or eight `i32` add one `movq` of `%rsp` before the stack argument. `var t = (a, b); sum(t)` is 4 instructions more in `main` than `sum(a, b)`, from the stores of that local.

An array element of a tuple is passed by value. `func sum(a [4]i32, b i32)` passes a pointer. `func sum(t ([4]i32, i32))` copies the four elements into registers. `main` is 11 instructions more, and the callee is 5 more.

A tuple result uses the same registers as several results. `return (40, 2)` from `func pair() ((i32, i32))` is 3 instructions, the same as `return 40, 2`. The caller that reads `value[0]` reloads the stored tuple: 1 instruction more in `main` for two `i32`, and 7 more for eight `i32`. `(i32, f64)` reloads the `f64` when the caller reads only the `i32`, 4 instructions more in `main`.
