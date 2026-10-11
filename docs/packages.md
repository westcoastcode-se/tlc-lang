# Packages

[Index](README.md) · [Previous: The compiler](compiler.md) · [Next: Types](types.md) · [Grammar](grammar.md)

The file on the command line is its own package. Other `.tlc` files next to it are not compiled with it. An import names a directory. Every `.tlc` file directly in that directory is one package, and a subdirectory is a different package.

The qualifier is the `package` name when every file in the directory agrees. When no file has a `package` clause, the qualifier is the directory's name. Clauses that disagree produce `package mismatch`. The entry file's qualifier is its `package` name, or the file name without `.tlc`.

```c
package math;

export func add(a int, b int) int {
    return a + b;
}
```

`package` comes first, before imports and declarations. The name is an identifier.

Imports come after `package` and before every declaration. There are three forms:

```c
import "math";
import "math" as m;
import { add, Point } from "math";
```

`as` and `from` are ordinary words in those positions. They are identifiers everywhere else.

The first form binds the package qualifier as a name, so the call is `math.add(40, 2)`. The alias makes it `m.add(40, 2)`. The selective form brings the named exports into the file, so the call is `add(40, 2)`. An import is private to the file that wrote it.

The path is a string and names a directory. An empty path and an absolute path are rejected. A path that is not a directory, including one that ends in `.tlc`, produces `unknown package`.

- `./` and `../` are resolved relative to the importing file and must name a directory.
- Every other path is searched in the entry file's directory, then in each `-P` root. A path `vendor/<name>` that is not found there is then loaded from the vendor directory. The first directory that contains a `.tlc` file wins.
- A directory with no `.tlc` file produces `unknown package`.

The vendor directory holds packages that ship with the compiler, such as a binding to a C library. `import "vendor/sdl3"` loads `<vendor>/sdl3`.

```text
lib/tlc/vendor/sdl3/
    video.tlc
    libSDL3.a
    libextra.so
    link
```

`examples/triangle` is a program that imports `vendor/sdl3` and `vendor/gl`. Each of those packages names a system library in its `link` file.

Every `.tlc` file in that package is compiled with the program. A static archive (`.a`) and a shared library (`.so`, `.dylib`, or a versioned name such as `libfoo.so.1`) in the same directory is linked into the executable. When one library name has both an archive and a shared library, the archive is linked. Shared libraries are found through an rpath that points at the package directory. The same package imported from several files is linked once.

A file named `link` in the package adds more linker arguments after those libraries. Each line holds the same flags as the command line: `-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, `-Wl`, and library paths. `#` comments out the rest of the line. A relative path is resolved from the package directory. `$ORIGIN` in an rpath is left as written. `--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.

An import cycle produces `circular import` followed by the qualifiers, for example `a -> b -> a`. The same package may be imported from several files and is loaded once.

Inside a package, every file can use what the other files in the directory declare. A name that is not `static` is visible from every file. The same non-static name in two files is `redefinition`. `static` stays private to its file, so two files may each declare `static` with the same name. A method is written in the file that defines its struct or enum, and the other files can call it.

## Visibility

Only a file-scope `export` is published to importers. A name that was not exported produces `name is not exported`.

```c
package math;

func secret() int {
    return 1;
}

export func add(a int, b int) int {
    return a + b + secret();
}
```

`secret` can be called from any file in the package. An importer sees `add`.

`export` applies to functions, variables, and `type` declarations other than a trait. `export type Name trait` is `not supported yet`. `export static` is an error. `export extern(abi="c")` is allowed on a function, and the symbol keeps its C name. `export` together with `extern` on a variable is `export extern`.

File-scope `static` keeps the symbol private to that file. A `static` local is `static locals are not supported yet`.

`extern` and `extern(abi="tlc")` name a symbol from a tlc library. The symbol is `qualifier.name`, and the calling convention is the compiler's own, so the function may return several results. `extern(abi="c")` names a symbol from a C library, and only then. It keeps the C name and the C calling convention. It returns one result, the same as a C function. Linking a program that uses `extern(abi="c")` requires a C library on the link line: `-l`, or a library file such as `libfoo.a`. `--check` and `--emit-*` do not link, so they accept the declaration. Without that library the link step reports `extern(abi="c") requires a C library`. Its parameters and its result cannot include a function pointer that returns several results. A struct used by an `extern(abi="c")` function, by value or through a pointer, must itself be `extern(abi="c")`. That includes a struct nested inside it. An `extern(abi="c") struct` uses the platform C memory layout, so its fields line up with a static or shared library. `bool` in that struct is one byte. On this host a struct of at most 16 bytes is passed in registers, and a larger struct is passed as a pointer. A function that is not `extern(abi="c")` keeps the compiler's own convention for struct values. That is how a program calls a C library:

```c
extern(abi="c") func printf(fmt *const char, ...) int;

func main() int {
    printf("hi\n");
    return 0;
}
```

`const` is accepted on parameters of an `extern(abi="c")` declaration. A variadic function may be declared with `extern(abi="c")` and without a body. A definition that uses `...` is rejected. `extern` does not accept `...`.

[Index](README.md) · [Previous: The compiler](compiler.md) · [Next: Types](types.md) · [Grammar](grammar.md)
