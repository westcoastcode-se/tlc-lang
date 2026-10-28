# Constants on a type

[Index](../README.md) · [Previous: Variables on a type](variable.md) · [Next: Traits](trait.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A compiler constant may belong to a struct, an enum, or a primitive type alias.

## Where the constant is written

It is written beside the type, in the same file: `comptime const Entity::next = 28`.

`const spare = 2` inside the braces is that same constant.

A plain `const Entity::next` is a runtime value that cannot be assigned.

The constant is not a field, so `sizeof` stays the size of the fields.

A field may use the same name.

Each specialization of a generic struct has its own constant.

`const Box[T]::extra = 8` and `const Buf[N, T]::count = N` are those constants.

The use is `Entity.next`, `Box[i32].extra`, or `Buf[4, i32].count`.

`const spare = 2` inside the braces is that constant. It is not a member.

`const Entity::next` inside a function is `const on a type is only valid at file scope`.

`comptime const Entity::next` inside a function is that same error.

`export const` and `extern const` inside the braces are `not supported yet`.

`comptime const` inside the braces is `expected an identifier`.

## Assignment and address

Assigning the constant, or taking its address, is reported as `not an lvalue`.

A field store through a struct constant is reported as `type mismatch`.

## Export and extern

`export const Entity::next` publishes it, including `export const Box[T]::limit` on a generic struct.

Another package reads `pkg.Entity.next` and `pkg.Box[i32].limit` when the type and the constant are exported.

Without `export`, the name is reported as `next is not exported`.

An unexported type is reported as `Entity is not exported`.

`extern const n = 4` and `extern const Entity::next = 28` are file scope.

`extern` and `extern(abi="tlc")` name `qualifier.n` or `qualifier.Entity.next`.

`extern(abi="c")` keeps the C name.

`extern(abi="c", name="symbol")` sets it.

A use in this file still folds.

`extern(abi="c") const Box[T]::limit` is `extern(abi="c") on a trait or a generic`.

`extern var Entity::next` is `not supported yet`.

`export var Box[T]::extra` publishes that variable.

Another package reads and assigns `pkg.Box[i32].extra`.

```c
type Entity struct {
    id i32;
};

comptime const Entity::next = 28;

type Box[T type] struct {
    value T;
};

comptime const Box[T]::extra = 8;

type Buf[N i32, T type] struct {
    value T;
};

const Buf[N, T]::count = N;

func main() i32 {
    var e Entity;
    e.id = 2;
    return Entity.next + e.id + Box[i32].extra + Buf[4, i32].count;
}
```

```asm
main:
movl $2, -8(%rsp)                # constant
movl -8(%rsp), %eax              # move
addl $28, %eax                   # add
addl $8, %eax                    # add
addl $4, %eax                    # add
retq                             # return
```

The program returns 42.

`Buf[4, i32].count` is `N`, which is 4 for that instantiation.

`Box[i32].extra` and `Box[Point].extra` are two constants.

`const spare = 2` inside the braces and `const Entity::next = 40` beside the struct are the same kind of constant.

```c
type Entity struct {
    id i32;
    const spare = 2;
};

const Entity::next = 40;

func main() i32 {
    var e Entity;
    e.id = Entity.spare;
    return Entity.next + e.id;
}
```

```asm
main:
movl (%rip), %eax                # spare
movl %eax, -8(%rsp)              # e.id
movl (%rip), %eax                # next
addl -8(%rsp), %eax              # next + e.id
retq                             # return
```

The program returns 42.

[Index](../README.md) · [Previous: Variables on a type](variable.md) · [Next: Traits](trait.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
