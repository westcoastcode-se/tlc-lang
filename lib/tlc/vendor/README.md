# Vendor libraries

`tlc` packages in this directory are imported as `vendor/<name>`. A binary at `build/tlc` or `bin/tlc` finds `<prefix>/lib/tlc/vendor`. `TLC_LIB` selects a different directory.

```text
lib/tlc/vendor/sdl3/
    sdl.tlc
    link
lib/tlc/vendor/gl/
    gl.tlc
    link
```

`import "vendor/sdl3"` compiles the `.tlc` files in `sdl3`. Archives (`.a`) and shared libraries (`.so`, `.dylib`, and versioned names such as `libfoo.so.1`) in that package directory are linked into the executable. A static archive is used when the same library also has a shared object. A shared library is found through an rpath that points at the package directory.

`link` is optional. Its lines are extra linker arguments, such as `-lSDL3` or `-lpthread`. A relative path is resolved from the package directory. `#` comments out the rest of a line.

`--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.
