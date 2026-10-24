# Type aliases

[Index](../README.md) · [Previous: Enums](enum.md) · [Next: Keywords](../keyword.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

```c
type Point struct {
    x i32;
    y i32;
};

type Ptr *Point;
type Integer i32;
type Pair [2]i32;

func main() i32 {
    var p Point;
    p.x = 40;
    p.y = 2;
    var q Ptr = Ptr(&p);
    var n Integer = Integer(0);
    var pair Pair;
    pair[0] = p.x;
    return (*q).y + i32(n) + pair[0];
}
```

`type Integer i32` is a distinct type with the same representation as `i32`. A cast confirms which type a value has: `Integer(10)` and `i32(n)`. `var n Integer = 10`, `var wide i32 = n`, and `n + 1` are reported as `type mismatch`. `n + Integer(1)` has type `Integer`. `if (n)` is reported as `type mismatch: expected bool, found Integer`. The comparison is `if (n != Integer(0))`. Two aliases of `i32`, such as `Integer` and `Count`, are different types. `Ptr` is different from `*Point`, and `Pair` is different from `[2]i32`. `pair[0]` still has the element type. A parameter, a result, and a field can use the name. `sizeof(Integer)` is `sizeof(i32)`. `typeof(Integer).name()` is `Integer`. A file-scope `const value Integer = Integer(10)` and `const value = Integer(10)` have type `Integer`. That name is not an array length. A primitive alias can have methods, static functions, and `var Integer::ten`. A slice alias such as `type string_view []const u8` can have value methods. `Ptr`, `Pair`, and a struct alias such as `type Spot Point` can have a static function: `Ptr.from(&p)`, `Pair.from(1, 2)`, and `Spot.origin(40, 2)`. A method on those aliases, and `var Ptr::name`, are reported as `type mismatch`. The builtin name `i32` cannot have a method or a static function either. `self *this` on a slice alias is reported as `not supported yet`.

`type Duo (i32, i32)` is an alias of that tuple. `Duo` is a different type from `(i32, i32)`. `Duo((40, 2))` names it. `var value Duo = (40, 2)` is `type mismatch`. `value[0]` and `value[0] = 1` use a constant index. `value[i]` when `i` is a `var` is `not supported yet`. `(i32)` is the type `i32`. `()` is `type mismatch`. A parameter may be written `(i32, f32)`. An element may be a struct, a slice of one, or an array: `type Pair (Point, i32)`, `type Row ([3]i32, i32)`. `value[0][1]` reads one element of that array. `type Node (i32, *Node)` names that tuple. A by-value element of the alias itself, such as `type Pair (Pair, i32)`, is `not supported yet`. `var p *Duo` is a pointer to that alias. `var value (i32, i32)`, a field written `(i32, i32)`, `sizeof(Duo)`, and `[2]Duo` are `not supported yet`. A field uses the alias name: `value Duo`. `func f() (i32, i32)` is still two results. `func f() ((i32, i32))` is `not supported yet`. `type Something[K,V] (K,V)` is `expected ']'` on the comma. A bracket list on `type` is type parameters only when `struct`, `enum`, `trait`, or `union` follows. `func take[P1 type, P2 type](other (P1, P2))` is `not supported yet`.

`type Point struct`, `type Color enum`, and `type Shape trait` define a new type. `type Point Point` is reported as `redefinition`, because the alias repeats the struct. `type Point i32` beside `type Point struct` is reported as `redefinition`. A variable and `sizeof` use the bare name, `var p Point` and `sizeof(Point)`. `struct`, `union`, and `trait` in that position, as in `var p struct Point`, `var s union Shape`, `var t trait Area`, or `sizeof(struct Point)`, are reported as `expected a type`. An exported type alias is imported as a type name.

## Cost

`type Integer i32` uses the same instructions as `i32`. `Integer(n)`, `i32(n)`, `Spot(p)`, `Ptr(p)`, and `Duo((40, 2))` add no instructions. The compiler changes the type and reuses the value. `var value Duo = Duo((40, 2)); return value[0] + value[1];` is 6 instructions: two `movl` stores, two loads, `addl`, and `retq`. A parameter `value Duo`, or `value (i32, i32)`, whose body is `return value[0] + value[1]` is those same 6 instructions. The call is a traced call. A cast from a slice to a slice alias of the same element, such as `string_view(text)` when `text` is already `[]const u8`, also adds none. A new variable stores that value once, the same as any other initializer. A cast that changes the width, such as `Byte(n)` when `n` is `i32` and `Byte` is `i8`, keeps the low bits. A method on the alias is a traced call, the same as a method on a struct of that representation. A slice-alias method is measured on [string](../std/string.md).

[Index](../README.md) · [Previous: Enums](enum.md) · [Next: Keywords](../keyword.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
