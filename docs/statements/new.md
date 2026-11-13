# new and delete

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `new` and `delete`, which reserve storage and later release it.

## One object

`new T` reserves space for one `T` and yields `*T`.

`T` may be any pointer type, such as `?*[2]i32` or `?**[]Point`. `new ?*[2]i32` yields `*?*[2]i32`.

The result is that pointer.

> **Warning:** When allocation returns `null`, `new` calls `builtin.panic("out of memory")`.

`delete p` releases that pointer and has no result.

With no allocator written, both use `builtin.allocator`.

`--allocator` names that type instead. `Malloc` calls `malloc` and `free`. `tlc --test` uses `debug.DebugAllocator` unless the flag names another type.

`new(&arena)` keeps the type of that pointer.

## Choosing an allocator

`new(&arena) T` and `delete(&arena) p` take a pointer.

The pointee is the allocator type, and that type has the methods of `Allocator`.

The type is taken from the pointer, so the call is `new(&arena) Point`.

The compiler reports `new(arena)` as `type mismatch: expected *Arena, found Arena`.

The compiler reports a `*const` pointer as `type mismatch: expected *Arena, found *const Arena`.

`const p = &arena` is a mutable `*Arena`, so `new(p)` uses `arena`.

`new(&builtin.allocator)` is `Malloc` and calls `malloc` directly.

Inside a `comptime func`, a `new` or `delete` that uses the heap is `not supported yet`. The allocator is written at the call, and its `malloc`, `free`, and `realloc` do not call `malloc`, `realloc`, or `free`.

`new(alloc) T` and `delete(alloc) p` are allowed when `alloc` has type `*Allocator`. The call that passes `alloc` names the struct, and that struct is used the same way as `new(&arena)`. Passing `&builtin.allocator` still calls `malloc` directly.

## Fields written after the block is reserved

`new Point{.x = 40, .y = 2}` writes those fields after the block is reserved.

A field that is left out takes the value written on the field, and stays uninitialized when the field has none.

A later `var p = new Point{.x = 4}` or `p := new Point{.x = 4}` in the same block is `uninitialized` on that field.

That is the same as the first `new`.

`new Point{0...}` stores 0 in every field the list does not name.

`new i32` does not store the reserved integer.

`new [n]i32` does not store the elements.

`new [n]Point` with no element list stores each field of each element.

A `*T` field has to be named unless that field has a value.

A field whose type contains a `*T` has to be named unless each of those pointers has a value.

Leaving it out is reported as `type mismatch`.

A `?*T` field that is left out is null when the field has no value.

`new Point{1, 2}` and an initializer on a type that is not a struct are reported as `type mismatch`.

## Arrays and one specialization

`new [n]T` reserves `n` elements and yields `[]T`. The length is `n`. `buf[i]` is rejected at run time when `i` is outside that length.

`new [0]T` is an empty slice. `.len` is 0. The call does not allocate.

The count is `i32` or `i64`. `u64` is `type mismatch: expected i32, found u64`.

A negative constant, including a negative `i64`, is `type mismatch`. A negative count that is not a constant calls `builtin.panic("count must be positive")`.

An `i64` past the `i32` length calls `builtin.panic("size does not fit")`. A size that does not fit calls the same panic.

```c
func main() i32 {
    var n i64 = 4;
    var buf = new [n]i32;
    return buf.len;
}
```

```asm
main:
subq $104, %rsp                  # allocate
movq $4, 80(%rsp)                # n = 4
movq 80(%rsp), %rax              # n
movq %rax, 72(%rsp)              # move
cmpq $0, %rax                    # n < 0
jge .Lpositive                   # else panic: count must be positive
.Lpositive:
movq 72(%rsp), %rax              # n
cmpq $0, %rax                    # n == 0
jne .Lready                      # else empty slice
.Lready:
movq 72(%rsp), %rax              # n
cmpq $2147483647, %rax           # length fits i32
jg .Lbig                         # panic: size does not fit
movl %eax, 36(%rsp)              # length
movl %eax, %eax                  # zero-extend
movq %rax, 40(%rsp)              # move
movabsq $4611686018427387903, %rcx # byte limit
cmpq %rcx, %rax                  # size fits
ja .Loverflow                    # panic: size does not fit
movq 40(%rsp), %rax              # count
shlq $2, %rax                    # bytes
movq %rax, 16(%rsp)              # move
# trace frame for malloc, then the matching pop
movq 16(%rsp), %rdi              # bytes
callq malloc                     # call malloc
cmpq $0, %rax                    # null
jne .Lok                         # else panic: out of memory
.Lok:
movq 8(%rsp), %rcx               # pointer
movl 36(%rsp), %eax              # length
movq %rcx, 56(%rsp)              # slice data
movl %eax, 64(%rsp)              # slice length
movq 56(%rsp), %rcx              # buf data
movl 64(%rsp), %eax              # buf.len
movq %rcx, 88(%rsp)              # move
movl %eax, 96(%rsp)              # move
movl 96(%rsp), %eax              # buf.len
addq $104, %rsp                  # release
retq
```

The program returns 4.

`new [n]Point{{.x = 1}...}` stores that value in every element.

`new [n]Point{.x = 1}` is parsed as a field list and is reported as `type mismatch`.

`new [_]Point{{.x = 1}, {.y = 2}}` yields `*[2]Point`.

`new Box[i32]` allocates one specialization.

```c
func main() i32 {
    var p *Point = new Point{.x = 40};
    var x i32 = p.x;
    delete p;
    return x;
}
```

```asm
main:
subq $40, %rsp                   # subtract
# trace frame for the call, then the matching pop
movl $8, %edi                    # constant
callq malloc                     # call malloc
movq %rax, 8(%rsp)               # move
cmpq $0, %rax                    # compare
jne .L1                          # branch
# trace frame for the call, then the matching pop
callq panic                      # call panic
.L1:
movq 8(%rsp), %rax               # move
movl $40, (%rax)                 # constant
movq %rax, 24(%rsp)              # move
movq 24(%rsp), %rax              # move
movl (%rax), %eax                # move
movl %eax, 36(%rsp)              # move
movq 24(%rsp), %rdi              # move
callq free                       # call free
movl 36(%rsp), %eax              # move
addq $40, %rsp                   # add
retq                             # return
```

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
