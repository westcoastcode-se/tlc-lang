# Conditions

[Index](../README.md) · [Previous: Narrowing](narrowing.md) · [Next: Type values](type.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

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
