# void, bool, and floating-point types

[Index](../README.md) · [Previous: Pointers](pointer.md) · [Next: Literals](literal.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`void` is a return type and a pointee. It is neither a value nor a variable type.

A `bool` value is 0 or 1.

`f32` is the 32-bit floating type. `f64` is the 64-bit floating type. A literal without a suffix has type `f64`. A suffix `f` or `F` makes it `f32`. `1.5 + 1` is `f64`. Comparisons of floating values yield `bool`. `&&` and `||` on a floating value are reported as `type mismatch: expected bool`. `%` and the bitwise operators on a floating value are reported as `type mismatch`. A file-scope `var` or `const` may hold either type, as in `var scale = 1.5f` and `const bias = 0.5f`. A local `bool` with no initializer is false. A local `f32` or `f64` with no initializer is 0.0. An initializer written in the source replaces that value.

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

## Assembly

```asm
main:
# trace frame for the call, then the matching pop
callq skip                       # call skip
retq                             # return

skip:
retq                             # return
```

[Index](../README.md) · [Previous: Pointers](pointer.md) · [Next: Literals](literal.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
