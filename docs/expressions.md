# Expressions

[Index](README.md) · [Previous: Types](types.md) · [Next: Statements](statements.md) · [Grammar](grammar.md)

The operators are `+ - * / %`, `& | ^ ~ << >>`, `&& || !`, the comparisons, unary `+` and `-`, assignment `=`, calls, indexing, `.`, address-of `&`, and dereference `*`. Binding and associativity are given in the [grammar](grammar.md#expressions).

```c
func main() i32 {
    var n i32 = 6;
    var p *i32 = &n;
    n = -n + 8;
    n = n * 3 / 2 % 5;
    n = n << 1 >> 1;
    n = (n & 7) | 1;
    n = n ^ 1;
    n = ~n;
    if (n < 0 && *p != 0) {
        return 1;
    }
    return 0;
}
```

## Logical operators

`&&` and `||` short-circuit. In the program below, `bump` is called twice and `main` returns 2.

```c
var calls i32 = 0;

func bump() i32 {
    calls = calls + 1;
    return 1;
}

func main() i32 {
    if (0 && bump()) {}
    if (1 || bump()) {}
    if (1 && bump()) {}
    if (0 || bump()) {}
    return calls;
}
```

A shift is evaluated in the common type.

## Pointers and `null`

Pointers that point at the same type can be compared with `==` and `!=`, including `*T` with `?*T`. A pointer can be compared with `null`, and `null == null` is allowed. The result is `i32`. Pointers of different types cannot be compared. Relational operators on pointers are rejected. `p == 0` is reported as `type mismatch`.

`*T` has a value. `?*T` may be `null`. `null` may be assigned or returned only to `?*T`. `&x` has type `*T` and may be stored in `*T` or `?*T`. `var p = null` has no pointer type to take and is reported as `type mismatch`. Write `var p ?*Point = null`. A `*T` with no initializer is reported as `type mismatch`, and so is a struct or array that contains one, because that memory would be zero. An omitted `?*T` field may be `null`. An omitted `*T` field is reported as `type mismatch`. `extern` storage is not checked. Assigning an integer, including `0`, to a pointer is reported as `type mismatch`. An address that comes from an integer is a cast, `*i32(n)`. Assigning `null` to `i32` or `bool` is reported as `type mismatch`. `true` and `false` do not convert to pointers.

Pointer arithmetic is reported as `type mismatch`. A subscript index has type `i32`.

```c
func main() i32 {
    var n i32 = 1;
    var p *i32 = &n;
    var q ?*i32 = null;
    if (p != q && q == null) {
        return *p;
    }
    return 0;
}
```

## Narrowing

A name compared with `null` is `*T` where that comparison shows it has a value. After `if (p == null) { return; }`, `p` is `*T` for the rest of the function. `if (p != null) { use(p); }` narrows `p` inside the block. The `else` of `p == null`, and the `else` of `p != null` when that `else` always leaves, do the same. `while (p != null)` and `for (; p != null;)` narrow the body. The body of `do` runs before the check and stays `?*T`. A field comparison such as `person.home == null` does not narrow. `*p`, `p.field`, and `p[i]` on a `?*T` are reported as `type mismatch` until the name is narrowed. Passing `?*T` where `*T` is required is reported as `type mismatch` until then.

Postfix `!` asserts a `?*T` at that use. `pointer!` has type `*T`. `myfunc(pointer!)` passes it where `*T` is required, and `pointer!.value` reads a field. The name stays `?*T` for the next use. `!` on a value that is already `*T`, or on a non-pointer, is reported as `type mismatch`. The compiler does not insert a null check. Prefix `!` is logical not on an integer.

```c
func read(p *i32) i32 {
    return *p;
}

func main() i32 {
    var n i32 = 40;
    var p ?*i32 = &n;
    return read(p!);
}
```

```c
type Address struct {
    street i32;
};

type Person struct {
    home *Address;
};

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

## Conditions

A condition in `if`, `while`, `do`, and `for` is an integer after promotion. A pointer used as a condition is reported as `compare with null`. `switch` requires exactly `i32`.

```c
func main() i32 {
    var n i32 = 1;
    if (n) {
        return n;
    }
    return 0;
}
```

## `Type`

`typeof(i32)` and `typeof n` have type `Type`. `==` and `!=` compare two `Type` values and yield `i32`. `t.name()` returns the diagnostic spelling as `*u8`. Other operators on `Type` are reported as `type mismatch`.

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

## Casts

A cast is the type followed by the value in parentheses: `i32(n)`, `i64(n)`, `*u8(bits)`, `*void(p)`, `*Point(p)`. A name declared as a type in this file is a cast too: `Integer(n)` after `type Integer i32`, and `ShapeType(shape)` to read the tag of a tlc union. That cast is how a value enters or leaves the alias. `Integer(n)` has type `Integer`, and `i32(value)` has type `i32`. A cast between an alias and a value with the same representation is allowed, including `Spot(p)` after `type Spot Point` and `Ptr(&p)` after `type Ptr *Point`. A cast to a different struct is reported as `type mismatch`. From another file in the package, `ShapeType(shape)` still reads that tag.

A cast between integer types is allowed. Signed `i32` can be cast to and from a pointer. The cast keeps the type that was written: `*i32(p)` is `*i32`, and `?*i32(p)` is `?*i32`. `?*i32(null)` is allowed. `*i32(null)`, `i64(p)`, `u32(p)`, and `i32(null)` are reported as `type mismatch`. `i32*(p)` is reported as `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type.

```c
type Integer i32;

func main() i32 {
    var n i32 = 42;
    var p *i32 = &n;
    var bytes *u8 = *u8(p);
    var back *i32 = *i32(bytes);
    var q ?*i32 = ?*i32(null);
    var wide = i64(n);
    var alias = Integer(n);
    if (q == null && *back == i32(alias) && wide == 42) {
        return *back;
    }
    return 0;
}
```

## Tuples and lists

`(40, 2)` is a tuple. The element types are the types of the values before promotion, so `(40, 'a')` is `i32` and `u8`. `(40)` is the value `40`. `var value = (40, 2)` and `value := (40, 2)` each bind that tuple. `var value1, value2 = value` and `a, b := value` unpack it, one name per element. `var value1, value2 = (40, 2)` unpacks the literal the same way. The number of names matches the tuple. A different count is reported as `type mismatch`. `value[0]` reads one element. The index is a constant.

`{10, 20, 12}` fills an array. Where a struct is expected, that brace list is reported as `type mismatch`. An empty `{}` names no fields.

```c
func main() i32 {
    var value = (40, 2);
    var value1, value2 = value;
    a, b := (7, 'a');
    value1, value2 = (1, 2);
    var row [3]i32 = {10, 20, 12};
    return value[0] + value1 + a + row[2] + b;
}
```

[Index](README.md) · [Previous: Types](types.md) · [Next: Statements](statements.md) · [Grammar](grammar.md)
