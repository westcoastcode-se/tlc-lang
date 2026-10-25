# Options

[Index](../README.md) · [Next: Debug information](debug.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

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
| `-O0` | Keep every store. Do not optimize. |
| `-O`, `-O1` | LLVM's first optimization level. |
| `-O2` | LLVM's second optimization level. |
| `-O3` | LLVM's third optimization level. |
| `-Wl,<opt>` | Pass `<opt>` through to the linker. |
| `lsp` | Speak the language server protocol on standard input and output. |

`--check`, `--emit-llvm`, and `--emit-obj` accept the link flags and do not link.

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

[Index](../README.md) · [Next: Debug information](debug.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
