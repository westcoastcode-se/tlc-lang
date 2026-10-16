# Builtin

[Index](README.md) · [Previous: Functions](functions.md) · [Next: Structs, pointers, and arrays](structs.md) · [Grammar](grammar.md)

`import "builtin"` loads the compiler package. It exports `trace`, `va`, and `panic`. `trace` returns `*const u8`. `va` returns `*u8`. Both take no arguments. An argument is reported as `argument count`. `panic` takes one message and does not return a value. The import links the C library. `import "builtin/env/consts"` loads `family`, described at the end of this page.

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

## `allocator`

`Allocator` is a built-in trait. A struct satisfies it by defining these methods, each with a pointer receiver:

```c
func malloc(self *this, size i32, align i32) ?*u8;
func free(self *this, ptr *u8) void;
func realloc(self *this, ptr *u8, size i32, align i32) ?*u8;
```

`import "builtin"` also provides `allocator`, a `Malloc` value. `new T` is `new(builtin.allocator) T`, and `delete p` is `delete(builtin.allocator) p`. `Malloc` calls the C functions `malloc`, `free`, and `realloc`. `malloc` returns `null` when it cannot reserve the bytes. `new` then aborts. That choice is not final.

`new T` reserves `sizeof(T)` bytes, zeroes them, and yields `*T`. `new T{.field = expr}` and `new(allocator) T{.field = expr}` write those fields after the block is reserved. A field that is left out stays zero. `new Point{1, 2}` is a value list and is reported as `type mismatch`. An initializer on a type that is not a struct, such as `new i32{1}`, is reported as `type mismatch`. `new Node` is reported as `type mismatch` when `Node` contains a `*T`, because that pointer would be zero. `delete` passes that pointer to `free`. A file-scope `new` is reported as `global initializer is not a constant`.

`new [n]T` and `new(allocator) [n]T` reserve `n` elements of `T` and yield `*T`, a pointer to the first element. `n` has type `i32`. `buf[i]` reads and writes element `i`. There is no bounds check. The elements are zero. `delete buf` frees the block. A constant `n` that is not positive is reported as `type mismatch`. Any other `n` that is not positive aborts. `new []T` is reported as `expected an expression`.

`new [n]Point{{.x = 1}...}` stores that value in every element. The value is evaluated once. A field that is left out stays zero. `new [2]Point{{.x = 1}, {.y = 2}}` stores one value per element when the constant count matches the list. A runtime count with a list and no `...` is reported as `type mismatch`. `new [n]Point{.x = 1}` is that same mismatch: the element list is another pair of braces. `new [_]Point{{.x = 1, .y = 2}, {.y = 10}}` takes the length from the list, so the reserved object is `[2]Point` and the result is `*[2]Point`. `new [_]Point{{.x = 1}...}` has no length to repeat and is reported as `type mismatch`. `new [n]Node` is reported as `type mismatch` when an element would contain a zero `*T`. An element value that sets that field is accepted.

`new Box[i32]` and `new Box[i32]{.value = 1}` allocate one specialization. The fields are the fields of that specialization. `new i32[4]` is reported as `type mismatch`. The buffer is `new [4]i32`.

```c
import "builtin";

type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var p *Point = new Point{.x = 40, .y = 2};
    var sum i32 = p.x + p.y;
    delete(builtin.allocator) p;
    return sum;
}
```

## `va`

`builtin.va()` reads the extra arguments of the variadic function it is called from. The pointer is the `ap` that `vprintf` takes. Each call reads those arguments from the start.

```c
import "builtin";

extern(abi="c") func vprintf(fmt *const u8, ap *u8) i32;

func log(fmt *const u8, ...) i32 {
    return vprintf(fmt, builtin.va());
}

func main() i32 {
    log("n=%d\n", 40);
    return 0;
}
```

The program prints `n=40`. An extra argument is an integer, a floating-point value, a pointer, an enum, or `null`. A struct is reported as `not supported yet`. An integer narrower than `i32` is passed as `i32`. `f32` is passed as `f64`.

`builtin.va()` outside a variadic function is reported as `not supported yet`. So is `defer builtin.va()`, and so is using the name without calling it: `builtin.va` and `var p = builtin.va`.

## `panic`

`builtin.panic(message)` prints the message and the call stack, then aborts the process. The message is `*const u8`. A string and a `*u8` may be passed. The text goes to the standard error stream:

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

## `builtin/env/consts`

`import "builtin/env/consts"` loads the compiler package `consts`. It exports the constant `family`. The type is `*const u8`. On Windows the text is `windows`. On Linux, macOS, and every other system the text is `unix`. The value is chosen from the system `tlc` is running on. A directory named `builtin/env/consts` on the search path is that package instead. The compiler package is used when the search finds none.

```c
import "builtin/env/consts";

extern(abi="c") func printf(fmt *const u8, ...) i32;

func main() i32 {
    printf("%s\n", consts.family);
    return 0;
}
```

On Linux or macOS the program prints `unix`. On Windows it prints `windows`. `import { family } from "builtin/env/consts"` brings the name into the file. Assigning `consts.family`, or writing `consts.family[0] = 1`, is reported as `type mismatch`.

[Index](README.md) · [Previous: Functions](functions.md) · [Next: Structs, pointers, and arrays](structs.md) · [Grammar](grammar.md)
