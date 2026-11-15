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
| `--run` | Compile, link, and run. The program uses this terminal's stdin, stdout, and stderr. |
| `--test` | Compile, link, and run every `test`. `main` is not called. Bare `new` and `delete` use `debug.DebugAllocator`. This is a debug build. |
| `--debug` | A null postfix `!` calls `builtin.panic("null pointer")`. A race on a global is reported. `tlc --test` does this too. A release build leaves both checks out. |
| `--allocator <type>` | Type that bare `new` and `delete` use. `Malloc` is the default and calls `malloc` and `free`. `pkg.Name` is a struct in that package, and it satisfies `Allocator`. `tlc --test` uses `debug.DebugAllocator` unless this flag names a type. |
| `--max-memory <size>` | The most memory the compiler may use, such as `512M` or `4G`. `1G` is the default. `0` is no limit. |
| `-o <file>` | Output path. |
| `-P <dir>` | Extra package search root, after the entry file's directory. |
| `--lib <dir>` | Library directory searched before the default `lib/tlc`. The flag may be repeated. |
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

The entry directory and each `-P` root are searched before any `--lib` directory.

The directories are used in the order written.

`import "std/testing"` loads `<dir>/std/testing` from the first directory that contains that package, and otherwise `<prefix>/lib/tlc/std/testing`.

`import "builtin"` and `import "std/mem"` use that same list.

`vendor/<name>` stays `lib/tlc/vendor`, or the directory in `TLC_LIB`.

A flag with no directory is `missing directory after --lib`.

The language server uses the same list.

`--check`, `--emit-llvm`, and `--emit-obj` accept the link flags and do not link.

## Memory

`--max-memory` limits the memory of the whole compiler: parsing, checking, `comptime` calls, and LLVM. A size is a byte count, or a number with `K`, `M`, `G`, or `T`, each a power of 1024, in either case. `--max-memory=2G` is the same flag.

Memory counts what the compiler has written to, in RAM or in swap. A large array that is never touched does not count. Shared library code does not count.

Past the limit the compiler stops with status 1. A `comptime` call that is running is named:

```text
main.tlc:11:24: error: compile-time memory limit exceeded
```

Outside a `comptime` call, such as while LLVM optimizes, the message has no place:

```text
error: compile-time memory limit exceeded
```

A size that is not one of those forms is `invalid size after --max-memory: 12X`. A missing size is `missing size after --max-memory`.

The limit is checked every 10 milliseconds, so the compiler can go a little past it before it stops. The program that `--run` starts is a separate process and has no such limit. The language server has no limit.

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
