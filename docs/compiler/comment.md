# Comments

[Index](../README.md) · [Previous: A program](program.md) · [Next: Packages](../packages/package.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A `//` comment runs to the end of the line. A `/* ... */` comment may span lines. The language has no preprocessor and no `#include`.

```c
// The process exits with status 42.
func main() i32 {
    /* one
       main */
    return 40 + 2;
}
```

## Cost

A comment is discarded before any instruction is chosen. It adds none.

[Index](../README.md) · [Previous: A program](program.md) · [Next: Packages](../packages/package.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
