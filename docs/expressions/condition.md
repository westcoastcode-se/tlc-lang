# Conditions

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A condition in `if`, `while`, `do`, and `for` is an integer after promotion. A pointer used as a condition is reported as `compare with null`. `switch` requires exactly `i32`.

```c
func main() i32 {
    var n i32 = 1;
    if (n) {
        return n;
    }
    return 0;
}
```

## Cost

`if (n)` on an `i32` local is `cmpl $0` and a conditional jump: 2 instructions, after the stores that put a value in `n`. A true branch that returns a constant is 1 `movl` and `retq`. An `else` that is reached executes its own return. A `bool` condition reloads the byte, masks it, zero-extends it, and then does that compare: 4 instructions before the branch. A pointer condition is rejected while compiling.

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
