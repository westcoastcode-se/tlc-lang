# Language server

[Index](../README.md) · [Previous: Debug information](debug.md) · [Next: Linking](link.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`tlc lsp` is the language server. Its sources are `src/lsp.cpp` and `src/lsp.hpp`, and the `tlc` binary includes them. Any editor can start that command. It reads the editor's buffer and typechecks that text, including when it differs from the file on disk. The server publishes diagnostics and answers completion, hover, go to definition, and document symbols.

In Zed, hold Ctrl and click a function name to open its definition. That includes a function brought in by `import { name } from "pkg"`.

Completion offers keywords, built-in types, and names in scope. After a dot it offers the fields, methods, and enumerators of a value's type. A slice offers `len`. A slice alias also offers the value methods written in that file, and an exported method from another package. A parameter of a trait type offers that trait's methods and fields, so `s *Shape` completes `s.` with `area`, `grow`, and `w`. A type name offers that type's static functions, and an enum name also offers its enumerators. A name bound with `:=`, `var name = expression`, or `const name = expression` uses the type of that expression when the expression is a call, a name, a field, or a cast. `p := Point.make(1)` then completes `p.`. A designated initializer `{.field = value}` completes the fields of that struct. The type is taken from the variable, from the name being assigned, from the result of the enclosing function after `return`, or from the surrounding field when the initializer is nested, as in `{.complex = {.inner = 1}}`. A file-scope `const` is one of the names offered. Zed starts this server from the TLC extension in `tlc-plugins-zed`. Set `lsp.tlc.binary.path` in Zed settings when `tlc` is not `build/tlc` in the open project and not on `PATH`. Set `lsp.tlc.binary.arguments` to `["lsp"]` beside that path. Zed starts the path on its own, and a missing argument list runs `tlc` with no arguments. Highlighting comes from the grammar in `tlc-tree-sitter`.

## Cost

The language server typechecks the buffer. It emits no instructions into the program.

[Index](../README.md) · [Previous: Debug information](debug.md) · [Next: Linking](link.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
