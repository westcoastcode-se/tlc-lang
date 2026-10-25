# Statements

[Index](../README.md) · [Previous: Tuples and lists](../expressions/tuple.md) · [Next: for](for.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A block is a sequence of statements in braces. `if`, `while`, and `for` take a block. In a [macro](../functions/macro.md), `if` keeps the arm whose condition folds. `else` is a block or another `if`. A body without `{` is reported as `expected '{'`. `do` and `switch` take a statement, so their body need not be a block. An empty statement is `;`.

```c
func main() i32 {
    var n i32 = 0;
    if (n != 0) {
        n = 1;
    } else if (true) {
        n = 40;
    } else {
        n = 2;
    }
    while (n < 42) {
        n = n + 1;
    }
    do n = n + 1; while (false);
    ;
    return n;
}
```

## Assembly

```asm
main:
movl $0, -4(%rsp)                # constant
cmpl $0, -4(%rsp)                # compare
je .L1                           # branch
movl $1, -4(%rsp)                # constant
jmp .L2                          # jump
.L1:
movb $1, %al                     # constant
testb $1, %al                    # test
jne .L3                          # branch
jmp .L4                          # jump
.L2:
jmp .L5                          # jump
.L3:
movl $40, -4(%rsp)               # constant
jmp .L6                          # jump
.L4:
movl $2, -4(%rsp)                # constant
.L6:
jmp .L2                          # jump
.L5:
cmpl $42, -4(%rsp)               # compare
jge .L7                          # branch
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
movl %eax, -4(%rsp)              # move
jmp .L5                          # jump
.L7:
jmp .L8                          # jump
.L8:
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
movl %eax, -4(%rsp)              # move
xorl %eax, %eax                  # zero the result
testb $1, %al                    # test
jne .L8                          # branch
jmp .L9                          # jump
.L9:
movl -4(%rsp), %eax              # move
retq                             # return
```

[Index](../README.md) · [Previous: Tuples and lists](../expressions/tuple.md) · [Next: for](for.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
