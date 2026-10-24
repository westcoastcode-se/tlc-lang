# trace

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`import "builtin"` loads the compiler package. It exports `trace`, `va`, `panic`, and `Type`. `builtin.Type` is the type of a type, described in [The Type type](../types/type.md). `trace` returns `*const u8`. `va` returns `*u8`. Both take no arguments. An argument is reported as `argument count`. `panic` takes one message and does not return a value. The import links the C library. `import "builtin/env/consts"` loads `family`, described in [builtin/env/consts](family.md).

```c
import "builtin";
import "builtin" as b;
import { trace, va, panic } from "builtin";
```

The first form calls them as `builtin.trace()`, `builtin.va()`, and `builtin.panic(message)`. The alias uses the new qualifier. The selective form brings the names into the file. A directory named `builtin` on the search path is that package. The compiler package is used when the search finds none.

## `trace`

`builtin.trace()` returns `*const u8`. The text lists the calls that have not returned, newest first. A line is `file:line:column: name`. The name is the function that made the call, and the location is that call. The last line has no newline. Calls of `builtin.trace` and `builtin.va` are omitted. A call of `builtin.panic` is kept. When no such call is still running, the text is empty.

```c
import "builtin";

extern(abi="c") func printf(fmt *const u8, ...) i32;

func leaf() i32 {
    printf("%s\n", *u8(builtin.trace()));
    return 1;
}

func mid() i32 {
    return leaf();
}

func main() i32 {
    return mid();
}
```

`leaf` is calling `printf`, whose argument is the trace. `mid` has called `leaf`, and `main` has called `mid`, so the program prints:

```text
main.tlc:11:12: mid
main.tlc:15:12: main
```

The file name is the source path relative to the entry file's directory. A source file outside that directory is named by its file name. A method uses the method name. An anonymous function is named `anon`. Inside a `test`, the name is the test's name.

At most 32 calls are kept. The text is at most 4095 bytes. The pointer refers to one buffer, and the next `builtin.trace()` replaces that text.

`var f = builtin.trace` is a function pointer. `defer builtin.trace()` runs the call when the deferred calls run.

```c
import "builtin";

func main() i32 {
    var f = builtin.trace;
    defer f();
    defer builtin.trace();
    return 0;
}
```

## Cost

`builtin.trace()` does not push a frame for itself. The call still runs the function that walks the stored frames and writes the text. Each frame a previous call pushed is 24 bytes: a file pointer, a line, a column, and a name pointer. The push that stored it is the 36-instruction sequence in the caller of that previous call, on the path where fewer than 32 frames are stored. When 32 frames are already stored the push is skipped.

The pointer returned by `trace` addresses one buffer. The next call overwrites it. The walk and the bytes it writes are inside `trace`, and they grow with the number of frames.

[Index](../README.md) · [Previous: Traits](../methods/trait.md) · [Next: allocator](allocator.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
