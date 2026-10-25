# Packages

[Index](../README.md) · [Previous: Comments](../compiler/comment.md) · [Next: Imports](import.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A package is a directory of `.tlc` files. The file named on the command line is its own package: other `.tlc` files beside it are not compiled with it. An import names a directory. Every `.tlc` file directly in that directory belongs to one package. A subdirectory is a different package.

A file whose name ends in `_unix.tlc` is loaded only when the compiler's family is `unix` (Linux, macOS, and every other system that is not Windows). A file whose name ends in `_windows.tlc` is loaded only when the family is `windows`. `util.tlc` and `util_extra.tlc` are loaded on every system. The file named on the command line is loaded even when its name ends in one of those suffixes. A directory whose `.tlc` files are all for the other family is reported as `unknown package`.

```text
math/
    add.tlc
    sep_unix.tlc
    sep_windows.tlc
```

On Linux or macOS the package is `add.tlc` and `sep_unix.tlc`. On Windows it is `add.tlc` and `sep_windows.tlc`.

## The package clause

`package` is the first declaration in a file, before imports. The name is an identifier.

```c
package math;

export func add(a i32, b i32) i32 {
    return a + b;
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $2, %esi                    # constant
callq add                        # call add
retq                             # return

add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

The qualifier is that name when every file in the directory agrees. When no file has a `package` clause, the qualifier is the directory's name. Clauses that disagree are reported as `package mismatch`. The entry file's qualifier is its `package` name, or the file name without `.tlc`.

[Index](../README.md) · [Previous: Comments](../compiler/comment.md) · [Next: Imports](import.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
