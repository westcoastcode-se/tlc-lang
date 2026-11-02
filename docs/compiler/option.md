# Options

[Index](../README.md) · [Next: Debug information](debug.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`tlc` compiles one entry file.

The file's `main` is the program's `main`.

```text
tlc [options] <entry.tlc>
tlc lsp
tlc --version
tlc --help
```

## Flags

| Option | Meaning |
| --- | --- |
| `--check` | Parse and typecheck. Emit no code. |
| `--emit-llvm` | Print LLVM IR. |
| `--emit-obj` | Write an object file. The name is `a.o` when `-o` is absent. |
| `--run` | Compile, link, and run. |
| `--test` | Compile, link, and run every `test`. `main` is not called. Bare `new` and `delete` use `debug.DebugAllocator`. This is a debug build. |
| `--debug` | A null postfix `!` calls `builtin.panic("null pointer")`. A race on a global is reported. `tlc --test` does this too. A release build leaves both checks out. |
| `--allocator <type>` | Type that bare `new` and `delete` use. `Malloc` is the default and calls `malloc` and `free`. `pkg.Name` is a struct in that package, and it satisfies `Allocator`. `tlc --test` uses `debug.DebugAllocator` unless this flag names a type. |
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

## Races

A debug build reports a race on a global.

Two threads access that global, and at least one access writes it. An atomic access is not one of those.

`thread.spawn` and `thread.join` order the accesses around them. A `thread.Mutex` does not.

The report goes to the standard error stream. Each side is the calls that have not returned, newest first. The side whose thread started first is printed first.

A release build does not emit the check.

```c
import "std/thread";

var n i32;

func write_n() void {
    n = 1;
}

func work(arg *void) *void {
    write_n();
    return arg;
}

func set_n() void {
    n = 2;
}

func main() i32 {
    var dummy i32 = 0;
    var t = thread.spawn(work, *void(&dummy));
    set_n();
    _ = thread.join(t);
    return 0;
}
```

`tlc --debug --run` prints:

```text
race
main.tlc:21:5: main
main.tlc:10:5: work
```

[Index](../README.md) · [Next: Debug information](debug.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
