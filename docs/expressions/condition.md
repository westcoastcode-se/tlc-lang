# Conditions

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A condition in `if`, `while`, `do`, and `for` is a `bool`. An integer is reported as `type mismatch: expected bool, found i32`. The comparison is written out, as in `if (n != 0)`. A pointer used as a condition is reported as `compare with null`. An `f32` is reported as `type mismatch: expected bool, found f32`. An enum is not a condition, including `extern(abi="c")`. `if (value)` when `value` is `Color` is `type mismatch: expected bool, found Color`. The comparison is `if (value == Color.Blue)`. `&&`, `||`, and prefix `!` require a `bool` and yield a `bool`. `assert` checks a constant `bool`.

```c
func main() i32 {
    var n i32 = 1;
    if (n != 0) {
        return n;
    }
    return 0;
}
```

## Cost

`if (n != 0)` on an `i32` local is `cmpl $0` and a conditional jump: 2 instructions, after the store that put a value in `n`. A true branch that returns a constant is 1 `movl` and `retq`. An `else` that is reached executes its own return. A `bool` condition reloads the byte, masks it, zero-extends it, and then does that compare: 4 instructions before the branch. A pointer condition is rejected while compiling.

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
