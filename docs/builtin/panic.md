# panic

[Index](../README.md) · [Previous: va](va.md) · [Next: memeql and memcmp](mem.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `builtin.panic`, which prints a message and aborts the process.

## What gets printed

`builtin.panic(message)` prints the message and the call stack, then aborts the process.

`new` calls it with `out of memory` when allocation returns `null`.

Postfix `!` calls it with `null pointer` when that pointer is `null`.

The message is `*const u8`.

A string and a `*u8` may be passed.

The text goes to the standard error stream:

```text
panic: message
```

## The stack

The stack follows, newest first, one line per call.

A line is `file:line:column: name`.

The name is the function that made the call.

The first stack line is the function that called `builtin.panic`, at that call.

The stack is the calls of the thread that called `builtin.panic`.

`builtin.trace()` lists the same calls in that order.

A null pointer is printed as `(null)`.

At most 32 calls are kept.

## Rejected calls

No argument, or more than one, is reported as `argument count`.

A value that is not a pointer to `u8` is reported as `type mismatch`.

The call has no result, so `var n = builtin.panic("out of memory")` is `type mismatch`.

```c
import "builtin";

func leaf() void {
    builtin.panic("out of memory");
}

func mid() void {
    leaf();
}

func main() i32 {
    mid();
    return 0;
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq mid                        # call mid
retq                             # return

mid:
# trace frame for the call, then the matching pop
callq leaf                       # call leaf
retq                             # return

leaf:
# trace frame for the call, then the matching pop
callq panic                      # call panic
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
```

## What this program does

`leaf` is calling `panic`, `mid` has called `leaf`, and `main` has called `mid`.

The process aborts.

`tlc --run` exits with status 128, and the standard error stream is:

```text
panic: out of memory
main.tlc:4:5: leaf
main.tlc:8:5: mid
main.tlc:12:5: main
```

## Where the names come from

The file name is the source path relative to the entry file's directory, as for `builtin.trace`.

`var f = builtin.panic` is a function pointer, and `f("out of memory")` aborts at that call.

`defer builtin.panic("out of memory")` aborts when the deferred calls run.

[Index](../README.md) · [Previous: va](va.md) · [Next: memeql and memcmp](mem.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
