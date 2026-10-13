# Enums and type aliases

[Index](README.md) · [Previous: Methods](methods.md) · [Grammar](grammar.md)

An enum is its own type. A value is named with the type, a dot, and the enumerator. It is not an `int`, and it does not convert to its representation. The semicolon after `}` may be omitted.

```c
type Color enum {
    Red,
    Green = 40,
    Blue,
};

func main() int {
    value := Color.Blue;
    if (value == Color.Blue) {
        return 1;
    }
    return 0;
}
```

## Values

`Red` is 0, `Green` is 40, and `Blue` is 41. Without an explicit value, the next constant is one higher than the previous one. The representation is `int` when the definition does not name a primitive. A primitive is written in parentheses after `enum`:

```c
type Tiny enum(i8) {
    A,
    B,
};

func main() int {
    var v Tiny = Tiny.B;
    if (v == Tiny.A) {
        return 0;
    }
    return sizeof(Tiny);
}
```

`sizeof(Tiny)` is 1. The primitive may be `bool` or an integer type, including `i8`, `u32`, and `char`. A value that does not fit is reported as `type mismatch`. An enumerator is not an lvalue. `==` and `!=` compare two values of the same enum and yield `int`. Other operators are reported as `type mismatch`.

## C enums

`extern(abi="c") type Name enum` is the C form. It has no underlying type of its own. The representation is `int`, and a value may be passed or compared where an integer is expected. `extern(abi="c") type Name enum(i8)` is reported as `extern enum`. Methods are still allowed.

```c
extern(abi="c") type Flag enum {
    Off,
    On = 1,
}

func Flag::enabled(self this) int {
    if (self == Flag.On) {
        return 1;
    }
    return 0;
}

func main() int {
    var n int = Flag.On;
    var f Flag = Flag.On;
    return n + f.enabled();
}
```

## Export and methods

An exported enum exports the type. The enumerators come with it.

```c
import { Color } from "geom";

func main() int {
    value := Color.Blue;
    return 0;
}
```

A method uses the same form as a struct method. `this` is the enum value. A function with no `this` parameter is static and is called as `Color.label()`.

```c
type Color enum {
    Red,
    Green = 40,
    Blue,
};

func Color::code(self this) int {
    if (self == Color.Blue) {
        return 41;
    }
    return 0;
}

func Color::label() int {
    return 1;
}

func main() int {
    var value = Color.Blue;
    return value.code() + Color.label();
}
```

`value.code()` passes `value`. The method is written in the same file as `type Color enum`. Every file in the package can call it, and it is not exported.

## Type aliases

```c
type Point struct {
    x int;
    y int;
};

type Ptr *Point;
type Integer int;
type Pair [2]int;

func main() int {
    var p Point;
    p.x = 40;
    p.y = 2;
    var q Ptr = &p;
    var n Integer = 0;
    var pair Pair;
    pair[0] = p.x;
    return (*q).y + n + pair[0];
}
```

`type Integer int` gives another name to a type. `type Point struct`, `type Color enum`, and `type Shape trait` define a new type. `type Point Point` is reported as `redefinition`, because the alias repeats the struct. `type Point int` beside `type Point struct` is reported as `redefinition`. A variable and `sizeof` use the bare name, `var p Point` and `sizeof(Point)`. `struct` in that position, as in `var p struct Point` or `sizeof(struct Point)`, is reported as `expected a type`. An exported type alias is imported as a type name.

## Keywords

Beyond C, `import`, `export`, `package`, `trait`, `func`, `defer`, `var`, and `type` are keywords. `impl`, `from`, `as`, and `this` are identifiers. The full list is in the [grammar](grammar.md#tokens).

[Index](README.md) · [Previous: Methods](methods.md) · [Grammar](grammar.md)
