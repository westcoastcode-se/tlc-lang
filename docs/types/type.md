# The Type type

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`builtin.Type` is the type of a tlc type. `import "builtin"` names it `builtin.Type`. `import { Type } from "builtin"` names it `Type`. `typeof(i32)` and `typeof n` produce that type. A bare `Type`, with no such import, is reported as `not supported yet`. `typeof(Point)`, `typeof(*i32)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)`, `typeof(union Shape)`, and `typeof(trait Area)` are reported as `expected a type`. `typeof('A')` is `u8`, and that value is different from `typeof(i8)`. `typeof` does not decay an array. `var t builtin.Type` with no value is reported as `type mismatch`. `typeof(builtin.Type).name()` spells `Type`.

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*builtin.Type` uses the value `p` points at. `name` is a method of `builtin.Type`. `t.name()` returns `*u8`. The text is the spelling diagnostics use: `i32`, `*i32`, `?*Point`, `[4]i32`, `Box.spec.i32`. `name` takes no arguments. Another argument is reported as `argument count`. Another method is reported as `undeclared identifier`. `t.name` without the call is reported as `undeclared identifier`.

`t.fields()` returns the fields of a named struct, in the order they are written. The result is a slice. `fields.len` is how many there are. `fields[i].name` is that field's name as `*u8`. `fields[i].kind` is that field's type, a `builtin.Type`, so `fields[i].kind.name()` spells it and `fields[i].kind.fields()` reads a struct field's own fields. The field is `kind` because `type` is a keyword. An alias of a struct lists that struct's fields. `typeof(Alias)` stays different from `typeof(Point)`. A generic struct lists the fields of that specialization: `typeof(Box[i32])` has `value` of type `i32`. Any other type has length 0, including `i32`, a pointer, an array, a slice, an alias of a primitive, an anonymous struct, and a union. `fields` takes no arguments. Another argument is reported as `argument count`. `t.fields` without the call is reported as `undeclared identifier`. `p.fields()` on a `*builtin.Type` uses the value `p` points at.

`==` and `!=` compare two `builtin.Type` values and yield `bool`. Other operators on `builtin.Type` are reported as `type mismatch`.

```c
import "builtin";

type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var n i32 = 1;
    var t builtin.Type = typeof(n);
    var text *u8 = t.name();
    var fields = typeof(Point).fields();
    if (t == typeof(i32) && typeof('A') == typeof(u8) && text != null && fields.len == 2 &&
        fields[0].kind == typeof(i32)) {
        return 1;
    }
    return 0;
}
```

## Cost

`typeof(i32)` used as a value is the address of a type descriptor, one `leaq`. `var t builtin.Type = typeof(n)` stores a null pointer, then that address. `t == typeof(i32)` is a pointer compare: 5 instructions once both addresses are loaded, the same shape as `a == b` on integers. The whole function that stores one parameter, materializes both descriptors, and returns the compare is 10 instructions including `retq`.

`func name_of(n i32) *u8 { var t builtin.Type = typeof(n); return t.name(); }` is 5 instructions including `retq`: one `leaq` of the descriptor, the store, the reload, and the load of the spelling. `return typeof(Point).fields().len` is 3 instructions including `retq`. `typeof(Point).fields()[0].kind == typeof(i32)` is 7 instructions including `retq` on the path where the field is `i32`. Two `typeof` expressions compared from `assert` or from a file-scope `const` are folded and add no instructions. A call such as `fields()` is not a constant, so `assert` does not fold it.

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
