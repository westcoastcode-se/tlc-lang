# Not supported yet

What the language pages leave out. The compiler reports most of these as `not supported yet`. Diagnostics stay the short English phrases already used (`not supported yet`, `type mismatch`).

[Bugs](#bugs) are wrong behavior. They are fixed before every section below. A new bug is added at the top of that list. [Inconsistencies](#inconsistencies) are accepted forms whose rules do not match. Each bullet there is one change. [Optimization](#optimization) goes through the features, including tuples, so a form does not keep more stack or more registers than the values it moves. That pass does not change the language.

## Bugs

No open bugs. A new bug is inserted here, before every other section, and fixed before those sections.

## `if` and `for` in a macro

In a `macro`, a condition or a bound that does not fold runs at run time. `comptime if` whose condition does not fold is `not supported yet`. `comptime switch` whose value does not fold is `not supported yet`. `comptime for` whose bound does not fold is `not supported yet`. `const for` is not a form.

## Generics and compile-time

`...` is only legal on `extern(abi="c")`. `func id[T type](value T, ...) T` is `not supported yet`. `extern(abi="c")` on a generic is `extern(abi="c") on a trait or a generic`. `any` is not a keyword. `...` in a type-parameter list is `expected an identifier`. A bare `extern` on a generic function declares a function the compiler implements. It emits nothing. A body on that declaration is `not supported yet`. A later function with the same parameter list is `redefinition`. A call the compiler does not implement is `not supported yet`. `extern(abi="tlc")` on a generic function is `not supported yet`. `items Seq[T]` takes `T` from that value. A parameter whose type is a trait that does not use `T`, such as `s Shape` beside `[T type]`, stays `not supported yet`.

- A function in a generic value argument must be a `comptime func`. An ordinary function is `not supported yet`. `add(3) + 1` is `expected ']'`. A `var` in that argument is `type mismatch`.
- A `comptime func` calls a `comptime func`. A call of any other function is `not supported yet`.
- A `new` or `delete` in a `comptime func` that uses the heap is `not supported yet`. That includes no allocator, `Malloc`, and an allocator whose `malloc`, `free`, or `realloc` calls `malloc`, `realloc`, or `free`.
- A `var` is not promoted into a `comptime const`. `var f = add` then `comptime const ops = (f, 1)` is `var is not a compile-time constant`. A `comptime func` in that `var` is that error. `var f = add` then `comptime const op = f` is that error. A file-scope `var f = add` then `comptime const op = f` is `global initializer is not a constant`. Write `comptime const f = add`.
- A `var` address in a `comptime const`, `comptime const pair = (&n, 2)`, is `var is not a compile-time constant`. `null` is `type mismatch`. Passing `(&addr, 2)` to a `comptime` parameter is `not supported yet`. There is no spelling for the pointer. `&addr` alone on `comptime p *i32` is `type mismatch`.
- A `var` array in a `comptime const` tuple is `var is not a compile-time constant`. Passing that tuple to a `comptime` parameter is `not supported yet`. There is no spelling for the array.
- More than 32 nested specializations of that function. A body that calls `scale(n + 1, x)` is `not supported yet`. A generic body `return width[add(N)]()` or `return Buf[add(N)].span()` is the same limit.
- `count(U)`.
- A bare `extern` on a method template declares a method the compiler implements. It emits nothing. A body on that declaration is `not supported yet`. A later method with the same parameter list is `redefinition`. A call the compiler does not implement is `not supported yet`. `extern(abi="tlc")` on that template is `not supported yet`. `extern(abi="c")` on that template is rejected. `...` on a method template is not a form. `func Bag::read[T type](self *this, items Seq[T])` takes `T` from that value. A parameter whose type is a trait that does not use `T`, such as `s Shape` beside `[T type]`, stays `not supported yet`.
- `type Name[T type] i32` and `type Name[T type] *T` are `expected ']'`. A trait as the type a generic alias names is `not supported yet`. An alias that names itself is `not supported yet`. An enum does not take type parameters. `type Name[T type] enum` is not a form and is reported as `not supported yet`. `extern type Num[T type] union` is `not supported yet`. `extern(abi="c") type Num[T type] union` is `extern(abi="c") on a trait or a generic`. An empty `type Num[T type] union {}` is `not supported yet`. A second value on that union is `not supported yet`. A value on a tagged arm is `not supported yet`. A call is `global initializer is not a constant`. A second value on a union field inside `type Box[T type] struct` is `not supported yet`.
### Inference

- A compile-time integer is written at the call: `repeat[4](7)`. It is not inferred from a value argument.
- There is no partial specialization and no SFINAE.

### Parsing

- An array length may be a name, `[N]i32`. The name is a file-scope `const` already folded, a `const` inside a function whose initializer folds to a non-negative `i32`, or a `comptime` parameter of `func ordinary(comptime n i32)` once the call substitutes it. `var row [n]i32` then `row.len` is `n`. The result `[n]i32` is that array. `ordinary(1)[0]` is 40. `func fill(comptime n i32, value i32) [n]i32` is that array too. `fill(1, 40)[0]` is 40. A length whose other elements are not stored is `uninitialized`. `comptime const N = sizeof(Point)` is that length once `Point` is complete, including a field `buf [N]u8`. `buf [sizeof(Point)]u8` is that length. Inside the struct, that length is the size of the other fields. A `var`, `var n const i32`, and an initializer that does not fold are `not supported yet`. A negative length is `array length must be >= 0`.

### Constant evaluation

`assert`, `const`, and a compile-time integer share one evaluator. An `assert` whose condition does not fold is `not supported yet`. A call is not a constant, except a call of a `comptime func` that returns `i32`, a `bool`, an `f32`, an `f64`, an `i64`, a `u32`, an enumerator, a struct, a tuple, an `[N]i32`, an `[N]f32`, or a constant `[]const u8` (a body that is one `return` of a constant folds, and a body with statements is compiled and run), a call of an ordinary function whose every parameter is `comptime` and whose body is that same `return`, or whose body uses that parameter as an array length (that body is compiled and run), and `mem.memeql[u8]` or `mem.memcmp[u8]` of two constant `[]const u8` values. A `comptime func` whose result is not `i32`, not a `bool`, not an `f32`, not an `f64`, not an `i64`, not a `u32`, not an enumerator, not a struct, not a tuple, not an `[N]i32`, not an `[N]f32`, and not a constant `[]const u8` is `not supported yet`. A `u64` result is `not supported yet`. A file-scope `const` whose initializer folds to `i32` is an element of a tuple passed to a `comptime func`. A file-scope `const` array of `i32`, a nested array of those, or an array of structs is that element too. A local `const` and a `var` in that tuple stay `not supported yet`. A local `const` of that array stays `not supported yet`. `var got = pair(40)` and `var made = move(origin)` copy that folded value. An index of a constant string outside that string is `index out of range`. A file-scope folded `const` is that index or a range bound. A bound past the string, or a high bound below the low bound, does not fold. An index of a `comptime const` array folds, including in `assert`. An index outside that array is `index out of range`. A `const` that is not `comptime` is not a comptime argument. A suffix that does not fit in `i32` keeps that type. `comptime const n = 3000000000u32` is that `u32`. `9223372036854775807i64` and `18446744073709551615u64` are those types. The name is not an `i32`. `comptime const m = n + 1u32` is that sum, wrapped in `u32`. Inside a function, `[n]i32` is that array when `n` is a `comptime const` of `i64`, `u32`, or `u64` and the value fits in a non-negative `i32`. `0i64` is `[0]i32`. A value that does not fit stays `not supported yet`. A negative value is `array length must be >= 0`. `width[4i64]()` and `repeat[4u32](40)` are that integer. A value that does not fit stays `type mismatch`. `comptime if (n > 0)` keeps that arm when the value is a `u32` or an `i64`. `u64` stays `not supported yet` as that condition. An unsuffixed number that fits in `i32` is `i32`. A larger number that fits in `i64` is `i64`. A number past `i64` is `type mismatch`.

## Reflection

A plain `while` or `do` in a `macro` runs at run time. `emit` in that body is an ordinary statement. A statement there without `emit` is not pasted. A `var` passed to a `comptime func`, a runtime statement inside `comptime { ... }`, `emit` outside a `macro`, `comptime for` whose bounds do not fold, `comptime if` whose condition is not comptime, `comptime switch` whose value is not comptime, a method, `extern`, `export`, a value call of a `macro`, a value call that does not fold, and `const for` are `not supported yet`.

## Types and operators

- Pattern matching on a tagged union. `type Shape union(ShapeType)` is matched by reading `ShapeType(shape)` and then the field. A match that binds the active arm is not a form, and the spelling is not decided. An optional value in the standard library would be that union, not `?` on a primitive.
- A slice is not a C type. A parameter, a result, or a field of `extern(abi="c")` is `extern(abi="c") cannot use a slice`. `*T(items)` is the data pointer and `items.len` is the length.
- A file-scope slice whose initializer is not the name of an array, a slice of one of those constants, a string, a constant range of one of those, or a call. A bound that does not fold is `global initializer is not a constant`. An array literal is `global initializer is not a constant`. A range, a literal, or a call used as the initializer of a `const` slice is `global initializer is not a constant` when it is not one of those constant ranges.
- A cast to `void`.
- An atomic value wider than 64 bits is `not supported yet`. `atomic.Bool` stores a `u8`. A struct atomic is the whole value when that value is 1, 2, 4, or 8 bytes and its alignment is at least that size. `add`, `sub`, `and`, `or`, and `xor` on a struct are `not supported yet`. A struct whose size is not one of those, or whose alignment is lower, is `not supported yet`. A plain `Point` of two `i32` is that case.

## Declarations

- `extern` on an inferred variable, `var name = expr` or `name := expr`. `export var name = expr` and `export name := expr` are accepted.
- A file-scope array element that is not a constant, such as `n + 1` when `n` is a `var`, or `give() + 1`. That is `global initializer is not a constant`. A `const` with that call, and `var n = give(); var row = [_]i32{n}`, are that error.
- A bare `extern`, `extern(abi="tlc")`, and any abi other than `"c"` and `"compiler"`. The diagnostic is `not supported yet`. `extern(abi="c")` is the C library. `extern(abi="compiler")` marks a type, method, or function the compiler implements or checks. In `lib/tlc/builtin`, a signature that does not match that contract is `type mismatch`. `extern(name="symbol")` without `abi="c"` is also `name requires abi="c"`.
- `const` before a type on a function type, a result, an array, an associated variable, or `const name const Type`. `func (const i32) i32`, `func f() const i32`, `const [4]i32 row`, `const n const i32`, and `var Type::name const Type` are `not supported yet`. `var row const [4]i32` is `expected a type`. `type Name[T type] const T` is `expected ']'`. `const *i32` is not a spelling of `*const i32`. `[]const [4]T` is a slice of `[4]T` whose element is const.

## Functions

- A function has a body. `func add(a i32, b i32) i32;`, `export func add(a i32, b i32) i32;`, and `func id[T type](value T) T;` are `not supported yet`. `extern(abi="c")` may end with `;`. A call of that function with no body is accepted by `--check`. A bare `extern` on a generic function declares a function the compiler implements. It emits nothing. A later TLC body is `redefinition`. A call the compiler does not implement is `not supported yet`. `extern(abi="c")` on that generic is `extern(abi="c") on a trait or a generic`. `extern(abi="tlc")` is `not supported yet`. A comptime parameter on the bare `extern` is `not supported yet`.
- An overloaded name used as a value without a written type, `var f = show` when `show` has several parameter lists, is `not supported yet`. `var f func (i32, f32) void = show` chooses that list. A call selects the list.
- `builtin.va` outside a variadic function.
- `...` is only legal on `extern(abi="c")`. A function with a body may use `...` today, and `builtin.va()` reads the extra arguments. That body is not the TLC form. A variadic prototype with no body, and `...` on a bare `extern` or `extern(abi="tlc")`, stay `not supported yet`. Further TLC arguments are a generic, not `...`. `any` is not a keyword.
- Using an inner function that reads a `comptime const` as a value. `var f = pass` and passing that function as an argument are `not supported yet`. A direct call reads that `comptime const`, including a tuple. A `var`, a runtime `const`, and a parameter are `not supported yet`. `thread.spawn` takes a file-scope function for that reason. A prototype, `func add(b i32) i32;` before the body, is not allowed. It is `not supported yet`, the same as a file-scope function with no body. `export`, `extern`, and a method on a function in a block are the same gap. A short anonymous function that is not called directly follows that function. A full anonymous function called directly, or used from `defer`, can read that `comptime const`. `func` is not a lambda.

## Methods and traits

- `const name` and `var name` inside a struct are `not supported yet`. The constant and the variable are `const Type::name` and `var Type::name` beside the struct. `export` and `extern` on those inside the braces stay `not supported yet`.
- A method has a body. `func Point::read(self this) i32;`, `export func Point::read(self this) i32;`, `func Point::origin() Point;`, `func Vec[T]::get(self *this) T;`, and `func Point::id[T type](self *this, value T) T;` are `not supported yet`. An `extern(abi="c")` method may end with `;`. A bare `extern` on a generic method declares a method the compiler implements. It emits nothing. A body on that declaration is `not supported yet`. A later method with the same parameter list is `redefinition`. A call the compiler does not implement is `not supported yet`. `extern(abi="c")` on a plain method stays that form. `extern(abi="c")` on a method template is rejected. `extern(abi="tlc")` is `not supported yet`. A comptime parameter on the bare `extern` is `not supported yet`. A trait default with no body is `not supported yet`.
- A trait used as a type alias, or `**Shape`. A parameter is `Shape` or `*Shape`. `var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, `[]Shape`, and a field `item Shape` in a struct or a trait are `trait has no size`.
- `...` is not added to a function or a method that takes a trait. `func use(s Shape, ...)`, `func use(s *Shape, ...)`, `func Rect::paint(self *this, s Shape, ...)`, `extern func use(s Shape, ...)`, and a prototype `func use(s Shape, ...) i32;` are `not supported yet`. `extern(abi="c")` on that function, with or without `...`, is `extern(abi="c") on a trait or a generic`. Further TLC arguments are a generic, not `...`. `any` is not a keyword. A bare `extern` and `extern(abi="tlc")` on that function are `not supported yet`.

## Compiler

No open items.

## Inconsistencies

An accepted form whose rule does not match its page is inserted here. Each bullet is one change. The first sentence is what `tlc --check` does. Closing a bullet updates the pages it names. A gap that is already listed above stays there.

## Optimization

Go through the features, including tuples, so a form does not keep more stack or more registers than the values it moves. The measure is the x86-64 instructions that run, from `tlc --emit-obj` with no `-O` flag, as `docs/cost.md` describes. A feature page shows the assembly after the extra moves are gone.

A by-value tuple parameter is passed as one argument per element. A pointer to a tuple, an array of tuples, a nested tuple, and `extern(abi="c")` stay one value. An array element uses the same address an array parameter uses. Two `i32` is 5 instructions in the callee, the same as two parameters. `func sum(t (i32, f64)) i32 { return t[0]; }` is 3, and the callee does not store the `f64`. Eight `i32` is 5 in the callee, the same as eight parameters. `func sum(t ([4]i32, i32))` passes the address of the array, the same as `func sum(a [4]i32, b i32)`.

`var t = (a, b); sum(t)` in the same block, with no store to `t` before the call, is the same instructions in `main` as `sum(a, b)`. The call passes the loads of `a` and `b`. The store of `t` is not read, so it is removed.

A tuple result uses the same registers as several results. `return (40, 2)` from `func pair() ((i32, i32))` is 3 instructions, the same as `return 40, 2`. `var value = pair()` in one block, with no store to `value` and no address taken before the read, passes `value[i]` from those registers. The store of `value` is not read, so it is removed. `return value[0] + value[1]` is 39 instructions in `main`. Two results stored in `a` and `b` are 42. Eight `i32` is 47 in `main`, and eight results are 50. `return value[0]` from `(i32, f64)` does not store the `f64`. `main` is 36. Two results are 38.
