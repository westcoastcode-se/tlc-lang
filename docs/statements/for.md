# for

[Index](../README.md) · [Previous: Statements](block.md) · [Next: switch](switch.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`for` has three clauses, as in C. A declaration in the first clause is visible only inside the loop. That clause may also bind several results with `var`, `:=`, or `=`. A comma in the name list must be followed by another name.

```c
func main() i32 {
    var s i32 = 0;
    for (var i i32 = 1; i <= 4; i = i + 1) {
        s = s + i;
    }
    return s;
}
```

```asm
main:
movl $0, -8(%rsp)                # constant
movl $1, -4(%rsp)                # constant
.L2:
cmpl $4, -4(%rsp)                # compare
jg .L1                           # branch
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
movl %eax, -8(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
movl %eax, -4(%rsp)              # move
jmp .L2                          # jump
.L1:
movl -8(%rsp), %eax              # move
retq                             # return
```

`continue` starts the next iteration. `break` leaves the loop.

```c
func main() i32 {
    var s i32 = 0;
    for (var i i32 = 0; i < 5; i = i + 1) {
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            break;
        }
        s = s + i;
    }
    return s;
}
```

```asm
main:
movl $0, -8(%rsp)                # constant
movl $0, -4(%rsp)                # constant
.L4:
cmpl $5, -4(%rsp)                # compare
jge .L1                          # branch
cmpl $2, -4(%rsp)                # compare
je .L2                           # branch
jmp .L3                          # jump
.L5:
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
movl %eax, -4(%rsp)              # move
jmp .L4                          # jump
.L1:
movl -8(%rsp), %eax              # move
retq                             # return
.L2:
jmp .L5                          # jump
.L3:
jmp .L6                          # jump
.L6:
cmpl $4, -4(%rsp)                # compare
jne .L7                          # branch
jmp .L1                          # jump
.L7:
jmp .L8                          # jump
.L8:
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
movl %eax, -8(%rsp)              # move
jmp .L5                          # jump
```

`for (var item : items)` binds each element. `for (const item : items)` is the immutable binding: `item = ...` is `type mismatch`, and `&item` is `*const E`. The name is new in the loop and hides an outer name. Assigning `var item` does not write the collection. `break`, `continue`, and `defer` are the ones the three-clause `for` uses. `continue` runs the next index. The collection is an array, a slice, or a struct with `len` and `at`. A file does not import a package to write the loop. `for (var item : ptr_of(items))` binds `*E`. A const element binds `*const E`, and a store through it is `type mismatch`. `item = &other` reseats the pointer. `const` rejects that assignment. `ptr_of` on a temporary is `not an lvalue`. `for (item : items)` without `var` or `const` is `expected ';'`. Completion, hover, and definition see the name inside the loop.

```c
func main() i32 {
    var items [4]i32;
    items[0] = 1;
    items[1] = 2;
    items[2] = 3;
    items[3] = 4;
    var sum = 0;
    for (var item : items) {
        sum = sum + item;
    }
    return sum;
}
```

```asm
main:
movl $1, -28(%rsp)               # constant
movl $2, -24(%rsp)               # constant
movl $3, -20(%rsp)               # constant
movl $4, -16(%rsp)               # constant
movl $0, -12(%rsp)               # constant
movl $0, -8(%rsp)                # constant
.L2:
cmpl $4, -8(%rsp)                # compare
jge .L1                          # branch
movl -8(%rsp), %ecx              # move
movslq %ecx, %rax                # move
movl -28(%rsp,%rax,4), %eax      # move
movslq %ecx, %rcx                # move
movl %eax, -4(%rsp)              # move
movl -12(%rsp), %eax             # move
addl -4(%rsp), %eax              # add
movl %eax, -12(%rsp)             # move
movl -8(%rsp), %eax              # move
addl $1, %eax                    # add
movl %eax, -8(%rsp)              # move
jmp .L2                          # jump
.L1:
movl -12(%rsp), %eax             # move
retq                             # return
```

The loop stores 1, 2, 3, and 4, then adds them. It returns 10.

In a [`macro`](../functions/macro.md), `for` is the same three clauses, unrolled while compiling. `low`, `high`, and `step` in `for (var i = low; i < high; i = i + step)` are compile-time integers, `step` is positive, and the body runs at most 32 times. Each copy of the body sees `i` as that integer, so `emit` can use it. `for (var item : items)` in a `macro` unrolls when the length is known, at most 32 elements. `item` in each copy is `items[0]`, `items[1]`, and so on. `for (var slot : ptr_of(items))` uses `&items[0]`, `&items[1]`, and so on, when `items` is an lvalue and the length is known. A slice `var`, `ptr_of` on a temporary, a length past 32, a bound that does not fold, and a step that is not positive are `not supported yet`. Outside a `macro`, `for` repeats at run time. `emit while` and `emit do` paste those loops the same way. A plain `while` or `do` in a `macro` is `not supported yet`. `emit for` in a `macro` pastes that loop into the caller. The bound may be a `var`. The body is ordinary code, so a nested `for` also runs later. `emit for` outside a `macro` is `not supported yet`.

`import "std/seq"` names the iteration trait. `seq.Seq[T]` and `*seq.Seq[T]` check `len` and `at`. `[N]T` and `[]T` satisfy `seq.Seq[E]`, where `E` is the element an index reads. `len` is `.len` and `at` is the index. `for (var item : ptr_of(items))` binds the address of that element. A slice alias satisfies the trait only when it has the methods. A pointer to the array or the slice does not satisfy it. The three-clause `for` does not call those methods. The trait is on [seq](../std/seq.md).

[Index](../README.md) · [Previous: Statements](block.md) · [Next: switch](switch.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
