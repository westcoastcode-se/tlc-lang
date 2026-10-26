# sizeof

[Index](../README.md) · [Previous: Strings](string.md) · [Next: Slices](slice.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`sizeof` produces `i32`. `sizeof(u8)` is 1, `sizeof(i32)` is 4, `sizeof(i64)` is 8, a pointer is 8, and `sizeof([]i32)` is 16. Both `sizeof(i32)` and `sizeof n` are accepted. `sizeof(Point)` and `sizeof(enum Color)` name a declared type. `sizeof(Shape)` names a union. `sizeof(struct Point)`, `sizeof(union Shape)`, and `sizeof(trait Area)` are reported as `expected a type`. `sizeof(Area)` when `Area` is a trait is reported as `undeclared identifier`. `sizeof(builtin.Type)` is 8. That `i32` may be a compile-time integer type argument: `width[sizeof(Point)]()` is `width[8]()`.

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

```asm
main:
movl $16, %eax                   # constant
retq                             # return
```

[Index](../README.md) · [Previous: Strings](string.md) · [Next: Slices](slice.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
