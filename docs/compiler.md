# The compiler

[Index](README.md) · [Next: Packages](packages.md) · [Grammar](grammar.md)

`tlc` compiles one entry file. The file's `main` is the program's `main`.

```text
tlc [options] <entry.tlc>
tlc lsp
tlc --version
tlc --help
```

## Options

| Option | Meaning |
| --- | --- |
| `--check` | Parse and typecheck. Emit no code. |
| `--emit-llvm` | Print LLVM IR. |
| `--emit-obj` | Write an object file. The name is `a.o` when `-o` is absent. |
| `--run` | Compile, link, and run. |
| `--test` | Compile, link, and run every `test`. `main` is not called. |
| `-o <file>` | Output path. |
| `-P <dir>` | Extra package search root, after the entry file's directory. |
| `-l <lib>` | Link `lib<lib>` when producing an executable. |
| `-L <dir>` | Directory searched for libraries. |
| `-Bstatic` | Link later `-l` libraries from static archives (`.a`). |
| `-Bdynamic` | Link later `-l` libraries from shared libraries (`.so`). |
| `-static` | Link all libraries statically. |
| `-g` | Emit DWARF debug information. |
| `-Wl,<opt>` | Pass `<opt>` through to the linker. |
| `lsp` | Speak the language server protocol on standard input and output. |

`--check`, `--emit-llvm`, and `--emit-obj` accept the link flags and do not link.

## Debug information

`-g` writes DWARF into the object file. Each function records its source file and its line. Each instruction records the line and column that produced it. Parameters, locals, and globals record their addresses. A debugger uses that information to set a breakpoint, step, and read those variables.

Stepping over an `if` whose condition is false continues at the statement after the `if`. A file-scope `const` has no address, so it is not a debug variable. `--check` accepts `-g` and emits no code. `--emit-llvm` prints the same information as LLVM debug metadata.

```sh
tlc -g -o app main.tlc
```

The executable stays on disk. `gdb app` or `lldb app` can break on a source line. In Zed, `debugger: start` uses the TLC extension. The session builds the open file with `-g` into `build/debug/` and launches it under CodeLLDB, or under GDB. A breakpoint is a click in the gutter.

## Language server

`tlc lsp` is the language server. Its sources are `src/lsp.cpp` and `src/lsp.hpp`, and the `tlc` binary includes them. Any editor can start that command. It reads the editor's buffer and typechecks that text, including when it differs from the file on disk. The server publishes diagnostics and answers completion, hover, go to definition, and document symbols.

In Zed, hold Ctrl and click a function name to open its definition. That includes a function brought in by `import { name } from "pkg"`.

Completion offers keywords, built-in types, and names in scope. After a dot it offers the fields, methods, and enumerators of a value's type. A parameter of a trait type offers that trait's methods and fields, so `s *Shape` completes `s.` with `area`, `grow`, and `w`. A type name offers that type's static functions, and an enum name also offers its enumerators. A name bound with `:=`, `var name = expression`, or `const name = expression` uses the type of that expression when the expression is a call, a name, a field, or a cast. `p := Point.make(1)` then completes `p.`. A designated initializer `{.field = value}` completes the fields of that struct. The type is taken from the variable, from the name being assigned, from the result of the enclosing function after `return`, or from the surrounding field when the initializer is nested, as in `{.complex = {.inner = 1}}`. A file-scope `const` is one of the names offered. Zed starts this server from the TLC extension in `tlc-plugins-zed`. Set `lsp.tlc.binary.path` in Zed settings when `tlc` is not `build/tlc` in the open project and not on `PATH`. Set `lsp.tlc.binary.arguments` to `["lsp"]` beside that path. Zed starts the path on its own, and a missing argument list runs `tlc` with no arguments. Highlighting comes from the grammar in `tlc-tree-sitter`.

## Linking

`-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, and `-Wl` are passed to `cc` after the object file, in the order written. A path that ends in `.a`, `.so`, or `.dylib`, and a versioned shared library such as `libfoo.so.1`, is a library file and is passed through the same way. `-Bstatic` stays in effect until `-Bdynamic`. Put `-Bdynamic` after the static libraries when the C library should stay shared.

## Libraries shipped with the compiler

Vendor packages are imported as `vendor/<name>`. `TLC_LIB` selects their directory. When that variable is unset, the directory is `lib/tlc/vendor` next to the compiler prefix. Both `bin/tlc` and `build/tlc` use `<prefix>/lib/tlc/vendor`. The entry directory and each `-P` root are searched first, using the path `vendor/<name>`.

Standard packages live in `lib/tlc` beside that vendor directory. `import "std/testing"` loads `<prefix>/lib/tlc/std/testing`. `import "builtin"` is supplied by the compiler. `import "builtin/env/consts"` supplies `consts.family`. The entry directory and each `-P` root are searched first.

## Symbol names

A symbol is named `qualifier.name`. `extern` and `extern(abi="tlc")` use that name and are linked from a tlc library. `extern(abi="c")` keeps the C name.

## A program

```c
func main() i32 {
    return 40 + 2;
}
```

```sh
tlc --run main.tlc
```

The process exits with status 42.

## Comments

A `//` comment runs to the end of the line. A `/* ... */` comment may span lines. The language has no preprocessor and no `#include`.

```c
// The process exits with status 42.
func main() i32 {
    /* one
       main */
    return 40 + 2;
}
```

[Index](README.md) · [Next: Packages](packages.md) · [Grammar](grammar.md)
