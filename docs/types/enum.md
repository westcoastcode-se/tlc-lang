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

```asm
main:
movb $1, -1(%rsp)                # constant
movsbl -1(%rsp), %eax            # move
cmpl $0, %eax                    # compare
jne .L1                          # branch
xorl %eax, %eax                  # zero the result
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
movl $1, %eax                    # constant
retq                             # return
```

`sizeof(Tiny)` is 1. The primitive may be `bool` or an integer type, including `i8`, `u32`, and `u8`. A value that does not fit is reported as `type mismatch`. An enumerator is not an lvalue. `==` and `!=` compare two values of the same enum and yield `bool`. Other operators are reported as `type mismatch`.

`switch` on the enum uses that enum. A `case` is one of its enumerators. `case 1` is `type mismatch`. The enum does not convert to its representation, so `var n i32 = Color.Red` is `type mismatch`. `if (value)` is `type mismatch: expected bool, found Color`. The condition is `value == Color.Blue`. An ordinary `switch`, including `switch (Color.Blue)`, typechecks every case. `comptime switch (Color.Blue)` keeps that case and the statements that follow until `break`. The other cases are not typechecked and not emitted. A `var` does not fold, so `comptime switch` on it is `not supported yet`.

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

`code(Color.Blue)` is 4. `Blue` is 41, one past `Green`:

```asm
main:
# trace frame for the call, then the matching pop
movl $41, %edi                   # Color.Blue
callq code
retq

code:
movl %edi, -4(%rsp)
movl -4(%rsp), %eax
movl %eax, -8(%rsp)
subl $40, %eax                   # case Green
je .L1
jmp .L2
.L2:
movl -8(%rsp), %eax
subl $41, %eax                   # case Blue
je .L3
jmp .L4
.L1:
movl $2, %eax                    # Green
retq
.L3:
movl $4, %eax                    # Blue
retq
.L4:
xorl %eax, %eax                  # default
retq
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

```asm
enabled:
movl %edi, -4(%rsp)              # move
cmpl $1, -4(%rsp)                # compare
jne .L1                          # branch
movl $1, %eax                    # constant
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
xorl %eax, %eax                  # zero the result
retq                             # return
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

The binding is not read, so the function is the return:

```asm
main:
xorl %eax, %eax                  # return 0
retq
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

```asm
label:
movl $1, %eax                    # constant
retq                             # return

code:
movl %edi, -4(%rsp)              # move
cmpl $41, -4(%rsp)               # compare
jne .L1                          # branch
movl $41, %eax                   # constant
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
xorl %eax, %eax                  # zero the result
retq                             # return
```

`value.code()` passes `value`. The method is written in the same file as `type Color enum`. Every file in the package can call it, and it is not exported.

[Index](../README.md) · [Previous: Arrays](array.md) · [Next: Type aliases](alias.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
