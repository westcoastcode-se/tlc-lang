# Conditions

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the condition that `if`, `while`, `do`, and `for` test.

## A condition is a `bool`

A condition in `if`, `while`, `do`, and `for` is a `bool`.

The compiler reports an integer as `type mismatch: expected bool, found i32`.

The comparison is written out, as in `if (n != 0)`.

The compiler reports a pointer used as a condition as `compare with null`.

The compiler reports an `f32` as `type mismatch: expected bool, found f32`.

An enum is not a condition, including `extern(abi="c")`.

`if (value)` when `value` is `Color` is `type mismatch: expected bool, found Color`.

The comparison is `if (value == Color.Blue)`.

`&&`, `||`, and prefix `!` require a `bool` and yield a `bool`.

`assert` checks a constant `bool`.

```c
func main() i32 {
    var n i32 = 1;
    if (n != 0) {
        return n;
    }
    return 0;
}
```

```asm
main:
movl $1, -4(%rsp)                # constant
cmpl $0, -4(%rsp)                # compare
je .L1                           # branch
movl -4(%rsp), %eax              # move
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
