# Imports

[Index](../README.md) · [Previous: Packages](package.md) · [Next: Libraries](library.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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
4. A path that is still missing is loaded from the standard library, `lib/tlc` next to the compiler prefix. `import "std/testing"` loads `<prefix>/lib/tlc/std/testing`. `import "builtin"` is the compiler package when no directory of that name was found. `import "builtin/env/consts"` is the compiler package `consts` in the same situation. It exports `family`.

The first directory that contains a `.tlc` file is the package. The vendor directory holds packages that ship with the compiler, such as a binding to a C library. `import "vendor/sdl3"` loads `<vendor>/sdl3`.

```text
lib/tlc/vendor/sdl3/
    video.tlc
    libSDL3.a
    libextra.so
    link
```

`examples/triangle` imports `vendor/sdl3` and `vendor/gl`. Each of those packages names a system library in its `link` file. `link_unix` or `link_windows` adds arguments for that family.

## Cost

An import is resolved while compiling. It adds no instructions. A call through the qualifier costs the same traced call as a call in the same file: 36 instructions in the caller when fewer than 32 frames are stored.

[Index](../README.md) · [Previous: Packages](package.md) · [Next: Libraries](library.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
