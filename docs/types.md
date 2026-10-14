# Types

[Index](README.md) · [Previous: Packages](packages.md) · [Next: Expressions](expressions.md) · [Grammar](grammar.md)

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

## Pointers

`*T` is a pointer that has a value. `?*T` is that pointer or `null`. `*?*T` and `?**T` are different types. `?` is written immediately before `*`.

`*const T` is a pointer to constant `T`. The variable may be reseated. A store through it, `*p = 1` or `p[i] = 1`, is reported as `type mismatch`. Reading `*p` and `p[i]` is allowed. `*T` may be stored in `*const T`. `*const T` may not be stored in `*T`. `?*const T` is that pointer or `null`. `**const T` is a pointer to a pointer to constant `T`. `*const *T` is a pointer to a constant pointer.

```c
func main() i32 {
    var n i32 = 1;
    var p *i32 = &n;
    var q ?*i32 = null;
    var c *const i32 = p;
    var inner *i32 = &n;
    var to_const **const i32 = &inner;
    var to_ptr *const *i32 = &inner;
    q = p;
    return *p + *c + **to_const + **to_ptr;
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

## `void`, `bool`, and floating-point types

`void` is a return type and a pointee. It is neither a value nor a variable type.

A `bool` value is 0 or 1.

`f32` is the 32-bit floating type. `f64` is the 64-bit floating type. A literal without a suffix has type `f64`. A suffix `f` or `F` makes it `f32`. `1.5 + 1` is `f64`. Comparisons of floating values yield `i32`. `%` and the bitwise operators on a floating value are reported as `type mismatch`. A file-scope `var` or `const` may hold either type, as in `var scale = 1.5f` and `const bias = 0.5f`.

```c
func skip() void {
    return;
}

func main() i32 {
    var a bool = true;
    var x f32 = 1.5f;
    var y f64 = 20.5;
    skip();
    if (a && x < y) {
        return 1;
    }
    return 0;
}
```

## Literals

| Literal | Type |
| --- | --- |
| `42` | `i32` |
| `42i8`, `42u8` | `i8` and `u8`, when the value fits |
| `42i16`, `42u16` | `i16` and `u16`, when the value fits |
| `42i32`, `42u32` | `i32` and `u32`, when the value fits |
| `42i64`, `42u64` | `i64` and `u64`, when the value fits |
| `'A'` | `u8` |
| `"hi"` | `*const u8`, including the terminating null |
| `` `hi` `` | `*const u8`; the text may span lines |
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

## Strings

A string between double quotes cannot cross a line. A string between backticks may span lines. Both are constants of type `*const u8`. The bytes include a terminating null. In a raw string the characters are copied as written: a backslash is not an escape, and a newline in the source is a newline in the string. The next backtick ends the string. `var text = "hi"` has type `*const u8`. `text[0] = 1` and `var p *u8 = "hi"` are reported as `type mismatch`.

```c
var text *const u8 = `hello
world`;
```

## `sizeof`

`sizeof` produces `i32`. `sizeof(u8)` is 1, `sizeof(i32)` is 4, `sizeof(i64)` is 8, a pointer is 8, and `sizeof([]i32)` is 16. Both `sizeof(i32)` and `sizeof n` are accepted. `sizeof(Point)` and `sizeof(enum Color)` name a declared type. `sizeof(struct Point)` is reported as `expected a type`. `sizeof(Type)` is 8.

```c
type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var n i32 = 1;
    return sizeof(i32) + sizeof n + sizeof(Point);
}
```

## Slices

`[]T` is a readonly slice of contiguous `T` values. Brackets come before the element type, as in `[]i32` and `func head(a []i32) []i32`. `[N]T` converts to `[]T`. `type Row []i32` names that slice, and `[N]i32` converts to `Row`. `Row` does not convert to `[]i32`. `type Pair [2]i32` does not convert to `[]i32`. `var v = row` keeps the array. It does not infer a slice.

`v[i]` reads one element. The index has type `i32`. Storing an element, `v[i] = 1`, is reported as `type mismatch`. `v[i].x` reads a field. Storing that field is reported as `type mismatch`. When the element is `*T`, `*v[i] = 1` writes through the pointer. `v.len` is `i32` and is the number of elements. `var empty []i32` has length 0.

`sizeof v` is 16, the same as `sizeof([]T)`. `typeof([]i32).name()` spells `[]i32`.

`i32[]` is reported as `type mismatch`. `*[]T`, `[][]T`, `[N][]T`, a struct field of slice type, and `const v []i32` are reported as `not supported yet`. An array parameter is already a pointer. `return row` from `func open(row [4]i32) []i32` is reported as `return type: expected []i32, found *i32`. A file-scope slice is initialized from the name of an array or another slice. Another initializer is reported as `global initializer is not a constant`.

```c
func head(v []i32) i32 {
    return v[0];
}

func main() i32 {
    var row = [_]i32{10, 20, 12};
    var v []i32 = row;
    return head(v) + v.len;
}
```

## The `Type` type

`Type` is the type of a tlc type. `typeof(i32)` and `typeof n` produce a `Type`. `typeof(Point)`, `typeof(*i32)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)` is reported as `expected a type`. `typeof('A')` is `u8`, and that value is different from `typeof(i8)`. `typeof` does not decay an array. `var t Type` with no value is reported as `type mismatch`.

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*Type` uses the value `p` points at. `name` is a method of `Type`. `t.name()` returns `*u8`. The text is the spelling diagnostics use: `i32`, `*i32`, `?*Point`, `[4]i32`, `Box.spec.i32`. `name` takes no arguments. Another argument is reported as `argument count`. Another method is reported as `undeclared identifier`. `t.name` without the call is reported as `undeclared identifier`.

`==` and `!=` compare two `Type` values and yield `i32`, either 0 or 1. Other operators on `Type` are reported as `type mismatch`.

```c
func main() i32 {
    var n i32 = 1;
    var t Type = typeof(n);
    var text *u8 = t.name();
    if (t == typeof(i32) && typeof('A') == typeof(u8) && text != null) {
        return 1;
    }
    return 0;
}
```

[Index](README.md) · [Previous: Packages](packages.md) · [Next: Expressions](expressions.md) · [Grammar](grammar.md)
