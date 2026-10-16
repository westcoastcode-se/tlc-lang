# string

[Index](../README.md) · [Previous: Strings](../types/string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`import "std/string"` loads `lib/tlc/std/string`. The package is ordinary tlc source. The compiler does not implement these functions.

`string.string_view` is `[]const u8`. A string literal and a plain `[]const u8` convert to it. `string_view` does not convert back to `[]const u8`. It still converts to `*const u8` by passing the address and dropping the length, the same way any other `[]const u8` does. `string.from` is that conversion as a function. The parameter of a method is the name `string_view`. `this` is only the receiver.

```c
import "std/string";

func main() i32 {
    var text string.string_view = "hello";
    if (text.equals("hello") && text.find_first_of("ol") == 2) {
        return text.find("lo");
    }
    return 0;
}
```

The program returns 3. `equals` is true when the lengths match and every byte matches. `equals("", "")` is true. `compare` walks unsigned bytes and returns `-1`, `0`, or `1`. A shared prefix leaves the shorter view first, so `compare("", "a")` is negative and `compare("", "")` is `0`. `find_first_of` returns the first index whose byte is in the set, or `-1`. An empty view and an empty set are `-1`. `find` returns the first index of the needle, or `-1`. An empty needle returns `0`, including on an empty view. `starts_with` and `ends_with` compare that range. `if (text.equals("hello"))` is a `bool` condition. `!text.equals("hello")` is the integer not of that `bool`.

`const greeting string.string_view = "hello"` keeps the alias. The header is the same slice as `const label = "hello"`.

## Cost

Counts are the instructions that run, from `tlc --emit-obj` on `x86_64-pc-linux-gnu`. Alignment `nop` is left out. A call of one of these functions records a trace frame in the caller. None of `from`, `equals`, `compare`, or `find_first_of` records another frame. `find`, `starts_with`, and `ends_with` call `equals`, and that call does.

`string.from(text)` is 5 instructions: store the pointer, store the length, reload both, and `retq`. `string_view(text)` adds none when the value is already a slice.

`var text string.string_view = "hi"` then `if (text.equals("hi")) return 1` else `return 0` executes 52 instructions in `main` when the bytes match and fewer than 32 frames are stored. That is both slices, the frame, `callq`, the `bool` test, and `return 1`. The `else` is 53. `equals` is extra.

Inside `equals`, different lengths return false in 15 instructions. Two empty views return true in 26. Equal lengths whose first byte differs return false in 40. One matching byte returns true in 55. Each further matching byte adds 29.

`compare` of two empty views returns 0 in 46 instructions. An empty view compared with `"a"` returns `-1` in 34. `"a"` compared with an empty view returns `1` in 48. Equal lengths whose first byte is less return `-1` in 39.

`find_first_of` on an empty view returns `-1` in 16 instructions. An empty set against one byte returns `-1` in 40. A first byte that is the first character of the set returns `0` in 41.

`find` of an empty needle returns `0` in 16 instructions and does not call `equals`. A needle longer than the view returns `-1` in 26 and does not call `equals`. A match at index 0 is 90 instructions plus `equals`. Those 90 include the trace frame of that call.

`starts_with` returns false in 17 instructions when the view is shorter than the prefix, and does not call `equals`. Otherwise it is 62 instructions plus `equals`. `ends_with` is 17 on that short path and 71 plus `equals` otherwise. The 62 and the 71 include the trace frame of `equals`.

[Index](../README.md) · [Previous: Strings](../types/string.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
