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

`testing.error` prints the frames from `builtin.StackTrace.get()`, then the message. The frames are the tlc calls that have not returned. `error` frees that copy.

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

`tlc --test` reserves bare `new` with `debug.DebugAllocator`. A block that `delete` does not release fails that test.

```c
test "keeps the block" {
    var p = new i32;
    _ = p;
}
```

A build without `--test` does not emit the test, so this example emits nothing.

`tlc --test` prints one line for that block, then the frame, then the failure:

```text
not released
main.tlc:2:13: keeps the block
fail: keeps the block
```

The frame is that `new`. The name is the test. The harness does not record its own call.

A call that entered the allocating function is printed under that line, after the `new`:

```c
func leak() i32 {
    var p = new i32;
    _ = p;
    return 1;
}

test "keeps the block" {
    return leak();
}
```

A build without `--test` does not emit this test either. `tlc --test` prints:

```text
not released
main.tlc:2:13: leak
main.tlc:8:12: keeps the block
fail: keeps the block
```

The first frame is the `new` inside `leak`. The second is the `leak()` call. The allocator frees that `StackTrace` when the block is released and when `report` prints it.

`--allocator Malloc` keeps `malloc` and `free` for that run, so the same test does not report the block. `--allocator pkg.Name` uses another struct that satisfies `Allocator`.

A build without `--test` typechecks the tests and does not run them.

[Index](../README.md) · [Previous: Structs and arrays](aggregate.md) · [Next: Function pointers](pointer.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
