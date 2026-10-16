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

`type Integer i32` is a distinct type with the same representation as `i32`. A cast confirms which type a value has: `Integer(10)` and `i32(n)`. `var n Integer = 10`, `var wide i32 = n`, and `n + 1` are reported as `type mismatch`. `n + Integer(1)` has type `Integer`. A condition accepts the alias, as in `if (n)`. Two aliases of `i32`, such as `Integer` and `Count`, are different types. `Ptr` is different from `*Point`, and `Pair` is different from `[2]i32`. `pair[0]` still has the element type. A parameter, a result, and a field can use the name. `sizeof(Integer)` is `sizeof(i32)`. `typeof(Integer).name()` is `Integer`. A file-scope `const value Integer = Integer(10)` and `const value = Integer(10)` have type `Integer`. That name is not an array length. A primitive alias can have methods, static functions, and `var Integer::ten`. `Ptr`, `Pair`, and a struct alias such as `type Spot Point` cannot. The builtin name `i32` cannot either.

`type Point struct`, `type Color enum`, and `type Shape trait` define a new type. `type Point Point` is reported as `redefinition`, because the alias repeats the struct. `type Point i32` beside `type Point struct` is reported as `redefinition`. A variable and `sizeof` use the bare name, `var p Point` and `sizeof(Point)`. `struct` in that position, as in `var p struct Point` or `sizeof(struct Point)`, is reported as `expected a type`. An exported type alias is imported as a type name.

## Cost

`type Integer i32` uses the same instructions as `i32`. `Integer(n)`, `i32(n)`, `Spot(p)`, and `Ptr(p)` add no instructions. The compiler changes the type and reuses the value. A new variable stores that value once, the same as any other initializer. A cast that changes the width, such as `Byte(n)` when `n` is `i32` and `Byte` is `i8`, keeps the low bits. A method on the alias is a traced call, the same as a method on a struct of that representation.

[Index](../README.md) · [Previous: Enums](enum.md) · [Next: Keywords](../keyword.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
