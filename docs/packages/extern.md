# extern

[Index](../README.md) · [Previous: Visibility](visibility.md) · [Next: Numeric types](../types/integer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`extern` names a symbol that a library defines.

## Only `abi="c"`

A bare `extern` and `extern(abi="tlc")` are `not supported yet`.

That includes a function, a method, a generic, a trait, a `const`, and a `type`.

The compiler emits nothing for that declaration.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

## A C library

`extern(abi="c")` names a symbol from a C library.

It keeps the C name and the C calling convention.

It returns one result.

A method keeps the name `Type.name`.

`extern(abi="c")` is rejected on a trait or a generic.

That includes a trait default, a generic function, a method template, and a method of a generic struct.

`extern(abi="c")` is also rejected on a function whose parameter is a trait.

`extern(abi="c") type Name[T] struct`, `extern(abi="c") type Name trait`, and `extern(abi="c") type Name[T type] trait` are rejected too.

That error is `extern(abi="c") on a trait or a generic`.

A generic prototype with no body is `not supported yet`.

A method template prototype with no body is `not supported yet`.

A comptime parameter on that prototype is `not supported yet`.

`...` on a function that is not `extern(abi="c")` is `not supported yet`.

`export type Seq[T type] trait` publishes that trait.

It has no size and no symbol.

Another package names `pkg.Seq[i32]`.

The package `std/seq` declares that trait. The compiler treats `Seq` in that directory as the iteration trait.

`export` publishes a function or an ordinary method whose parameter is a trait when the ABI is not C.

`extern(abi="c") type Name union(Enum)`, a field `data union(Enum)`, and a field whose type is a tagged union, inside an `extern(abi="c")` struct or union, are `extern(abi="c") on a tagged union`.

The C union is `extern(abi="c") type Name union { ... }`.

A bare `extern type Name union(Enum)` is `not supported yet`. A tagged union without `extern` keeps the tag.

Linking a program that uses `extern(abi="c")` requires a C library on the link line, either `-l` or a library file such as `libfoo.a`.

`--check` and `--emit-*` do not link, so they accept the declaration.

> **Warning:** Without that library the link step reports `extern(abi="c") requires a C library`.

## The C object name

`extern(abi="c", name="symbol")` sets the C object symbol.

The name in the program stays the identifier, so a call is written with that identifier.

The arguments can be written in either order.

`extern(name="real_add")` and `extern(abi="tlc", name="real_add")` are `name requires abi="c"`.

A variable uses the same rule: `extern(abi="c", name="errno")`.

A file-scope constant uses that linkage too.

`extern const n = 4` and `extern(abi="tlc") const step = 3` are `not supported yet`.

`extern(abi="c", name="limit") const n = 4` is the C name `limit`.

`name` adds no instructions.

`extern(abi="c", name="symbol")` on a `type` is reported as `not supported yet`.

An empty name is `type mismatch`.

A repeated `abi` or `name` in one `extern` is `redefinition`.

Two declarations of one function may omit `name` or repeat the same name.

Two different names are `redefinition`.

## Parameters, results, and layout

A parameter or result of an `extern(abi="c")` function cannot be a function pointer that returns several results.

A struct used by such a function, by value or through a pointer, must itself be `extern(abi="c")`.

That includes a struct nested inside it.

An `extern(abi="c") struct` uses the platform C memory layout, so its fields line up with a static or shared library.

`bool` in that struct is one byte.

On this host a struct of at most 16 bytes is passed in registers.

A larger struct is passed as a pointer.

A function that is not `extern(abi="c")` keeps the compiler's own convention for struct values.

```c
extern(abi="tlc") func parts(p Point) (i32, i32);

func main() i32 {
    return 0;
}
```

`extern(abi="tlc")` emits nothing. The diagnostic is `not supported yet`.

```c
extern(abi="c") func printf(fmt *const u8, ...) i32;

func main() i32 {
    printf("hi\n");
    return 0;
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq printf                     # call printf
retq                             # return
```

```c
extern(abi="c", name="printf") func print(fmt *const u8, ...) i32;
extern(abi="c", name="errno") var last_error i32;

func main() i32 {
    print("hi\n");
    return last_error;
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq printf                     # call printf
retq                             # return
```

`print` is the name in the program.

The object file links `printf` and `errno`.

`*const T` on a parameter, including an `extern(abi="c")` parameter, is a pointer to constant `T`.

A variadic function may be declared with `extern(abi="c")` and without a body.

A function with a body may use `...`.

`builtin.va()` reads the extra arguments.

[Index](../README.md) · [Previous: Visibility](visibility.md) · [Next: Numeric types](../types/integer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
