# Strings

[Index](../README.md) · [Previous: Literals](literal.md) · [Next: sizeof](sizeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes string literals, constant bytes, and the pointer a C library takes.

## Quoted strings and raw strings

A string between double quotes cannot cross a line, but a string between backticks may span lines.

Both are `[]const u8`: a pointer to the bytes and an `i32` length.

The length is the number of bytes and does not count the terminating null.

> **Warning:** The null is still stored after the bytes, so the address is a C string.

In a raw string the characters are copied as written, and a backslash is not an escape.

A newline in the source is a newline in the string, and the next backtick ends the string.

`var text = "hi"` has type `[]const u8`, `text.len` is 2, and `text[0]` reads a byte.

The compiler reports `text[0] = 1` and `var bytes []u8 = "hi"` as `type mismatch`.

## A constant string is a slice

A constant string is a slice.

`const label = "hi"` at file scope is `[]const u8`, the same type as `var text = "hi"`.

`label.len` is 2, and the name has no address.

`const n = "hi".len` is 2, and `return "hi".len` is `movl $2, %eax` and `retq`.

`const n = "hello"[1:4].len` is 3.

`var text = "hi"` then `text.len` still loads the length.

## A constant index

A constant index reads one byte while compiling, so `"hi"[1]` is 105.

`assert("hi"[1] == 105, "byte")` folds, and `const n = "hi"[1]` is that integer.

That integer can be an array length.

A file-scope `const` in the same file whose initializer is a string literal is the same bytes.

`const label = "hi"` then `label[1]` is 105, and so is `const again = label` then `again[1]`.

The index may be another folded integer, so `const i = 1` then `"hi"[i]` is 105.

An escape is the decoded byte, so `"\n"[0]` is 10 and `"\xFF"[0]` is 255.

A raw string is the same index.

An index below 0 or past the last byte does not fold.

The length does not include the terminating null, so `"hi"[2]` is not that null.

The compiler reports `const n = "hi"[2]` as `global initializer is not a constant`.

The same index in `assert` is `not supported yet`.

A `var` used as the index at file scope is `global initializer is not a constant`.

Inside a function, `"hi"[i]` when `i` is a `var` still loads the byte.

`const text = "hi"` inside a function is that slice, not a compile-time string, so `text[1]` loads.

A file-scope `const p *const u8 = "hi"` is a pointer, and `p[0]` does not fold.

## A constant range

A constant range of those bytes is that slice while compiling, so `"hello"[1:4][0]` is 101.

`assert("hello"[1:4][0] == 101, "e")` folds, and `const n = "hello"[1:4][0]` is that integer.

A missing low bound is 0, and a missing high bound is the length.

`"hello"[1:][0]`, `"hello"[:2][1]`, and `"hello"[:][1]` are 101.

`const label = "hello"` then `label[1:4][0]` is 101, and so is `const cut = label[1:4]` then `cut[0]`.

A further range is those bytes again: `"hello"[1:4][0:2][1]` is 108.

The index may be another folded integer.

An escape is the decoded byte, so `"\n"[0:1][0]` is 10 and `"\xFF"[0:1][0]` is 255.

A null in the middle is a byte, so `"a\000b"[1:2][0]` is 0.

A raw string is the same range.

An empty range has no byte.

A high bound below the low bound, or a bound past the last byte, does not fold.

The length does not include the terminating null, so `"hello"[1:6]` is not a slice through that null.

The compiler reports `const n = "hello"[1:4][3]`, `const n = "hello"[2:2][0]`, `const n = "hello"[3:1][0]`, and `const n = "hello"[1:9][0]` as `global initializer is not a constant`.

The same index in `assert` is `not supported yet`.

`.len` of a constant range folds, so `const n = "hello"[1:4].len` is 3.

A `var` used as a bound at file scope is `global initializer is not a constant`.

The range used as a slice is still that header.

`const cut = "hello"[1:4]` has length 3, and `cut[0]` is 101.

`cut[i]` loads when `i` is a `var`.

Inside a function, `"hello"[1:4][i]` when `i` is a `var` still loads the byte.

`const text = "hello"` inside a function is not a compile-time string, so `text[1:4][0]` loads.

A range of an integer array stays that header.

## Equality and order

`==` and `!=` on two `[]u8` or `[]const u8` values, including two `var` values, are `compare with builtin.memeql`.

`+` on `[]const u8` is `type mismatch: expected an integer, found []const u8`.

Constant slices are compared with [`builtin.memeql`](../builtin/mem.md) and ordered with `builtin.memcmp`.

`builtin.memeql[u8]("hi", "hi")` is true while compiling, and `builtin.memeql[u8]("hello"[0:2], "he")` is true.

`""` equals `""`, and different lengths are false.

A null in the middle counts, and the terminating null does not, so `builtin.memeql[u8]("a\000b", "ab")` is false.

`builtin.memcmp[u8]("ab", "ac")` is `-1`.

A `var` or a local `const` runs the function.

A bound past the string is not a constant slice, so a file-scope `const` of that call is `global initializer is not a constant`.

## The pointer a C library takes

`*const u8` is the pointer a C library takes.

It is written when that library is the destination.

Those destinations are `const c_text *const u8 = "unix"`, `var text *const u8 = "hi"`, `func len(s *const u8)`, and a variadic argument such as `printf("%s", "hi")`.

A `[]const u8` converts to that pointer by passing its address and dropping the length.

`printf("%s", label)` and `var p *const u8 = label` are the C form.

`const label = "hi"` then `const p *const u8 = label` stores that address. `label.len` stays 2.

`p[0]` is `h`, `p[1]` is `i`, and `p[2]` is the terminating null.

A `[]const u8` taken from a `[N]u8` converts the same way. A constant range does too, so `const cut = "hello"[1:4]` then `const p *const u8 = cut` starts at `e`.

The compiler reports `var p *u8 = "hi"` and `const p *u8 = label` as `type mismatch`.

```c
const label = "hi";
const p *const u8 = label;

func main() i32 {
    return i32(p[0]) + i32(p[1]) - 167;
}
```

```asm
main:
movq p(%rip), %rax               # p
movzbl (%rax), %eax              # p[0]
movq p(%rip), %rcx               # p
movzbl 1(%rcx), %ecx             # p[1]
addl %ecx, %eax                  # p[0] + p[1]
subl $167, %eax                  # minus 167
retq                             # return
```

The program returns 42.

## A quoted string, a raw string, and a C pointer

```c
var text = "Hello";
var raw = `hello
world`;
var pointer *const u8 = "Hello";
```

```asm
main:
leaq (%rip), %rax                # address
movq %rax, -16(%rsp)             # move
movl $5, -8(%rsp)                # constant
movl -8(%rsp), %eax              # move
movq -16(%rsp), %rcx             # move
retq                             # return
```

`text.len` is 5, and `raw` includes the newline.

`pointer[0]` is the same first byte, and the read walks bytes until the null.

## The string package

[`std/string`](../std/string.md) is the library package `lib/tlc/std/string`.

It names this slice `string_view` and provides `equals`, `compare`, and `find_first_of`.

Those functions are tlc source, and the compiler does not implement them.

[Index](../README.md) · [Previous: Literals](literal.md) · [Next: sizeof](sizeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
