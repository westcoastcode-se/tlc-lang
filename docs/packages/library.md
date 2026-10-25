# Libraries

[Index](../README.md) · [Previous: Imports](import.md) · [Next: Visibility](visibility.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

Every `.tlc` file that belongs to an imported package is compiled with the program. A file for the other OS family is left out. A static archive (`.a`) and a shared library (`.so`, `.dylib`, or a versioned name such as `libfoo.so.1`) in the same directory is linked into the executable. When one library name has both an archive and a shared library, the archive is linked. Shared libraries are found through an rpath that points at the package directory. A package imported from several files is linked once.

A file named `link` adds further linker arguments after those libraries. `link_unix` and `link_windows` use the same lines. The file for the compiler's family is added after `link`. The other family is left out. `link_linux` is not a family file. The families are `unix` and `windows`, the same names as `*_unix.tlc`, `*_windows.tlc`, and `consts.family`. A package under `lib/tlc` uses that same reader. A package outside the vendor directory and `lib/tlc` contributes no link file. Each line holds the same flags as the command line: `-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, `-Wl`, and library paths. `#` comments out the rest of the line. A relative path is resolved from the package directory. `$ORIGIN` in an rpath is left as written. `--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.

An import cycle is reported as `circular import` followed by the qualifiers, for example `a -> b -> a`. The same package may be imported from several files and is loaded once.

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

[Index](../README.md) · [Previous: Imports](import.md) · [Next: Visibility](visibility.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
