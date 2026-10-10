# tlc

`tlc` 0.1.0 is a compiler for a C dialect. You compile one entry file. `import` names a directory of `.tlc` files, and only `export` is visible outside that package.

The language is described in [docs/language.md](docs/language.md).

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
```

With no mode flag, `tlc` writes an executable. The default name is `a.out`. `cc` links it. `--run` compiles, links, and runs. The exit status is the program's. `-l` and `-L` link shared or static libraries. `-Bstatic` and `-Bdynamic` choose which kind each `-l` uses. A library path, such as `libfoo.a`, `libfoo.so`, or `libfoo.so.1`, is passed to the linker as well.
