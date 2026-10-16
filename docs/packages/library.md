# Libraries

[Index](../README.md) · [Previous: Imports](import.md) · [Next: Visibility](visibility.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

Every `.tlc` file that belongs to an imported package is compiled with the program. A file for the other OS family is left out. A static archive (`.a`) and a shared library (`.so`, `.dylib`, or a versioned name such as `libfoo.so.1`) in the same directory is linked into the executable. When one library name has both an archive and a shared library, the archive is linked. Shared libraries are found through an rpath that points at the package directory. A package imported from several files is linked once.

A file named `link` adds further linker arguments after those libraries. Each line holds the same flags as the command line: `-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, `-Wl`, and library paths. `#` comments out the rest of the line. A relative path is resolved from the package directory. `$ORIGIN` in an rpath is left as written. `--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.

An import cycle is reported as `circular import` followed by the qualifiers, for example `a -> b -> a`. The same package may be imported from several files and is loaded once.

## Cost

Archives and shared libraries are link inputs. Loading one adds no instructions to a function.

[Index](../README.md) · [Previous: Imports](import.md) · [Next: Visibility](visibility.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
