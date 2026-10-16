# Options

[Index](../README.md) · [Next: Debug information](debug.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

## Cost

`--check` emits no instructions. `--emit-llvm` prints the instructions the compiler built and does not assemble them. `--emit-obj` and a normal build write those instructions. `tlc` does not optimize them.

`return 40 + 2` is folded while compiling. `main` executes 2 instructions, `movl $42, %eax` and `retq`.

[Index](../README.md) · [Next: Debug information](debug.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
