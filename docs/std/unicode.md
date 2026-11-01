# unicode

[Index](../README.md) · [Previous: builtin/env/consts](../builtin/family.md) · [Next: ascii](ascii.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "std/unicode"` loads `lib/tlc/std/unicode`.

`unicode.string` is a tagged union. It stores a length and the bytes of a UTF-8 string.

A string of at most 22 bytes is the `Short` arm. Those bytes sit in the value. A longer string is the `Long` arm. `from` reserves that storage with `new`, and `release` passes the pointer to `delete`.

`from(text)` and `text.release()` use `builtin.allocator`. `from(text, alloc)` and `text.release(alloc)` pass that `*Allocator` to `new` and `delete`. The struct exports `malloc`, `free`, and `realloc`.

`release` of a `Long` string switches the value to an empty `Short` arm and then calls `delete`. A later `release` does not call `delete`.

`len()` is the number of bytes. `at(i)` reads one byte.

`unicode.string_view` is `[]const u8`.

`equals`, `compare`, `find_first_of`, `find`, `starts_with`, and `ends_with` are methods on that view and on `unicode.string`.

`compare` returns `-1`, `0`, or `1`.

`find` and `find_first_of` return the first index, or `-1`.

An empty needle makes `find` return `0`.

```c
import "std/unicode";

func main() i32 {
    var text = unicode.from("hi"b);
    return text.len();
}
```

## Cost

`from` of two bytes stores the `Short` arm and does not call `malloc`. `release` of that value does not call `free`.

[Index](../README.md) · [Previous: builtin/env/consts](../builtin/family.md) · [Next: ascii](ascii.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
