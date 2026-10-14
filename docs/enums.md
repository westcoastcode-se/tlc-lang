# Enums and type aliases

[Index](README.md) · [Previous: Methods](methods.md) · [Grammar](grammar.md)

An enum is its own type. A value is named with the type, a dot, and the enumerator. It is not an `i32`, and it does not convert to its representation. The semicolon after `}` may be omitted.

```c
type Color enum {
    Red,
    Green = 40,
    Blue,
};

func main() i32 {
    value := Color.Blue;
    if (value == Color.Blue) {
        return 1;
    }
    return 0;
}
```

## Values

`Red` is 0, `Green` is 40, and `Blue` is 41. Without an explicit value, the next constant is one higher than the previous one. The representation is `i32` when the definition does not name a primitive. A primitive is written in parentheses after `enum`:

```c
type Tiny enum(i8) {
    A,
    B,
};

func main() i32 {
    var v Tiny = Tiny.B;
    if (v == Tiny.A) {
        return 0;
    }
    return sizeof(Tiny);
}
```

`sizeof(Tiny)` is 1. The primitive may be `bool` or an integer type, including `i8`, `u32`, and `u8`. A value that does not fit is reported as `type mismatch`. An enumerator is not an lvalue. `==` and `!=` compare two values of the same enum and yield `i32`. Other operators are reported as `type mismatch`.

## C enums

`extern(abi="c") type Name enum` is the C form. It has no underlying type of its own. The representation is `i32`, and a value may be passed or compared where an integer is expected. `extern(abi="c") type Name enum(i8)` is reported as `extern enum`. Methods are still allowed.

```c
extern(abi="c") type Flag enum {
    Off,
    On = 1,
}

func Flag::enabled(self this) i32 {
    if (self == Flag.On) {
        return 1;
    }
    return 0;
}

func main() i32 {
    var n i32 = Flag.On;
    var f Flag = Flag.On;
    return n + f.enabled();
}
```

## Export and methods

An exported enum exports the type. The enumerators come with it.

```c
import { Color } from "geom";

func main() i32 {
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

func Color::code(self this) i32 {
    if (self == Color.Blue) {
        return 41;
    }
    return 0;
}

func Color::label() i32 {
    return 1;
}

func main() i32 {
    var value = Color.Blue;
    return value.code() + Color.label();
}
```

`value.code()` passes `value`. The method is written in the same file as `type Color enum`. Every file in the package can call it, and it is not exported.

## Type aliases

```c
type Point struct {
    x i32;
    y i32;
};

type Ptr *Point;
type Integer i32;
type Pair [2]i32;

func main() i32 {
    var p Point;
    p.x = 40;
    p.y = 2;
    var q Ptr = Ptr(&p);
    var n Integer = Integer(0);
    var pair Pair;
    pair[0] = p.x;
    return (*q).y + i32(n) + pair[0];
}
```

`type Integer i32` is a distinct type with the same representation as `i32`. A cast confirms which type a value has: `Integer(10)` and `i32(n)`. `var n Integer = 10`, `var wide i32 = n`, and `n + 1` are reported as `type mismatch`. `n + Integer(1)` has type `Integer`. A condition accepts the alias, as in `if (n)`. Two aliases of `i32`, such as `Integer` and `Count`, are different types. `Ptr` is different from `*Point`, and `Pair` is different from `[2]i32`. `pair[0]` still has the element type. A parameter, a result, and a field can use the name. `sizeof(Integer)` is `sizeof(i32)`. `typeof(Integer).name()` is `Integer`. A file-scope `const value Integer = Integer(10)` and `const value = Integer(10)` have type `Integer`. That name is not an array length. A primitive alias can have methods, static functions, and `var Integer::ten`. `Ptr`, `Pair`, and a struct alias such as `type Spot Point` cannot. The builtin name `i32` cannot either.

`type Point struct`, `type Color enum`, and `type Shape trait` define a new type. `type Point Point` is reported as `redefinition`, because the alias repeats the struct. `type Point i32` beside `type Point struct` is reported as `redefinition`. A variable and `sizeof` use the bare name, `var p Point` and `sizeof(Point)`. `struct` in that position, as in `var p struct Point` or `sizeof(struct Point)`, is reported as `expected a type`. An exported type alias is imported as a type name.

## Keywords

Beyond C, `import`, `export`, `package`, `trait`, `func`, `defer`, `var`, and `type` are keywords. `impl`, `from`, `as`, and `this` are identifiers. The full list is in the [grammar](grammar.md#tokens).

[Index](README.md) · [Previous: Methods](methods.md) · [Grammar](grammar.md)
