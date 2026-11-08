# Nested functions

[Index](../README.md) · [Previous: Several results](result.md) · [Next: Generics](generic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes a function written inside a block, and which names that function can use.

## Where the function is written

A function may be written inside a block, and it has a body in that block.

Its name is visible from that declaration through the end of the block, including blocks nested inside it.

A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value.

## What the function can use

The function can call functions already in scope, and it can use globals.

It can read a `comptime const` of an enclosing function. The call is direct.

A `var`, a runtime `const`, and a parameter are `not supported yet`.

It can read a function pointer the same way when that name is a `comptime const`. The inner function returns that pointer or calls it.

A method is an enclosing function in the same way.

An anonymous function, used from `defer` or called directly, is that same kind of function.

> **Warning:** The compiler reports `not supported yet` for a `var`, a runtime `const`, or a parameter from an enclosing function. It reports `not supported yet` for `var f = pass` when `pass` reads a `comptime const`, and for passing that function as an argument. `func` is not a lambda. `export`, `extern`, and a method on a function in a block are `not supported yet`.

## Reading a comptime const

```c
func main() i32 {
    comptime const n = 40;
    func add(b i32) i32 {
        return n + b;
    }
    return add(2);
}
```

```asm
main:
movl $40, 20(%rsp)               # n = 40
# trace frame for the call, then the matching pop
movl $2, %edi                    # constant
leaq 20(%rsp), %rsi              # n
callq 1                          # call 1
retq                             # return

1:
movl %edi, -4(%rsp)              # move
movl (%rsi), %eax                # n
addl -4(%rsp), %eax              # add
retq                             # return
```

The program returns 42.

A runtime `const` and a `var` are `not supported yet`.

## Reading a tuple

```c
func main() i32 {
    comptime const t = (40, 2);
    func add() i32 {
        return t[0] + t[1];
    }
    return add();
}
```

```asm
main:
movl $2, 20(%rsp)                # t[1] = 2
movl $40, 16(%rsp)               # t[0] = 40
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # t
callq 1                          # call 1
retq                             # return

1:
movl $42, %eax                   # 40 + 2
retq                             # return
```

The program returns 42.

The index is a constant. A runtime `const` tuple and a `var` tuple are `not supported yet`.

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

func outer() i32 {
    comptime const fp = add;
    func pass() func (i32) i32 {
        return fp;
    }
    return pass()(41);
}
```

```asm
outer:
leaq add(%rip), %rax             # add
movq %rax, 48(%rsp)              # fp = add
movq 48(%rsp), %rax              # fp
movq %rax, 32(%rsp)              # move
# trace frame for the call, then the matching pop
movq 32(%rsp), %rdi              # fp
callq 1                          # call 1
# trace frame for the call, then the matching pop
movl $41, %edi                   # constant
callq *%rax                      # call *%rax
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
