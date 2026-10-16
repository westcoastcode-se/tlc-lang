# Type values

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`typeof(i32)` and `typeof n` have type `Type`. `==` and `!=` compare two `Type` values and yield `i32`. Two `typeof` expressions are constants, so `assert(typeof(i32) == typeof(i32))` folds while compiling. `t.name()` returns the diagnostic spelling as `*u8`. Other operators on `Type` are reported as `type mismatch`.

```c
func main() i32 {
    var n i32 = 1;
    var t Type = typeof(n);
    if (typeof(n) == typeof(i32) && t.name() != null) {
        return 1;
    }
    return 0;
}
```

## Cost

Materializing `typeof` is one `leaq` of a descriptor. Comparing two `Type` values is a pointer compare, 5 instructions once both addresses are loaded. `assert` and a file-scope `const` fold the compare and add no instructions. `t.name()` is a traced call, 36 instructions in the caller when fewer than 32 frames are stored, plus the callee that builds the text.

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
