# TLC

`tlc` is a compiled C-family language. This repository is the compiler, the language reference, the standard library, and the tests. Highlighting lives in `../tlc-plugins-zed-grammars`. Zed tasks, binary lookup, and the debug locator live in `../tlc-plugins-zed`.

The language is the reference in `docs`. `docs/grammar.md` is the syntax. A feature page is the meaning. `TODO.md` is the list of forms the compiler rejects on purpose. A spelling the grammar does not list is not part of the language, even when the parser recognizes a similar C form and then rejects it.

## Before a language change

Read the feature page and the matching section of `TODO.md`. Implement that behavior. Remove a `TODO.md` entry when the form works. Add an entry for a form you parsed and left rejected.

Ask before inventing syntax, a diagnostic, or a runtime rule that the reference and `TODO.md` do not already describe. Leave neighboring unsupported forms as they are.

## Where the compiler work goes

Search and edit `src`, `docs`, `tests`, `lib`, and `examples`. Leave `vendor` alone. It is LLVM and other third-party trees.

The first comment in a `.cpp` file says what that file owns. Put the change in that file. Paths below are under `src/`.

| Concern | Files |
| --- | --- |
| Tokens | `lex/token.hpp`, `lex/lexer.cpp` |
| Imports and the file walk | `parse/parser.cpp` |
| File-scope declarations | `parse/parser_decl.cpp` |
| Statements and locals | `parse/parser_stmt.cpp` |
| Expressions | `parse/parser_expr.cpp` |
| Types | `parse/parser_type.cpp` |
| AST | `ast/ast.hpp`, `ast/ast.cpp` |
| Types and `analyze()` | `check/sema.cpp`, `check/sema.hpp` |
| Names and scopes | `check/sema_lookup.cpp` |
| Spelling a type | `check/sema_type.cpp` |
| Calls and initializers | `check/sema_expr.cpp` |
| One expression | `check/sema_value.cpp` |
| Checker folding | `check/sema_fold.cpp` |
| Statements and function bodies | `check/sema_stmt.cpp` |
| Definite assignment | `check/sema_init.cpp` |
| Packages, structs, globals | `check/sema_unit.cpp` |
| Generic templates and substitution | `check/sema_generic.cpp` |
| Generic instantiation and trait defaults | `check/sema_spec.cpp` |
| Checker state | `check/check.hpp` |
| Object file and codegen entry | `gen/codegen.cpp` |
| Lowering a type | `gen/codegen_lower.cpp` |
| Struct, union, enum, and alias layout | `gen/codegen_layout.cpp` |
| Codegen folding | `gen/codegen_fold.cpp` |
| Constant initializers and array stores | `gen/codegen_const.cpp` |
| Function bodies and captured names | `gen/codegen_func.cpp` |
| Calls | `gen/codegen_call.cpp` |
| Addresses, indexes, and slices | `gen/codegen_addr.cpp` |
| Expression values | `gen/codegen_value.cpp` |
| Statements and locals | `gen/codegen_stmt.cpp` |
| Trace, panic, `new`, and `delete` | `gen/codegen_builtin.cpp` |
| Emitting a package | `gen/codegen_global.cpp` |
| x86-64 SysV ABI | `gen/codegen_abi.cpp` |
| DWARF (`-g`) | `gen/codegen_debug.cpp` |
| CLI and the compile pipeline | `driver/driver.cpp` |
| Package loading, `_unix` / `_windows` filter | `pkg/pkg.cpp` |
| Diagnostics text | `diag/diag.cpp` |
| JSON and the language-server bytes | `lsp/lsp_json.cpp` |
| Index for completion, hover, and definition | `lsp/lsp_index.cpp` |
| Completion, hover, diagnostics, and semantic tokens | `lsp/lsp_complete.cpp` |
| Language server loop | `lsp/lsp.cpp` |

A new `.cpp` goes in `tlc_lib` or `tlc_tests` in `CMakeLists.txt`. The build is C++23 with `-fno-exceptions`, `-Wall`, `-Wextra`, and `-Wpedantic`. Do not throw. Match the surrounding file: namespace `tlc`, `snake_case` methods, trailing-underscore members. Comments are English and only explain a constraint the code does not show. Prefer a short, obvious function.

`assert`, `const`, and compile-time integers share one evaluator in `check/sema_fold.cpp` and `check/sema_unit.cpp`. `CodeGen::fold` and `CodeGen::fold_number` in `gen/codegen_fold.cpp` repeat it. A constant that one side folds and the other rejects is a bug. Update both.

`import "builtin"` is the compiler package. Its declarations live in `lib/tlc/builtin`. Adding, changing, or removing a builtin name, signature, method, or parameter updates `lib/tlc/builtin/builtin.tlc` in the same change, including the comment above that declaration. That file is what the editor shows. The compiler still supplies the bodies of the intrinsics. Do not describe a builtin in the compiler and leave the directory stale, and do not give the directory a signature the compiler does not implement.

`tlc lsp` publishes diagnostics from `analyze`. Completion, hover, definition, document symbols, and semantic tokens come from `index_unit` and `index_imports` in `lsp/lsp_index.cpp`. That index reads the parse and does not use the checker. A new declaration, field, method, or binding form has to be indexed there, or the editor will not offer it. Semantic tokens color names from that index. Keywords, literals, and built-in types stay in the tree-sitter grammar.

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

Change `../tlc-plugins-zed-grammars` when tokens or highlighting change. Match `src/lex/lexer.cpp` and the token section of `docs/grammar.md`. The grammar tokenizes. It does not parse statements. From that repo, regenerate with `npx tree-sitter-cli@0.25.8 generate` and run `npm test`. Do not copy the grammar or a `.wasm` into this repository.

Change `../tlc-plugins-zed` when tasks, compiler lookup, or the debug locator change. Highlighting and diagnostics do not belong there. Zed starts `lsp.tlc.binary.path` itself, and `lsp.tlc.binary.arguments` must include `lsp`. Reinstall the dev extension only after an extension change.
