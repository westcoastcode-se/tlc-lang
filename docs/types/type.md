# The Type type

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`Type` is the type of a tlc type. `typeof(i32)` and `typeof n` produce a `Type`. `typeof(Point)`, `typeof(*i32)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)` is reported as `expected a type`. `typeof('A')` is `u8`, and that value is different from `typeof(i8)`. `typeof` does not decay an array. `var t Type` with no value is reported as `type mismatch`.

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*Type` uses the value `p` points at. `name` is a method of `Type`. `t.name()` returns `*u8`. The text is the spelling diagnostics use: `i32`, `*i32`, `?*Point`, `[4]i32`, `Box.spec.i32`. `name` takes no arguments. Another argument is reported as `argument count`. Another method is reported as `undeclared identifier`. `t.name` without the call is reported as `undeclared identifier`.

`==` and `!=` compare two `Type` values and yield `bool`. Other operators on `Type` are reported as `type mismatch`.

```c
func main() i32 {
    var n i32 = 1;
    var t Type = typeof(n);
    var text *u8 = t.name();
    if (t == typeof(i32) && typeof('A') == typeof(u8) && text != null) {
        return 1;
    }
    return 0;
}
```

## Cost

`typeof(i32)` used as a value is the address of a type descriptor, one `leaq`. `var t Type = typeof(n)` stores a null pointer, then that address. `t == typeof(i32)` is a pointer compare: 5 instructions once both addresses are loaded, the same shape as `a == b` on integers. The whole function that stores one parameter, materializes both descriptors, and returns the compare is 10 instructions including `retq`.

`t.name()` is a traced call. Two `typeof` expressions compared from `assert` or from a file-scope `const` are folded and add no instructions.

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
