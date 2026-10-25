# new and delete

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`new T` reserves space for one `T` and yields `*T`. The result is that pointer. When allocation returns `null`, `new` calls `builtin.panic("out of memory")`. `delete p` releases that pointer and has no result. With no allocator written, both use `builtin.allocator`. `new(&arena) T` and `delete(&arena) p` take a pointer. The pointee is the allocator type, and that type has the methods of `Allocator`. The type is taken from the pointer, so the call is `new(&arena) Point`. `new(arena)` is reported as `type mismatch: expected *Arena, found Arena`. A `*const` pointer is reported as `type mismatch: expected *Arena, found *const Arena`. `const p = &arena` is a mutable `*Arena`, so `new(p)` uses `arena`. `new(&builtin.allocator)` is `Malloc` and calls `malloc` directly. `new Point{.x = 40, .y = 2}` writes those fields after the block is reserved. A field that is left out takes the value written on the field, and stays uninitialized when the field has none. A later `var p = new Point{.x = 4}` or `p := new Point{.x = 4}` in the same block is `uninitialized` on that field, the same as the first `new`. `new Point{0...}` stores 0 in every field the list does not name. `new i32` does not store the reserved integer. `new [n]i32` does not store the elements. `new [n]Point` with no element list stores each field of each element. A `*T` field has to be named unless that field has a value. A field whose type contains a `*T` has to be named unless each of those pointers has a value. Leaving it out is reported as `type mismatch`. A `?*T` field that is left out is null when the field has no value. `new Point{1, 2}` and an initializer on a type that is not a struct are reported as `type mismatch`. `new [n]T` reserves `n` elements and yields `*T`. `new [n]Point{{.x = 1}...}` stores that value in every element. `new [n]Point{.x = 1}` is parsed as a field list and is reported as `type mismatch`. `new [_]Point{{.x = 1}, {.y = 2}}` yields `*[2]Point`. `new Box[i32]` allocates one specialization.

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
subq $24, %rsp                   # subtract
movl $4, %edi                    # constant
callq malloc                     # call malloc
movq %rax, 8(%rsp)               # move
cmpq $0, %rax                    # compare
jne .L1                          # branch
movl (%rip), %eax                # move
movl %eax, 4(%rsp)               # move
cmpl $32, %eax                   # compare
jb .L2                           # branch
jmp .L3                          # jump
.L1:
movq 8(%rsp), %rax               # move
movq %rax, 16(%rsp)              # move
movq 16(%rsp), %rdi              # move
callq free                       # call free
xorl %eax, %eax                  # zero the result
addq $24, %rsp                   # add
retq                             # return
.L2:
movl 4(%rsp), %eax               # move
leaq (%rip), %rcx                # address
movslq %eax, %rdx                # move
imulq $24, %rdx, %rdx            # multiply
addq %rdx, %rcx                  # add
leaq (%rip), %rdx                # address
movq %rdx, (%rcx)                # move
movl $2, 8(%rcx)                 # constant
movl $13, 12(%rcx)               # constant
leaq (%rip), %rdx                # address
movq %rdx, 16(%rcx)              # move
addl $1, %eax                    # add
movl %eax, (%rip)                # move
.L3:
leaq (%rip), %rdi                # address
callq panic                      # call panic
```

[Index](../README.md) · [Previous: Bindings](binding.md) · [Next: assert](assert.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
