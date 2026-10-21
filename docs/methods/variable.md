# Variables on a type

[Index](../README.md) · [Previous: Methods on primitive aliases](alias.md) · [Next: Constants on a type](constant.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A variable may belong to a struct, an enum, or a primitive type alias. It is written beside the type, in the same file, and it is not a field. `sizeof` and the layout stay the same. The use names the type: `Entity.next`. A field of the same spelling is still the field, so `e.next` reads that field. A missing field is reported as `undeclared identifier`.

```c
type Entity struct {
    id i32;
};

var Entity::next i32 = 40;
var Entity::bias = 0;
var Entity::spare i32;

func Entity::make() Entity {
    var e Entity;
    e.id = Entity.next;
    Entity.next = Entity.next + 1;
    return e;
}

type Point struct {
    x i32;
};

type Box[T type] struct {
    value T;
};

var Box[T]::extra i32 = 1;

type Buf[N i32, T type] struct {
    value T;
};

var Buf[N, T]::count i32 = N;

type Color enum { Red };

var Color::label i32;

func main() i32 {
    var e = Entity.make();
    var p = &Entity.next;
    Box[Point].extra = 9;
    return e.id + Box[i32].extra + *p + Buf[4, Point].count + Entity.bias + Entity.spare + Color.label - 44;
}
```

The program returns 42. `Entity.next` is one variable for `Entity`. `Box[i32].extra` and `Box[Point].extra` are two variables. `Buf[4, Point].count` is `N`, which is 4 for that instantiation. Omitting `=` zeroes the variable, the same as any other file-scope `var`. `var Entity::bias = 0` infers the type. `var Entity::row = [_]i32{1, 2}` and `var Entity::typed [2]i32 = {3, 1}` are that variable. `var Entity::scale = 1.5f` is an `f32` variable for the type. A second `var Entity::next`, or a static function of that name, is reported as `redefinition`. A variable in another file is reported as `method in another file`. An unknown type is reported as `type mismatch`.

`var` inside the struct is reported as `not supported yet`. The variable is written beside the type. A member may use the same name: `e.next` is the member and `Entity.next` is the variable. `sizeof` stays the size of the members.

`export var Entity::next i32 = 40` publishes that variable. Another package reads and assigns `pkg.Entity.next` when `Entity` is exported. Without `export`, the name is reported as `next is not exported`. `extern` on the variable, and `export var Box[T]::extra`, are reported as `not supported yet`. `extern(abi="c") var Box[T]::extra` and `extern(abi="c")` on a trait are `extern(abi="c") on a trait or a generic`. A member another package can name is `export next i32` inside the struct.

```c
package math;

export type Entity struct {
    id i32;
};

export var Entity::next i32 = 40;
```

```c
import "math";

func main() i32 {
    math.Entity.next = math.Entity.next + 2;
    return math.Entity.next;
}
```

The program returns 42.

## Cost

`Entity.next` is a global. Reading it is 1 `movl`. Assigning it is 1 load, the arithmetic, and 1 store. It is not inside the struct, so a value of the struct does not copy it. Each specialization of a generic struct has its own global. A use of one specialization does not load the other. A zero initializer occupies the binary and adds no instruction at startup. `extern(abi="c")` on a generic or a trait variable is rejected and adds no instructions.

[Index](../README.md) · [Previous: Methods on primitive aliases](alias.md) · [Next: Constants on a type](constant.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
