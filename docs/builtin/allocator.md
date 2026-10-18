# allocator

[Index](../README.md) · [Previous: trace](trace.md) · [Next: va](va.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`Allocator` is a built-in trait. A struct satisfies it by defining these methods, each with a pointer receiver:

```c
unsafe func malloc(self *this, size i32, align i32) ?*u8;
func free(self *this, ptr *u8) void;
unsafe func realloc(self *this, ptr *u8, size i32, align i32) ?*u8;
```

`malloc` and `realloc` are unsafe. The struct methods repeat `unsafe`. `free` does not. A struct that defines those three also gains the default `Allocator.malloc[T]`:

```c
unsafe func Allocator::malloc[T type](self *this) ?*T {
    var value = self.malloc(sizeof(T), alignof(T));
    return ?*T(value);
}
```

The call is `unsafe arena.malloc[i32]()`. The result is `?*T`, and the caller handles `null`. A struct method `unsafe func Arena::malloc[T type](self *this) ?*T` wins over that default.

`import "builtin"` also provides `allocator`, a `Malloc` value. `new T` uses that allocator, and so does `delete p`. `new(&builtin.allocator) T` and `delete(&builtin.allocator) p` name it. The argument is a pointer, and the pointee type is taken from that pointer. `Malloc` calls the C functions `malloc`, `free`, and `realloc`. `malloc` returns `null` when it cannot reserve the bytes. `new` then calls `builtin.panic("out of memory")`. The result of `new` is `*T`.

`new T` reserves `sizeof(T)` bytes and yields `*T`. `new i32` does not store the reserved integer. `new Point` stores each field: the value written on the field, or zero when the field has none. `new T{.field = expr}` and `new(&arena) T{.field = expr}` write those fields after the block is reserved. A field that is left out takes that same value. `new Point{1, 2}` is a value list and is reported as `type mismatch`. An initializer on a type that is not a struct, such as `new i32{1}`, is reported as `type mismatch`. `new Node` is reported as `type mismatch` when `Node` contains a `*T` that has no value, because that field has to be named. `delete` passes that pointer to `free` and has no result. A file-scope `new` is reported as `global initializer is not a constant`. `malloc` and `realloc` are unsafe calls. `free` is an ordinary call. `new` and `delete` are not unsafe calls. `malloc` and `realloc` return `?*u8`, and the caller handles `null`. `new(&arena) T` does not. A `null` result from that `malloc` calls `builtin.panic("out of memory")`. A `?*T` field that is left out is null when the field has no value. A cast of a pointer to a struct or union, or to a different primitive, is [`unsafe`](../expressions/unsafe.md).

`new [n]T` and `new(&arena) [n]T` reserve `n` elements of `T` and yield `*T`, a pointer to the first element. `n` has type `i32`. `buf[i]` reads and writes element `i`. There is no bounds check. The elements of `new [n]i32` are not stored. `new [n]Point` stores each field of each element. `delete buf` frees the block. A constant `n` that is not positive is reported as `type mismatch`. Any other `n` that is not positive aborts. `new []T` is reported as `expected an expression`.

`new [n]Point{{.x = 1}...}` stores that value in every element. The value is evaluated once. The value is a struct value, so a field it leaves out takes the value written on the field, or zero when the field has none. A field left out of `new Point{.x = 1}` takes that same value. `new [2]Point{{.x = 1}, {.y = 2}}` stores one value per element when the constant count matches the list. A runtime count with a list and no `...` is reported as `type mismatch`. `new [n]Point{.x = 1}` is that same mismatch: the element list is another pair of braces. `new [_]Point{{.x = 1, .y = 2}, {.y = 10}}` takes the length from the list, so the reserved object is `[2]Point` and the result is `*[2]Point`. `new [_]Point{{.x = 1}...}` has no length to repeat and is reported as `type mismatch`. `new [n]Node` is reported as `type mismatch` when an element contains a `*T` and no value sets that field. An element value that sets that field is accepted.

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
    delete(&builtin.allocator) p;
    return sum;
}
```

## Cost

`new i32` and `delete p` together execute 13 instructions in `main` when `malloc` returns a block. That count is on [new and delete](../statements/new.md). `malloc` and `free` do not push a trace frame.

A custom allocator is a pointer receiver plus the size and the alignment. `new(&arena) T` evaluates that pointer and calls `Arena.malloc` the same way, without a trace frame. `new(&arena) i32` does not store the reserved integer. `new(&arena) Point` stores each field after the call returns, the same as `new Point`. A `null` result calls `builtin.panic("out of memory")`, the same as a `null` from `malloc`. `new(&builtin.allocator) T` does not evaluate the pointer. It calls `malloc` directly. `Allocator.realloc` returns `?*u8`. There is no `realloc` expression, so that method adds no instructions unless a call writes it.

[Index](../README.md) · [Previous: trace](trace.md) · [Next: va](va.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
