# trace

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `import "builtin"` and the call-stack text from `builtin.trace`.

## The compiler package

`import "builtin"` loads the compiler package.

The compiler's own copy is `lib/tlc/builtin/builtin.tlc`.

An editor shows the signatures in that file and the `//` comments above them.

It exports `trace`, `va`, `panic`, `memeql`, `memcmp`, and `Type`.

`builtin.Type` is the type of a type, described in [The Type type](../types/type.md).

`trace` returns `*const u8`.

`va` returns `*u8`.

Both take no arguments.

An argument is reported as `argument count`.

`panic` takes one message and does not return a value.

The import links the C library.

`import "builtin/env/consts"` loads `family`, described in [builtin/env/consts](family.md).

```c
import "builtin";
import "builtin" as b;
import { trace, va, panic } from "builtin";
```

```asm
main:
pushq %rax
callq trace                      # call trace
movq %rax, (%rsp)                # move
cmpq $0, (%rsp)                  # compare
jne .L1                          # branch
movl $1, %eax                    # constant
popq %rcx
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
xorl %eax, %eax                  # zero the result
popq %rcx
retq                             # return

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
xorl %edx, %edx                  # xorl
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
```

## How the names are spelled

The first form calls them as `builtin.trace()`, `builtin.va()`, and `builtin.panic(message)`.

The alias uses the new qualifier.

The selective form brings the names into the file.

A directory named `builtin` on the search path is that package.

The compiler package is used when the search finds none.

## The text `trace` returns

`builtin.trace()` returns `*const u8`.

The text lists the calls that have not returned, newest first.

A line is `file:line:column: name`.

The name is the function that made the call, and the location is that call.

The last line has no newline.

Calls of `builtin.trace` and `builtin.va` are omitted.

A call of `builtin.panic` is kept.

When no such call is still running, the text is empty.

```c
import "builtin";

extern(abi="c") func printf(fmt *const u8, ...) i32;

func leaf() i32 {
    printf("%s\n", *u8(builtin.trace()));
    return 1;
}

func mid() i32 {
    return leaf();
}

func main() i32 {
    return mid();
}
```

```asm
mid:
# trace frame for the call, then the matching pop
callq leaf                       # call leaf
retq                             # return

leaf:
subq $24, %rsp                   # subtract
callq trace                      # call trace
movq %rax, 8(%rsp)               # move
movl (%rip), %ecx                # move
movl %ecx, 16(%rsp)              # move
xorl %eax, %eax                  # zero the result
cmpl $32, %ecx                   # compare
movb %al, 23(%rsp)               # move
jae .L1                          # branch
movl 16(%rsp), %eax              # move
leaq (%rip), %rcx                # address
movslq %eax, %rdx                # move
imulq $24, %rdx, %rdx            # multiply
addq %rdx, %rcx                  # add
leaq (%rip), %rdx                # address
movq %rdx, (%rcx)                # move
movl $6, 8(%rcx)                 # constant
movl $5, 12(%rcx)                # constant
leaq (%rip), %rdx                # address
movq %rdx, 16(%rcx)              # move
addl $1, %eax                    # add
movl %eax, (%rip)                # move
movb $1, %al                     # constant
movb %al, 23(%rsp)               # move
.L1:
movq 8(%rsp), %rsi               # move
movb 23(%rsp), %al               # move
movb %al, 7(%rsp)                # move
leaq (%rip), %rdi                # address
movb $0, %al                     # constant
callq printf                     # call printf
movb 7(%rsp), %al                # move
testb $1, %al                    # test
jne .L2                          # branch
jmp .L3                          # jump
.L2:
movl (%rip), %eax                # move
subl $1, %eax                    # subtract
movl %eax, (%rip)                # move
.L3:
movl $1, %eax                    # constant
addq $24, %rsp                   # add
retq                             # return

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

`leaf` is calling `printf`, whose argument is the trace.

`mid` has called `leaf`, and `main` has called `mid`, so the program prints:

```text
main.tlc:11:12: mid
main.tlc:15:12: main
```

## How a line is named

The file name is the source path relative to the entry file's directory.

A source file outside that directory is named by its file name.

A method uses the method name.

An anonymous function is named `anon`.

Inside a `test`, the name is the test's name.

## How much text is kept

At most 32 calls are kept.

The text is at most 4095 bytes.

The pointer refers to this thread's buffer, and the next `builtin.trace()` on that thread replaces that text.

Another thread has its own frames and its own buffer.

## A function pointer and `defer`

`var f = builtin.trace` is a function pointer.

`defer builtin.trace()` runs the call when the deferred calls run.

```c
import "builtin";

func main() i32 {
    var f = builtin.trace;
    defer f();
    defer builtin.trace();
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # subtract
movq (%rip), %rax                # move
movq %rax, 8(%rsp)               # move
movb $1, 23(%rsp)                # constant
testb $1, 23(%rsp)               # test
jne .L1                          # branch
jmp .L2                          # jump
.L1:
callq trace                      # call trace
.L2:
xorl %eax, %eax                  # zero the result
addq $24, %rsp                   # add
retq                             # return

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

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
