# Pointers

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`*T` is a pointer that has a value. `?*T` is that pointer or `null`. `*?*T` and `?**T` are different types. `?` is written immediately before `*`.

`*const T` is a pointer to constant `T`. The variable may be reseated. A store through it, `*p = 1` or `p[i] = 1`, is reported as `type mismatch`. Reading `*p` and `p[i]` is allowed. `*T` may be stored in `*const T`. `*const T` may not be stored in `*T`. `?*const T` is that pointer or `null`. `**const T` is a pointer to a pointer to constant `T`. `*const *T` is a pointer to a constant pointer.

```c
func main() i32 {
    var n i32 = 1;
    var p *i32 = &n;
    var q ?*i32 = null;
    var c *const i32 = p;
    var inner *i32 = &n;
    var to_const **const i32 = &inner;
    var to_ptr *const *i32 = &inner;
    q = p;
    return *p + *c + **to_const + **to_ptr;
}
```

A sized array converts to `*T` when `T` is the element and that pointer is written. `var p *i32 = row` is the address of the first element. `take(row)` is the same pointer when `take` has a parameter `*i32`. `*const T` adds `const`. Dropping `const` is reported as `type mismatch`. `?*T` does not accept that pointer. `var q ?*i32 = row` is `type mismatch`. `var p = row` keeps the array. `*[N]T` is `&row`, and passing the array there is reported as `type mismatch`. `var v []i32 = row` is a slice of the array.

`**[4]i32` is a pointer to a pointer to an array. `?*[4]i32` is that pointer or `null`. `*const [4]i32` is a pointer to a constant array. `p[i]` and `(*p)[i]` read and write the same element of `*[4]i32`. `p[1:3]`, `p[1:]`, `p[:2]`, and `p[:]` on `*[4]i32` are `[]i32`, and so are `(*p)[1:3]` and `(*p)[:]`. `*[2][3]i32` gives `[][3]i32`. `*[2][]i32` gives `[][]i32`. A range of `*[N]Duo` or `*[N][M]Duo` is `type mismatch`. `*i32` and a still-optional `?*[4]i32` are reported as `type mismatch`. `p[i]` and `(*p)[i]` on `*[2][2]i32` are the row `[2]i32`. `p[1][0]` is one element. Each further array is that same index. `var row [2]i32 = p[1]` copies the row. A later store through `p` does not change the copy. `p[1] = line` copies `line` into that row, and so does `(*p)[1] = line`. A later store into `line` does not change the row. `(**deep)[2]` and `(*deep)[2]` read the same element. A store through `*const [4]i32`, `p[i] = 1`, is reported as `type mismatch`. Reading `p[i]` is allowed. `p.len` and `(*p).len` on `*[N]T` are the written length `N`. `(&row).len` and `buf.len`, when `buf` is `new [_]T`, are that length. `p.len` on `*[N][M]T` and on `*[N][]T` is `N`. `p[0].len` is the inner length. Reading `p.len` on `*const [N]T` is allowed. `*i32`, `*[]T`, `**[N]T`, `new [n]T`, and a still-optional `?*[N]T` are reported as `type mismatch`. Assigning `p.len`, or taking its address, is reported as `not an lvalue`. `return p` where `[]i32` is expected builds the slice header that `p[:]` builds. `var s []i32 = p` is that slice. A file-scope `var view []i32 = p` is reported as `global initializer is not a constant`. `p[:]` is the slice.

`var deep **[4]i32` with no initializer is reported as `type mismatch`. `var opt ?*[4]i32` may omit the initializer. The pointer is null. `*opt` and `opt[i]` are reported as `type mismatch` while `opt` is optional. Postfix operators bind left to right, so `opt![i]` is `(opt!)[i]`. It narrows the pointer, then indexes. After `if (opt == null) return`, `opt` is `*[4]i32` for the rest of the block, so `opt[i]` is the element, `opt.len` is the length, and `opt!` is reported as `type mismatch`.

## Assembly

```asm
main:
movl $1, -20(%rsp)               # constant
leaq -20(%rsp), %rax             # address
movq %rax, -16(%rsp)             # move
movq -16(%rsp), %rax             # move
movq %rax, -8(%rsp)              # move
movq -16(%rsp), %rax             # move
movl (%rax), %eax                # move
movq -8(%rsp), %rcx              # move
addl (%rcx), %eax                # add
retq                             # return
```

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
