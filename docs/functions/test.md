# Tests

[Index](../README.md) · [Previous: Structs and arrays](aggregate.md) · [Next: Function pointers](pointer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`test "name" { ... }` is a file-scope test. The body is a function that returns `i32`. Reaching the end passes, and so does `return 0`. Any other `return` fails that test. `return;` is reported as `type mismatch`. The same test name twice in one file is reported as `redefinition`.

`import "std/testing"` provides `testing.error`. It returns `1`. The first argument is a string. Further arguments are integers, floating-point values, pointers, or enums, and the string is a `printf` format. A floating-point argument is passed as `f64`.

`testing.error` prints `builtin.trace()`, then the message. `builtin.trace` is the tlc calls that have not returned. Each line is `file:line:column: name`, newest first, and the last line has no newline. `builtin.va` reads the extra arguments of the variadic function it is called from.

```c
import "std/testing";

func add(a i32, b i32) i32 {
    return a + b;
}

test "add joins its arguments" {
    var value = add(2, 2);
    if (value != 4) {
        return testing.error("Expected 4 but was %d", value);
    }
}
```

`main.tlc:10:16: add joins its arguments: Expected 4 but was 5` is that report when `value` is 5. `import "builtin"` is supplied by the compiler. `std/testing` is `lib/tlc/std/testing`. A directory with the same path next to the entry file is that package.

`tlc --test file.tlc` compiles the program, runs every test, and skips `main`. A failing test prints `fail:` and the name. The process status is the number of failed tests. A build without `--test` typechecks the tests and does not run them.

## Cost

A `test` is a function that returns `i32`. Its body costs whatever the statements cost. Reaching the end returns 0, which is `xorl %eax, %eax` and `retq` when the body is empty. `testing.error` is a traced call. A build without `--test` emits no call of the test.

[Index](../README.md) · [Previous: Structs and arrays](aggregate.md) · [Next: Function pointers](pointer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
