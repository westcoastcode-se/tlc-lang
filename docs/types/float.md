# void, bool, and floating-point types

[Index](../README.md) · [Previous: Pointers](pointer.md) · [Next: Literals](literal.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `void`, `bool`, and the floating-point types `f32` and `f64`.

## void

`void` is a return type and a pointee, and it is neither a value nor a variable type.

## bool

A `bool` value is 0 or 1, and a local `bool` with no initializer is false.

An initializer written in the source replaces that value.

## f32 and f64

`f32` is the 32-bit floating type.

`f64` is the 64-bit floating type.

A literal without a suffix has type `f64`, and a suffix `f` or `F` makes it `f32`.

`1.5 + 1` is `f64`.

Comparisons of floating values yield `bool`.

The compiler reports `&&` and `||` on a floating value as `type mismatch: expected bool`.

The compiler reports `%` and the bitwise operators on a floating value as `type mismatch`.

A file-scope `var` or `const` may hold either type, as in `var scale = 1.5f` and `const bias = 0.5f`.

A local `f32` or `f64` with no initializer is 0.0, and a written initializer replaces that value.

An integer becomes a float where a float is expected: `var x f64 = 2` is 2.0, and `1.5 + 1` adds 1.0.

One float width becomes the other where it is expected: `var x f32 = 1.5` rounds to `f32`, and `var d f64 = x` widens. A `return` converts the same way: `return 2` and `return 1.5` in a function that returns `f32`, and `return 1, 2.5` into `(f64, f32)`. A tuple element does too: `var u (f64, i32) = (1, 2)` holds 1.0.

A float becomes an integer or a `bool` only through a cast. `i32(2.5)` is 2 and `i32(-2.5)` is -2: the cast drops the fraction. `bool(0.5)` is true, as is any value but 0.

Without the cast the compiler reports `type mismatch: expected i32, found f64`. That holds for an initializer, `var n i32 = 2.5` and `var g i32 = 2.5` at file scope, a store `n = 1.5f`, a field `Point{.x = 2.5}`, a tuple element `(1, 2.5)` into `(i32, u8)`, a call result `var n i64 = give()` when `give` returns `f64`, and an argument `take(2.5)` to `n i32`, also to a `comptime` parameter and to a `comptime func`. `return 2.5` in a function that returns `i32` is `return type: expected i32, found f64`.

## A bool compared with two floats

```c
func skip() void {
    return;
}

func main() i32 {
    var a bool = true;
    var x f32 = 1.5f;
    var y f64 = 20.5;
    skip();
    if (a && x < y) {
        return 1;
    }
    return 0;
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq skip                       # call skip
retq                             # return

skip:
retq                             # return
```

[Index](../README.md) · [Previous: Pointers](pointer.md) · [Next: Literals](literal.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
