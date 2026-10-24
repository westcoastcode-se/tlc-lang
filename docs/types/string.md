# Strings

[Index](../README.md) · [Previous: Literals](literal.md) · [Next: sizeof](sizeof.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A string between double quotes cannot cross a line. A string between backticks may span lines. Both are `[]const u8`: a pointer to the bytes and an `i32` length. The length is the number of bytes and does not count the terminating null. The null is still stored after the bytes, so the address is a C string. In a raw string the characters are copied as written: a backslash is not an escape, and a newline in the source is a newline in the string. The next backtick ends the string. `var text = "hi"` has type `[]const u8`. `text.len` is 2. `text[0]` reads a byte. `text[0] = 1` and `var bytes []u8 = "hi"` are reported as `type mismatch`.

A constant string is a slice. `const label = "hi"` at file scope is `[]const u8`, the same type as `var text = "hi"`. `label.len` is 2, and the name has no address. `const n = "hi".len` is `global initializer is not a constant`.

A constant index reads one byte while compiling. `"hi"[1]` is 105. `assert("hi"[1] == 105, "byte")` folds, and `const n = "hi"[1]` is that integer. It can be an array length. A file-scope `const` in the same file whose initializer is a string literal is the same bytes: `const label = "hi"` then `label[1]` is 105, and so is `const again = label` then `again[1]`. The index may be another folded integer, so `const i = 1` then `"hi"[i]` is 105. An escape is the decoded byte: `"\n"[0]` is 10 and `"\xFF"[0]` is 255. A raw string is the same index. An index below 0 or past the last byte does not fold. The length does not include the terminating null, so `"hi"[2]` is not that null. `const n = "hi"[2]` is `global initializer is not a constant`. The same index in `assert` is `not supported yet`. A `var` used as the index at file scope is `global initializer is not a constant`. Inside a function, `"hi"[i]` when `i` is a `var` still loads the byte. `const text = "hi"` inside a function is that slice, not a compile-time string, so `text[1]` loads. A file-scope `const p *const u8 = "hi"` is a pointer. `p[0]` does not fold. A range such as `"hi"[1:]` stays a slice.

`*const u8` is the pointer a C library takes. It is written when that library is the destination: `const c_text *const u8 = "unix"`, `var text *const u8 = "hi"`, `func len(s *const u8)`, and a variadic argument such as `printf("%s", "hi")`. A `[]const u8` converts to that pointer by passing its address and dropping the length, so `printf("%s", label)` and `var p *const u8 = label` are the C form. `var p *u8 = "hi"` is reported as `type mismatch`.

```c
var text = "Hello";
var raw = `hello
world`;
var pointer *const u8 = "Hello";
```

`text.len` is 5. `raw` includes the newline. `pointer[0]` is the same first byte, and the read walks bytes until the null.

[`std/string`](../std/string.md) is the library package `lib/tlc/std/string`. It names this slice `string_view` and provides `equals`, `compare`, and `find_first_of`. Those functions are tlc source. The compiler does not implement them.

## Cost

`var text = "hi"` is 3 instructions: 1 `leaq` of the bytes, 1 `movq` of that address, and 1 `movl` of the length 2. The bytes, including the terminating null, sit in the binary and are not copied. A raw string is the same three instructions. `text.len` is 1 load of the length. `text[0]` is 1 load of the address and 1 `movzbl` of the byte. `return "hi"[1]` is 2 instructions: `movl $105, %eax` and `retq`. A file-scope `const n = "hi"[1]` used as `return n` is those same 2. `return text.len` in `main` is 6 instructions: those three, the length load, a load of the pointer, and `retq`.

`var text *const u8 = "hi"` is 1 `leaq` and 1 store of that address. Passing the literal to `*const u8`, including `printf("hi\n")`, is that address and adds no length. The call stays 36 instructions in `main` when fewer than 32 frames are stored. `const label = "hi"` keeps the slice header in the binary. `return label.len` is 3 instructions: 1 load of the length, 1 load of the pointer, and `retq`. `var p *const u8 = label` followed by `return 0` is 5 instructions: both fields are loaded, the address is stored, then `xor` and `retq`.

[Index](../README.md) · [Previous: Literals](literal.md) · [Next: sizeof](sizeof.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
