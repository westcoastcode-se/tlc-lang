# typeof as a result

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Structs and arrays](aggregate.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A result may be `typeof(expression)`. The expression is checked after the parameters exist, so `func id(x i32) typeof(x)` has result `i32`. That spelling is the result type. An expression `typeof(x)` still has type `Type`.

```c
func id(x i32) typeof(x) {
    return x;
}

func main() i32 {
    return id(42);
}
```

## Cost

`typeof(x)` written as a result type is resolved while compiling. `return x` from `func id(x i32) typeof(x)` is 3 instructions: store the parameter, load it, `retq`. The `typeof` adds none.

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Structs and arrays](aggregate.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
