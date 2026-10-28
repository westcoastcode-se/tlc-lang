# switch

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `switch`, which selects statements from an `i32` or an enum value.

## Fallthrough

> **Warning:** `switch` falls through until `break`.

The value promotes to `i32`, so `i8` and `i16` match.

The compiler reports `bool`, `u32`, `i64`, and `f32` as `type mismatch: expected i32`.

A `case` label is a constant `i32`, and `case 1u32` is `type mismatch`.

Each value appears once, and `default` appears once.

## A TLC enum

A TLC enum switches as that enum, so `case Color.Blue` matches a `Color`.

`case 1` is `type mismatch`, and the enum does not convert to `i32`.

A `case` that is not a constant is `not supported yet`.

Two labels with the same representation are `redefinition`.

## A C enum

An `extern(abi="c")` enum switches as `i32`, so `case Flag.On` and `case 1` are that integer.

A TLC enumerator in that `switch` is `type mismatch`.

An enum is still not a condition, so `if (value)` is `type mismatch: expected bool, found Color`.

```c
func main() i32 {
    var n i32 = 1;
    var x i32 = 0;
    switch (n) {
    case 1:
        x = 2;
    case 2:
        x = x + 40;
        break;
    default:
        x = 7;
        break;
    }
    return x;
}
```

```asm
main:
movl $1, -8(%rsp)                # constant
movl $0, -4(%rsp)                # constant
movl -8(%rsp), %eax              # move
movl %eax, -12(%rsp)             # move
subl $1, %eax                    # subtract
je .L1                           # branch
jmp .L2                          # jump
.L2:
movl -12(%rsp), %eax             # move
subl $2, %eax                    # subtract
je .L3                           # branch
jmp .L4                          # jump
.L5:
movl -4(%rsp), %eax              # move
retq                             # return
.L1:
movl $2, -4(%rsp)                # constant
.L3:
movl -4(%rsp), %eax              # move
addl $40, %eax                   # add
movl %eax, -4(%rsp)              # move
jmp .L5                          # jump
.L4:
movl $7, -4(%rsp)                # constant
jmp .L5                          # jump
```

The program returns 42.

`break` leaves the innermost loop or `switch`, and `continue` is valid inside loops.

## A switch that runs at run time

An ordinary `switch` runs at run time, even when the value is a file-scope `const`, a literal, or an enumerator.

## `comptime switch`

`comptime switch` keeps the matching case and the statements after it, through `break`, when the value folds.

> **Warning:** The other cases are not typechecked and not emitted.

A value that is not comptime is `not supported yet`.

A `comptime` parameter is comptime.

The copy that still receives it typechecks every case.

A local `const` whose initializer folds to `i32` is that value.

`const n = 1` then `case 1` returns 42.

```c
func main() i32 {
    const n = 1;
    comptime switch (n) {
    case 1:
        return 42;
    default:
        return 0;
    }
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`const m = n + 1` is that sum.

`const n i32 = 1` is that binding.

`const n = -1` then `case -1` is those same two instructions.

A local `const` hides a file-scope `const` of the same name.

A `var` does not fold.

`var n const i32` does not fold.

An initializer that does not fold is `not supported yet`.

`comptime switch` in a `macro` keeps the matching case and the statements that follow until `break` when the value folds. A value that does not fold is `not supported yet`.

```c
const n = 1;

func main() i32 {
    var x i32;
    comptime switch (n) {
    case 1:
        x = 2;
    case 2:
        x = x + 40;
        break;
    default:
        x = 7;
        break;
    }
    return x;
}
```

```asm
main:
movl $2, -4(%rsp)                # constant
movl -4(%rsp), %eax              # move
addl $40, %eax                   # add
movl %eax, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

The program returns 42.

`default` is not in the function.

A local `const n = 1` is those same instructions.

```c
type Color enum {
    Red,
    Green = 40,
    Blue,
};

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

```asm
code:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
movl %eax, -8(%rsp)              # move
subl $40, %eax                   # subtract
je .L1                           # branch
jmp .L2                          # jump
.L2:
movl -8(%rsp), %eax              # move
subl $41, %eax                   # subtract
je .L3                           # branch
jmp .L4                          # jump
.L1:
movl $2, %eax                    # constant
retq                             # return
.L3:
movl $4, %eax                    # constant
retq                             # return
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
```

`code(Color.Blue)` returns 4.

`case 41` on that `switch` is `type mismatch`.

`comptime switch (Color.Blue)` that returns 42 from that case is two instructions.

There is no compare.

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
