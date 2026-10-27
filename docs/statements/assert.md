# assert

[Index](../README.md) · [Previous: new and delete](new.md) · [Next: Functions](../functions/function.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `assert`, which checks a constant `bool` while the program compiles.

## Where `assert` may stand

`assert` may stand at file scope, and it may also stand inside a function.

## The condition and the message

The condition is an expression such as `sizeof(i32) == 4` or `typeof(i32) == typeof(i64)`.

It may also be a float comparison, `1.5f < 2.0f`, or a string byte, `"hi"[1] == 105`.

`"hello"[1:4][0] == 101` indexes a slice of a string and compares that byte.

`builtin.memeql[u8]("hi", "hi")` compares two strings while the program compiles.

The message is a string literal.

## What a true or false check does

A false condition is an error, and the diagnostic is that message.

A true `assert` produces no code.

## Conditions the compiler rejects

The compiler reports an integer as `type mismatch: expected bool, found i32`.

The compiler reports a condition that is not a constant as `not supported yet`.

```c
assert(sizeof(i32) == 4, "i32 must be 4 bytes");
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: new and delete](new.md) · [Next: Functions](../functions/function.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
