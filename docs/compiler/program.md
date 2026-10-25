# A program

[Index](../README.md) · [Previous: Symbol names](symbol.md) · [Next: Comments](comment.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

```c
func main() i32 {
    return 40 + 2;
}
```

```sh
tlc --run main.tlc
```

The process exits with status 42.

## Assembly

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

[Index](../README.md) · [Previous: Symbol names](symbol.md) · [Next: Comments](comment.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
