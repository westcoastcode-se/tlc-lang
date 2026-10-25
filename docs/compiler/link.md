# Linking

[Index](../README.md) · [Previous: Language server](lsp.md) · [Next: Symbol names](symbol.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

## Linking

`-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, and `-Wl` are passed to `cc` after the object file, in the order written. A path that ends in `.a`, `.so`, or `.dylib`, and a versioned shared library such as `libfoo.so.1`, is a library file and is passed through the same way. `-Bstatic` stays in effect until `-Bdynamic`. Put `-Bdynamic` after the static libraries when the C library should stay shared.

## Libraries shipped with the compiler

Vendor packages are imported as `vendor/<name>`. `TLC_LIB` selects their directory. When that variable is unset, the directory is `lib/tlc/vendor` next to the compiler prefix. Both `bin/tlc` and `build/tlc` use `<prefix>/lib/tlc/vendor`. The entry directory and each `-P` root are searched first, using the path `vendor/<name>`.

Standard packages live in `lib/tlc` beside that vendor directory. `import "std/testing"` loads `<prefix>/lib/tlc/std/testing`. `import "builtin"` is supplied by the compiler. `import "builtin/env/consts"` supplies `consts.family`. The entry directory and each `-P` root are searched first.

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
