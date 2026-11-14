# Literals

[Index](../README.md) · [Previous: void, bool, and floating-point types](float.md) · [Next: Strings](string.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page lists each literal spelling and the type that spelling has.

## Types

| Literal | Type |
| --- | --- |
| `42` | `i32` |
| `3000000000` | `i64` |
| `42i8`, `42u8` | `i8` and `u8`, when the value fits |
| `42i16`, `42u16` | `i16` and `u16`, when the value fits |
| `42i32`, `42u32` | `i32` and `u32`, when the value fits |
| `42i64`, `42u64` | `i64` and `u64`, when the value fits |
| `'A'` | a Unicode code point, `i32` |
| `'A'b` | `u8` |
| `"hi"` | `[]const u8` |
| `"hi"b` | `[]const u8` |
| `` `hi` `` | `[]const u8`; the text may span lines |
| `` `hi`b `` | `[]const u8`; the text may span lines |
| `true`, `false` | `bool` |
| `null` | null pointer, stored only in `?*T` |

## Integer suffixes

An unsuffixed number that fits in `i32` is `i32`. A larger number that fits in `i64` is `i64`. `3000000000` is that `i64`.

A number past `i64`, such as `18446744073709551615`, is `type mismatch`.

Another primitive type is a suffix or a cast. `3000000000u32` is `u32`. `i32(3000000000)` is that cast.

```c
func main() i32 {
    var wide = 3000000000;
    if (wide == 3000000000) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movl $3000000000, %eax           # wide
movq %rax, -8(%rsp)              # store wide
movl $3000000000, %eax           # 3000000000
cmpq %rax, -8(%rsp)              # wide == 3000000000
jne .L1                          # branch
movl $42, %eax                   # 42
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
xorl %eax, %eax                  # zero the result
retq                             # return
```

A suffix is one of `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, and `u64`.

The compiler reports `1u` and `1l` as `invalid integer suffix`.

The compiler reports a value that does not fit in the suffix as `value does not fit`. `256u8` and `4294967296u32` are that error.

## Character literals

A character literal is a Unicode code point stored as `i32`, so `'A'` is 65. `'A'b` and `'\n'b` are `u8`.

`'\x100'` and `'\400'` are the code point 256.

The character may be written in the source as UTF-8. `'å'` is 229, `'€'` is 8364, and `'😀'` is 128512. Bytes that are not one UTF-8 sequence, such as a lone continuation byte, an overlong form, or a surrogate, are `unexpected character`.

`'å'b` is the byte 229, the same as `'\xE5'b`. `'€'b` is above 255 and is `type mismatch`.

A code point above U+10FFFF, such as `'\x110000'` or `'\x10000000000000041'`, is `type mismatch`.

The compiler reports a byte character above 255 as `type mismatch`.

The escapes are `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\a`, `\b`, `\f`, `\v`, `\?`, octal digits, and `\x` followed by hex digits.

## Bases and floating literals

An integer literal is decimal, `0x` hex, `0b` binary, or `0` octal.

A `'` between digits is a separator and does not change the value.

`0x1p+4` is a hexadecimal floating literal of type `f64`, and it needs a binary exponent.

A suffix `f` makes a floating literal `f32` rather than `f64`.

## A mix of literal forms

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

```asm
main:
movl $1000, -32(%rsp)            # constant
movl $42, -28(%rsp)              # constant
movl $10, -24(%rsp)              # constant
movl $8, -20(%rsp)               # constant
movb $20, -15(%rsp)              # constant
movw $42, -14(%rsp)              # constant
movl $10, -12(%rsp)              # constant
movsd (%rip), %xmm0              # move
movsd %xmm0, -8(%rsp)            # move
movsd -8(%rsp), %xmm0            # move
xorl %eax, %eax                  # zero the result
movsd (%rip), %xmm1              # move
ucomisd %xmm1, %xmm0
movb %al, -33(%rsp)              # move
jne .L1                          # branch
jp .L1                           # branch
cmpl $10, -12(%rsp)              # compare
sete %al                         # flag to byte
movb %al, -33(%rsp)              # move
.L1:
movb -33(%rsp), %al              # move
testb $1, %al                    # test
jne .L2                          # branch
jmp .L3                          # jump
.L2:
movl -28(%rsp), %eax             # move
retq                             # return
.L3:
jmp .L4                          # jump
.L4:
movl -32(%rsp), %eax             # move
addl -24(%rsp), %eax             # add
addl -20(%rsp), %eax             # add
movsbl -15(%rsp), %ecx           # move
addl %ecx, %eax                  # add
movswl -14(%rsp), %ecx           # move
addl %ecx, %eax                  # add
retq                             # return
```

[Index](../README.md) · [Previous: void, bool, and floating-point types](float.md) · [Next: Strings](string.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
