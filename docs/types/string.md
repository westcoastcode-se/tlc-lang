# Strings

[Index](../README.md) · [Previous: Literals](literal.md) · [Next: sizeof](sizeof.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A string between double quotes cannot cross a line. A string between backticks may span lines. Both are constants of type `*const u8`. The bytes include a terminating null. In a raw string the characters are copied as written: a backslash is not an escape, and a newline in the source is a newline in the string. The next backtick ends the string. `var text = "hi"` has type `*const u8`. `text[0] = 1` and `var p *u8 = "hi"` are reported as `type mismatch`.

```c
var text *const u8 = `hello
world`;
```

## Cost

A string literal is one address. `var text *const u8 = "hi"` is 1 `leaq` of the bytes and 1 store of that address. The bytes, including the terminating null, sit in the binary and are not copied at the store. A raw string is the same address.

[Index](../README.md) · [Previous: Literals](literal.md) · [Next: sizeof](sizeof.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
