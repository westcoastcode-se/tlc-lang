# Expressions

[Index](README.md) · [Previous: Types](types.md) · [Next: Statements](statements.md) · [Grammar](grammar.md)

The operators are `+ - * / %`, `& | ^ ~ << >>`, `&& || !`, the comparisons, unary `+` and `-`, assignment `=`, calls, indexing, `.`, address-of `&`, and dereference `*`.

`&&` and `||` short-circuit. In the program below, `bump` is called twice and `main` returns 2.

```c
var calls int = 0;

func bump() int {
    calls = calls + 1;
    return 1;
}

func main() int {
    if (0 && bump()) {}
    if (1 || bump()) {}
    if (1 && bump()) {}
    if (0 || bump()) {}
    return calls;
}
```

A shift is evaluated in the common type.

Pointers of the same type can be compared with `==` and `!=`. A pointer can be compared with `null`, and `null == null` is allowed. The result is `int`. Pointers of different types cannot be compared. Relational operators on pointers are rejected. `p == 0` is `type mismatch`.

An integer literal `0` may be assigned to a pointer, and the pointer becomes null. `null` may be assigned to any pointer. Assigning `null` to `int` or `bool` is `type mismatch`. `true` and `false` do not convert to pointers.

A condition in `if`, `while`, `do`, and `for` is an integer after promotion. A pointer used as a condition is `type mismatch`. `switch` requires exactly `int`.

A cast is the type followed by the value in parentheses: `i32(n)`, `int(n)`, `*char(bits)`, `*void(p)`, `*Point(p)`. A name declared as a type in this file is a cast too: `Integer(n)` after `type Integer int`, and `ShapeType(shape)` to read the tag of a tlc union. A cast to a struct is `type mismatch`. From another file in the package, `ShapeType(shape)` still reads that tag. A cast between integer types is allowed. Signed `int` can be cast to and from a pointer. `null` can be cast to a pointer. `long(p)`, `uint(p)`, and `int(null)` are `type mismatch`. `int*(p)` is `type mismatch`. A cast to `void` is `not supported yet`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type.

Pointer arithmetic is `type mismatch`. A subscript index has type `int`.

`++`, `--`, compound assignment, `?:`, and `->` are `not supported yet`.

[Index](README.md) · [Previous: Types](types.md) · [Next: Statements](statements.md) · [Grammar](grammar.md)
