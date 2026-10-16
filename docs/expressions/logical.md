# Logical operators

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`&&` and `||` short-circuit. In the program below, `bump` is called twice and `main` returns 2.

```c
var calls i32 = 0;

func bump() i32 {
    calls = calls + 1;
    return 1;
}

func main() i32 {
    if (0 && bump()) {}
    if (1 || bump()) {}
    if (1 && bump()) {}
    if (0 || bump()) {}
    return calls;
}
```

A shift is evaluated in the common type.

## Cost

`a && b` stores both `i32` parameters, then compares `a` with 0. When `a` is 0 the function executes 10 instructions including `retq`, and the compare of `b` does not run. When `a` is not 0 it also compares `b`, which is 3 more instructions. `a || b` has the same shape and skips `b` when `a` is not 0.

A call written as the right operand is skipped the same way, so its trace frame does not run. A shift of two locals is the 3-instruction shift on [Numeric types](../types/integer.md).

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
