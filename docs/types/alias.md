# Type aliases

[Index](../README.md) · [Previous: Enums](enum.md) · [Next: Keywords](../keyword.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

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

```asm
main:
movl $40, -32(%rsp)              # constant
movl $2, -28(%rsp)               # constant
leaq -32(%rsp), %rax             # address
movq %rax, -24(%rsp)             # move
movl $0, -12(%rsp)               # constant
movl -32(%rsp), %eax             # move
movl %eax, -8(%rsp)              # move
movq -24(%rsp), %rax             # move
movl 4(%rax), %eax               # move
addl -12(%rsp), %eax             # add
addl -8(%rsp), %eax              # add
retq                             # return
```

`type Integer i32` is a distinct type with the same representation as `i32`. A cast confirms which type a value has: `Integer(10)` and `i32(n)`. `var n Integer = 10`, `var wide i32 = n`, and `n + 1` are reported as `type mismatch`. `n + Integer(1)` has type `Integer`. `if (n)` is reported as `type mismatch: expected bool, found Integer`. The comparison is `if (n != Integer(0))`. Two aliases of `i32`, such as `Integer` and `Count`, are different types. `Ptr` is different from `*Point`, and `Pair` is different from `[2]i32`. `pair[0]` still has the element type. A parameter, a result, and a field can use the name. `sizeof(Integer)` is `sizeof(i32)`. `typeof(Integer).name()` is `Integer`. A file-scope `const value Integer = Integer(10)` and `const value = Integer(10)` have type `Integer`. That name is not an array length. A primitive alias can have methods, static functions, and `var Integer::ten`. A slice alias such as `type string_view []const u8` can have value methods. `Ptr`, `Pair`, and a struct alias such as `type Spot Point` can have a static function: `Ptr.from(&p)`, `Pair.from(1, 2)`, and `Spot.origin(40, 2)`. A method on those aliases, and `var Ptr::name`, are reported as `type mismatch`. The builtin name `i32` cannot have a method or a static function either. `self *this` on a slice alias is reported as `not supported yet`.

`type Duo (i32, i32)` is an alias of that tuple. `Duo` is a different type from `(i32, i32)`. `Duo((40, 2))` names it. `var value Duo = (40, 2)` is `type mismatch`. `value[0]` and `value[0] = 1` use a constant index. `value[i]` when `i` is a `var` is `not supported yet`. `(i32)` is the type `i32`. `()` is `type mismatch`. A parameter may be written `(i32, f32)`. An element may be a struct, a slice of one, or an array: `type Pair (Point, i32)`, `type Row ([3]i32, i32)`. `value[0][1]` reads one element of that array. `type Node (i32, *Node)` names that tuple. A by-value element of the alias itself, such as `type Pair (Pair, i32)`, is `not supported yet`. `var p *Duo` is a pointer to that alias. `var value (i32, i32) = (40, 2)` is that tuple, and `value[0]` reads an element. A field may be written `value (i32, i32)`, and `box.value = (40, 2)` stores it. `type Box[T type] struct { pair (T, i32); }` stores that tuple. `box.pair = (40, 2)` stores it, and `box.pair[0]` reads an element. `sizeof(Duo)` is the size of the alias. `[2]Duo` is an array of it: `row[0] = Duo((40, 2))` stores one element, and `row[0][0]` reads one field. `[2][2]Duo` indexes the same way, and `grid[1][1][0]` reads one field. `p[0][0]` on `var p *[2]Duo = &row` reads one field. A tuple does not convert to a slice. `row[0:2]`, `row[:]`, the same range of a parameter `[N]Duo` or `[N][M]Duo`, and `p[0:2]` on `*[N]Duo` are `type mismatch`. `[]Duo` and `[][N]Duo` are `not supported yet`. A field may also use the alias name: `value Duo`. `func f() (i32, i32)` is still two results. `func f() ((i32, i32))` is one tuple result. `return (40, 2)` matches it. `return 40, 2` does not. `type Something[K type, V type] (K, V)` is an alias of that tuple. `Something[i32, i32]` is a different type from `(i32, i32)` and from `Something[i32, f32]`. `Something[i32, i32]((40, 2))` names it. `var value Something[i32, i32] = (40, 2)` is `type mismatch`. `type Something[K, V] (K, V)` is `expected a type` on the comma. A bracket list on `type` is type parameters when `struct`, `enum`, `trait`, `union`, or a tuple follows. `type Name[T type] enum` and `type Name[T type] union` are `not supported yet`. `type Name[T type] i32` is `expected ']'`. `func take[P1 type, P2 type](other (P1, P2))` is that tuple.

`type Point struct`, `type Color enum`, and `type Shape trait` define a new type. `type Point Point` is reported as `redefinition`, because the alias repeats the struct. `type Point i32` beside `type Point struct` is reported as `redefinition`. A variable and `sizeof` use the bare name, `var p Point` and `sizeof(Point)`. `struct`, `union`, and `trait` in that position, as in `var p struct Point`, `var s union Shape`, `var t trait Area`, or `sizeof(struct Point)`, are reported as `expected a type`. An exported type alias is imported as a type name.

[Index](../README.md) · [Previous: Enums](enum.md) · [Next: Keywords](../keyword.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
