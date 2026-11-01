# unicode

[Index](../README.md) · [Previous: builtin/env/consts](../builtin/family.md) · [Next: ascii](ascii.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "std/unicode"` loads `lib/tlc/std/unicode`.

`unicode.string` is a union with no tag. Both arms start with the length. That length selects the bytes.

A string of at most 22 bytes reads the array in the value. A longer string reads the pointer. `from` reserves that storage with `new`, and `release` passes the pointer to `delete`.

`from(text)` and `text.release()` use `builtin.allocator`. `from(text, alloc)` and `text.release(alloc)` pass that `*Allocator` to `new` and `delete`. The struct exports `malloc`, `free`, and `realloc`.

`release` of a string longer than 22 bytes sets the length to 0 and then calls `delete`. A later `release` sees that length and does not call `delete`.

`len()` is the number of bytes. `at(i)` reads one byte. `view()` is the `string_view` of those bytes.

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

`from` of two bytes stores those bytes in the value and does not call `malloc`. `release` of that value does not call `free`.

[Index](../README.md) · [Previous: builtin/env/consts](../builtin/family.md) · [Next: ascii](ascii.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
