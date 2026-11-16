# panic

[Index](../README.md) · [Previous: va](va.md) · [Next: atomic](atomic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `builtin.panic`, which prints a message and aborts the process.

## What gets printed

`builtin.panic(message)` prints the message and the call stack, then aborts the process.

`new` calls it with `out of memory` when allocation returns `null`.

`new [n]T` calls it with `count must be positive` when `n` is negative and not a constant.

`new [n]T` calls it with `size does not fit` when the byte count does not fit.

A stack overflow is reported as `stack overflow`, as described in [Stack overflow](#stack-overflow).

Postfix `!` calls it with `null pointer` when that pointer is `null`. A debug build emits that call. `tlc --debug` and `tlc --test` are debug builds. A release build does not.

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

`builtin.StackTrace.get()` returns those same calls.

A null pointer is printed as `(null)`.

At most 32 calls are kept.

## Stack overflow

A thread that runs past its stack prints `panic: stack overflow` and its call stack, then aborts. Every build does this, not only a debug build. Deep recursion and a chain of large frames are the usual causes.

```text
panic: stack overflow
main.tlc:5:12: down
main.tlc:5:12: down
...
main.tlc:9:12: main
```

The stack lines are the same as for `builtin.panic`. Only the 32 outermost calls are kept, so deep recursion prints the first 32 calls and not the last.

Before its first statement, `main` installs a `SIGSEGV` handler that runs on a 64 KiB signal stack. A thread started with `pthread_create`, including `thread.spawn`, gets its own signal stack. A function with a frame larger than a page touches each page in order, so an overflow always reaches the guard page below the stack.

A `SIGSEGV` that is not an overflow goes to the handler that was installed before `main`. A null pointer read in C is one. With no earlier handler, the default action ends the process.

A thread that C starts on its own gets its signal stack the first time it calls a tlc function through a C function pointer. The wrapper that C calls sets it up and frees it when the thread exits. Each tlc function passed to C has that wrapper.

This works on Linux on x86-64 and AArch64.

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

The file name is the source path relative to the entry file's directory, as for a `Frame`.

`var f = builtin.panic` is a function pointer, and `f("out of memory")` aborts at that call.

`defer builtin.panic("out of memory")` aborts when the deferred calls run.

[Index](../README.md) · [Previous: va](va.md) · [Next: atomic](atomic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
