# Nested functions

[Index](../README.md) · [Previous: Several results](result.md) · [Next: Generics](generic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes a function written inside a block, and which names that function can use.

## Where the function is written

A function may be written inside a block, and it has a body in that block.

Its name is visible from that declaration through the end of the block, including blocks nested inside it.

A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value.

## What the function can use

The function can call functions already in scope, and it can use globals.

It can also read a function pointer from an enclosing function, either a parameter or a local that holds that pointer. The inner function returns that pointer or calls it.

The call of the inner function is direct. A method is an enclosing function in the same way.

An anonymous function, used from `defer` or called directly, is that same kind of function.

> **Warning:** The compiler reports `not supported yet` for `var f = pass`, and for passing that inner function as an argument. It reports `not supported yet` for a local or a parameter that is not a function pointer.

## Several parameter lists

Several parameter lists on one inner name are chosen by the call, the same way as a file-scope function.

```c
func main() i32 {
    func add(a i32, b i32) i32 {
        return a + b;
    }
    return add(20, 22);
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $20, %edi                   # constant
movl $22, %esi                   # constant
callq 1                          # call 1
retq                             # return

1:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

## Returning an enclosing function pointer

```c
func add(n i32) i32 {
    return n + 1;
}

func outer(op func (i32) i32) i32 {
    func pass() func (i32) i32 {
        return op;
    }
    return pass()(41);
}
```

```asm
outer:
# trace frame for the call, then the matching pop
callq 1                          # call 1
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return

1:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
retq                             # return
```

The program returns 42.

[Index](../README.md) · [Previous: Several results](result.md) · [Next: Generics](generic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
