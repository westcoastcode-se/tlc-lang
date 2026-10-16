# panic

[Index](../README.md) · [Previous: va](va.md) · [Next: builtin/env/consts](family.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`builtin.panic(message)` prints the message and the call stack, then aborts the process. `new` calls it with `out of memory` when allocation returns `null`. The message is `*const u8`. A string and a `*u8` may be passed. The text goes to the standard error stream:

```text
panic: message
```

The stack follows, newest first, one line per call. A line is `file:line:column: name`. The name is the function that made the call. The first stack line is the function that called `builtin.panic`, at that call. `builtin.trace()` lists the same calls in that order. A null pointer is printed as `(null)`. At most 32 calls are kept.

No argument, or more than one, is reported as `argument count`. A value that is not a pointer to `u8` is reported as `type mismatch`. The call has no result, so `var n = builtin.panic("out of memory")` is `type mismatch`.

```c
import "builtin";

func leaf() void {
    builtin.panic("out of memory");
}

func mid() void {
    leaf();
}

func main() i32 {
    mid();
    return 0;
}
```

`leaf` is calling `panic`, `mid` has called `leaf`, and `main` has called `mid`. The process aborts. `tlc --run` exits with status 128, and the standard error stream is:

```text
panic: out of memory
main.tlc:4:5: leaf
main.tlc:8:5: mid
main.tlc:12:5: main
```

The file name is the source path relative to the entry file's directory, as for `builtin.trace`. `var f = builtin.panic` is a function pointer, and `f("out of memory")` aborts at that call. `defer builtin.panic("out of memory")` aborts when the deferred calls run.

## Cost

`builtin.panic(message)` is a traced call. The argument is the string address, one `leaq` when the message is a literal, inside the 36-instruction frame when fewer than 32 frames are stored. The callee writes the message and walks the same frames `trace` walks, then aborts. That walk is inside `panic` and grows with the number of frames. A null message is printed as `(null)`.

[Index](../README.md) · [Previous: va](va.md) · [Next: builtin/env/consts](family.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
