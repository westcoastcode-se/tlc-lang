# typeof is not a result

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Structs and arrays](aggregate.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`typeof` is not a result type. `func id(x i32) typeof(x)` is `expected a type` on `typeof`. `(typeof(x))` is the same error. An expression `typeof(x)` still has type `Type`. A result whose type comes from the value is a generic `T`, or no written type. `any` is not a keyword.

```c
func id(x i32) typeof(x) {
    return x;
}
```

## Cost

`func id(x i32) typeof(x)` is rejected and adds no instructions.

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Structs and arrays](aggregate.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
