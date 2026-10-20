# Logical operators

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`&&` and `||` short-circuit. In the program below, `bump` is called twice and `main` returns 2.

```c
var calls i32 = 0;

func bump() bool {
    calls = calls + 1;
    return true;
}

func main() i32 {
    if (false && bump()) {}
    if (true || bump()) {}
    if (true && bump()) {}
    if (false || bump()) {}
    return calls;
}
```

Both operands are `bool`. An integer operand is reported as `type mismatch: expected bool, found i32`. The result is `bool`. A shift is evaluated in the common type.

## Cost

`a && b` on two `bool` parameters masks and stores both, then tests `a`. When `a` is false the function executes 15 instructions including `retq`, and `b` is not tested. When `a` is true it also tests `b`, which is 6 more instructions. `a || b` has the same shape and skips `b` when `a` is true.

A call written as the right operand is skipped the same way, so its trace frame does not run. A shift of two locals is the 3-instruction shift on [Numeric types](../types/integer.md).

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
