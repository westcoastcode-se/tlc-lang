# new and delete

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`new T` reserves space for one `T` and yields `*T`. `delete p` releases that pointer. With no allocator written, both use `builtin.allocator`. `new(allocator) T` and `delete(allocator) p` use another value whose type has the methods of `Allocator`. The allocator is described with `builtin`. It is passed the same way as the receiver of `malloc`: a const value and a `*const` pointer are `type mismatch: expected *Arena, found *const Arena`. `const p = &arena` is a mutable `*Arena`, so `new(p)` uses `arena`. `new Point{.x = 40, .y = 2}` writes those fields after the block is reserved. The block is not zeroed. A field that is left out stays uninitialized. A `*T` field, or a field whose type contains one, has to be named. Leaving it out is reported as `type mismatch`. `new Point{1, 2}` and an initializer on a type that is not a struct are reported as `type mismatch`. `new [n]T` reserves `n` elements and yields `*T`. `new [n]Point{{.x = 1}...}` stores that value in every element. `new [_]Point{{.x = 1}, {.y = 2}}` yields `*[2]Point`. `new Box[i32]` allocates one specialization.

```c
func main() i32 {
    var p *Point = new Point{.x = 40};
    var x i32 = p.x;
    delete p;
    return x;
}
```

## Cost

`var p *i32 = new i32` followed by `delete p` and `return 0` executes 13 instructions when `malloc` returns a block. `malloc` is called with 4, the null check jumps to the success path, the pointer is stored, and `free` is called. The reserved `i32` is not stored. `malloc` and `free` do not push a trace frame. Their own instructions are outside this count. The failure path calls `dprintf` and `abort`.

`new Point{.x = 40}` does that allocation for `sizeof(Point)` and then stores each named field. A missing field is not stored. `new [n]i32` checks that `n` is positive, multiplies by the element size, and uses that byte count. A non-positive `n` aborts. `new [_]Point{...}` uses the list length as a constant size, so the multiply is folded. The element value is a struct value, so a field that value leaves out is zero.

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
