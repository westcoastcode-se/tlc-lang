# Debug information

[Index](../README.md) · [Previous: Options](option.md) · [Next: Language server](lsp.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`-g` writes DWARF so a debugger can follow the executable in source.

## What `-g` records

`-g` writes DWARF into the object file.

Each function records its source file and its line.

Each instruction records the line and column that produced it.

Parameters, locals, and globals record their addresses.

A debugger uses that information to set a breakpoint, step, and read those variables.

## Stepping and constants

Stepping over an `if` whose condition is false continues at the statement after the `if`.

A file-scope `const` has no address, so it is not a debug variable.

`--check` accepts `-g` and emits no code.

`--emit-llvm` prints the same information as LLVM debug metadata.

```sh
tlc -g -o app main.tlc
```

The executable stays on disk.

`gdb app` or `lldb app` can break on a source line.

In Zed, `debugger: start` uses the TLC extension.

The session builds the open file with `-g` into `build/debug/` and launches it under CodeLLDB, or under GDB.

A breakpoint is a click in the gutter.

```c
func main() i32 {
    return 0;
}
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Options](option.md) · [Next: Language server](lsp.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
