# Expressions

[Index](../README.md) · [Previous: Keywords](../keyword.md) · [Next: Logical operators](logical.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the operators you can write in an expression.

## The operator set

The arithmetic operators are `+ - * / %`, and the bitwise operators are `& | ^ ~ << >>`.

`&& || !` are the logical operators, and the comparisons are operators too.

`value!.field ?? fallback` yields `fallback` when a pointer in that `!` chain is null. That `!` does not call `builtin.panic("null pointer")`.

Unary `+` and `-` are operators, and so is assignment `=`.

`++n` adds 1 and yields the new value. `n++` yields the value `n` had, then adds 1.

`--n` subtracts 1 and yields the new value. `n--` yields the value `n` had, then subtracts 1.

The operand is an integer lvalue. A `const` binding is `type mismatch`. A value that is not a place is `not an lvalue`. The result is not a place.

`n += m`, `n -= m`, `n *= m`, `n /= m`, `n %= m`, `n &= m`, `n |= m`, `n ^= m`, `n <<= m`, and `n >>= m` apply that operation and store the result in `n`. The value is the new `n`. The place is read once.

A signed `>>=` keeps the sign. An unsigned `>>=` shifts in zeros. `(-2147483647 - 1) %= -1` stores 0.

```c
func main() i32 {
    var n = 43;
    n %= 2;
    n <<= 1;
    n |= 40;
    return n;
}
```

```asm
main:
movl $43, -4(%rsp)               # constant
movl -4(%rsp), %eax              # move
movl $2, %ecx                    # constant
cltd
idivl %ecx
movl %edx, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
shll %eax                        # shll
movl %eax, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
orl $40, %eax                    # orl
movl %eax, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

`cond ? then : else` yields `then` when `cond` is true and `else` when it is false. `cond` is a `bool`. An integer is `type mismatch: expected bool, found i32`. A pointer is `compare with null`.

Both arms are typechecked. Only the chosen arm runs.

The arms have the same type, or both are integers and the result is their common type. `null` may be one arm when the other is an optional pointer.

`?:` is right-associative and looser than `??`. `p ?? q ? 1 : 2` is `(p ?? q) ? 1 : 2`.

The middle arm is an expression. `flag ? n = 1 : 2` stores `1` in `n` when `flag` is true. `flag ? 1 : n = 2` assigns the chosen value, and that value is `not an lvalue`.

`p != null ? p.x : 0` narrows `p` in the then-arm. `p == null ? 0 : p.x` narrows `p` in the else-arm.

`return true ? 40 : 2` is `movl $40, %eax` and `retq`.

```c
func main() i32 {
    var flag = true;
    return flag ? 40 : 2;
}
```

```asm
main:
movl $1, -4(%rsp)                # flag = true
cmpl $0, -4(%rsp)                # flag
je .L1                           # branch
movl $40, %eax                   # 40
movl %eax, -8(%rsp)              # chosen
jmp .L2                          # jump
.L1:
movl $2, %eax                    # 2
movl %eax, -8(%rsp)              # chosen
jmp .L2                          # jump
.L2:
movl -8(%rsp), %eax              # return
retq
```

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
