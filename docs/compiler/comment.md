# Comments

[Index](../README.md) · [Previous: A program](program.md) · [Next: Packages](../packages/package.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A `//` comment runs to the end of the line. A `/* ... */` comment may span lines. The language has no preprocessor and no `#include`. An editor shows the `//` lines immediately above a declaration together with that declaration. A blank line ends that text.

```c
// The process exits with status 42.
func main() i32 {
    /* one
       main */
    return 40 + 2;
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

[Index](../README.md) · [Previous: A program](program.md) · [Next: Packages](../packages/package.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
