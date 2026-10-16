# extern

[Index](../README.md) · [Previous: Visibility](visibility.md) · [Next: Numeric types](../types/integer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`extern` and `extern(abi="tlc")` name a symbol from a tlc library. The symbol is `qualifier.name`. The calling convention is the compiler's own, so the function may return several results.

`extern(abi="c")` names a symbol from a C library. It keeps the C name and the C calling convention, and it returns one result. Linking a program that uses `extern(abi="c")` requires a C library on the link line, either `-l` or a library file such as `libfoo.a`. `--check` and `--emit-*` do not link, so they accept the declaration. Without that library the link step reports `extern(abi="c") requires a C library`.

A parameter or result of an `extern(abi="c")` function cannot be a function pointer that returns several results. A struct used by such a function, by value or through a pointer, must itself be `extern(abi="c")`. That includes a struct nested inside it. An `extern(abi="c") struct` uses the platform C memory layout, so its fields line up with a static or shared library. `bool` in that struct is one byte. On this host a struct of at most 16 bytes is passed in registers, and a larger struct is passed as a pointer. A function that is not `extern(abi="c")` keeps the compiler's own convention for struct values.

```c
type Point struct {
    x i32;
    y i32;
};

extern(abi="tlc") func parts(p Point) (i32, i32) {
    return p.x, p.y;
}

func main() i32 {
    var p Point;
    p.x = 20;
    p.y = 22;
    a, b := parts(p);
    return a + b;
}
```

```c
extern(abi="c") func printf(fmt *const u8, ...) i32;

func main() i32 {
    printf("hi\n");
    return 0;
}
```

`*const T` on a parameter, including an `extern(abi="c")` parameter, is a pointer to constant `T`. A variadic function may be declared with `extern(abi="c")` and without a body. A function with a body may use `...`, and `builtin.va()` reads the extra arguments.

## Cost

`extern(abi="c") func printf(fmt *const u8, ...) i32` followed by `printf("hi\n")` executes 36 instructions in `main` on the path where the trace holds fewer than 32 frames. The trace frame is the same one a tlc call pushes. The format string is one `leaq`, and the variadic call also executes `movb $0, %al` because no floating argument is passed. `printf` itself is outside this count.

`extern(abi="tlc")` uses the same trace frame. A struct of at most 16 bytes is passed in registers under `extern(abi="c")`. Each `i32` field is one `movl` into an argument register after the value has been loaded. A larger struct is passed as a pointer.

[Index](../README.md) · [Previous: Visibility](visibility.md) · [Next: Numeric types](../types/integer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
