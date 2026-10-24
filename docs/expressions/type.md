# Type values

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`typeof(i32)` and `typeof n` have type `builtin.Type`. `import "builtin"` names that type. `==` and `!=` compare two `builtin.Type` values and yield `bool`. Two `typeof` expressions are constants, so `assert(typeof(i32) == typeof(i32))` folds while compiling. `t.name()` returns the diagnostic spelling as `*u8`. `t.fields()` returns `[]const Field`, the fields of a named struct. Assigning an element is `type mismatch`. `fields[i].name` is the field's name, a `[]const u8`, and `fields[i].kind` is `*const builtin.Type`. `*fields[i].kind` is its type. Storing through `fields[i].kind` is `type mismatch`. Other operators on `builtin.Type` are reported as `type mismatch`.

```c
import "builtin";

func main() i32 {
    var n i32 = 1;
    var t builtin.Type = typeof(n);
    if (typeof(n) == typeof(i32) && t.name() != null) {
        return 1;
    }
    return 0;
}
```

## Cost

Materializing `typeof` is one `leaq` of a descriptor. Comparing two `Type` values is a pointer compare, 5 instructions once both addresses are loaded. `assert` and a file-scope `const` fold the compare and add no instructions. `t.name()` loads the spelling from the descriptor. A function that stores `typeof(n)` and returns `t.name()` is 5 instructions including `retq`. `return typeof(Point).fields().len` is 3 instructions including `retq`.

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
