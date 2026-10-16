# assert

[Index](../README.md) · [Previous: new and delete](new.md) · [Next: Functions](../functions/function.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`assert` checks a constant integer while compiling. It may stand at file scope or inside a function. The condition is an expression such as `sizeof(i32) == 4`, `typeof(i32) == typeof(i64)`, or `1.5f < 2.0f`, and the message is a string literal. A false condition is an error, and the diagnostic is that message. A true `assert` produces no code. A condition that is not a constant is reported as `not supported yet`.

```c
assert(sizeof(i32) == 4, "i32 must be 4 bytes");
```

## Cost

A true `assert` adds no instructions. The condition is folded while compiling, and the function that contains it is unchanged. A false condition is an error and emits nothing.

[Index](../README.md) · [Previous: new and delete](new.md) · [Next: Functions](../functions/function.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
