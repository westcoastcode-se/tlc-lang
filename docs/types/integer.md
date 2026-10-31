# Numeric types

[Index](../README.md) · [Previous: extern](../packages/extern.md) · [Next: Pointers](pointer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page lists every numeric type, its bit width, and the rules for converting integers.

## Bit widths

The numeric types are `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, and `f64`.

`bool` and `void` are not numeric types.

On `x86_64-pc-linux-gnu`, a pointer is 64 bits wide.

| Type | Bits |
| --- | --- |
| `i8`, `u8` | 8 |
| `i16`, `u16` | 16 |
| `i32`, `u32` | 32 |
| `i64`, `u64` | 64 |
| `f32` | 32 |
| `f64` | 64 |
| pointer | 64 |

## How a numeric type is spelled

A numeric type is `i`, `u`, or `f` followed by the width.

Integer widths are 8, 16, 32, and 64, and float widths are 32 and 64.

`int`, `uint`, `short`, `long`, `ulong`, `char`, `float`, and `double` are ordinary identifiers.

The compiler reports `not supported yet` when one of those names is used where a type is required.

The compiler reports `signed` and `unsigned` as `type mismatch`.

## Extension and assignment

`i8` is sign-extended, and `u8` is zero-extended.

A character literal has type `u8`.

`*i32` and `*u32` are different types.

Integer types are assignment-compatible with each other and with `bool`.

```c
func main() i32 {
    var a i16 = 1;
    var b i64 = 2;
    var c u8 = 65;
    var d i8 = c;
    var e u8 = c;
    return a + b + d + e;
}
```

```asm
main:
movw $1, -18(%rsp)               # constant
movq $2, -16(%rsp)               # constant
movb $65, -3(%rsp)               # constant
movzbl -3(%rsp), %eax            # move
movb %al, -2(%rsp)               # move
movzbl -3(%rsp), %eax            # move
movb %al, -1(%rsp)               # move
movswl -18(%rsp), %eax           # move
movq -16(%rsp), %rcx             # move
cltq
addq %rcx, %rax                  # add
movsbl -2(%rsp), %ecx            # move
movslq %ecx, %rcx                # move
addq %rcx, %rax                  # add
movzbl -1(%rsp), %ecx            # move
movl %ecx, %ecx                  # move
addq %rcx, %rax                  # add
retq                             # return
```

## Rank

The rank order, from narrowest to widest, is:

1. `bool`
2. `i8` and `u8`
3. `i16` and `u16`
4. `i32` and `u32`
5. `i64` and `u64`

An unsigned type has the same rank as its signed counterpart.

## Arithmetic conversions

A value whose rank is below `i32` widens to `i32`, with `i8` sign-extended and `u8` zero-extended.

The usual arithmetic conversions apply after that widening.

`+`, `-`, `*`, `/`, `%`, and the bitwise operators produce the common type.

Comparisons and `!` produce `i32`, either 0 or 1.

Unsigned arithmetic wraps.

A `u32` that is 0 and then decreased by 1 becomes `4294967295`, so `n < 1` is false.

A signed quotient wraps in its width. `(-2147483647 - 1) / -1` is `-2147483648`, and `(-2147483647 - 1) % -1` is `0`.

> **Warning:** A mixed comparison converts the signed operand. `1u32 < -1` is true, because `-1` becomes the largest unsigned value.

## Stores

Assignment is allowed among the integer ranks, `u8`, and `bool`.

A store into `bool` becomes 0 or 1, and a store into a narrower integer keeps the low bits.

## A rejected program

```c
func main() i32 {
    var n u32 = 0;
    var flag bool = 2;
    var narrow i8 = 300;
    n = n - 1;
    if (n < 1) {
        return 0;
    }
    return flag + narrow;
}
```

The compiler rejects this example as `type mismatch` and emits nothing.

[Index](../README.md) · [Previous: extern](../packages/extern.md) · [Next: Pointers](pointer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
