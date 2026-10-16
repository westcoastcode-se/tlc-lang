# A program

[Index](../README.md) · [Previous: Symbol names](symbol.md) · [Next: Comments](comment.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

```c
func main() i32 {
    return 40 + 2;
}
```

```sh
tlc --run main.tlc
```

The process exits with status 42.

## Cost

The program below executes 2 instructions. `40 + 2` is folded to 42, and `main` moves that immediate into `%eax` and returns.

[Index](../README.md) · [Previous: Symbol names](symbol.md) · [Next: Comments](comment.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
