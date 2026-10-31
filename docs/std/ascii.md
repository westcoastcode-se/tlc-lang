# ascii

[Index](../README.md) · [Previous: unicode](unicode.md) · [Next: seq](seq.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "std/ascii"` loads `lib/tlc/std/ascii`.

`ascii.string` is the same tagged union as [`unicode.string`](unicode.md). The bytes are ASCII. `from` calls `builtin.panic("not ascii")` when a byte is 128 or greater.

A string of at most 22 bytes is the `Short` arm. A longer string is the `Long` arm, reserved with `new` and released with `delete`.

`from(text)` and `text.release()` use `builtin.allocator`. `from(text, alloc)` and `text.release(alloc)` pass that `*Allocator` to `new` and `delete`. The struct exports `malloc`, `free`, and `realloc`.

`release` of a `Long` string switches the value to an empty `Short` arm and then calls `delete`. A later `release` does not call `delete`.

```c
import "std/ascii";

func main() i32 {
    var text = ascii.from("Hi"b);
    return text.at(1);
}
```

## Cost

`from` of two ASCII bytes stores the `Short` arm and does not call `malloc`.

[Index](../README.md) · [Previous: unicode](unicode.md) · [Next: seq](seq.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
