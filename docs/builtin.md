# Builtin

[Index](README.md) · [Previous: Functions](functions.md) · [Next: Structs, pointers, and arrays](structs.md) · [Grammar](grammar.md)

`import "builtin"` loads the compiler package. It exports two functions, `trace` and `va`. `trace` returns `*const u8`. `va` returns `*char`. Both take no arguments. An argument is reported as `argument count`. The import links the C library.

```c
import "builtin";
import "builtin" as b;
import { trace, va } from "builtin";
```

The first form calls them as `builtin.trace()` and `builtin.va()`. The alias uses the new qualifier. The selective form brings the names into the file. A directory named `builtin` on the search path is that package. The compiler package is used when the search finds none.

## `trace`

`builtin.trace()` returns `*const u8`. The text lists the calls that have not returned, oldest first. A line is `file:line:column: name`. The name is the function that made the call, and the location is that call. The last line has no newline. Calls of `builtin.trace` and `builtin.va` are omitted. When no such call is still running, the text is empty.

```c
import "builtin";

extern(abi="c") func printf(fmt *const char, ...) int;

func leaf() int {
    printf("%s\n", *char(builtin.trace()));
    return 1;
}

func mid() int {
    return leaf();
}

func main() int {
    return mid();
}
```

`main` has called `mid`, and `mid` has called `leaf`. `leaf` is calling `printf`, whose argument is the trace, so the program prints:

```text
main.tlc:15:12: main
main.tlc:11:12: mid
```

The file name is the source path relative to the entry file's directory. A source file outside that directory is named by its file name. A method uses the method name. An anonymous function is named `anon`. Inside a `test`, the name is the test's name.

At most 32 calls are kept. The text is at most 4095 bytes. The pointer refers to one buffer, and the next `builtin.trace()` replaces that text.

`var f = builtin.trace` is a function pointer. `defer builtin.trace()` runs the call when the deferred calls run.

```c
import "builtin";

func main() int {
    var f = builtin.trace;
    defer f();
    defer builtin.trace();
    return 0;
}
```

## `va`

`builtin.va()` reads the extra arguments of the variadic function it is called from. The pointer is the `ap` that `vprintf` takes. Each call reads those arguments from the start.

```c
import "builtin";

extern(abi="c") func vprintf(fmt *const char, ap *char) int;

func log(fmt *char, ...) int {
    return vprintf(fmt, builtin.va());
}

func main() int {
    log("n=%d\n", 40);
    return 0;
}
```

The program prints `n=40`. An extra argument is an integer, a floating-point value, a pointer, an enum, or `null`. A struct is reported as `not supported yet`. An integer narrower than `int` is passed as `int`. `float` is passed as `double`.

`builtin.va()` outside a variadic function is reported as `not supported yet`. So is `defer builtin.va()`, and so is using the name without calling it: `builtin.va` and `var p = builtin.va`.

[Index](README.md) · [Previous: Functions](functions.md) · [Next: Structs, pointers, and arrays](structs.md) · [Grammar](grammar.md)
