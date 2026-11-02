# trace

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the call stack and `builtin.StackTrace`.

## The compiler package

`import "builtin"` loads the compiler package.

The compiler's own copy is `lib/tlc/builtin/builtin.tlc`.

An editor shows the signatures in that file and the `//` comments above them.

It exports `va`, `panic`, `Type`, `Frame`, `StackTrace`, and `Malloc`. `extern(abi="compiler")` marks each declaration the compiler implements. `memeql` and `memcmp` are in [`std/mem`](../std/mem.md).

`builtin.Type` is the type of a type, described in [The Type type](../types/type.md).

`va` returns `*u8` and takes no arguments.

An argument is reported as `argument count`.

`panic` takes one message and does not return a value.

The import links the C library.

`import "builtin/env/consts"` loads `family`, described in [builtin/env/consts](family.md).

```c
import "builtin";
import "builtin" as b;
import { va, panic, StackTrace } from "builtin";
```

## How the names are spelled

The first form calls them as `builtin.va()`, `builtin.panic(message)`, and `builtin.StackTrace.get()`.

The alias uses the new qualifier.

The selective form brings the names into the file.

A directory named `builtin` on the search path is that package.

The compiler package is used when the search finds none.

## The frames a call records

A call records one frame, then calls.

`new` records one frame the same way. The name is the function that contains the `new`, and the location is that `new`.

The frames are the calls that have not returned, newest first.

A line is `file:line:column: name`.

The name is the function that made the call, and the location is that call.

Calls of `builtin.va` and `builtin.StackTrace.get` are omitted.

A call of `builtin.panic` is kept.

When no such call is still running, the list is empty.

At most 32 calls are kept.

Another thread has its own frames.

## How a line is named

The file name is the source path relative to the entry file's directory.

A source file outside that directory is named by its file name.

A method uses the method name.

An anonymous function is named `anon`.

Inside a `test`, the name is the test's name.

## A copy of the frames

`builtin.StackTrace.get()` returns `StackTrace`.

`frames` is `[]const Frame`. The frames are the calls that have not returned, newest first.

A `Frame` holds `file`, `line`, `column`, and `name`. `file` and `name` are `*const u8`. They address the bytes the trace frame already points at.

At most 32 frames are copied.

The call of `get` pushes no frame.

The list is a copy. The caller owns it. `trace.free()` releases that list. The file and name bytes stay.

A second `free` releases a null pointer and leaves the length at 0.

A `StackTrace` that is not freed leaks that list. The allocation is the libc heap.

When no call is still running, `frames.len` is 0. `free` on that value does nothing.

A null result from `malloc` calls `panic("out of memory")`.

```c
import "builtin";

func leaf() i32 {
    var trace = builtin.StackTrace.get();
    var n = trace.frames.len;
    trace.free();
    return n;
}

func main() i32 {
    return leaf();
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq leaf                       # call leaf
retq                             # return

leaf:
subq $40, %rsp                   # subtract
callq StackTrace.get             # call get
movl %edx, 24(%rsp)              # move
movq %rax, 16(%rsp)              # move
movl 24(%rsp), %eax              # move
movl %eax, 36(%rsp)              # move
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # address
callq StackTrace.free            # call free
movl 36(%rsp), %eax              # move
addq $40, %rsp                   # add
retq                             # return

StackTrace.get:
subq $24, %rsp                   # subtract
movl %fs:0, %eax                 # move
movl %eax, 20(%rsp)              # move
cmpl $0, %eax                    # compare
jne .L1                          # branch
xorl %edx, %edx                  # zero the result
movl %edx, %eax                  # move
addq $24, %rsp                   # add
retq                             # return
.L1:
movl 20(%rsp), %eax              # move
movl %eax, %eax                  # move
imulq $24, %rax, %rdi            # multiply
callq malloc                     # call malloc
movq %rax, %rcx                  # move
movq %rcx, 8(%rsp)               # move
xorl %eax, %eax                  # zero the result
cmpq $0, %rcx                    # compare
movl %eax, 16(%rsp)              # move
jne .L2                          # branch
leaq (%rip), %rdi                # address
callq panic                      # call panic
.L2:
movl 20(%rsp), %ecx              # move
movl 16(%rsp), %eax              # move
movl %eax, 4(%rsp)               # move
cmpl %ecx, %eax                  # compare
jge .L3                          # branch
movl 4(%rsp), %eax               # move
movq 8(%rsp), %rcx               # move
movl 20(%rsp), %esi              # move
movl %eax, %edx                  # move
notl %edx                        # not
addl %esi, %edx                  # add
movslq %edx, %rdx                # move
leaq (%rdx,%rdx,2), %rsi         # address
movq %fs:0, %rdx                 # move
leaq (%rdx,%rsi,8), %rdx         # address
movslq %eax, %rsi                # move
imulq $24, %rsi, %rsi            # multiply
addq %rsi, %rcx                  # add
movq (%rdx), %rsi                # move
movq %rsi, (%rcx)                # move
movl 8(%rdx), %esi               # move
movl %esi, 8(%rcx)               # move
movl 12(%rdx), %esi              # move
movl %esi, 12(%rcx)              # move
movq 16(%rdx), %rdx              # move
movq %rdx, 16(%rcx)              # move
addl $1, %eax                    # add
movl %eax, 16(%rsp)              # move
jmp .L2                          # jump
.L3:
movl 20(%rsp), %edx              # move
movq 8(%rsp), %rax               # move
addq $24, %rsp                   # add
retq                             # return

StackTrace.free:
pushq %rax
movq %rdi, (%rsp)                # move
movq (%rdi), %rdi                # move
callq free                       # call free
movq (%rsp), %rdi                # move
movq $0, (%rdi)                  # constant
movl $0, 8(%rdi)                 # constant
popq %rax
retq                             # return
```

`leaf` was entered from `main`, so the program returns 1.

The frame is the `leaf()` call:

```text
main.tlc:11:12: main
```

`get` returns that frame list in `%rax` and the length in `%edx`.

`free` takes the address of the `StackTrace`.

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
