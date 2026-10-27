# Logical operators

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `&&` and `||`, which decide the result without always evaluating both operands.

## Short-circuit

`&&` and `||` short-circuit, so in the program below `bump` is called twice and `main` returns 2.

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

```asm
main:
# trace frame for the call, then the matching pop
callq bump                       # call bump
retq                             # return

bump:
movl (%rip), %eax                # move
incl %eax
movl %eax, (%rip)                # move
movb $1, %al                     # constant
retq                             # return
```

## Operand types

Both operands are `bool`, and the result is `bool`.

The compiler reports an integer operand as `type mismatch: expected bool, found i32`.

A shift is evaluated in the common type.

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
