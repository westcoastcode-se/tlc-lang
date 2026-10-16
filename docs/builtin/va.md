# va

[Index](../README.md) · [Previous: allocator](allocator.md) · [Next: panic](panic.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`builtin.va()` reads the extra arguments of the variadic function it is called from. The pointer is the `ap` that `vprintf` takes. Each call reads those arguments from the start.

```c
import "builtin";

extern(abi="c") func vprintf(fmt *const u8, ap *u8) i32;

func log(fmt *const u8, ...) i32 {
    return vprintf(fmt, builtin.va());
}

func main() i32 {
    log("n=%d\n", 40);
    return 0;
}
```

The program prints `n=40`. An extra argument is an integer, a floating-point value, a pointer, an enum, or `null`. A struct is reported as `not supported yet`. An integer narrower than `i32` is passed as `i32`. `f32` is passed as `f64`.

`builtin.va()` outside a variadic function is reported as `not supported yet`. So is `defer builtin.va()`, and so is using the name without calling it: `builtin.va` and `var p = builtin.va`.

## Cost

`builtin.va()` does not push a trace frame. It saves the incoming argument registers and fills a `va_list`. In a variadic function with no floating arguments, saving those registers, writing the list, and returning whether the pointer is null executes 24 instructions. The eight xmm registers are saved only when the caller passed floating arguments. An integer narrower than `i32` is widened to `i32` at the call that passed it. An `f32` argument is widened to `f64` at that call. A struct argument is rejected while compiling.

[Index](../README.md) · [Previous: allocator](allocator.md) · [Next: panic](panic.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
