# new and delete

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`new T` reserves space for one `T` and yields `*T`. The result is that pointer. When allocation returns `null`, `new` calls `builtin.panic("out of memory")`. `delete p` releases that pointer and has no result. With no allocator written, both use `builtin.allocator`. `new(&arena) T` and `delete(&arena) p` take a pointer. The pointee is the allocator type, and that type has the methods of `Allocator`. The type is taken from the pointer, so the call is `new(&arena) Point`. `new(arena)` is reported as `type mismatch: expected *Arena, found Arena`. A `*const` pointer is reported as `type mismatch: expected *Arena, found *const Arena`. `const p = &arena` is a mutable `*Arena`, so `new(p)` uses `arena`. `new(&builtin.allocator)` is `Malloc` and calls `malloc` directly. `new Point{.x = 40, .y = 2}` writes those fields after the block is reserved. A field that is left out takes the value written on the field, and stays uninitialized when the field has none. A later `var p = new Point{.x = 4}` or `p := new Point{.x = 4}` in the same block is `uninitialized` on that field, the same as the first `new`. `new Point{0...}` stores 0 in every field the list does not name. `new i32` does not store the reserved integer. `new [n]i32` does not store the elements. `new [n]Point` with no element list stores each field of each element. A `*T` field has to be named unless that field has a value. A field whose type contains a `*T` has to be named unless each of those pointers has a value. Leaving it out is reported as `type mismatch`. A `?*T` field that is left out is null when the field has no value. `new Point{1, 2}` and an initializer on a type that is not a struct are reported as `type mismatch`. `new [n]T` reserves `n` elements and yields `*T`. `new [n]Point{{.x = 1}...}` stores that value in every element. `new [n]Point{.x = 1}` is parsed as a field list and is reported as `type mismatch`. `new [_]Point{{.x = 1}, {.y = 2}}` yields `*[2]Point`. `new Box[i32]` allocates one specialization.

```c
func main() i32 {
    var p *Point = new Point{.x = 40};
    var x i32 = p.x;
    delete p;
    return x;
}
```

## Cost

`var p *i32 = new i32` followed by `delete p` and `return 0` executes 13 instructions when `malloc` returns a block. `malloc` is called with 4, the null check jumps to the success path, the pointer is stored, and `free` is called. The reserved `i32` is not stored. `malloc` and `free` do not push a trace frame. Their own instructions are outside this count. The failure path pushes a trace frame and calls `builtin.panic("out of memory")`. `panic` prints the message and the stack, then aborts. Those instructions are on the failure path.

`new Point{.x = 40}` on a `Point` of two `i32` fields does that allocation for `sizeof(Point)`, stores 0 in each field, and then stores 40 in `x`: two `movl` of 0 and one `movl` of 40. When `y i32 = 2`, the first of those stores writes 2. `new Point` with no named fields stores 0 in each field, or the value written on the field. `new [n]i32` checks that `n` is positive, multiplies by the element size, and uses that byte count. A non-positive `n` aborts. The elements are not stored. `new [_]Point{...}` uses the list length as a constant size, so the multiply is folded. The element value is a struct value, so a field that value leaves out takes the value written on the field, or zero when the field has none. `new [n]Point` stores those values in each element. With no value on the fields, that is two `movl` of 0 per element.

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
