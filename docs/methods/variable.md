# Variables on a type

[Index](../README.md) · [Previous: Methods on primitive aliases](alias.md) · [Next: Constants on a type](constant.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A variable may belong to a struct, an enum, or a primitive type alias.

## Written beside the type

The variable is written beside the type, in the same file.

The variable is not a field.

`sizeof` and the layout stay the same.

The use names the type: `Entity.next`.

A field of the same spelling is still the field, so `e.next` reads that field.

A missing field is reported as `undeclared identifier`.

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

```asm
main:
# trace frame for the call, then the matching pop
callq make                       # call make
retq                             # return

make:
movl (%rip), %eax                # move
movl %eax, -8(%rsp)              # move
movl (%rip), %eax                # move
incl %eax
movl %eax, (%rip)                # move
movl -8(%rsp), %eax              # move
retq                             # return
```

The program returns 42.

`Entity.next` is one variable for `Entity`.

`Box[i32].extra` and `Box[Point].extra` are two variables.

`Buf[4, Point].count` is `N`, which is 4 for that instantiation.

Omitting `=` zeroes the variable, the same as any other file-scope `var`.

`var Entity::bias = 0` infers the type.

`var Entity::row = [_]i32{1, 2}` and `var Entity::typed [2]i32 = {3, 1}` are that variable.

`var Entity::scale = 1.5f` is an `f32` variable for the type.

A second `var Entity::next`, or a static function of that name, is reported as `redefinition`.

A variable in another file is reported as `method in another file`.

An unknown type is reported as `type mismatch`.

## Not a member

A member is already a variable, so it is written `next i32` and not `var next i32`.

`var` inside the braces is reported as `not supported yet`.

The variable on the type is written beside it.

`var Entity::next` inside a function is not a declaration.

It is reported as `not supported yet`.

`Entity::next := 1` is not a declaration either.

It is reported as `expected a declaration`.

A member may use the same name: `e.next` is the member and `Entity.next` is the variable.

`sizeof` stays the size of the members.

## Export and extern

`export var Entity::next i32 = 40` publishes that variable.

Another package reads and assigns `pkg.Entity.next` when `Entity` is exported.

`export var Box[T]::extra` publishes that variable on a generic struct.

Another package reads and assigns `pkg.Box[i32].extra` when `Box` and the variable are exported.

`Box[i32].extra` and `Box[Point].extra` stay two variables.

Without `export`, the name is reported as `next is not exported`.

An unexported type is reported as `Box is not exported`.

`extern` on the variable is reported as `not supported yet`.

`extern(abi="c") var Box[T]::extra` and `extern(abi="c")` on a trait are `extern(abi="c") on a trait or a generic`.

A member another package can name is `export next i32` inside the struct.

```c
package math;

export type Entity struct {
    id i32;
};

export var Entity::next i32 = 40;
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

```c
import "math";

func main() i32 {
    math.Entity.next = math.Entity.next + 2;
    return math.Entity.next;
}
```

```asm
main:
movq (%rip), %rax                # address of Entity.next
movl (%rax), %ecx                # load it
addl $2, %ecx                    # add 2
movq (%rip), %rax
movl %ecx, (%rax)                # store it
movq (%rip), %rax
movl (%rax), %eax                # return it
retq
```

The program returns 42.

```c
package math;

export type Box[T type] struct {
    value T;
};

export var Box[T]::extra i32 = 40;
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

```c
import "math";

func main() i32 {
    math.Box[i32].extra = math.Box[i32].extra + 2;
    return math.Box[i32].extra;
}
```

```asm
main:
movq (%rip), %rax                # address of Box[i32].extra
movl (%rax), %ecx                # load it
addl $2, %ecx                    # add 2
movq (%rip), %rax
movl %ecx, (%rax)                # store it
movq (%rip), %rax
movl (%rax), %eax                # return it
retq
```

The program returns 42.

`math.Box[i32].extra` and `math.Box[Point].extra` are two globals.

[Index](../README.md) · [Previous: Methods on primitive aliases](alias.md) · [Next: Constants on a type](constant.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
