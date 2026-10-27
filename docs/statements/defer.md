# defer

[Index](../README.md) · [Previous: switch](switch.md) · [Next: Bindings](binding.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `defer`, which runs one call when the enclosing block is left.

## When the call runs

The call can be a function, a method, an inner function, or an anonymous function.

Calls in the same block run in reverse order.

A `defer` whose statement did not run does not run.

`return`, `break`, `continue`, and reaching the end of the block all run the calls of the blocks they leave.

A `return` expression is evaluated before those calls.

> **Warning:** Arguments, and a method receiver, are evaluated at the `defer`, not when the block is left.

```c
func note(n i32) void {
}

func main() i32 {
    defer note(1);
    defer func (value i32) {
    }(10);
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # subtract
movl $1, 12(%rsp)                # constant
movb $1, 11(%rsp)                # constant
movl $10, 20(%rsp)               # constant
movb $1, 19(%rsp)                # constant
testb $1, 19(%rsp)               # test
jne .L1                          # branch
jmp .L2                          # jump
.L1:
movl 20(%rsp), %edi              # move
callq 1                          # call 1
.L2:
testb $1, 11(%rsp)               # test
jne .L3                          # branch
jmp .L4                          # jump
.L3:
movl 12(%rsp), %edi              # move
callq note                       # call note
.L4:
xorl %eax, %eax                  # zero the result
addq $24, %rsp                   # add
retq                             # return

note:
retq                             # return

1:
retq                             # return
```

`note(1)` runs after the anonymous call.

## Anonymous functions

An anonymous function is an expression, `func (parameters) { body }`.

The return type may be omitted, and then it is `void`.

A return type is written after the parameters when it is present: `func () i32 { return 1; }`.

Several results use the same parenthesized list as a named function: `func () (i32, i32) { return 1, 2; }`.

The call is the argument list after the body.

[Index](../README.md) · [Previous: switch](switch.md) · [Next: Bindings](binding.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
