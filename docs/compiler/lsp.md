# Language server

[Index](../README.md) · [Previous: Debug information](debug.md) · [Next: Linking](link.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`tlc lsp` is the language server.

## What the server answers

Its sources are under `src/lsp/`.

The `tlc` binary includes them.

Any editor can start that command.

The server reads the editor's buffer and typechecks that text.

It does this even when the buffer differs from the file on disk.

The server publishes diagnostics and answers completion, hover, go to definition, document symbols, and semantic tokens.

`textDocumentSync` is incremental.

A `didChange` applies each range in order.

A change with no range still replaces the whole buffer.

In Zed, hold Ctrl and click a function name to open its definition.

That includes a function brought in by `import { name } from "pkg"`.

## Completion

Completion offers keywords, built-in types, and names in scope.

After a dot it offers the fields, methods, and enumerators of a value's type.

A slice, a sized array, or a written `*[N]T` offers `len`.

Hovering `len` shows `len i32`.

`*i32` and a still-optional `?*[N]T` do not.

A call, an index, and a parenthesized expression are receivers too.

`Point.make(1).`, `p.copy().`, and `(p).` offer that value's fields and instance methods.

`slice[0].` offers the element, and so does `headed[0].` when `headed` is `*[N]Point`.

`span[0].` on `*[N]i32` offers nothing.

A range such as `slice[0:0].` is a slice and offers `len`.

A call that returns a slice or a sized array, such as `points().`, offers `len`.

An index of `?*T` offers nothing.

When that receiver is not a name and has no members, completion does not fall through to keywords.

`(kept).` when `kept` is `i32` offers nothing, and so does `p.sum().`.

A slice alias also offers the value methods written in that file, and an exported method from another package.

A parameter of a trait type offers that trait's methods and fields, so `s *Shape` completes `s.` with `area`, `grow`, and `w`.

A type name offers that type's static functions, and an enum name also offers its enumerators.

Another package's `pkg.Type.` offers the exported static functions.

A name bound with `:=`, `var name = expression`, or `const name = expression` uses the type of that expression.

The expression has to be a call, a name, a field, or a cast.

`p := Point.make(1)` then completes `p.`.

A designated initializer `{.field = value}` completes the fields of that struct.

The type is taken from the variable, or from the name being assigned.

It is also taken from the result of the enclosing function after `return`.

When the initializer is nested, the type comes from the surrounding field, as in `{.complex = {.inner = 1}}`.

A file-scope `const` is one of the names offered.

The name in `for (var item : items)` and `for (const item : items)` is in scope in the loop.

That name hides an outer name there.

## Hover, rename, format, and signature help

Hover shows the element of an array or a slice, and a pointer to that element after `ptr_of`.

A struct collection names the binding and does not invent an element type.

Rename rewrites the binding under the cursor and the uses that see it.

A use in the initializer of a later `var n = n + 1` still belongs to the earlier `n`.

Formatting reindents each line by the brace depth, four spaces per level, and leaves the rest of the line as written.

Signature help shows the parameter list of the call around the cursor.

A diagnostic `expected ';'` in the requested range offers a quick fix that inserts `;`.

## Starting the server from Zed

Zed starts this server from the TLC extension in `tlc-plugins-zed`.

Set `lsp.tlc.binary.path` in Zed settings when `tlc` is not `build/tlc` in the open project and not on `PATH`.

Set `lsp.tlc.binary.arguments` to `["lsp"]` beside that path.

Zed starts the path on its own.

A missing argument list runs `tlc` with no arguments.

## Semantic tokens

`textDocument/semanticTokens/full` and `textDocument/semanticTokens/range` color a name by its role in that index.

The roles include a struct, an enum, a field, a method, a variable, and the other roles the index records.

The declaration modifier marks the name where it is declared.

A `const` is read-only, and a static method is static.

`len` on a slice or a sized array is a property when it is not a real member.

Keywords, literals, and built-in types stay colored by the grammar in `tlc-plugins-zed-grammars`.

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

[Index](../README.md) · [Previous: Debug information](debug.md) · [Next: Linking](link.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
