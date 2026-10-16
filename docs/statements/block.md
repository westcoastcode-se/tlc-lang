# Statements

[Index](../README.md) · [Previous: Tuples and lists](../expressions/tuple.md) · [Next: for](for.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A block is a sequence of statements in braces. `if`, `while`, and `for` take a block. `else` is a block or another `if`. A body without `{` is reported as `expected '{'`. `do` and `switch` take a statement, so their body need not be a block. An empty statement is `;`.

```c
func main() i32 {
    var n i32 = 0;
    if (n) {
        n = 1;
    } else if (1) {
        n = 40;
    } else {
        n = 2;
    }
    while (n < 42) {
        n = n + 1;
    }
    do n = n + 1; while (0);
    ;
    return n;
}
```

## Cost

`if (n)` is 2 instructions, a compare with 0 and a jump, after `n` has been stored. `var n i32 = 0` is 1 store of 0, and `n = n + 1` is 1 load, 1 `addl $1`, and 1 store: 3 instructions.

`while (n < 3)` around `n = n + 1`, with `n` starting at 0, is 13 instructions for the whole function including `retq`. Each iteration that continues is a compare, a set, a mask, a zero-extend, a compare with 0, the add, the store, and a jump back. `do` runs the body before that test. An empty statement adds no instruction.

[Index](../README.md) · [Previous: Tuples and lists](../expressions/tuple.md) · [Next: for](for.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
