# Macros

[Index](../README.md) · [Previous: Comptime](comptime.md) · [Next: typeof is not a result](typeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page is how a `macro func` pastes statements into the caller while tlc compiles.

## A macro pastes statements

A `macro func` generates statements.

A statement call is replaced by the statements written with `emit`.

A parameter without `comptime` is replaced by the argument expression.

A `comptime` parameter is a value known while compiling.

The values are the same ones a [`comptime func`](comptime.md) accepts.

`emit` outside a `macro` is `not supported yet`.

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

`hello(label, tail, 40 + 2)` emits `printf("Hello %s %s %d\n", "Per", tail, 42)`.

The file-scope `const` string and the folded integer are comptime values.

`tail` has no `comptime` mark.

The argument expression is pasted and read when the statement runs.

A `var` passed to a `comptime` parameter is `not supported yet`.

A local `const` whose initializer folds to `i32` is that integer.

Using one parameter in two `emit` statements evaluates that argument again.

`put(&n, k + 2)` with `k` holding 40 evaluates `k + 2` twice.

```c
macro func put(slot *i32, value i32) void {
    emit *slot = value;
    emit *slot = *slot + value;
}
```

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

## Conditions and loops

In a `macro`, `if` keeps the arm whose condition folds and discards the other arm.

`for` unrolls when the bounds fold.

A condition or a bound that does not fold runs at run time.

`comptime if` keeps one arm when the condition folds. The other arm is not typechecked.

`pick(&n, 1)` stores 40.

```c
macro func pick(slot *i32, n comptime i32) void {
    comptime if (n == 1) {
        emit *slot = 40;
    } else {
        emit *slot = 2;
    }
}

func main() i32 {
    var n = 0;
    pick(&n, 1);
    return n;
}
```

```asm
main:
movl $40, -4(%rsp)               # pick stores 40
movl -4(%rsp), %eax              # return n
retq
```

A local `const` whose initializer folds to `i32` is that condition.

`pick(&n, flag)` stores 40 when `flag` is `const flag = 1`.

```c
macro func pick(slot *i32, n i32) void {
    comptime if (n == 1) {
        emit *slot = 40;
    } else {
        emit *slot = 2;
    }
}

func main() i32 {
    var n = 0;
    const flag = 1;
    pick(&n, flag);
    return n;
}
```

```asm
main:
movl $40, -4(%rsp)               # pick stores 40
movl -4(%rsp), %eax              # return n
retq
```

A `comptime if` whose condition does not fold is `not supported yet`. It emits nothing.

`switch` in a `macro` is `not supported yet`.

`comptime switch` keeps the matching case and the statements that follow until `break` when the value folds. The other cases are not typechecked and not emitted.

`pick(&x, 1)` stores 2, then adds 40.

`main` returns 42.

```c
macro func pick(slot *i32, n comptime i32) void {
    comptime switch (n) {
    case 1:
        emit *slot = 2;
    case 2:
        emit *slot = *slot + 40;
        break;
    default:
        emit *slot = "no";
    }
}

func main() i32 {
    var x = 0;
    pick(&x, 1);
    return x;
}
```

```asm
main:
movl $2, -4(%rsp)                # x = 2
movl -4(%rsp), %eax              # x
addl $40, %eax                   # x + 40
movl %eax, -4(%rsp)              # store x
movl -4(%rsp), %eax              # return x
retq
```

A local `const` whose initializer folds to `i32` is that value.

`pick(&n, flag)` stores 40 when `flag` is `const flag = 1`.

```c
macro func pick(slot *i32, n i32) void {
    comptime switch (n) {
    case 1:
        emit *slot = 40;
        break;
    default:
        emit *slot = 2;
    }
}

func main() i32 {
    var n = 0;
    const flag = 1;
    pick(&n, flag);
    return n;
}
```

```asm
main:
movl $40, -4(%rsp)               # pick stores 40
movl -4(%rsp), %eax              # return n
retq
```

A value that does not fold is `not supported yet`. It emits nothing.

`comptime for` unrolls when the bounds fold.

A bound that does not fold is `not supported yet`. It emits nothing.

`const for` is not a form.

Outside a `macro`, an ordinary `if` and an ordinary `switch` run at run time.

`comptime if` keeps one arm when the condition folds.

A local `const` whose initializer folds to `i32` is that condition.

A `var` does not fold.

The other arm is not typechecked and not emitted.

A condition that is not comptime is `not supported yet`.

A `comptime` parameter is comptime.

The copy that still receives it typechecks both arms.

`comptime switch` keeps the matching case and the statements that follow until `break` when the value folds.

The other cases are not typechecked and not emitted.

A value that is not comptime is `not supported yet`.

A local `const` whose initializer folds to `i32` is that value.

A `var` does not fold.

`pick(&n, 1)` then `add3(&n)`, from `n` at 0, stores 40 and then adds `1`, `2`, and `3`.

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

`pick(&n, 1)` stores 40.

`add3` emits the body for `1`, `2`, and `3`.

## How far a macro loop unrolls

`for (var i = low; i < high; i = i + step)` unrolls when `low`, `high`, and `step` fold to integers.

`step` has to be positive.

The trip count is at most 32.

The index in each copy is that integer.

`for (var item : items)` unrolls when the length is known, at most 32 elements.

A slice `var` is `not supported yet`.

`ptr_of` on a temporary is `not an lvalue`.

A length past 32 is `not supported yet`.

A bound that does not fold runs at run time. The index is that variable.

`add(&total, n)` with `n` at 4 adds `0`, `1`, `2`, and `3`. It returns 6.

```c
macro func add(slot *i32, n i32) void {
    for (var i = 0; i < n; i = i + 1) {
        emit *slot = *slot + i;
    }
}

func main() i32 {
    var total = 0;
    var n = 4;
    add(&total, n);
    return total;
}
```

```asm
main:
movl $0, -12(%rsp)               # total = 0
movl $4, -8(%rsp)                # n = 4
movl $0, -4(%rsp)                # i = 0
.L2:
movl -4(%rsp), %eax              # i
cmpl -8(%rsp), %eax              # i < n
jge .L1                          # branch
movl -12(%rsp), %eax             # total
addl -4(%rsp), %eax              # total + i
movl %eax, -12(%rsp)             # store total
movl -4(%rsp), %eax              # i
addl $1, %eax                    # i + 1
movl %eax, -4(%rsp)              # store i
jmp .L2                          # jump
.L1:
movl -12(%rsp), %eax             # return total
retq
```

A step that is not positive is `not supported yet`.

Those loops are on [`for`](../statements/for.md).

A condition that does not fold runs at run time. Both arms are typechecked.

`pick(&n, flag)` with `flag` at 0 stores 2.

```c
macro func pick(slot *i32, n i32) void {
    if (n == 1) {
        emit *slot = 40;
    } else {
        emit *slot = 2;
    }
}

func main() i32 {
    var n = 0;
    var flag = 0;
    pick(&n, flag);
    return n;
}
```

```asm
main:
movl $0, -4(%rsp)                # flag = 0
cmpl $1, -4(%rsp)                # flag == 1
jne .L1                          # branch
movl $40, -8(%rsp)               # n = 40
jmp .L2                          # jump
.L1:
movl $2, -8(%rsp)                # n = 2
.L2:
movl -8(%rsp), %eax              # return n
retq
```

## Pasting a loop or a branch

`emit for`, `emit if`, `emit while`, and `emit do` paste that statement into the caller.

The body is ordinary code.

A nested `for` or `if` in that body runs later.

The bound of an `emit for` may be a `var`.

Those forms outside a `macro` are `not supported yet`.

A plain `while` or `do` in a `macro` runs at run time.

The body is expanded first, so `emit` there is an ordinary statement.

A statement in that body without `emit` is `not supported yet`.

`until(&n, limit)` with `n` at 0 and `limit` at 3 stores 3.

```c
macro func until(slot *i32, limit i32) void {
    while (*slot < limit) {
        emit *slot = *slot + 1;
    }
}

func main() i32 {
    var n = 0;
    var limit = 3;
    until(&n, limit);
    return n;
}
```

```asm
main:
movl $0, -8(%rsp)                # n = 0
movl $3, -4(%rsp)                # limit = 3
.L2:
movl -8(%rsp), %eax              # n
cmpl -4(%rsp), %eax              # n < limit
jge .L1                          # branch
movl -8(%rsp), %eax              # n
addl $1, %eax                    # n + 1
movl %eax, -8(%rsp)              # store n
jmp .L2                          # jump
.L1:
movl -8(%rsp), %eax              # return n
retq
```

`do` runs the body before it tests the condition.

`emit` in that body is an ordinary statement.

## Limits on a macro

A value call of a `macro` is `not supported yet`.

`var f = name` is `not supported yet`.

A method on a `macro` is `not supported yet`.

`extern` and `export` on a `macro` are `not supported yet`.

A type parameter is accepted.

The parameter is the argument expression, so a tuple may contain a `var`.

`values comptime T` does not: a tuple that contains a `var` is `not supported yet`.

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

`show(("John Doe", age))` emits `printf("%s\n", "John Doe")` and `printf("%d\n", age)` when `age` is a `var`.

[`builtin.fmt`](../expressions/tuple.md) fills `{0}` and `{1}` from a compile-time tuple.

[Index](../README.md) · [Previous: Comptime](comptime.md) · [Next: typeof is not a result](typeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
