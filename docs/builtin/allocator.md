# allocator

[Index](../README.md) · [Previous: trace](trace.md) · [Next: va](va.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the `Allocator` trait and the `new` and `delete` expressions that use it.

## Methods a struct defines

`Allocator` is a built-in trait.

A struct satisfies it by defining these methods, each with a pointer receiver:

```c
unsafe func malloc(self *this, size i32, align i32) ?*u8;
func free(self *this, ptr *u8) void;
unsafe func realloc(self *this, ptr *u8, size i32, align i32) ?*u8;
```

```asm
main:
subq $24, %rsp                   # subtract
movl $4, %edi                    # constant
callq malloc                     # call malloc
movq %rax, 8(%rsp)               # move
cmpq $0, %rax                    # compare
jne .L1                          # branch
movl (%rip), %eax                # move
movl %eax, 4(%rsp)               # move
cmpl $32, %eax                   # compare
jb .L2                           # branch
jmp .L3                          # jump
.L1:
movq 8(%rsp), %rax               # move
movq %rax, 16(%rsp)              # move
movq 16(%rsp), %rax              # move
movl $40, (%rax)                 # constant
movq 16(%rsp), %rdi              # move
callq free                       # call free
movl $40, %eax                   # constant
addq $24, %rsp                   # add
retq                             # return
.L2:
movl 4(%rsp), %eax               # move
leaq (%rip), %rcx                # address
movslq %eax, %rdx                # move
imulq $24, %rdx, %rdx            # multiply
addq %rdx, %rcx                  # add
leaq (%rip), %rdx                # address
movq %rdx, (%rcx)                # move
movl $2, 8(%rcx)                 # constant
movl $13, 12(%rcx)               # constant
leaq (%rip), %rdx                # address
movq %rdx, 16(%rcx)              # move
addl $1, %eax                    # add
movl %eax, (%rip)                # move
.L3:
leaq (%rip), %rdi                # address
callq panic                      # call panic
```

## Which methods are unsafe

`malloc` and `realloc` are unsafe.

The struct methods repeat `unsafe`.

`free` does not.

A struct that defines those three also gains the default `Allocator.malloc[T]`:

```c
unsafe func Allocator::malloc[T type](self *this) ?*T {
    var value = self.malloc(sizeof(T), alignof(T));
    return ?*T(value);
}
```

## The default typed allocation

Pasting this default beside the compiler's `Allocator` is `redefinition` and emits nothing.

A struct that does not define `malloc[T]` calls this body.

The call is `unsafe arena.malloc[i32]()`.

The same call through a trait parameter is `unsafe a.malloc[i32]()` when `a` is `*Allocator`.

The result is `?*T`, and the caller handles `null`.

A struct method `unsafe func Arena::malloc[T type](self *this) ?*T` wins over that default.

## The `Malloc` value

`import "builtin"` also provides `allocator`, a `Malloc` value.

`Malloc.malloc`, `Malloc.free`, and `Malloc.realloc` are declared in `lib/tlc/builtin/builtin.tlc`.

`new T` uses that allocator, and so does `delete p`.

`new(&builtin.allocator) T` and `delete(&builtin.allocator) p` name it.

The argument is a pointer, and the pointee type is taken from that pointer.

`Malloc` calls the C functions `malloc`, `free`, and `realloc`.

`malloc` returns `null` when it cannot reserve the bytes.

`new` then calls `builtin.panic("out of memory")`.

The result of `new` is `*T`.

## What `new` stores

`new T` reserves `sizeof(T)` bytes and yields `*T`.

`new i32` does not store the reserved integer.

`new Point` stores each field: the value written on the field, or zero when the field has none.

`new T{.field = expr}` and `new(&arena) T{.field = expr}` write those fields after the block is reserved.

A field that is left out takes that same value.

`new Point{1, 2}` is a value list and is reported as `type mismatch`.

An initializer on a type that is not a struct, such as `new i32{1}`, is reported as `type mismatch`.

`new Node` is reported as `type mismatch` when `Node` contains a `*T` that has no value.

That field has to be named.

`delete` passes that pointer to `free` and has no result.

A file-scope `new` is reported as `global initializer is not a constant`.

`malloc` and `realloc` are unsafe calls.

`free` is an ordinary call.

`new` and `delete` are not unsafe calls.

`malloc` and `realloc` return `?*u8`, and the caller handles `null`.

`new(&arena) T` does not make the caller handle `null`.

A `null` result from that `malloc` calls `builtin.panic("out of memory")`.

A `?*T` field that is left out is null when the field has no value.

A cast of a pointer to a struct or union, or to a different primitive, is [`unsafe`](../expressions/unsafe.md).

## An array of elements

`new [n]T` and `new(&arena) [n]T` reserve `n` elements of `T` and yield `*T`, a pointer to the first element.

`n` has type `i32`.

`buf[i]` reads and writes element `i`.

There is no bounds check.

The elements of `new [n]i32` are not stored.

`new [n]Point` stores each field of each element.

`delete buf` frees the block.

A constant `n` that is not positive is reported as `type mismatch`.

Any other `n` that is not positive aborts.

`new []T` is reported as `expected an expression`.

## One value in every element

`new [n]Point{{.x = 1}...}` stores that value in every element.

The value is evaluated once.

The value is a struct value, so a field it leaves out takes the value written on the field, or zero when the field has none.

A field left out of `new Point{.x = 1}` takes that same value.

`new [2]Point{{.x = 1}, {.y = 2}}` stores one value per element when the constant count matches the list.

A runtime count with a list and no `...` is reported as `type mismatch`.

`new [n]Point{.x = 1}` is that same mismatch: the element list is another pair of braces.

`new [_]Point{{.x = 1, .y = 2}, {.y = 10}}` takes the length from the list, so the reserved object is `[2]Point` and the result is `*[2]Point`.

`new [_]Point{{.x = 1}...}` has no length to repeat and is reported as `type mismatch`.

`new [n]Node` is reported as `type mismatch` when an element contains a `*T` and no value sets that field.

An element value that sets that field is accepted.

## A generic type

`new Box[i32]` and `new Box[i32]{.value = 1}` allocate one specialization.

The fields are the fields of that specialization.

`new i32[4]` is reported as `type mismatch`.

The buffer is `new [4]i32`.

```c
import "builtin";

type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var p *Point = new Point{.x = 40, .y = 2};
    var sum i32 = p.x + p.y;
    delete(&builtin.allocator) p;
    return sum;
}
```

```asm
main:
subq $40, %rsp                   # subtract
movl $8, %edi                    # constant
callq malloc                     # call malloc
movq %rax, 16(%rsp)              # move
cmpq $0, %rax                    # compare
jne .L1                          # branch
movl (%rip), %eax                # move
movl %eax, 12(%rsp)              # move
cmpl $32, %eax                   # compare
jb .L2                           # branch
jmp .L3                          # jump
.L1:
movq 16(%rsp), %rax              # move
movl $40, (%rax)                 # constant
movl $2, 4(%rax)                 # constant
movq %rax, 24(%rsp)              # move
movq 24(%rsp), %rax              # move
movl (%rax), %eax                # move
movq 24(%rsp), %rcx              # move
addl 4(%rcx), %eax               # add
movl %eax, 36(%rsp)              # move
movq 24(%rsp), %rdi              # move
callq free                       # call free
movl 36(%rsp), %eax              # move
addq $40, %rsp                   # add
retq                             # return
.L2:
movl 12(%rsp), %eax              # move
leaq (%rip), %rcx                # address
movslq %eax, %rdx                # move
imulq $24, %rdx, %rdx            # multiply
addq %rdx, %rcx                  # add
leaq (%rip), %rdx                # address
movq %rdx, (%rcx)                # move
movl $9, 8(%rcx)                 # constant
movl $20, 12(%rcx)               # constant
leaq (%rip), %rdx                # address
movq %rdx, 16(%rcx)              # move
addl $1, %eax                    # add
movl %eax, (%rip)                # move
.L3:
leaq (%rip), %rdi                # address
callq panic                      # call panic

realloc:
subq $40, %rsp                   # subtract
movq %rsi, 16(%rsp)              # move
movl %ecx, 28(%rsp)              # move
movl %edx, %eax                  # move
movq %rax, 32(%rsp)              # move
cmpq $0, %rsi                    # compare
jne .L1                          # branch
movl 28(%rsp), %eax              # move
cmpl $16, %eax                   # compare
jg .L2                           # branch
jmp .L3                          # jump
.L1:
movq 32(%rsp), %rsi              # move
movq 16(%rsp), %rdi              # move
callq realloc                    # call realloc
addq $40, %rsp                   # add
retq                             # return
.L3:
movq 32(%rsp), %rdi              # move
callq malloc                     # call malloc
movq %rax, 8(%rsp)               # move
jmp .L4                          # jump
.L2:
movq 32(%rsp), %rax              # move
movl 28(%rsp), %ecx              # move
movl %ecx, %ecx                  # move
movl %ecx, %edi                  # move
movq %rdi, %rcx                  # move
subq $1, %rcx                    # subtract
addq %rcx, %rax                  # add
xorl %ecx, %ecx
movl %ecx, %edx                  # move
divq %rdi
movq %rax, %rsi                  # move
imulq %rdi, %rsi                 # multiply
callq aligned_alloc              # call aligned_alloc
movq %rax, 8(%rsp)               # move
.L4:
movq 8(%rsp), %rax               # move
addq $40, %rsp                   # add
retq                             # return

malloc:
subq $24, %rsp                   # subtract
movl %edx, 12(%rsp)              # move
movl %esi, %eax                  # move
movq %rax, 16(%rsp)              # move
cmpl $16, %edx                   # compare
jg .L1                           # branch
movq 16(%rsp), %rdi              # move
callq malloc                     # call malloc
movq %rax, (%rsp)                # move
jmp .L2                          # jump
.L1:
movq 16(%rsp), %rax              # move
movl 12(%rsp), %ecx              # move
movl %ecx, %ecx                  # move
movl %ecx, %edi                  # move
movq %rdi, %rcx                  # move
subq $1, %rcx                    # subtract
addq %rcx, %rax                  # add
xorl %ecx, %ecx
movl %ecx, %edx                  # move
divq %rdi
movq %rax, %rsi                  # move
imulq %rdi, %rsi                 # multiply
callq aligned_alloc              # call aligned_alloc
movq %rax, (%rsp)                # move
.L2:
movq (%rsp), %rax                # move
addq $24, %rsp                   # add
retq                             # return

panic:
subq $24, %rsp                   # subtract
movq %rdi, %rdx                  # move
leaq (%rip), %rax                # address
cmpq $0, %rdx                    # compare
cmoveq %rax, %rdx
movl $2, %edi                    # constant
leaq (%rip), %rsi                # address
movb $0, %al                     # constant
callq dprintf                    # call dprintf
movl (%rip), %eax                # move
movl %eax, 20(%rsp)              # move
.L2:
movl 20(%rsp), %eax              # move
movl %eax, 16(%rsp)              # move
cmpl $0, %eax                    # compare
je .L1                           # branch
movl 16(%rsp), %ecx              # move
subl $1, %ecx                    # subtract
movl %ecx, 12(%rsp)              # move
leaq (%rip), %rax                # address
movslq %ecx, %rcx                # move
imulq $24, %rcx, %rcx            # multiply
addq %rcx, %rax                  # add
movq (%rax), %rdx                # move
movl 8(%rax), %ecx               # move
movl 12(%rax), %r8d              # move
movq 16(%rax), %r9               # move
movl $2, %edi                    # constant
leaq (%rip), %rsi                # address
movb $0, %al                     # constant
callq dprintf                    # call dprintf
movl 12(%rsp), %eax              # move
movl %eax, 20(%rsp)              # move
jmp .L2                          # jump
.L1:
callq abort                      # call abort

trace:
subq $40, %rsp                   # subtract
movb $0, (%rip)                  # constant
movl (%rip), %ecx                # move
xorl %eax, %eax                  # zero the result
movl %ecx, 32(%rsp)              # move
movl %eax, 36(%rsp)              # move
.L2:
movl 32(%rsp), %eax              # move
movl 36(%rsp), %ecx              # move
movl %ecx, 24(%rsp)              # move
movl %eax, 28(%rsp)              # move
cmpl $0, %eax                    # compare
je .L1                           # branch
movl 24(%rsp), %ecx              # move
movl $4096, %eax                 # constant
subl %ecx, %eax                  # subtract
movl %eax, 20(%rsp)              # move
cmpl $1, %eax                    # compare
jle .L1                          # branch
movl 24(%rsp), %r10d             # move
movl 20(%rsp), %esi              # move
movl 28(%rsp), %r11d             # move
subl $1, %r11d                   # subtract
movl %r11d, 12(%rsp)             # move
leaq (%rip), %rax                # address
movslq %r11d, %rcx               # move
imulq $24, %rcx, %rcx            # multiply
addq %rcx, %rax                  # add
movq (%rax), %rcx                # move
movl 8(%rax), %r8d               # move
movl 12(%rax), %r9d              # move
movq 16(%rax), %rax              # move
leaq (%rip), %rdx                # address
leaq (%rip), %rdi                # address
cmpl $0, %r11d                   # compare
cmoveq %rdi, %rdx
leaq (%rip), %rdi                # address
movslq %r10d, %r10               # move
addq %r10, %rdi                  # add
movl %esi, %esi                  # move
movq %rax, (%rsp)                # move
movb $0, %al                     # constant
callq snprintf                   # call snprintf
movl 20(%rsp), %ecx              # move
movl %eax, %esi                  # move
movl 24(%rsp), %eax              # move
xorl %edx, %edx
cmpl $0, %esi                    # compare
cmovgl %esi, %edx
subl $1, %ecx                    # subtract
cmpl %ecx, %edx                  # compare
cmovll %edx, %ecx
addl %ecx, %eax                  # add
movl %eax, 16(%rsp)              # move
movl 16(%rsp), %eax              # move
movl 12(%rsp), %ecx              # move
movl %ecx, 32(%rsp)              # move
movl %eax, 36(%rsp)              # move
jmp .L2                          # jump
.L1:
leaq (%rip), %rax                # address
addq $40, %rsp                   # add
retq                             # return

free:
pushq %rax
movq %rsi, (%rsp)                # move
movq %rdi, %rax                  # move
movq (%rsp), %rdi                # move
callq free                       # call free
popq %rax
retq                             # return
```

[Index](../README.md) · [Previous: trace](trace.md) · [Next: va](va.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
