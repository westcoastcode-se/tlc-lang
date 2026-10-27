# Pointers

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `*T`, optional pointers, const pointers, and pointers to arrays.

## A pointer has a value

`*T` is a pointer that has a value, and `?*T` is that pointer or `null`.

`*?*T` and `?**T` are different types, because `?` is written immediately before `*`.

## A pointer to a constant

`*const T` is a pointer to constant `T`, and the variable may be reseated.

The compiler reports a store through it, `*p = 1` or `p[i] = 1`, as `type mismatch`.

Reading `*p` and `p[i]` is allowed.

`*T` may be stored in `*const T`, but `*const T` may not be stored in `*T`.

`?*const T` is that pointer or `null`.

`**const T` is a pointer to a pointer to constant `T`.

`*const *T` is a pointer to a constant pointer.

## Locals, null, and a const pointer

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

## When a sized array becomes a pointer

A sized array converts to `*T` when `T` is the element and that pointer is written.

`var p *i32 = row` is the address of the first element, and `take(row)` is that pointer when `take` has a parameter `*i32`.

`*const T` adds `const`, and the compiler reports dropping `const` as `type mismatch`.

`?*T` does not accept that pointer, so `var q ?*i32 = row` is `type mismatch`.

`var p = row` keeps the array, because the pointer type is not written.

`*[N]T` is `&row`, and the compiler reports passing the array there as `type mismatch`.

`var v []i32 = row` is a slice of the array.

## Pointers to arrays

`**[4]i32` is a pointer to a pointer to an array, and `?*[4]i32` is that pointer or `null`.

`*const [4]i32` is a pointer to a constant array.

`p[i]` and `(*p)[i]` read and write the same element of `*[4]i32`.

`p[1:3]`, `p[1:]`, `p[:2]`, and `p[:]` on `*[4]i32` are `[]i32`.

`(*p)[1:3]` and `(*p)[:]` are `[]i32` as well.

`*[2][3]i32` gives `[][3]i32`.

`*[2][]i32` gives `[][]i32`.

The compiler reports a range of `*[N]Duo` or `*[N][M]Duo` as `type mismatch`.

The compiler reports a range of `*i32`, or of a still-optional `?*[4]i32`, as `type mismatch`.

## Rows and nested arrays

`p[i]` and `(*p)[i]` on `*[2][2]i32` are the row `[2]i32`, and `p[1][0]` is one element.

Each further array is that same index.

`var row [2]i32 = p[1]` copies the row, and a later store through `p` does not change the copy.

`p[1] = line` copies `line` into that row, and so does `(*p)[1] = line`.

A later store into `line` does not change the row.

`(**deep)[2]` and `(*deep)[2]` read the same element.

The compiler reports a store through `*const [4]i32`, such as `p[i] = 1`, as `type mismatch`.

Reading `p[i]` is allowed.

## Length

`p.len` and `(*p).len` on `*[N]T` are the written length `N`.

`(&row).len` and `buf.len`, when `buf` is `new [_]T`, are that same length.

`p.len` on `*[N][M]T` and on `*[N][]T` is `N`, and `p[0].len` is the inner length.

Reading `p.len` on `*const [N]T` is allowed.

The compiler reports that read on `*i32`, `*[]T`, `**[N]T`, `new [n]T`, and a still-optional `?*[N]T` as `type mismatch`.

The compiler reports assigning `p.len`, or taking its address, as `not an lvalue`.

## A slice from an array pointer

`return p` where `[]i32` is expected builds the slice header that `p[:]` builds.

`var s []i32 = p` is that slice, and `p[:]` is the slice.

The compiler reports a file-scope `var view []i32 = p` as `global initializer is not a constant`.

## An optional pointer to an array

The compiler reports `var deep **[4]i32` with no initializer as `type mismatch`.

`var opt ?*[4]i32` may omit the initializer, and the pointer is null.

The compiler reports `*opt` and `opt[i]` as `type mismatch` while `opt` is optional.

Postfix operators bind left to right, so `opt![i]` is `(opt!)[i]`.

That expression narrows the pointer, then indexes.

After `if (opt == null) return`, `opt` is `*[4]i32` for the rest of the block.

`opt[i]` is then the element, and `opt.len` is the length.

The compiler reports `opt!` in that block as `type mismatch`.

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
