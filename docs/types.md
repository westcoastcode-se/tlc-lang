# Types

[Index](README.md) · [Previous: Packages](packages.md) · [Next: Expressions](expressions.md) · [Grammar](grammar.md)

Integers on `x86_64-pc-linux-gnu` follow LP64. `long` and `i64` are both 64 bits. They are different types.

| Type | Bits |
| --- | --- |
| `char` | 8 |
| `i8`, `u8` | 8 |
| `short`, `i16`, `u16` | 16 |
| `int`, `uint`, `i32`, `u32` | 32 |
| `long`, `ulong` | 64 |
| `i64`, `u64` | 64 |
| pointer | 64 |

`signed` and `unsigned` are not keywords. `long long` is `type mismatch`. Write `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, and `u64`. `short int` is `short`. `long int` is `long`. Contradictory specifiers are `type mismatch`.

`char`, `i8`, and `u8` are three types. They are assignment-compatible. `*int` and `*uint` are different types.

`*T` is a pointer with a value. `?*T` is that pointer or `null`. `*?*T` and `?**T` are different types. `?` on a type that is not a pointer, such as `?int`, is `not supported yet`.

`i8` and `u8` are the 8-bit integers. `i16` is `short` and `u16` is its unsigned counterpart. `i32` is `int` and `u32` is `uint`. `i64` and `u64` are the other 64-bit integers. `ulong` is 64 bits and a different type from `u64`. `long` stays a different 64-bit type from `i64`. `char` stays different from `i8`.

The rank order is `bool`, then `char`, `i8`, and `u8`, then `short`, `i16`, and `u16`, then `int`, `i32`, `uint`, and `u32`, then `long` and `ulong`, then `i64` and `u64`. An unsigned type has the same rank as its signed counterpart.

A type whose rank is below `int` widens to `int` when it is used as a value. `i8` is sign-extended. `char` and `u8` are zero-extended. The usual arithmetic conversions apply after that. `+`, `-`, `*`, `/`, `%`, and the bitwise operators produce the common type. Comparisons and `!` produce `int`, either 0 or 1.

Unsigned arithmetic wraps. A `uint` that is 0 and then decreased by 1 becomes `4294967295`, so `n < 1` is false.

A mixed comparison converts the signed operand. `1u32 < -1` is true, because `-1` becomes the largest unsigned value.

Assignment is allowed among the integer ranks, `char`, and `bool`. A store into `bool` becomes 0 or 1. A store into a narrower integer keeps the low bits.

`void` is a return type and a pointee. It is neither a value nor a variable type.

`float` and `f32` are the same 32-bit type. `double` and `f64` are the same 64-bit type. A literal without a suffix has type `double`. A suffix `f` or `F` makes it `float`. `1.5 + 1` is `double`. Comparisons of floating values yield `int`. `%` and bitwise operators are `type mismatch` on a floating value.

A `bool` value is 0 or 1.

## Literals

| Literal | Type |
| --- | --- |
| `42` | `int` |
| `42i8`, `42u8` | `i8` and `u8`, when the value fits |
| `42i16`, `42u16` | `i16` and `u16`, when the value fits |
| `42i32`, `42u32` | `int` and `uint`, when the value fits |
| `42i64`, `42u64` | `i64` and `u64`, when the value fits |
| `'A'` | `char` |
| `"hi"` | array of `char`, including the terminating null |
| `` `hi` `` | the same array type; the text may span lines |
| `true`, `false` | `bool` |
| `null` | null pointer, stored only in `?*T` |

An unsuffixed number greater than `2147483647` is `type mismatch`. It does not become `i64`. A suffix is one of `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, and `u64`. `1u` and `1l` are `invalid integer suffix`. A value that does not fit in the suffix is `type mismatch`.

A character literal has type `char`. `'A'` is 65 and `'\n'` is 10. A value above 255 is `type mismatch`. The escapes are `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\a`, `\b`, `\f`, `\v`, `\?`, octal digits, and `\x` followed by hex digits.

A string between backticks may span lines. It is an array of `char`, like a quoted string. The characters are copied as written: a backslash is not an escape, and a newline in the source is a newline in the string. The next backtick ends the string.

```c
var text *char = `hello
world`;
```

`sizeof` produces `int`. `sizeof(char)` is 1, `sizeof(int)` is 4, `sizeof(i64)` is 8, and a pointer is 8. Both `sizeof(int)` and `sizeof n` are accepted. `sizeof(Point)` and `sizeof(enum Color)` name a declared type. `sizeof(struct Point)` is `expected a type`. `sizeof(Type)` is 8.

`Type` is the type of a tlc type. `typeof(int)` and `typeof n` produce a `Type`. `typeof(Point)`, `typeof(*int)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)` is `expected a type`. `i32` is `int`, so `typeof(i32)` and `typeof(int)` are the same value. `typeof` does not decay an array or a function. A function, a tuple, or a trait is `not supported yet`. `var t Type` with no value is `type mismatch`.

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*Type` uses the value `p` points at. `name` is a method of `Type`. `t.name()` returns `*char`. The text is the spelling diagnostics use: `int`, `*int`, `?*Point`, `int[4]`, `Box.spec.int`. `name` takes no arguments. Another argument is `argument count`. Another method is `undeclared identifier`. `t.name` without the call is `undeclared identifier`.

`==` and `!=` compare two `Type` values and yield `int`, either 0 or 1. Other operators on `Type` are `type mismatch`.

[Index](README.md) · [Previous: Packages](packages.md) · [Next: Expressions](expressions.md) · [Grammar](grammar.md)
