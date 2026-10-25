# Enums

[Index](../README.md) · [Previous: Arrays](array.md) · [Next: Type aliases](alias.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

An enum is its own type. A value is named with the type, a dot, and the enumerator. It is not an `i32`, and it does not convert to its representation. `type Color enum {}` has no enumerators. `sizeof(Color)` is 4. `Color.Red` is reported as `undeclared identifier`. The semicolon after `}` may be omitted.

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

`sizeof(Tiny)` is 1. The primitive may be `bool` or an integer type, including `i8`, `u32`, and `u8`. A value that does not fit is reported as `type mismatch`. An enumerator is not an lvalue. `==` and `!=` compare two values of the same enum and yield `bool`. Other operators are reported as `type mismatch`.

`switch` on the enum uses that enum. A `case` is one of its enumerators. `case 1` is `type mismatch`. The enum does not convert to its representation, so `var n i32 = Color.Red` is `type mismatch`. `if (value)` is `type mismatch: expected bool, found Color`. The condition is `value == Color.Blue`.

```c
func code(value Color) i32 {
    switch (value) {
    case Color.Green:
        return 2;
    case Color.Blue:
        return 4;
    default:
        return 0;
    }
}
```

## C enums

`extern(abi="c") type Name enum` is the C form. It has no underlying type of its own. The representation is `i32`, and a value may be passed or compared where an integer is expected. `switch` on that enum is an `i32` switch, so `case Flag.On` and `case 1` both match. `if (f)` is still `type mismatch: expected bool, found Flag`. `extern(abi="c") type Name enum(i8)` is reported as `extern enum`. Methods are still allowed.

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

## Assembly

```asm
main:
movl $41, -4(%rsp)               # constant
cmpl $41, -4(%rsp)               # compare
jne .L1                          # branch
movl $1, %eax                    # constant
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Arrays](array.md) · [Next: Type aliases](alias.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
