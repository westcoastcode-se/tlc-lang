# trace

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "builtin"` loads the compiler package. The compiler's own copy is `lib/tlc/builtin/builtin.tlc`. An editor shows the signatures in that file and the `//` comments above them. It exports `trace`, `va`, `panic`, `memeql`, `memcmp`, and `Type`. `builtin.Type` is the type of a type, described in [The Type type](../types/type.md). `trace` returns `*const u8`. `va` returns `*u8`. Both take no arguments. An argument is reported as `argument count`. `panic` takes one message and does not return a value. The import links the C library. `import "builtin/env/consts"` loads `family`, described in [builtin/env/consts](family.md).

```c
import "builtin";
import "builtin" as b;
import { trace, va, panic } from "builtin";
```

The first form calls them as `builtin.trace()`, `builtin.va()`, and `builtin.panic(message)`. The alias uses the new qualifier. The selective form brings the names into the file. A directory named `builtin` on the search path is that package. The compiler package is used when the search finds none.

## `trace`

`builtin.trace()` returns `*const u8`. The text lists the calls that have not returned, newest first. A line is `file:line:column: name`. The name is the function that made the call, and the location is that call. The last line has no newline. Calls of `builtin.trace` and `builtin.va` are omitted. A call of `builtin.panic` is kept. When no such call is still running, the text is empty.

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

`leaf` is calling `printf`, whose argument is the trace. `mid` has called `leaf`, and `main` has called `mid`, so the program prints:

```text
main.tlc:11:12: mid
main.tlc:15:12: main
```

The file name is the source path relative to the entry file's directory. A source file outside that directory is named by its file name. A method uses the method name. An anonymous function is named `anon`. Inside a `test`, the name is the test's name.

At most 32 calls are kept. The text is at most 4095 bytes. The pointer refers to one buffer, and the next `builtin.trace()` replaces that text.

`var f = builtin.trace` is a function pointer. `defer builtin.trace()` runs the call when the deferred calls run.

```c
import "builtin";

func main() i32 {
    var f = builtin.trace;
    defer f();
    defer builtin.trace();
    return 0;
}
```

## Assembly

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

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
