# Tests

[Index](../README.md) · [Previous: Structs and arrays](aggregate.md) · [Next: Function pointers](pointer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes a file-scope test and how `tlc --test` runs it.

## A test at file scope

`test "name" { ... }` is a file-scope test. A `test` inside a function is `test is only valid at file scope`.

The body is a function that returns `i32`. Reaching the end passes, and so does `return 0`. Any other `return` fails that test.

The compiler reports `type mismatch` for `return;`. The compiler reports `redefinition` when the same test name appears twice in one file.

## Reporting a failure

`import "std/testing"` provides `testing.error`.

It returns `1`.

The first argument is a string. Further arguments are integers, floating-point values, pointers, or enums, and the string is a `printf` format. A floating-point argument is passed as `f64`.

`testing.error` prints `builtin.trace()`, then the message. `builtin.trace` is the tlc calls that have not returned.

Each line is `file:line:column: name`, newest first, and the last line has no newline.

`builtin.va` reads the extra arguments of the variadic function it is called from.

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

A build without `--test` does not emit the test. `add` is an ordinary function:

```asm
add:
movl %edi, -8(%rsp)              # a
movl %esi, -4(%rsp)              # b
movl -8(%rsp), %eax
addl -4(%rsp), %eax              # a + b
retq
```

## The printed report

`main.tlc:10:16: add joins its arguments: Expected 4 but was 5` is that report when `value` is 5.

`import "builtin"` is supplied by the compiler. `std/testing` is `lib/tlc/std/testing`. A directory with the same path next to the entry file is that package.

## Running the tests

`tlc --test file.tlc` compiles the program, runs every test, and skips `main`.

A failing test prints `fail:` and the name. The process status is the number of failed tests.

A build without `--test` typechecks the tests and does not run them.

[Index](../README.md) · [Previous: Structs and arrays](aggregate.md) · [Next: Function pointers](pointer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
