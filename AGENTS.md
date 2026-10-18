# TLC

`tlc` is a compiled C-family language. This repository is the compiler, the language reference, the standard library, and the tests. Highlighting lives in `../tlc-tree-sitter`. Zed tasks, binary lookup, and the debug locator live in `../tlc-plugins-zed`.

The language is the reference in `docs`. `docs/grammar.md` is the syntax. A feature page is the meaning. `TODO.md` is the list of forms the compiler rejects on purpose. A spelling the grammar does not list is not part of the language, even when the parser recognizes a similar C form and then rejects it.

## Before a language change

Read the feature page and the matching section of `TODO.md`. Implement that behavior. Remove a `TODO.md` entry when the form works. Add an entry for a form you parsed and left rejected.

Ask before inventing syntax, a diagnostic, or a runtime rule that the reference and `TODO.md` do not already describe. Leave neighboring unsupported forms as they are.

## Where the compiler work goes

Search and edit `src`, `docs`, `tests`, `lib`, and `examples`. Leave `vendor` alone. It is LLVM and other third-party trees.

The first comment in a `.cpp` file says what that file owns. Put the change in that file.

| Concern | Files |
| --- | --- |
| Tokens | `token.hpp`, `lexer.cpp` |
| Imports and the file walk | `parser.cpp` |
| File-scope declarations | `parser_decl.cpp` |
| Statements and locals | `parser_stmt.cpp` |
| Expressions | `parser_expr.cpp` |
| Types | `parser_type.cpp` |
| AST | `ast.hpp`, `ast.cpp` |
| Types and `analyze()` | `sema.cpp`, `sema.hpp` |
| Names and scopes | `sema_lookup.cpp` |
| Expressions, bodies, folding | `sema_expr.cpp` |
| Packages, structs, globals | `sema_unit.cpp` |
| Generics | `sema_generic.cpp` |
| Checker state | `check.hpp` |
| Object file and codegen entry | `codegen.cpp` |
| Layout and type lowering | `codegen_lower.cpp` |
| Bodies and codegen folding | `codegen_emit.cpp` |
| x86-64 SysV ABI | `codegen_abi.cpp` |
| DWARF (`-g`) | `codegen_debug.cpp` |
| CLI and the compile pipeline | `driver.cpp` |
| Package loading, `_unix` / `_windows` filter | `pkg.cpp` |
| Diagnostics text | `diag.cpp` |
| Language server | `lsp.cpp` |

A new `.cpp` goes in `tlc_lib` or `tlc_tests` in `CMakeLists.txt`. The build is C++23 with `-fno-exceptions`, `-Wall`, `-Wextra`, and `-Wpedantic`. Do not throw. Match the surrounding file: namespace `tlc`, `snake_case` methods, trailing-underscore members. Comments are English and only explain a constraint the code does not show. Prefer a short, obvious function.

`assert`, `const`, and compile-time integers share one evaluator in `sema_expr.cpp` and `sema_unit.cpp`. `CodeGen::fold` and `CodeGen::fold_number` in `codegen_emit.cpp` repeat it. A constant that one side folds and the other rejects is a bug. Update both.

`tlc lsp` publishes diagnostics from `analyze`. Completion, hover, definition, and document symbols come from `index_unit` and `index_imports` in `lsp.cpp`. That index reads the parse and does not use the checker. A new declaration, field, method, or binding form has to be indexed there, or the editor will not offer it. Rename, formatting, code actions, signature help, and semantic tokens stay missing until `TODO.md` says otherwise.

A form you can parse but cannot check and emit is reported as `not supported yet` and written into `TODO.md`. Do not accept it and emit the wrong code.

## Diagnostics

Keep the short English phrases already used, such as `not supported yet`, `type mismatch`, `redefinition`, and `global initializer is not a constant`. The rendered form is `file:line:column: error: message`. A changed phrase has to be updated in every test that matches it.

## Tests

From this repository, use the existing `build` directory:

```sh
cmake -S . -B build -G Ninja -DLLVM_DIR=/usr/lib/cmake/llvm
cmake --build build
./build/tlc_tests
```

The compiler links system LLVM 23.1. `./build/tlc_tests` is one binary and has no filter. The change is done when that command prints `all tests passed`. While iterating, `./build/tlc --check` or `./build/tlc --run` on the new program is enough. The binary Zed runs is `build/tlc`.

A behavior change gets a directory `tests/programs/<name>/` with `main.tlc` and `expect.txt`. Package files sit in subdirectories of that test. `expect.txt` keys:

- `exit: N` checks the process status.
- `compile-error: text` runs `--check` and requires `text` as a substring of the compiler stderr.
- `link-error: text` requires `text` as a substring of the compiler stderr. The linker message is stored there.
- `link: arg` appends one linker argument, repeated once per argument.
- `mode: test` runs the program with `--test`.
- `stdout:` and `stderr:` are exact. Each one consumes the rest of the file, so it comes last.

A semantic unit test does not prove code generation. A new expression or statement needs a program that runs, or a `compile-error` program when the form must be rejected. Language-server behavior goes in `tests/lsp_test.cpp`.

## Documentation

Comments and docs are English. Update the feature page when the meaning changes, and `docs/grammar.md` when the syntax changes. Link a new page from `docs/README.md` and from the previous and next pages. Each feature page keeps its navigation lines, a short example, and a Cost section.

With no `-O` flag, `tlc` removes a store that is overwritten or never read, and does not run the other LLVM optimizations. `-O0` keeps that store. `-O`, `-O1`, `-O2`, and `-O3` run LLVM's pipeline. A cost is the number of x86-64 instructions that run, measured as `docs/cost.md` describes, for `x86_64-pc-linux-gnu`. Measure with `tlc --emit-obj` before writing a count. Update or remove examples on the page, and fix `examples/triangle` and `examples/tetris` when a change breaks them.

## Packages

`import` names a directory of `.tlc` files. Only `export` is visible outside that package. `lib/tlc` is the standard library. `lib/tlc/vendor/<name>` is imported as `vendor/<name>` and may contain libraries plus an optional `link` file.

A file named `*_unix.tlc` is loaded only when the compiler's family is `unix`. `*_windows.tlc` is loaded only for `windows`. Every other `.tlc` name is loaded on both. Family is chosen when `tlc` itself is built. Put OS-specific vendor or library code in those suffixes.

## Editor and grammar

A language change that does not add a token or a highlight query stays in this repository. Rebuilding `build/tlc` updates the language server.

Change `../tlc-tree-sitter` when tokens or highlighting change. Match `lexer.cpp` and the token section of `docs/grammar.md`. The grammar tokenizes. It does not parse statements. From that repo, regenerate with `npx tree-sitter-cli@0.25.8 generate` and run `npm test`. Do not copy the grammar or a `.wasm` into this repository.

Change `../tlc-plugins-zed` when tasks, compiler lookup, or the debug locator change. Highlighting and diagnostics do not belong there. Zed starts `lsp.tlc.binary.path` itself, and `lsp.tlc.binary.arguments` must include `lsp`. Reinstall the dev extension only after an extension change.
