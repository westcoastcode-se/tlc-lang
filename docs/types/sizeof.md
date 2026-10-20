# sizeof

[Index](../README.md) · [Previous: Strings](string.md) · [Next: Slices](slice.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`sizeof` produces `i32`. `sizeof(u8)` is 1, `sizeof(i32)` is 4, `sizeof(i64)` is 8, a pointer is 8, and `sizeof([]i32)` is 16. Both `sizeof(i32)` and `sizeof n` are accepted. `sizeof(Point)` and `sizeof(enum Color)` name a declared type. `sizeof(Shape)` names a union. `sizeof(struct Point)`, `sizeof(union Shape)`, and `sizeof(trait Area)` are reported as `expected a type`. `sizeof(Area)` when `Area` is a trait is reported as `undeclared identifier`. `sizeof(Type)` is 8.

`alignof` produces `i32`, the alignment in bytes. `alignof(i32)` is 4. `alignof(Node)` when `Node` contains a pointer is 8. `alignof(Shape)` names a union. `alignof(union Shape)` and `alignof(trait Area)` are reported as `expected a type`. `alignof(Area)` when `Area` is a trait is reported as `undeclared identifier`. It folds the same way as `sizeof`. Both `alignof(i32)` and `alignof n` are accepted.

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

## Cost

`sizeof` is folded. `return sizeof(i32)` is the same 2 instructions as `return 4`: `movl $4, %eax` and `retq`. `sizeof(Point)` and `sizeof n` are immediates of the same kind. The operator adds no load. `alignof` is folded the same way.

[Index](../README.md) · [Previous: Strings](string.md) · [Next: Slices](slice.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
