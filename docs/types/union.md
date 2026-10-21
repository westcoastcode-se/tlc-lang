# Unions

[Index](../README.md) · [Previous: Structs](struct.md) · [Next: Arrays](array.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A tlc union names the enum that selects the active field. `union` is written only in that definition. A variable, a parameter, `sizeof`, and `alignof` use the name: `var s Shape`, `sizeof(Shape)`, and `alignof(Shape)`. `var s union Shape`, `sizeof(union Shape)`, and `alignof(union Shape)` are reported as `expected a type`. `var u union(Kind) { A struct { n i32; }; B struct { n i32; }; }` is a union with no name. `u.A.n` reads that arm. The value can be passed where a trait parameter asks for the same fields. `type Shape union(Kind) {}` is that union when `Kind` has no enumerators. `sizeof(Shape)` is the size of the tag, 4 for an `i32` enum. The enum is in the same package. Each enumerator is one field, and each field is one enumerator. A field that is not an enumerator is reported as `undeclared identifier`. An enumerator with no field is reported as `type mismatch`. The field's type is a struct. It can be written inline, or it can name a struct that is already declared. A field that is not a struct is reported as `type mismatch`. The name in parentheses is reported as `undeclared identifier` when it names nothing, and as `type mismatch` when it is not an enum.

```c
type ShapeType enum {
    Circle,
    Rectangle,
};

type Shape union(ShapeType) {
    Circle struct {
        radius f32;
    };
    Rectangle struct {
        width f32;
        height f32;
    };
};

type Circle struct {
    radius f32;
};

type Rectangle struct {
    width f32;
    height f32;
};

type Named union(ShapeType) {
    Circle Circle;
    Rectangle Rectangle;
};

func main() i32 {
    var shape Shape = {.Circle = {.radius = 1.0}};
    if (ShapeType(shape) != ShapeType.Circle) {
        return 0;
    }
    shape.Rectangle.width = 2.0;
    shape.Rectangle.height = 3.0;
    if (ShapeType(shape) == ShapeType.Rectangle) {
        return 1;
    }
    return 0;
}
```

The value stores one payload and the enum tag after it. Every field starts at offset 0, and the payload is large enough for the largest field. With `Circle` as one `f32` and `Rectangle` as two, `Shape` is 12 bytes: 8 for the payload and 4 for the `i32` tag. Assigning a field, or a field of that struct, stores that enumerator as the tag. `{.Circle = {.radius = 1.0}}` does the same. `ShapeType(shape)` reads the tag. Reading a field does not check the tag. A zero value has tag 0. A field of an inline payload may have a value, `n i32 = 1`. `Shape{.One = {}}` uses that value when the initializer does not name the field. Naming the field replaces it. The value is a constant, and a call is reported as `global initializer is not a constant`. `{.One = {0...}}` stores 0 and does not use the value. A union with no initializer does not choose an arm. Reading a field of that union is reported as `uninitialized`.

### Untagged unions

`type Word union` does not name an enum. The fields share one address, as in the C union below. `extern type Word union` is that same layout. `extern(abi="c")` is what makes the union a C type: a nested struct must then be `extern(abi="c")`. An empty `type Word union {}` is reported as `not supported yet`.

```c
type Num union {
    n i32;
    m i32;
};

func main() i32 {
    var u Num;
    u.n = 4;
    return u.m;
}
```

`Num` is 4 bytes. `u.n` and `u.m` are the same memory, so `main` returns 4.

### C unions

`extern(abi="c") union` is the C union. Every field starts at the same address. The size is the largest field, rounded up to the alignment, and the alignment is the strictest field. Reading a field reinterprets the stored bits. A struct or a union nested inside a C union must be `extern(abi="c")`, or the error is `struct must be extern(abi="c")`.

```c
extern(abi="c") type Num union {
    n i32;
    m i32;
};

extern(abi="c") type Word union {
    n i32;
    raw [8]u8;
};

func main() i32 {
    var u Num;
    u.n = 4;
    return u.m;
}
```

`Num` is 4 bytes. `Word` is 8. `u.n` and `u.m` are the same memory, so `main` returns 4.

## Cost

A tlc union stores the payload and then an `i32` tag. Assigning `shape.Rectangle.width` through a `*Shape` is 6 instructions including the pointer store and `retq`: the tag is stored as the immediate 1, and the `f32` is stored at offset 0. `var shape = Shape{.One = {}}` when the payload is `n i32 = 10`, then `return shape.One.n`, is 16 instructions including `retq`. One `movl` stores 10. The tag stored with that value is 0, and the union is copied into `shape`.

Reading the tag with `ShapeType(*shape)` loads the whole value. For a 12-byte `Shape` that load moves the 8 payload bytes one byte at a time and then loads the tag. The compare against `ShapeType.Circle` brings the function to 16 instructions including `retq`. Passing that union by value does the same byte-wise copy into the callee's stack. The function that builds a `Shape`, assigns one field, and returns 1 when the tag matches runs 60 instructions.

A C union does not store a tag. `u.n = 4` on a 4-byte union is 1 zero store and 1 store of 4. `return u.m` is 1 load of the same slot. The function is 4 instructions including `retq`.

[Index](../README.md) · [Previous: Structs](struct.md) · [Next: Arrays](array.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
