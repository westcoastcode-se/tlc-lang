# ascii

[Index](../README.md) · [Previous: unicode](unicode.md) · [Next: seq](seq.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "std/ascii"` loads `lib/tlc/std/ascii`.

`ascii.string` is the same union as [`unicode.string`](unicode.md). The bytes are ASCII. `from` calls `builtin.panic("not ascii")` when a byte is 128 or greater.

A string of at most 22 bytes reads the array in the value. A longer string reads the pointer, reserved with `new` and released with `delete`.

`from(text)` and `text.release()` use `builtin.allocator`. `from(text, alloc)` and `text.release(alloc)` pass that `*Allocator` to `new` and `delete`. The struct exports `malloc`, `free`, and `realloc`.

`release` of a string longer than 22 bytes sets the length to 0 and then calls `delete`. A later `release` sees that length and does not call `delete`.

`ascii.string_view` is `[]const u8`. `view()` is that view of the stored bytes.

`equals`, `compare`, `find_first_of`, `find`, `starts_with`, and `ends_with` are the same methods as on [`unicode.string`](unicode.md), on the view and on `ascii.string`.

`from` still calls `builtin.panic("not ascii")` when a byte is 128 or greater. A `string_view` does not.

```c
import "std/ascii";

func main() i32 {
    var text = ascii.from("Hi"b);
    return text.at(1);
}
```

## Cost

`from` of two ASCII bytes stores those bytes in the value and does not call `malloc`.

[Index](../README.md) · [Previous: unicode](unicode.md) · [Next: seq](seq.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
