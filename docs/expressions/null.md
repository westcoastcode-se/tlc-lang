# Pointers and null

[Index](../README.md) · [Previous: Logical operators](logical.md) · [Next: Narrowing](narrowing.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes pointer comparisons, `null`, and which pointers must be initialized.

## Comparing pointers

Pointers that point at the same type can be compared with `==` and `!=`, including `*T` with `?*T`.

A pointer can be compared with `null`, and `null == null` is allowed.

> **Warning:** The result of that comparison is `i32`.

Pointers of different types cannot be compared.

Relational operators on pointers are rejected.

The compiler reports `p == 0` as `type mismatch`.

## Which pointers may be `null`

`*T` has a value, and `?*T` may be `null`.

`null` may be assigned or returned only to `?*T`.

`&x` has type `*T` and may be stored in `*T`.

The compiler reports `var p ?*i32 = &x` as `type mismatch: expected ?*i32, found *i32`.

`var p = null` has no pointer type to take and is reported as `type mismatch`.

Write `var p ?*Point = null`.

A `*T` with no initializer is reported as `type mismatch`, and so is a struct or array that contains one.

That pointer has to be initialized.

An omitted `?*T` field of a struct value may be `null`.

An omitted `*T` field is reported as `type mismatch`.

A local `var p ?*i32` is null.

`extern` storage is not checked.

Assigning an integer, including `0`, to a pointer is reported as `type mismatch`.

An address that comes from an integer is a cast, `*i32(n)`.

Assigning `null` to `i32` or `bool` is reported as `type mismatch`.

`true` and `false` do not convert to pointers.

## Arithmetic and subscripts

Pointer arithmetic is reported as `type mismatch`.

A subscript index has type `i32`.

```c
func main() i32 {
    var n i32 = 1;
    var p *i32 = &n;
    var q ?*i32 = null;
    if (p != q && q == null) {
        return *p;
    }
    return 0;
}
```

```asm
main:
movl $1, -20(%rsp)               # constant
leaq -20(%rsp), %rax             # address
movq %rax, -16(%rsp)             # move
movq $0, -8(%rsp)                # constant
movq -16(%rsp), %rcx             # move
xorl %eax, %eax                  # zero the result
cmpq -8(%rsp), %rcx              # compare
movb %al, -21(%rsp)              # move
je .L1                           # branch
cmpq $0, -8(%rsp)                # compare
sete %al                         # flag to byte
movb %al, -21(%rsp)              # move
.L1:
movb -21(%rsp), %al              # move
testb $1, %al                    # test
jne .L2                          # branch
jmp .L3                          # jump
.L2:
movq -16(%rsp), %rax             # move
movl (%rax), %eax                # move
retq                             # return
.L3:
jmp .L4                          # jump
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Logical operators](logical.md) · [Next: Narrowing](narrowing.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
