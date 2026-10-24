# Literals

[Index](../README.md) · [Previous: void, bool, and floating-point types](float.md) · [Next: Strings](string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

| Literal | Type |
| --- | --- |
| `42` | `i32` |
| `42i8`, `42u8` | `i8` and `u8`, when the value fits |
| `42i16`, `42u16` | `i16` and `u16`, when the value fits |
| `42i32`, `42u32` | `i32` and `u32`, when the value fits |
| `42i64`, `42u64` | `i64` and `u64`, when the value fits |
| `'A'` | `u8` |
| `"hi"` | `[]const u8` |
| `` `hi` `` | `[]const u8`; the text may span lines |
| `true`, `false` | `bool` |
| `null` | null pointer, stored only in `?*T` |

An unsuffixed number greater than `2147483647` is reported as `type mismatch`. It does not become `i64`. A suffix is one of `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, and `u64`. `1u` and `1l` are reported as `invalid integer suffix`. A value that does not fit in the suffix is reported as `type mismatch`.

A character literal has type `u8`. `'A'` is 65 and `'\n'` is 10. A value above 255 is reported as `type mismatch`. The escapes are `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\a`, `\b`, `\f`, `\v`, `\?`, octal digits, and `\x` followed by hex digits.

An integer literal is decimal, `0x` hex, `0b` binary, or `0` octal. A `'` between digits is a separator. `0x1p+4` is a hexadecimal floating literal. It is `f64`, and it needs a binary exponent. A suffix `f` makes a floating literal `f32`.

```c
func main() i32 {
    var n = 1'000;
    var hex = 0x2A;
    var bits = 0b1010;
    var oct = 010;
    var narrow i8 = 20i8;
    var wide i16 = 42i16;
    var ch = '\n';
    var fp = 0x1p+4;
    if (fp == 16.0 && ch == 10) {
        return hex;
    }
    return n + bits + oct + narrow + wide;
}
```

## Cost

A literal has no instructions of its own. `return 4` is `movl $4, %eax` and `retq`. `return 0` is `xorl %eax, %eax` and `retq`. A suffix, a base prefix, and a digit separator are resolved while compiling. A character literal is the same immediate as its `u8` value.

[Index](../README.md) · [Previous: void, bool, and floating-point types](float.md) · [Next: Strings](string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
