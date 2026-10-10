# Vendor libraries

`tlc` searches this directory after the entry file's directory and every `-P` root. `TLC_LIB` selects a different directory. A binary at `build/tlc` or `bin/tlc` finds `<prefix>/lib/tlc`.

A package is a subdirectory that contains `.tlc` files:

```text
lib/tlc/sdl3/
    video.tlc
    libSDL3.a
    libextra.so
    link
```

`import "sdl3"` compiles those `.tlc` files. Archives (`.a`) and shared libraries (`.so`, `.dylib`, and versioned names such as `libfoo.so.1`) in the package directory are linked into the executable. A static archive is used when the same library also has a shared object. A shared library is found through an rpath that points at the package directory.

`link` is optional. Its lines are extra linker arguments, such as `-lpthread` or `-Bdynamic`. A relative path is resolved from the package directory. `#` comments out the rest of a line.

`--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.
