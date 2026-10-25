# Logical operators

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

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

## Assembly

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

[Index](../README.md) · [Previous: Expressions](operator.md) · [Next: Pointers and null](null.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
