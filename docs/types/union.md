# Unions

[Index](../README.md) · [Previous: Structs](struct.md) · [Next: Arrays](array.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A union stores one of several fields in the same memory.

## A tagged union

A tlc union names the enum that selects the active field. `union` is written only in that definition.

A variable, a parameter, `sizeof`, and `alignof` use the name: `var s Shape`, `sizeof(Shape)`, and `alignof(Shape)`. `var s union Shape`, `sizeof(union Shape)`, and `alignof(union Shape)` are reported as `expected a type`.

`var u union(Kind) { A struct { n i32; }; B struct { n i32; }; }` is a union with no name. `u.A.n` reads that arm. The value can be passed where a trait parameter asks for the same fields.

`extern type Shape union(Kind)` keeps the tag. `extern(abi="c") type Shape union(Kind)` is `extern(abi="c") on a tagged union`.

A C union has no tag. The C form is `extern(abi="c") type Shape union { ... }` with no enum. A named arm of that union must itself be `extern(abi="c")`, or the error is `struct must be extern(abi="c")`.

`type Shape union(Kind) {}` is that union when `Kind` has no enumerators. `sizeof(Shape)` is the size of the tag, 4 for an `i32` enum.

The enum is in the same package, or it is `pkg.Kind` when `pkg` is imported and `Kind` is exported. A qualifier that names no imported package is reported as `undeclared identifier`.

An enum that is not exported is reported as `Kind is not exported`. An exported name that is not an enum is reported as `type mismatch`.

`import { Kind }` does not bind the package name, so `union(pkg.Kind)` is `undeclared identifier` until the package itself is imported.

Each enumerator is one field, and each field is one enumerator. A field that is not an enumerator is reported as `undeclared identifier`. An enumerator with no field is reported as `type mismatch`.

The field's type is a struct. It can be written inline, or it can name a struct that is already declared.

The inline arm has the same members as a struct, so `data union { n i32; m i32; }` inside it is an anonymous union and `n` and `m` share one address. An anonymous union's inline struct can hold another union the same way.

A field that is not a struct is reported as `type mismatch`. A field that stores the union by value, `type Word union { n i32; w Word; }`, is reported as `recursive union main.Word -> main.Word` when the file is `main.tlc`.

The name in parentheses is reported as `undeclared identifier` when it names nothing, and as `type mismatch` when it is not an enum.

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

```asm
main:
movl $0x3f800000, -8(%rsp)       # constant
movss -8(%rsp), %xmm0            # move
movss %xmm0, -24(%rsp)           # move
movl $0, -16(%rsp)               # constant
movl -16(%rsp), %eax             # move
movb -17(%rsp), %cl              # move
movb -18(%rsp), %cl              # move
movb -19(%rsp), %cl              # move
movb -20(%rsp), %cl              # move
movb -21(%rsp), %cl              # move
movb -22(%rsp), %cl              # move
movb -24(%rsp), %cl              # move
movb -23(%rsp), %cl              # move
cmpl $0, %eax                    # compare
je .L1                           # branch
xorl %eax, %eax                  # zero the result
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
movl $0x40000000, -24(%rsp)      # constant
movl $1, -16(%rsp)               # constant
movl $0x40400000, -20(%rsp)      # constant
movl -16(%rsp), %eax             # move
movb -17(%rsp), %cl              # move
movb -18(%rsp), %cl              # move
movb -19(%rsp), %cl              # move
movb -20(%rsp), %cl              # move
movb -21(%rsp), %cl              # move
movb -22(%rsp), %cl              # move
movb -24(%rsp), %cl              # move
movb -23(%rsp), %cl              # move
cmpl $1, %eax                    # compare
jne .L3                          # branch
movl $1, %eax                    # constant
retq                             # return
.L3:
jmp .L4                          # jump
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
```

## Payload and tag

The value stores one payload and the enum tag after it. Every field starts at offset 0, and the payload is large enough for the largest field.

With `Circle` as one `f32` and `Rectangle` as two, `Shape` is 12 bytes: 8 for the payload and 4 for the `i32` tag.

Assigning a field, or a field of that struct, stores that enumerator as the tag. `{.Circle = {.radius = 1.0}}` does the same.

`ShapeType(shape)` reads the tag. `pkg.Kind(value)` reads the tag of `union(pkg.Kind)`.

> **Warning:** Reading a field does not check the tag.

A zero value has tag 0.

A field of an inline payload may have a value, `n i32 = 1`. `Shape{.One = {}}` uses that value when the initializer does not name the field. Naming the field replaces it.

The value is a constant, and a call is reported as `global initializer is not a constant`. `{.One = {0...}}` stores 0 and does not use the value.

A union with no initializer does not choose an arm. Reading a field of that union is reported as `uninitialized`.

### Untagged unions

`type Word union` does not name an enum. The fields share one address, as in the C union below.

`extern type Word union` is that same layout. `extern(abi="c")` is what makes the union a C type: a nested struct must then be `extern(abi="c")`.

An empty `type Word union {}` is reported as `not supported yet`.

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

```asm
main:
movl $4, -8(%rsp)                # constant
movl -8(%rsp), %eax              # move
retq                             # return
```

`Num` is 4 bytes. `u.n` and `u.m` are the same memory, so `main` returns 4.

A value on one of those fields, `n i32 = 4`, is reported as `not supported yet`. The same value inside an anonymous union field is that error too.

### A generic union

`type Num[T type] union` is that union for each type. A parameter that no field, method, or associated variable uses is `unused parameter`.

`Num[i32]` and `Num[u8]` are different types. Assigning one to the other is `type mismatch`. The fields still share one address.

`type Buf[N i32] union` takes a compile-time integer. `Buf[4]` names it. A field may be `[N]u8`.

`export type Num[T type] union` publishes it. Another package names `pkg.Num[i32]` when `Num` is exported. Without `export` the name is `Num is not exported`.

`extern type Num[T type] union` is `not supported yet`. `extern(abi="c") type Num[T type] union` is `extern(abi="c") on a trait or a generic`.

An empty `type Num[T type] union {}` is `not supported yet`. A value on a field, `n T = 4`, is `not supported yet`.

A method is `func Num[T]::get(self *this) T`. The call is `u.get()`.

`type Shape[T type] union(Kind)` is the tagged union. An arm names a struct, including `Arm[T]`. `One struct { value T; }` is `not supported yet`.

```c
type Num[T type] union {
    n T;
    m T;
};

func main() i32 {
    var u Num[i32];
    u.n = 42;
    return u.m;
}
```

```asm
main:
movl $42, -8(%rsp)               # constant
movl -8(%rsp), %eax              # move
retq                             # return
```

The program returns 42. `Num[i32]` is 4 bytes. `u.n` and `u.m` are the same memory.

### C unions

`extern(abi="c") union` is the C union. It does not name an enum.

`extern(abi="c") type Name union(Enum)`, a field `data union(Enum)`, and a field whose type is a tagged union, inside an `extern(abi="c")` struct or union, are `extern(abi="c") on a tagged union`.

The field may be the union, a pointer to it, an array of it, or an alias of it.

Every field starts at the same address. The size is the largest field, rounded up to the alignment, and the alignment is the strictest field.

Reading a field reinterprets the stored bits. A struct or an untagged union nested inside a C union must be `extern(abi="c")`, or the error is `struct must be extern(abi="c")`.

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

```asm
main:
movl $4, -8(%rsp)                # constant
movl -8(%rsp), %eax              # move
retq                             # return
```

`Num` is 4 bytes. `Word` is 8. `u.n` and `u.m` are the same memory, so `main` returns 4.

[Index](../README.md) · [Previous: Structs](struct.md) · [Next: Arrays](array.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
