# The compiler

[Index](README.md) · [Next: Packages](packages.md) · [Grammar](grammar.md)

```text
tlc [options] <entry.tlc>
tlc lsp
tlc --version
tlc --help
```

| Option | Meaning |
| --- | --- |
| `--check` | Parse and typecheck |
| `--emit-llvm` | Write LLVM IR |
| `--emit-obj` | Write an object file (`a.o` when `-o` is absent) |
| `--run` | Compile, link, and run |
| `-o <file>` | Output path |
| `-P <dir>` | Extra package search root, after the entry file's directory |
| `-l <lib>` | Link `lib<lib>` when producing an executable |
| `-L <dir>` | Directory searched for libraries |
| `-Bstatic` | Link later `-l` libraries from static archives (`.a`) |
| `-Bdynamic` | Link later `-l` libraries from shared libraries (`.so`) |
| `-static` | Link all libraries statically |
| `-g` | Emit DWARF debug information |
| `-Wl,<opt>` | Pass `<opt>` through to the linker |

`-g` writes DWARF into the object file. Each function records its source file and its line, and each instruction records the line and column that produced it. Parameters, locals, and globals record their addresses. A debugger uses that to set a breakpoint on a source line, step, and read those variables. Stepping over an `if` whose condition is false continues at the statement after the `if`. A file-scope `const` has no address, so it is not a debug variable. `--check` accepts `-g` and does not emit code. `--emit-llvm` prints the same information as LLVM debug metadata.

`tlc -g -o app main.tlc` writes that executable and leaves it on disk. `gdb app` or `lldb app` can break on a source line. In Zed, `debugger: start` uses `.zed/debug.json`. The session builds the open file with `-g` into `build/debug/` and launches it under CodeLLDB, or under GDB. A breakpoint is a click in the gutter.

`tlc lsp` speaks the language server protocol on standard input and output. The editor sends the buffer, and that text is what is checked, including when the file on disk is different. The server publishes diagnostics and answers completion, hover, go to definition, and document symbols. Completion offers keywords, built-in types, names in scope, and, after a dot, the fields, methods, and enumerators of a named type. A designated initializer `{.field = value}` completes the fields of that struct. The type is the variable's type, the assigned name's type, the result type after `return`, or the type of a field when the initializer is nested, as in `{.complex = {.inner = 1}}`. A file-scope `const` is one of those names. Zed starts this server from the TLC extension.

`-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, and `-Wl` are passed to `cc` after the object file, in the order written. A path that ends in `.a`, `.so`, or `.dylib`, and a versioned shared library such as `libfoo.so.1`, is a library file and is passed through the same way. `-Bstatic` stays in effect until `-Bdynamic`, so put `-Bdynamic` after the static libraries when the C library should stay shared. `--check`, `--emit-llvm`, and `--emit-obj` accept the flags and do not link.

Vendor packages are imported as `vendor/<name>`. Set `TLC_LIB` to choose their directory. When that variable is unset, the directory is `lib/tlc/vendor` next to the compiler prefix: `bin/tlc` and `build/tlc` both use `<prefix>/lib/tlc/vendor`. The entry directory and each `-P` root are searched first, using the full path `vendor/<name>`.

Standard packages live in `lib/tlc` next to that vendor directory. `import "std/testing"` loads `<prefix>/lib/tlc/std/testing`. `import "builtin"` is supplied by the compiler. The entry directory and each `-P` root are searched first.

The file on the command line is the entry. Its `main` is the program's `main`. A symbol is named `qualifier.name`. In a package with more than one file, a `static` symbol is named `qualifier.filename.name`, where `filename` is the source file without `.tlc`. `extern` and `extern(abi="tlc")` use that same name and are linked from a tlc library. `extern(abi="c")` keeps the C name.

## A program

```c
func main() int {
    return 40 + 2;
}
```

```sh
tlc --run main.tlc
```

The exit status is 42.

Comments are `//` to the end of the line and `/* ... */`. The language has no preprocessor and no `#include`.

[Index](README.md) · [Next: Packages](packages.md) · [Grammar](grammar.md)
