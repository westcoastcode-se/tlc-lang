# sizeof

[Index](../README.md) · [Previous: Strings](string.md) · [Next: Slices](slice.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `sizeof` and `alignof`, and which type forms each one accepts.

## sizeof

`sizeof` produces `i32`, the size of a type or a value.

`sizeof(u8)` is 1, `sizeof(i32)` is 4, and `sizeof(i64)` is 8.

A pointer is 8, and `sizeof([]i32)` is 16.

Both `sizeof(i32)` and `sizeof n` are accepted.

`sizeof(Point)` and `sizeof(enum Color)` name a declared type, and `sizeof(Shape)` names a union.

The compiler reports `sizeof(struct Point)`, `sizeof(union Shape)`, and `sizeof(trait Area)` as `expected a type`.

The compiler reports `sizeof(Area)` as `undeclared identifier` when `Area` is a trait.

The compiler reports `sizeof(add)` as `not supported yet` when `add` is a function.

`sizeof(builtin.Type)` is 24.

That `i32` may be a compile-time integer type argument, so `width[sizeof(Point)]()` is `width[8]()`.

## alignof

`alignof` produces `i32`, the alignment in bytes.

`alignof(i32)` is 4, and `alignof(Node)` is 8 when `Node` contains a pointer.

`alignof(Shape)` names a union.

The compiler reports `alignof(union Shape)` and `alignof(trait Area)` as `expected a type`.

The compiler reports `alignof(Area)` as `undeclared identifier` when `Area` is a trait.

`alignof` folds the same way as `sizeof`, and both `alignof(i32)` and `alignof n` are accepted.

## A sum of three sizes

```c
type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var n i32 = 1;
    return sizeof(i32) + sizeof n + sizeof(Point);
}
```

```asm
main:
movl $16, %eax                   # constant
retq                             # return
```

[Index](../README.md) · [Previous: Strings](string.md) · [Next: Slices](slice.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
