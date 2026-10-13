# Packages

[Index](README.md) · [Previous: The compiler](compiler.md) · [Next: Types](types.md) · [Grammar](grammar.md)

A package is a directory of `.tlc` files. The file named on the command line is its own package: other `.tlc` files beside it are not compiled with it. An import names a directory. Every `.tlc` file directly in that directory belongs to one package. A subdirectory is a different package.

## The package clause

`package` is the first declaration in a file, before imports. The name is an identifier.

```c
package math;

export func add(a i32, b i32) i32 {
    return a + b;
}
```

The qualifier is that name when every file in the directory agrees. When no file has a `package` clause, the qualifier is the directory's name. Clauses that disagree are reported as `package mismatch`. The entry file's qualifier is its `package` name, or the file name without `.tlc`.

## Imports

Imports follow `package` and precede every other declaration. An import is private to the file that wrote it. There are three forms:

```c
import "math";
import "math" as m;
import { add, Point } from "math";
```

| Form | Name in this file |
| --- | --- |
| `import "math"` | The qualifier, as in `math.add(40, 2)`. |
| `import "math" as m` | The alias, as in `m.add(40, 2)`. |
| `import { add, Point } from "math"` | Each listed export, as in `add(40, 2)`. |

`as` and `from` are ordinary identifiers in every other position.

The path is a string and names a directory. An empty path and an absolute path are rejected. A path that does not name a directory, including a path that ends in `.tlc`, is reported as `unknown package`. A directory with no `.tlc` file is reported the same way.

Search proceeds as follows.

1. A path that starts with `./` or `../` is resolved from the importing file. It must name a directory.
2. Every other path is searched in the entry file's directory, then in each `-P` root.
3. A path `vendor/<name>` that is still missing is loaded from the vendor directory.
4. A path that is still missing is loaded from the standard library, `lib/tlc` next to the compiler prefix. `import "std/testing"` loads `<prefix>/lib/tlc/std/testing`. `import "builtin"` is the compiler package when no directory of that name was found.

The first directory that contains a `.tlc` file is the package. The vendor directory holds packages that ship with the compiler, such as a binding to a C library. `import "vendor/sdl3"` loads `<vendor>/sdl3`.

```text
lib/tlc/vendor/sdl3/
    video.tlc
    libSDL3.a
    libextra.so
    link
```

`examples/triangle` imports `vendor/sdl3` and `vendor/gl`. Each of those packages names a system library in its `link` file.

## Libraries

Every `.tlc` file in an imported package is compiled with the program. A static archive (`.a`) and a shared library (`.so`, `.dylib`, or a versioned name such as `libfoo.so.1`) in the same directory is linked into the executable. When one library name has both an archive and a shared library, the archive is linked. Shared libraries are found through an rpath that points at the package directory. A package imported from several files is linked once.

A file named `link` adds further linker arguments after those libraries. Each line holds the same flags as the command line: `-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, `-Wl`, and library paths. `#` comments out the rest of the line. A relative path is resolved from the package directory. `$ORIGIN` in an rpath is left as written. `--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.

An import cycle is reported as `circular import` followed by the qualifiers, for example `a -> b -> a`. The same package may be imported from several files and is loaded once.

## Names inside a package

Every file in a package can use a name declared in another file of that directory, unless the name is `static`. The same non-`static` name in two files is `redefinition`. Two files may each declare `static` with the same name. A method is written in the file that defines its struct or enum. The other files of the package can call it.

## Visibility

Only a file-scope `export` is published to importers. A name that was not exported is reported as `name is not exported`.

```c
package math;

func secret() i32 {
    return 1;
}

export func add(a i32, b i32) i32 {
    return a + b + secret();
}
```

`secret` can be called from any file in the package. An importer sees `add`.

`export` applies to functions, variables, and `type` declarations. `export static` is an error. `export extern(abi="c")` is allowed on a function, and the symbol keeps its C name. `export` together with `extern` on a variable is written `export extern`.

File-scope `static` keeps the symbol private to that file.

```c
static func hidden() i32 {
    return 1;
}

static var slot i32 = 2;

func main() i32 {
    return hidden() + slot;
}
```

## `extern`

`extern` and `extern(abi="tlc")` name a symbol from a tlc library. The symbol is `qualifier.name`. The calling convention is the compiler's own, so the function may return several results.

`extern(abi="c")` names a symbol from a C library. It keeps the C name and the C calling convention, and it returns one result. Linking a program that uses `extern(abi="c")` requires a C library on the link line, either `-l` or a library file such as `libfoo.a`. `--check` and `--emit-*` do not link, so they accept the declaration. Without that library the link step reports `extern(abi="c") requires a C library`.

A parameter or result of an `extern(abi="c")` function cannot be a function pointer that returns several results. A struct used by such a function, by value or through a pointer, must itself be `extern(abi="c")`. That includes a struct nested inside it. An `extern(abi="c") struct` uses the platform C memory layout, so its fields line up with a static or shared library. `bool` in that struct is one byte. On this host a struct of at most 16 bytes is passed in registers, and a larger struct is passed as a pointer. A function that is not `extern(abi="c")` keeps the compiler's own convention for struct values.

```c
type Point struct {
    x i32;
    y i32;
};

extern(abi="tlc") func parts(p Point) (i32, i32) {
    return p.x, p.y;
}

func main() i32 {
    var p Point;
    p.x = 20;
    p.y = 22;
    a, b := parts(p);
    return a + b;
}
```

```c
extern(abi="c") func printf(fmt *const u8, ...) i32;

func main() i32 {
    printf("hi\n");
    return 0;
}
```

`*const T` on a parameter, including an `extern(abi="c")` parameter, is a pointer to constant `T`. A variadic function may be declared with `extern(abi="c")` and without a body. A function with a body may use `...`, and `builtin.va()` reads the extra arguments.

[Index](README.md) · [Previous: The compiler](compiler.md) · [Next: Types](types.md) · [Grammar](grammar.md)
