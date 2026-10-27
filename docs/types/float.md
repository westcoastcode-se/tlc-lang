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
