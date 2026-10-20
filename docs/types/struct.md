# Structs

[Index](../README.md) · [Previous: The Type type](type.md) · [Next: Unions](union.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A struct definition is `type Name struct`. Variables, parameters, results, and fields then use the bare name. `var s struct { value i32; } = {.value = 10};` is a struct with no name. `var s = struct{.value = 10};` is the same kind of struct: `value` is `i32` because `10` is an `i32`. A field may be another `struct{...}` or an array literal. `s.value` is that field. Either value can be passed where a trait parameter asks for the same fields. `var s = {.value = 10};` is reported as `type mismatch`. `var p struct Point` is reported as `expected a type`. `{10, 20}` where a struct is expected is reported as `type mismatch`. A pointer is written `*Point` and `*i32`, in a parameter, a result, and every other type position. `?*Point` may be `null`. `*Point` may not. A field is a name, then its type, in the same order as `var`: `x i32`, `next ?*Node`, `home *Address`, `raw [8]u8`. `x i32 = 1` is the value used when an initializer does not name that field. The value is a constant. A call is reported as `global initializer is not a constant`. `raw [2]i32 = {1, 2}` is that value for a sized array. A short list leaves the elements it does not name uninitialized. `0...` stores 0 in those elements. A file-scope variable stores 0 in an element the list does not name. Naming the field replaces the list. A field of an inline union payload uses that same value, as in `n i32 = 1` inside `union(Enum)`. A union with no initializer does not choose an arm. A value on a trait field is reported as `not supported yet`. A field ends with `;`. The semicolon after `}` may be omitted.

```c
type Point struct {
    x i32;
    y i32;
};

func make(x i32, y i32) Point {
    var p Point;
    p.x = x;
    p.y = y;
    return p;
}

func main() i32 {
    var p Point;
    p = make(40, 2);
    var q *Point = &p;
    return (*q).x + q.y;
}
```

The program returns 42.

A member is a variable of the value. It is written `name type;` or `name type = expr;`, without `var`. `export name type;` publishes that member. Another package can read and assign it. A member without `export` is reported as `name is not exported` from another package. The same package sees every member. `var`, `export var`, and `const` inside the struct are reported as `not supported yet`. A variable for the type is `var Entity::next` written after the struct.

`export item Secret` is not visible outside when `Secret` is a struct that is not visible. A struct is not visible when it is not exported, or when one of its members is a struct that is not visible. An inline struct is visible outside when the member is exported, as in `export body struct { k i32; }` or `body export struct { k i32; }`. Every member inside that inline struct is exported too. Writing `export` on one of them is allowed and changes nothing.

```c
type Entity struct {
    id i32;
    export shown i32;
};

func main() i32 {
    var e Entity;
    e.id = 2;
    e.shown = 40;
    return e.shown + e.id;
}
```

The program returns 42. `id` and `shown` are both members. `shown` is the one another package can name.

A constant on the struct is written beside it, `const Entity::next = 40`. It is not a field. `sizeof` still counts the fields only. Assigning it, or taking its address, is reported as `not an lvalue`. A `const` inside the struct is reported as `not supported yet`.

```c
type Entity struct {
    id i32;
};

const Entity::next = 40;

func main() i32 {
    var e Entity;
    e.id = 2;
    return Entity.next + e.id;
}
```

The program returns 42. `Entity.next` is the constant. `e.id` is the field.

## Generics

`type Vec[T type] struct` is generic. A use writes the arguments after the name, `Vec[i32]`. Another package writes `pkg.Vec[i32]` when the struct is exported. A struct value writes that application in front of the braces: `Vec[i32]{}`, `Vec[i32]{.n = 1}`, `pkg.Vec[i32]{}`, `Buf[4, i32]{}`, and `pkg.Buf[4, i32]{}`. A field that is left out takes the value written on the field, or zero when the field has none. A value list such as `Vec[i32]{1, 2}` is reported as `type mismatch`. A struct that is not exported is reported as `name is not exported`. A method is `func Vec[T]::get(self *this, i i32) T` in the same file. The names in `Vec[T]` are the struct's parameters, in that order. `type Buf[N i32, T type] struct` uses `func Buf[N, T]::get`. Leaving the list out, or writing a different name, is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. `Vec` with no arguments is reported as `type mismatch`. `Vec[i32]` and `Vec[u8]` are different types. A variable on that struct is `var Vec[T]::count i32`, and `Vec[i32].count` is a different variable from `Vec[u8].count`. A constant is `const Vec[T]::limit`, and `Vec[i32].limit` is a different constant from `Vec[u8].limit`. A static function is `func Vec[T]::make(value T) Vec[T]`, called as `Vec[i32].make(1)` or `Buf[4, i32].width()`. Another package calls `pkg.Vec[i32].make(1)` when the function is exported. Assigning one to the other, or passing one where the parameter is the other, is reported as `type mismatch`.

```c
type Vec[T type] struct {
    n T;
};

func Vec[T]::get(self this) T {
    return self.n;
}

func main() i32 {
    var v Vec[i32];
    v.n = 42;
    return v.get();
}
```

## Layout

A struct may contain a pointer to itself.

```c
type Node struct {
    n i32;
    next *Node;
};
```

`type Wide struct(align=16)` sets the alignment. The value is a positive power of two, at most 4096. The size is rounded up to that alignment. An alignment weaker than a field requires is reported as `type mismatch`.

```c
type Wide struct(align=16) {
    n i32;
}
```

`sizeof(Wide)` is 16.

An empty struct has no fields. `sizeof` is 0. It can have methods, so a generic `type Tag[T type] struct;` and a trait parameter both accept it. `type Empty struct {}` is the same empty struct.

```c
type Empty struct;

func Empty::id(self this) i32 {
    return 7;
}

type Marker trait {
    func id() i32;
}

func take(value Marker) i32 {
    return value.id();
}

func main() i32 {
    var value Empty;
    return take(value);
}
```

`take` on an `Empty` returns 7. `extern type Empty struct;` has the same size. An empty `extern(abi="c") struct` is a one-byte opaque handle.

`type Node struct;` is that empty struct. `type Node struct` with neither `{` nor `;`, and `type Color enum` or `type Shape trait` without `{`, are reported as `expected '{'`. A struct defined in another file of the same package is already visible, including through a pointer, so a separate declaration is not required.

## Initializers

A struct value names its fields. The type comes from the place the value is used: the variable, the assignment, the argument, or the result. It also comes from a type name written in front of the braces: `Point{}`, `Point{.x = 1}`, `geom.Point{}`, `Vec[i32]{}`, `Vec[i32]{.n = 1}`, `pkg.Vec[i32]{}`, `Buf[4, i32]{}`, and `pkg.Buf[4, i32]{}`. A field that is left out takes the value written on the field, or zero when the field has none. Naming a field twice is reported as `redefinition`. An unknown field is reported as `undeclared identifier`. Used where the type is not a struct, the initializer is reported as `type mismatch`. A name that is not a struct, such as `Integer{}`, is reported as `type mismatch`. A type application that is not a generic struct, such as `Integer[i32]{}`, is reported as `type mismatch`. The wrong number of type arguments is reported as `argument count`. A generic struct that is not exported is reported as `name is not exported`. `{10, 20}`, `Point{10, 20}`, and `Vec[i32]{1, 2}` are value lists, so where a struct is expected any one is reported as `type mismatch`. `(10, 20)` is a tuple, so where a struct is expected it is reported as `type mismatch`. An empty `{}` leaves every field zero and still needs a type from the place it is used. Mixing a named field and a positional value is reported as `type mismatch`. A designated initializer that omits a `*T` field is reported as `type mismatch`. An omitted `?*T` field is `null`. `new Point{.x = 1}` reserves a `Point` and writes the same value as `Point{.x = 1}`. A `*T` field has to be named, unless the field itself has a value. See [Builtin](../builtin/allocator.md).

```c
type Inner struct {
    inner_value i32;
};

type Box struct {
    value i32;
    complex Inner;
};

func main() i32 {
    var box Box = {.value = 10, .complex = {.inner_value = 20,},};
    return box.value + box.complex.inner_value;
}
```

The program returns 30. A trailing comma is allowed. `box = {.complex = {.inner_value = 42}}` replaces the whole value, so `box.value` becomes 0 when that field has no value. A call used as the whole initializer of a file-scope `var` runs before `main`. A `const`, and any other global initializer, stays a constant. A call in a local initializer is allowed.

`Point{}` is a `Point`, so an inferred binding does not write the type again. `var p = Point{}`, `const origin = Point{}`, and `p := Point{}` are that form. `Point{.x = 40}` fills `x`. `y` stays uninitialized when `y` has no value. `y i32 = 2` stores 2 in the field that was left out. `Point{0...}` stores 0 in every field the list does not name. `new Point{.x = 40}`, `var p Point`, and `var p = Point{}` use that same rule. A named field replaces the value written on the field.

```c
type Point struct {
    x i32;
    y i32;
};

const origin = Point{};

func main() i32 {
    var p = Point{};
    q := Point{.y = 2};
    p.x = 40;
    return p.x + q.y + origin.x;
}
```

The program returns 42. `origin` is a constant, so `origin.x = 1` is reported as `type mismatch`, and `&origin` is reported as `not an lvalue`.

## `extern` structs

An `extern(abi="c") struct` is marked so it can be used with an `extern(abi="c")` function. An empty body is an opaque handle: the type has no fields and occupies one byte, and a library uses it through a pointer. `extern struct` uses the compiler's own layout.

```c
extern(abi="c") type Pair struct {
    a i32;
    b i32;
};

extern(abi="c") type SDL_Window struct {}

extern(abi="c") func make_pair(a i32, b i32) Pair;
extern(abi="c") func SDL_DestroyWindow(window *SDL_Window) void;
```

A plain struct in an `extern(abi="c")` function is reported as `struct must be extern(abi="c")`.

## Fields and pointers

Values and pointers both use `.`. A pointer whose pointee is a struct is dereferenced once. `**Point` and `*i32` are left as they are, and the access is reported as `type mismatch`. A `?*Point` is reported as `type mismatch` until the name is known to be non-null, or until that use is written `p!.field`. A field reached through a pointer is an lvalue. Assigning a field of a struct value requires the struct to be an lvalue.

## Export

An exported struct is named with the package and without the `struct` keyword.

```c
import "geom";

func main() i32 {
    var p geom.Point;
    p.x = 40;
    p.y = 2;
    return geom.sum(p);
}
```

```c
package geom;

export type Point struct {
    x i32;
    y i32;
};

export func sum(p Point) i32 {
    return p.x + p.y;
}
```

Inside the package the type is written `Point`. `import { Point } from "geom"` makes `Point` a type name in the importing file. A struct is visible to every file in the package, so one file can declare `type A struct { b B; }` when `type B struct` is in another file of that package.

## Cost

`var p Point` on a struct of two `i32` fields adds no store when the fields have no value. Reading a field is reported as `uninitialized`. `p.x = 40` followed by `return p.x` is 3 instructions: one `movl` of 40, one `movl` into `%eax`, and `retq`. When the fields are `x i32 = 1` and `y i32 = 2`, `return p.x + p.y` is 5 instructions including `retq`: two `movl` of those values, one load, one `addl`, and `retq`. `raw [2]i32 = {10, 2}` and `return h.raw[0] + h.raw[1]` is those same 5. `raw [2]i32 = {10}` and `return h.raw[0]` is 3: one `movl` of 10, one load, and `retq`. The element that was not named adds no store. `raw [3]i32 = {10, 0...}` and the sum of the three elements is 8: three `movl` of 0, one `movl` of 10, one load, two `addl`, and `retq`. An unread local adds no instruction. Each later `p.x = 40` is 1 `movl` when that store is the value that is read. Reading `p.x` is 1 `movl`. `p.x + p.y` is 1 load of `p.x` and 1 `addl` that reads `p.y` from its slot.

A field through `*Point` reloads the pointer and then loads the field. `return p.x` on a `*Point` parameter is 4 instructions including the pointer store and `retq`.

`b = a` for two `i32` fields is 2 loads and 2 stores. Eight `i32` fields are 8 loads and 8 stores. The fields are moved one at a time.

`Point{.x = 40}` stores 40 and copies both fields into the variable. `var p = Point{.x = 40}; return p.x;` is 7 instructions: one `movl` of 40, one load of that field, one load of the unset field, two `movl` into `p`, one `movl` of `p.x` into `%eax`, and `retq`. Reading `p.y` is reported as `uninitialized`. `Point{0...}` stores 0 in both fields and copies them. `return p.x + p.y` is 9 instructions: two `movl` of 0, two loads, two `movl` into `p`, one `movl` into `%eax`, one `addl`, and `retq`. When `y i32 = 2`, `Point{.x = 40}` stores 2 and 40. `return p.x + p.y` is those same 9, with the store of 2 in place of a store of 0. `Point{}` adds no store when the fields have no value. `var p = Point{}; return 0;` is 2 instructions, `xorl %eax, %eax` and `retq`. `Vec[i32]{}` and `pkg.Vec[i32]{}` follow `Point{}`. `Vec[i32]{.n = 1}` follows `Point{.x = 40}`.

A generic struct is specialized while compiling. `Vec[i32]` then costs the same as a struct whose field is `i32`. `pkg.Vec[i32]` is that same specialization. `type Wide struct(align=16)` adds no instruction beyond the field stores. An empty struct has no field stores.

A by-value method copies every field into the argument registers before the traced call. Two `i32` fields are 2 `movl`. A pointer method passes the address with 1 `leaq`. The call itself is the 36-instruction trace frame when fewer than 32 frames are stored.

A constant on the type, `Entity.next`, is an immediate when it is an integer. It adds no load. A variable on the type is an ordinary global: 1 `movl` to read it.

[Index](../README.md) · [Previous: The Type type](type.md) · [Next: Unions](union.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
