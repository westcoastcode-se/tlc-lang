# Keywords

[Index](README.md) · [Previous: Type aliases](types/alias.md) · [Next: Expressions](expressions/operator.md) · [Assembly](cost.md) · [Grammar](grammar.md)

Beyond C, `import`, `export`, `package`, `trait`, `func`, `defer`, `var`, `type`, and `unsafe` are keywords. `impl`, `from`, `as`, and `this` are identifiers. The full list is in the [grammar](grammar.md#tokens).

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
