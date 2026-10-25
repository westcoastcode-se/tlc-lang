# assert

[Index](../README.md) · [Previous: new and delete](new.md) · [Next: Functions](../functions/function.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`assert` checks a constant `bool` while compiling. It may stand at file scope or inside a function. The condition is an expression such as `sizeof(i32) == 4`, `typeof(i32) == typeof(i64)`, `1.5f < 2.0f`, `"hi"[1] == 105`, `"hello"[1:4][0] == 101`, or `builtin.memeql[u8]("hi", "hi")`, and the message is a string literal. A false condition is an error, and the diagnostic is that message. A true `assert` produces no code. An integer is reported as `type mismatch: expected bool, found i32`. A condition that is not a constant is reported as `not supported yet`.

```c
assert(sizeof(i32) == 4, "i32 must be 4 bytes");
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: new and delete](new.md) · [Next: Functions](../functions/function.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
