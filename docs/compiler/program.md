# A program

[Index](../README.md) · [Previous: Symbol names](symbol.md) · [Next: Comments](comment.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A program is an entry file, and `tlc --run` executes its `main`.

The program reads `tlc`'s stdin and writes to its stdout and stderr directly, so output appears while the program runs, in the order it is written. `tlc --run` exits with the program's status, or 128 when the program ends on a signal. SIGINT, SIGTERM, SIGQUIT, and SIGHUP sent to `tlc` are passed on to the program. `tlc --test` runs its tests the same way.

```c
func main() i32 {
    return 40 + 2;
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

```sh
tlc --run main.tlc
```

The process exits with status 42.

[Index](../README.md) · [Previous: Symbol names](symbol.md) · [Next: Comments](comment.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
