# tlc

`tlc` 0.1.0 is a compiler for a C dialect. You compile one entry file. `import` names a directory of `.tlc` files, and only `export` is visible outside that package.

The language is described in [docs/README.md](docs/README.md).

## Build

The compiler links against the system LLVM 23.1.

```sh
cmake -S . -B build -G Ninja -DLLVM_DIR=/usr/lib/cmake/llvm
cmake --build build
```

## Run

```sh
build/tlc --run main.tlc
build/tlc -o prog main.tlc
build/tlc --check main.tlc
build/tlc --test main.tlc
```

With no mode flag, `tlc` writes an executable. The default name is `a.out`. `cc` links it. `--run` compiles, links, and runs. The exit status is the program's. `-l` and `-L` link shared or static libraries. `-Bstatic` and `-Bdynamic` choose which kind each `-l` uses. A library path, such as `libfoo.a`, `libfoo.so`, or `libfoo.so.1`, is passed to the linker as well.

`import "vendor/sdl3"` loads a package from `lib/tlc/vendor` next to the compiler prefix, or from the directory in `TLC_LIB`. A package there is `.tlc` bindings plus the static and shared libraries beside them. Those libraries are linked when the package is imported. `import "builtin"` is the compiler package. `import "std/testing"` loads `lib/tlc/std/testing`. See [Packages](docs/packages.md).

`examples/triangle` draws a triangle with SDL3 and OpenGL. `examples/tetris` is a game in the same kind of window:

```sh
build/tlc --run examples/triangle/main.tlc
build/tlc --run examples/tetris/main.tlc
```
