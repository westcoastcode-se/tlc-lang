# Numeric types

[Index](../README.md) · [Previous: extern](../packages/extern.md) · [Next: Pointers](pointer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

Numeric types are `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, and `f64`. `bool` and `void` are not numeric types. On `x86_64-pc-linux-gnu` a pointer is 64 bits.

| Type | Bits |
| --- | --- |
| `i8`, `u8` | 8 |
| `i16`, `u16` | 16 |
| `i32`, `u32` | 32 |
| `i64`, `u64` | 64 |
| `f32` | 32 |
| `f64` | 64 |
| pointer | 64 |

## Names

A numeric type is `i`, `u`, or `f` followed by the width. Integer widths are 8, 16, 32, and 64. Float widths are 32 and 64. `int`, `uint`, `short`, `long`, `ulong`, `char`, `float`, and `double` are ordinary identifiers. Using one where a type is required is reported as `not supported yet`. `signed` and `unsigned` are reported as `type mismatch`.

`i8` is sign-extended. `u8` is zero-extended. A character literal has type `u8`. `*i32` and `*u32` are different types. Integer types are assignment-compatible with each other and with `bool`.

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

## Rank and conversion

The rank order, from narrowest to widest, is:

1. `bool`
2. `i8` and `u8`
3. `i16` and `u16`
4. `i32` and `u32`
5. `i64` and `u64`

An unsigned type has the same rank as its signed counterpart.

A value whose rank is below `i32` widens to `i32`. `i8` is sign-extended. `u8` is zero-extended. The usual arithmetic conversions apply after that. `+`, `-`, `*`, `/`, `%`, and the bitwise operators produce the common type. Comparisons and `!` produce `i32`, either 0 or 1.

Unsigned arithmetic wraps. A `u32` that is 0 and then decreased by 1 becomes `4294967295`, so `n < 1` is false. A mixed comparison converts the signed operand. `1u32 < -1` is true, because `-1` becomes the largest unsigned value.

Assignment is allowed among the integer ranks, `u8`, and `bool`. A store into `bool` becomes 0 or 1. A store into a narrower integer keeps the low bits.

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

## Cost

An `i32` local with no initializer executes 1 `movl` of 0. `var a i32 = 20` executes 1 `movl` of 20. Reading the local is 1 `movl`.

`a + b`, `a - b`, `a * b`, `a & b`, `a | b`, and `a ^ b` on two `i32` locals are 2 instructions: load `a`, then the operation with `b` read from its slot. `a / b` is 3: load, `cltd`, `idivl`. `a % b` is 4, because the remainder is moved out of `%edx`. `a << b` and `a >> b` are 3: load the value, load the count into `%ecx`, shift. `-a` is 2: clear `%eax`, then subtract `a`. `~a` is 2: load, then `xorl $-1`. A comparison such as `a < b` or `a == b` is 5: load, compare, `setcc`, mask the bit, zero-extend to `i32`.

A parameter is stored on entry before any of those. An `i32` parameter is 1 `movl`. A function that only returns `a + b` for two `i32` parameters is 5 instructions including that pair of stores and `retq`.

`i8` and `i16` are widened to `i32` at each use. `a + b` on two `i8` parameters is 7 instructions plus `retq`: the bytes are stored, each is sign-extended, and then they are added. A `u8` source is zero-extended. `var d i8 = c` keeps the low 8 bits in 1 byte store. Storing into a `bool` is `testl` and `setne`, and that 0 or 1 is the store.

`u32` addition is the same 2 instructions as `i32` addition. A constant expression such as `return 40 + 2` is folded, and `main` is 2 instructions.

[Index](../README.md) · [Previous: extern](../packages/extern.md) · [Next: Pointers](pointer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
