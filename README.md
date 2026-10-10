# tlc

`tlc` 0.1.0 is a compiler for a C dialect. You compile one entry file. `import` names a directory of `.c` files, and only `export` is visible outside that package.

The language is described in [docs/language.md](docs/language.md).

## Build

The compiler links against the system LLVM 23.1.

```sh
cmake -S . -B build -G Ninja -DLLVM_DIR=/usr/lib/cmake/llvm
cmake --build build
```

## Run

```sh
build/tlc --run main.c
build/tlc -o prog main.c
build/tlc --check main.c
```

With no mode flag, `tlc` writes an executable. The default name is `a.out`. `cc` links it. `--run` compiles, links, and runs. The exit status is the program's.
