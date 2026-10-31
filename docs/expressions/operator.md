# Expressions

[Index](../README.md) · [Previous: Keywords](../keyword.md) · [Next: Logical operators](logical.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the operators you can write in an expression.

## The operator set

The arithmetic operators are `+ - * / %`, and the bitwise operators are `& | ^ ~ << >>`.

`&& || !` are the logical operators, and the comparisons are operators too.

Unary `+` and `-` are operators, and so is assignment `=`.

`++n` adds 1 and yields the new value. `n++` yields the value `n` had, then adds 1.

The operand is an integer lvalue. A `const` binding is `type mismatch`. A value that is not a place is `not an lvalue`. The result is not a place. `--` is `not supported yet`.

Calls, indexing, `.`, address-of `&`, and dereference `*` are operators too.

## Binding and associativity

Binding and associativity are given in the [grammar](../grammar.md#expressions).

```c
func main() i32 {
    var n i32 = 6;
    var p *i32 = &n;
    n = -n + 8;
    n = n * 3 / 2 % 5;
    n = n << 1 >> 1;
    n = (n & 7) | 1;
    n = n ^ 1;
    n = ~n;
    if (n < 0 && *p != 0) {
        return 1;
    }
    return 0;
}
```

```asm
main:
movl $6, -12(%rsp)               # constant
leaq -12(%rsp), %rax             # address
movq %rax, -8(%rsp)              # move
xorl %eax, %eax                  # zero the result
subl -12(%rsp), %eax             # subtract
addl $8, %eax                    # add
movl %eax, -12(%rsp)             # move
imull $3, -12(%rsp), %eax        # multiply
movl $2, %ecx                    # constant
cltd
idivl %ecx
movl $5, %ecx                    # constant
cltd
idivl %ecx
movl %edx, -12(%rsp)             # move
movl -12(%rsp), %eax             # move
shll %eax                        # shll
sarl %eax                        # sarl
movl %eax, -12(%rsp)             # move
movl -12(%rsp), %eax             # move
andl $7, %eax                    # andl
orl $1, %eax                     # orl
movl %eax, -12(%rsp)             # move
movl -12(%rsp), %eax             # move
xorl $1, %eax                    # xorl
movl %eax, -12(%rsp)             # move
movl -12(%rsp), %eax             # move
xorl $-0x1, %eax                 # xorl
movl %eax, -12(%rsp)             # move
xorl %eax, %eax                  # zero the result
cmpl $0, -12(%rsp)               # compare
movb %al, -13(%rsp)              # move
jge .L1                          # branch
movq -8(%rsp), %rax              # move
cmpl $0, (%rax)                  # compare
setne %al                        # flag to byte
movb %al, -13(%rsp)              # move
.L1:
movb -13(%rsp), %al              # move
testb $1, %al                    # test
jne .L2                          # branch
jmp .L3                          # jump
.L2:
movl $1, %eax                    # constant
retq                             # return
.L3:
jmp .L4                          # jump
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Keywords](../keyword.md) · [Next: Logical operators](logical.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
