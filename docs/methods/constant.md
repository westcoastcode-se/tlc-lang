# Constants on a type

[Index](../README.md) · [Previous: Variables on a type](variable.md) · [Next: Traits](trait.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A constant may belong to a struct, an enum, or a primitive type alias. It is written beside the type, in the same file: `const Entity::next = 28`. It is not a field, so `sizeof` stays the size of the fields. A field may use the same name. Each specialization of a generic struct has its own constant. `const Box[T]::extra = 8` and `const Buf[N, T]::count = N` are those constants. The use is `Entity.next`, `Box[i32].extra`, or `Buf[4, i32].count`. A `const` inside the braces is not a member. It is reported as `not supported yet`.

Assigning the constant, or taking its address, is reported as `not an lvalue`. A field store through a struct constant is reported as `type mismatch`. `export const Entity::next` publishes it, including `export const Box[T]::limit` on a generic struct. Another package reads `pkg.Entity.next` and `pkg.Box[i32].limit` when the type and the constant are exported. Without `export`, the name is reported as `next is not exported`. An unexported type is reported as `Entity is not exported`. `const Entity::next` inside a function is not a declaration. The constant is file scope, and that form is reported as `not supported yet`. `extern const n = 4` and `extern const Entity::next = 28` are file scope. `extern` and `extern(abi="tlc")` name `qualifier.n` or `qualifier.Entity.next`. `extern(abi="c")` keeps the C name, and `extern(abi="c", name="symbol")` sets it. A use in this file still folds. `extern(abi="c") const Box[T]::limit` is `extern(abi="c") on a trait or a generic`. `extern var Entity::next` is `not supported yet`. `export var Box[T]::extra` publishes that variable. Another package reads and assigns `pkg.Box[i32].extra`.

```c
type Entity struct {
    id i32;
};

const Entity::next = 28;

type Box[T type] struct {
    value T;
};

const Box[T]::extra = 8;

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

The program returns 42. `Buf[4, i32].count` is `N`, which is 4 for that instantiation. `Box[i32].extra` and `Box[Point].extra` are two constants.

[Index](../README.md) · [Previous: Variables on a type](variable.md) · [Next: Traits](trait.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
