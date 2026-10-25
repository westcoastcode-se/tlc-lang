# switch

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`switch` falls through until `break`. The value promotes to `i32`, so `i8` and `i16` match. `bool`, `u32`, `i64`, and `f32` are reported as `type mismatch: expected i32`. A `case` label is a constant `i32`. `case 1u32` is reported as `type mismatch`. Each value appears once, and `default` appears once.

A TLC enum switches as that enum. `case Color.Blue` matches a `Color`. `case 1` is `type mismatch`. The enum does not convert to `i32`. A `case` that is not a constant is `not supported yet`. Two labels with the same representation are `redefinition`.

An `extern(abi="c")` enum switches as `i32`. `case Flag.On` and `case 1` are that integer. A TLC enumerator in that `switch` is `type mismatch`. An enum is still not a condition. `if (value)` is `type mismatch: expected bool, found Color`.

```c
func main() i32 {
    var n i32 = 1;
    var x i32 = 0;
    switch (n) {
    case 1:
        x = 2;
    case 2:
        x = x + 40;
        break;
    default:
        x = 7;
        break;
    }
    return x;
}
```

```asm
main:
movl $1, -8(%rsp)                # constant
movl $0, -4(%rsp)                # constant
movl -8(%rsp), %eax              # move
movl %eax, -12(%rsp)             # move
subl $1, %eax                    # subtract
je .L1                           # branch
jmp .L2                          # jump
.L2:
movl -12(%rsp), %eax             # move
subl $2, %eax                    # subtract
je .L3                           # branch
jmp .L4                          # jump
.L5:
movl -4(%rsp), %eax              # move
retq                             # return
.L1:
movl $2, -4(%rsp)                # constant
.L3:
movl -4(%rsp), %eax              # move
addl $40, %eax                   # add
movl %eax, -4(%rsp)              # move
jmp .L5                          # jump
.L4:
movl $7, -4(%rsp)                # constant
jmp .L5                          # jump
```

The program returns 42. `break` leaves the innermost loop or `switch`. `continue` is valid inside loops.

```c
type Color enum {
    Red,
    Green = 40,
    Blue,
};

func code(value Color) i32 {
    switch (value) {
    case Color.Green:
        return 2;
    case Color.Blue:
        return 4;
    default:
        return 0;
    }
}
```

```asm
code:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
movl %eax, -8(%rsp)              # move
subl $40, %eax                   # subtract
je .L1                           # branch
jmp .L2                          # jump
.L2:
movl -8(%rsp), %eax              # move
subl $41, %eax                   # subtract
je .L3                           # branch
jmp .L4                          # jump
.L1:
movl $2, %eax                    # constant
retq                             # return
.L3:
movl $4, %eax                    # constant
retq                             # return
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
```

`code(Color.Blue)` returns 4. `case 41` on that `switch` is `type mismatch`.

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
