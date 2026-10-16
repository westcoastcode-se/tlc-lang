# allocator

[Index](../README.md) · [Previous: trace](trace.md) · [Next: va](va.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`Allocator` is a built-in trait. A struct satisfies it by defining these methods, each with a pointer receiver:

```c
func malloc(self *this, size i32, align i32) ?*u8;
func free(self *this, ptr *u8) void;
func realloc(self *this, ptr *u8, size i32, align i32) ?*u8;
```

`import "builtin"` also provides `allocator`, a `Malloc` value. `new T` is `new(builtin.allocator) T`, and `delete p` is `delete(builtin.allocator) p`. `Malloc` calls the C functions `malloc`, `free`, and `realloc`. `malloc` returns `null` when it cannot reserve the bytes. `new` then aborts. That choice is not final.

`new T` reserves `sizeof(T)` bytes and yields `*T`. The bytes are not zeroed. `new T{.field = expr}` and `new(allocator) T{.field = expr}` write those fields after the block is reserved. A field that is left out stays uninitialized. `new Point{1, 2}` is a value list and is reported as `type mismatch`. An initializer on a type that is not a struct, such as `new i32{1}`, is reported as `type mismatch`. `new Node` is reported as `type mismatch` when `Node` contains a `*T`, because that field has to be initialized. `delete` passes that pointer to `free`. A file-scope `new` is reported as `global initializer is not a constant`. The standard allocator is unsafe: it can reserve memory without initializing the other fields. `unsafe` is not a form yet.

`new [n]T` and `new(allocator) [n]T` reserve `n` elements of `T` and yield `*T`, a pointer to the first element. `n` has type `i32`. `buf[i]` reads and writes element `i`. There is no bounds check. The elements are uninitialized. `delete buf` frees the block. A constant `n` that is not positive is reported as `type mismatch`. Any other `n` that is not positive aborts. `new []T` is reported as `expected an expression`.

`new [n]Point{{.x = 1}...}` stores that value in every element. The value is evaluated once. The value is a struct value, so a field it leaves out is zero. A field left out of `new Point{.x = 1}` is uninitialized. `new [2]Point{{.x = 1}, {.y = 2}}` stores one value per element when the constant count matches the list. A runtime count with a list and no `...` is reported as `type mismatch`. `new [n]Point{.x = 1}` is that same mismatch: the element list is another pair of braces. `new [_]Point{{.x = 1, .y = 2}, {.y = 10}}` takes the length from the list, so the reserved object is `[2]Point` and the result is `*[2]Point`. `new [_]Point{{.x = 1}...}` has no length to repeat and is reported as `type mismatch`. `new [n]Node` is reported as `type mismatch` when an element contains a `*T` and no value sets that field. An element value that sets that field is accepted.

`new Box[i32]` and `new Box[i32]{.value = 1}` allocate one specialization. The fields are the fields of that specialization. `new i32[4]` is reported as `type mismatch`. The buffer is `new [4]i32`.

```c
import "builtin";

type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var p *Point = new Point{.x = 40, .y = 2};
    var sum i32 = p.x + p.y;
    delete(builtin.allocator) p;
    return sum;
}
```

## Cost

`new i32` and `delete p` together execute 13 instructions in `main` when `malloc` returns a block. That count is on [new and delete](../statements/new.md). `malloc` and `free` do not push a trace frame.

A custom allocator is a pointer receiver plus the size and the alignment. `new(allocator) T` loads that receiver and calls `malloc` the same way, without a trace frame. It does not zero the block. `realloc` has no expression that calls it, so it adds no instructions unless a method calls it directly.

[Index](../README.md) · [Previous: trace](trace.md) · [Next: va](va.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
