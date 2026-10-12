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

Pointers that point at the same type can be compared with `==` and `!=`, including `*T` with `?*T`. A pointer can be compared with `null`, and `null == null` is allowed. The result is `int`. Pointers of different types cannot be compared. Relational operators on pointers are rejected. `p == 0` is `type mismatch`.

`typeof(int)` and `typeof n` have type `Type`. `==` and `!=` compare two `Type` values and yield `int`. `t.name()` returns the diagnostic spelling as `*char`. Other operators on `Type` are `type mismatch`.

`*T` has a value. `?*T` may be `null`. `null` may be assigned or returned only to `?*T`. `&x` has type `*T` and may be stored in `*T` or `?*T`. `var p = null` has no pointer type to take and is `type mismatch`. Write `var p ?*Point = null`. A `*T` with no initializer is `type mismatch`, and so is a struct or array that contains one, because that memory would be zero. An omitted `?*T` field may be `null`. An omitted `*T` field is `type mismatch`. `extern` storage is not checked. Assigning an integer, including `0`, to a pointer is `type mismatch`. An address that comes from an integer is a cast, `*int(n)`. Assigning `null` to `int` or `bool` is `type mismatch`. `true` and `false` do not convert to pointers.

A name compared with `null` is `*T` where that comparison shows it has a value. After `if (p == null) { return; }`, `p` is `*T` for the rest of the function. `if (p != null) { use(p); }` narrows `p` inside the block. The `else` of `p == null`, and the `else` of `p != null` when that `else` always leaves, do the same. `while (p != null)` and `for (; p != null;)` narrow the body. The body of `do` runs before the check and stays `?*T`. A field comparison such as `person.home == null` does not narrow. `*p`, `p.field`, and `p[i]` on a `?*T` are `type mismatch` until the name is narrowed. Passing `?*T` where `*T` is required is `type mismatch` until then.

Postfix `!` asserts a `?*T` at that use. `pointer!` has type `*T`. `myfunc(pointer!)` passes it where `*T` is required, and `pointer!.value` reads a field. The name stays `?*T` for the next use. `!` on a value that is already `*T`, or on a non-pointer, is `type mismatch`. The compiler does not insert a null check. Prefix `!` is still logical not on an integer.

```c
func settle(person *Person, home *Address) void {
    person.home = home;
}

func bind(person ?*Person, home ?*Address) void {
    if (person == null) {
        return;
    }
    if (home == null) {
        return;
    }
    settle(person, home);
}
```

A condition in `if`, `while`, `do`, and `for` is an integer after promotion. A pointer used as a condition is `compare with null`. `switch` requires exactly `int`.

A cast is the type followed by the value in parentheses: `i32(n)`, `int(n)`, `*char(bits)`, `*void(p)`, `*Point(p)`. A name declared as a type in this file is a cast too: `Integer(n)` after `type Integer int`, and `ShapeType(shape)` to read the tag of a tlc union. A cast to a struct is `type mismatch`. From another file in the package, `ShapeType(shape)` still reads that tag. A cast between integer types is allowed. Signed `int` can be cast to and from a pointer. The cast keeps the type that was written: `*int(p)` is `*int`, and `?*int(p)` is `?*int`. `?*int(null)` is allowed. `*int(null)`, `long(p)`, `uint(p)`, and `int(null)` are `type mismatch`. `int*(p)` is `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type.

Pointer arithmetic is `type mismatch`. A subscript index has type `int`.

`(40, 2)` is a tuple. The element types are the types of the values before promotion, so `(40, 'a')` is `int` and `char`. `(40)` is the value `40`. `var value = (40, 2)` and `value := (40, 2)` each bind that tuple. `value[0]` reads one element. The index is a constant. `{10, 20, 12}` fills an array. Where a struct is expected, that brace list is `type mismatch`. An empty `{}` names no fields.

[Index](README.md) · [Previous: Types](types.md) · [Next: Statements](statements.md) · [Grammar](grammar.md)
