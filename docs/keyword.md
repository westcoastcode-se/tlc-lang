# Keywords

[Index](README.md) · [Previous: Type aliases](types/alias.md) · [Next: Expressions](expressions/operator.md) · [Assembly](cost.md) · [Grammar](grammar.md)

This page names the keywords tlc adds beyond C, and the spellings that stay identifiers.

Beyond C, `import`, `export`, `package`, `trait`, `func`, `defer`, `var`, `type`, `unsafe`, `comptime`, `macro`, `emit`, `test`, `assert`, `new`, `delete`, and `null` are keywords. `typeof` and `alignof` are keywords, as in C23.

`impl`, `from`, `as`, `static`, and `this` are identifiers.

The full list is in the [grammar](grammar.md#tokens).

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

[Index](README.md) · [Previous: Type aliases](types/alias.md) · [Next: Expressions](expressions/operator.md) · [Assembly](cost.md) · [Grammar](grammar.md)
