# Macros

[Index](../README.md) · [Previous: Comptime](comptime.md) · [Next: typeof is not a result](typeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A `macro func` generates statements. A statement call is replaced by the statements written with `emit`. A parameter without `comptime` is replaced by the argument expression. A `comptime` parameter is a value known while compiling. The values are the same ones a [`comptime func`](comptime.md) accepts. `emit` outside a `macro` is `not supported yet`.

```c
extern(abi="c") func printf(fmt *const u8, ...) i32;

macro func hello(name comptime []const u8, tail []const u8, n comptime i32) void {
    emit printf("Hello %s %s %d\n", name, tail, n);
}

const label = "Per";

func main() i32 {
    var tail = "Ada";
    hello(label, tail, 40 + 2);
    return 0;
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq printf                     # call printf
retq                             # return
```

`hello(label, tail, 40 + 2)` emits `printf("Hello %s %s %d\n", "Per", tail, 42)`. The file-scope `const` string and the folded integer are comptime values. `tail` has no `comptime` mark, so the argument expression is pasted and read when the statement runs. A `var` passed to a `comptime` parameter is `not supported yet`. Using one parameter in two `emit` statements evaluates that argument again.

```c
macro func put(slot *i32, value i32) void {
    emit *slot = value;
    emit *slot = *slot + value;
}
```

`put(&n, k + 2)` with `k` holding 40 evaluates `k + 2` twice:

```asm
main:
movl $40, -4(%rsp)               # k
movl -4(%rsp), %eax              # k
addl $2, %eax                    # k + 2
movl %eax, -8(%rsp)              # n = k + 2
movl -8(%rsp), %eax              # n
movl -4(%rsp), %ecx              # k
addl $2, %ecx                    # k + 2 again
addl %ecx, %eax                  # n + (k + 2)
movl %eax, -8(%rsp)              # store n
movl -8(%rsp), %eax              # return n
retq                             # return
```

In a `macro`, `if` keeps the arm whose condition folds. The other arm is discarded. `for` unrolls. There is no `comptime` prefix on either statement. `comptime if` and `comptime for` are `not supported yet`. `const for` is not a form.

```c
macro func pick(slot *i32, n comptime i32) void {
    if (n == 1) {
        emit *slot = 40;
    } else {
        emit *slot = 2;
    }
}

macro func add3(slot *i32) void {
    for (var i = 1; i < 4; i = i + 1) {
        emit *slot = *slot + i;
    }
}
```

`pick(&n, 1)` then `add3(&n)`, from `n` at 0, stores 40 and then adds 1, 2, and 3:

```asm
main:
movl $40, -4(%rsp)               # pick stores 40
movl -4(%rsp), %eax
addl $1, %eax                    # add 1
movl %eax, -4(%rsp)
movl -4(%rsp), %eax
addl $2, %eax                    # add 2
movl %eax, -4(%rsp)
movl -4(%rsp), %eax
addl $3, %eax                    # add 3
movl %eax, -4(%rsp)
movl -4(%rsp), %eax              # return n
retq
```

`pick(&n, 1)` stores 40. `add3` emits the body for `1`, `2`, and `3`. `for (var i = low; i < high; i = i + step)` unrolls when `low`, `high`, and `step` fold to integers, `step` is positive, and the trip count is at most 32. The index in each copy is that integer. `for (var item : items)` unrolls when the length is known, at most 32 elements. A slice `var`, `ptr_of` on a temporary, a length past 32, a bound that does not fold, and a step that is not positive are `not supported yet`. Those loops are on [`for`](../statements/for.md). A condition that does not fold is `not supported yet`.

`emit for`, `emit if`, `emit while`, and `emit do` paste that statement into the caller. The body is ordinary code, so a nested `for` or `if` runs later. The bound of an `emit for` may be a `var`. Those forms outside a `macro` are `not supported yet`. A plain `while` or `do` in a `macro` is `not supported yet`.

A value call of a `macro`, `var f = name`, a method, `extern`, and `export` are `not supported yet`. A type parameter is accepted. The parameter is the argument expression, so a tuple may contain a `var`. `values comptime T` does not: a tuple that contains a `var` is `not supported yet`.

```c
import "builtin";

extern(abi="c") func printf(fmt *const u8, ...) i32;

macro func show[T type](values T) void {
    for (var i = 0; i < values.len; i = i + 1) {
        if (typeof(values[i]) == typeof(i32)) {
            emit printf("%d\n", values[i]);
        } else {
            emit printf("%s\n", values[i]);
        }
    }
}
```

```asm
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

`show(("John Doe", age))` emits `printf("%s\n", "John Doe")` and `printf("%d\n", age)` when `age` is a `var`. [`builtin.fmt`](../expressions/tuple.md) fills `{0}` and `{1}` from a compile-time tuple.

[Index](../README.md) · [Previous: Comptime](comptime.md) · [Next: typeof is not a result](typeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
