# Structs, pointers, and arrays

[Index](README.md) · [Previous: Builtin](builtin.md) · [Next: Methods](methods.md) · [Grammar](grammar.md)

A struct definition is `type Name struct`. `type Vec[T type] struct` is generic. A use writes the arguments after the name, `Vec[int]`. A method is `func Vec[T]::get(self *this, i int) T` in the same file. The names in `Vec[T]` are the struct's parameters, in that order. `type Buf[N int, T type] struct` uses `func Buf[N, T]::get`. Leaving the list out, or writing a different name, is `type mismatch`. The wrong count is `argument count`. `func Vec[T type]::get` is `type mismatch`. `Vec` with no arguments is `type mismatch`. `Vec[int]` and `Vec[char]` are different types. Assigning one to the other, or passing one where the parameter is the other, is `type mismatch`. Variables, parameters, return types, and fields then use the bare name. A pointer is written `*Point` and `*int`, not `Point *` or `int *`, in a parameter, a result, and every other type position. `?*Point` may be `null`. `*Point` may not. A field is a name, then its type, in the same order and with the same type syntax as `var`: `x int`, `next ?*Node`, `home *Address`, `raw [8]char`. A designated initializer that omits a `*T` field is `type mismatch`. An omitted `?*T` field is `null`. The semicolon after `}` may be omitted. A field still ends with `;`.

```c
type Point struct {
    x int;
    y int;
};

func make(x int, y int) Point {
    var p Point;
    p.x = x;
    p.y = y;
    return p;
}

func main() int {
    var p Point;
    var q *Point;
    p = make(40, 2);
    q = &p;
    return (*q).x + q.y;
}
```

This program returns 42.

A struct may contain a pointer to itself. `type Wide struct(align=16)` sets the alignment. The value is a positive power of two, at most 4096. The size is rounded up to that alignment. An alignment weaker than a field requires is `type mismatch`.

```c
type Wide struct(align=16) {
    n int;
}
```

`sizeof(Wide)` is 16.

```c
type Node struct {
    n int;
    next *Node;
};
```

A struct value names its fields. The type comes from the place the value is used: the variable, the assignment, the argument, or the result. A field that is left out is zero. Naming a field twice is `redefinition`. An unknown field is `undeclared identifier`. Used where the type is not a struct, the initializer is `type mismatch`. `{10, 20}` is a value list, so where a struct is expected it is `type mismatch`. An empty `{}` leaves every field zero. Mixing a named field and a positional value is `type mismatch`.

```c
type Inner struct {
    inner_value int;
};

type Box struct {
    value int;
    complex Inner;
};

func main() int {
    var box Box = {.value = 10, .complex = {.inner_value = 20}};
    return box.value + box.complex.inner_value;
}
```

This returns 30. `box = {.complex = {.inner_value = 42}}` replaces the whole value, so `box.value` becomes 0. A global initializer must be a constant, the same rule as any other global. A call in a local initializer is allowed.

A struct or an enum is defined by its body. `type Node struct`, `extern(abi="c") type Node struct`, `type Color enum`, and `extern(abi="c") type Color enum` are `expected '{'` without that body. A struct defined in another file of the same package is already visible, including through a pointer, so a separate declaration is not required.

An `extern(abi="c") struct` is marked so it can be used with an `extern(abi="c")` function. An empty body is an opaque handle: the type has no fields and occupies one byte, and a library uses it through a pointer. `extern struct` uses the compiler's own layout.

```c
extern(abi="c") type Pair struct {
    a int;
    b int;
};

extern(abi="c") type SDL_Window struct {}

extern(abi="c") func make_pair(a int, b int) Pair;
extern(abi="c") func SDL_DestroyWindow(window *SDL_Window) void;
```

A plain struct in an `extern(abi="c")` function is `struct must be extern(abi="c")`.

A tlc union names the enum that selects the active field. The enum is in the same package. Each enumerator is one field, and each field is one enumerator. A field that is not an enumerator is `undeclared identifier`. An enumerator with no field is `type mismatch`. The field's type is a struct. It can be written inline, or it can name a struct that is already declared. A field that is not a struct is `type mismatch`. The name in parentheses is `undeclared identifier` when it names nothing, and `type mismatch` when it is not an enum.

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
```

The value stores one payload and the enum tag after it. Every field starts at offset 0, and the payload is large enough for the largest field. With `Circle` as one `f32` and `Rectangle` as two, `Shape` is 12 bytes: 8 for the payload and 4 for the `int` tag. Assigning a field, or a field of that struct, stores that enumerator as the tag. `{.Circle = {.radius = 1.0}}` does the same. `ShapeType(shape)` reads the tag. Reading a field does not check the tag. A zero value has tag 0.

`extern(abi="c") union` is the C union. Every field starts at the same address. The size is the largest field, rounded up to the alignment, and the alignment is the strictest field. Reading a field reinterprets the stored bits. A struct or a union nested inside a C union must be `extern(abi="c")`, or the error is `struct must be extern(abi="c")`.

```c
extern(abi="c") type Num union {
    n int;
    m int;
};

extern(abi="c") type Word union {
    n int;
    raw [8]char;
};
```

`Num` is 4 bytes. `Word` is 8. `u.n` and `u.m` are the same memory.

Values and pointers both use `.`. A pointer whose pointee is a struct is dereferenced once. `**Point` and `*int` are left as they are, and the access is `type mismatch`. A `?*Point` is `type mismatch` until the name is known to be non-null, or until that use is written `p!.field`. A field reached through a pointer is an lvalue. Assigning a field of a struct value requires the struct to be an lvalue.

An array length is a positive decimal `int` literal.

```c
func add4(p *int) int {
    return p[0] + p[1] + p[2] + p[3];
}

func main() int {
    var a [4]int;
    a[0] = 10;
    a[1] = 20;
    a[2] = 7;
    a[3] = 5;
    return add4(a);
}
```

The array becomes a pointer when it is passed. A string is an array of `char` and can be passed to `*char`. The index has type `int`.

An exported struct is named with the package and without the `struct` keyword:

```c
import "geom";

func main() int {
    var p geom.Point;
    p.x = 40;
    p.y = 2;
    return geom.sum(p);
}
```

```c
package geom;

export type Point struct {
    x int;
    y int;
};

export func sum(p Point) int {
    return p.x + p.y;
}
```

Inside the package the type is written `Point`. `import { Point } from "geom"` makes `Point` a type name in the importing file. A struct is visible to every file in the package, so one file can declare `type A struct { b B; }` when `type B struct` is in another file of that package.

[Index](README.md) · [Previous: Builtin](builtin.md) · [Next: Methods](methods.md) · [Grammar](grammar.md)
