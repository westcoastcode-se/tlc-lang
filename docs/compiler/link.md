# Linking

[Index](../README.md) · [Previous: Language server](lsp.md) · [Next: Symbol names](symbol.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

Linking passes the object file and the libraries to `cc`.

## Flags and library files

`-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, and `-Wl` are passed to `cc` after the object file, in the order written.

A path that ends in `.a`, `.so`, or `.dylib` is a library file and is passed through the same way.

A versioned shared library such as `libfoo.so.1` is passed through the same way.

`-Bstatic` stays in effect until `-Bdynamic`.

Put `-Bdynamic` after the static libraries when the C library should stay shared.

## Libraries shipped with the compiler

Vendor packages are imported as `vendor/<name>`.

`TLC_LIB` selects their directory.

When that variable is unset, the directory is `lib/tlc/vendor` next to the compiler prefix.

Both `bin/tlc` and `build/tlc` use `<prefix>/lib/tlc/vendor`.

The entry directory and each `-P` root are searched first, using the path `vendor/<name>`.

Standard packages live in `lib/tlc` beside that vendor directory.

`--lib <dir>` is searched before that directory. The flag may be repeated. The directories are used in the order written.

`import "std/testing"` loads `<dir>/std/testing` from the first of those directories that contains that package, and otherwise `<prefix>/lib/tlc/std/testing`.

`import "builtin"` and `import "std/mem"` use that same list. `import "builtin"` is the compiler package.

`import "builtin/env/consts"` supplies `consts.family`.

The entry directory and each `-P` root are searched first. `vendor/<name>` stays the vendor directory.

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

[Index](../README.md) · [Previous: Language server](lsp.md) · [Next: Symbol names](symbol.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
