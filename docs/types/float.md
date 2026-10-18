# void, bool, and floating-point types

[Index](../README.md) · [Previous: Pointers](pointer.md) · [Next: Literals](literal.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`void` is a return type and a pointee. It is neither a value nor a variable type.

A `bool` value is 0 or 1.

`f32` is the 32-bit floating type. `f64` is the 64-bit floating type. A literal without a suffix has type `f64`. A suffix `f` or `F` makes it `f32`. `1.5 + 1` is `f64`. Comparisons of floating values yield `i32`. `&&` and `||` on a floating value also yield `i32`. `%` and the bitwise operators on a floating value are reported as `type mismatch`. A file-scope `var` or `const` may hold either type, as in `var scale = 1.5f` and `const bias = 0.5f`. A local `bool` with no initializer is false. A local `f32` or `f64` with no initializer is 0.0. An initializer written in the source replaces that value.

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

## Cost

An empty `void` function executes 1 instruction, `retq`. Calling it still pushes a trace frame: 36 instructions in the caller when fewer than 32 frames are stored.

A `bool` parameter is stored as one byte. Using it as a condition reloads that byte, masks it, zero-extends it, and compares with 0. That is 4 instructions before the branch. Storing an integer into a `bool` is `test` and `setne`.

`f32` addition of two parameters is 2 `movss` stores on entry, then 1 `movss` and 1 `addss`. `f64` addition is the same shape with `movsd` and `addsd`. `a < b` on `f64` is 2 reloads, `ucomisd`, `seta`, a mask, and a zero-extend: 6 instructions after the parameters have been stored. A floating constant in `return` is folded the same way an integer constant is.

[Index](../README.md) · [Previous: Pointers](pointer.md) · [Next: Literals](literal.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
