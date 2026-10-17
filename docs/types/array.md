# Arrays

[Index](../README.md) · [Previous: Unions](union.md) · [Next: Enums](enum.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

An array length is a positive decimal `i32` literal. `[_]i32{10, 20, 12}` writes the values. `_` stands for the length, which is the number of values, so the type is `[3]i32`. A file-scope array takes the same lists, including a struct element: `var named [3]i32 = {10, 4, 1}`, `var row = [_]i32{10, 20, 12}`, and `var origin = [_]Point{Point{.x = 1, .y = 2}}`.

```c
func add4(p *i32) i32 {
    return p[0] + p[1] + p[2] + p[3];
}

func main() i32 {
    var a [4]i32;
    a[0] = 10;
    a[1] = 20;
    a[2] = 7;
    a[3] = 5;
    return add4(a);
}
```

The array becomes a pointer when it is passed. A string literal is `[]const u8`. It can be passed to `[]const u8` and to `*const u8`. It cannot be passed to `*u8` or `[]u8`. The index has type `i32`. `new [n]i32` reserves `n` elements and yields `*i32`. `buf[i] = 1` writes an element. `new [n]Point{{.x = 1}...}` stores that value in every element. `new [_]Point{{.x = 1}, {.y = 2}}` is `*[2]Point`. See [Builtin](../builtin/allocator.md).

`[]T` is a readonly slice of an array. `var v []i32 = row` takes a slice of `[N]i32`. `row[i:j]` takes the elements from `i` up to, but not including, `j`, and so does `v[i:j]`. `v[i]` reads an element, and `v[i] = 1` is reported as `type mismatch`. `v.len` is `i32`. `row.len` on a sized array is `i32`, the number of elements, with the same spelling. An empty array has length 0. The length is a compile-time integer. At file scope, `const N = row.len` can be an array length. Assigning `row.len`, or taking its address, is reported as `not an lvalue`. `sizeof([]T)` is 16. A slice parameter stays a slice. The pointer from an array parameter is not a slice, so `a.len` in `func take(a [3]i32)` is reported as `type mismatch`.

A field may be a slice, `items []i32`. It is stored as the slice header. `sizeof` of `{ n i32; items []i32; }` is 24. Assigning the field replaces that header. The same field in `extern(abi="c")` is reported as `not supported yet`.

## Cost

`var a [2]i32` with no initializer adds no instruction. The elements are uninitialized. `row.len` is the constant element count. `func main() i32 { var row [3]i32; return row.len; }` executes 2 instructions, `movl $3, %eax` and `retq`. The array is not read. `var row [3]i32 = {10, 20, 12}` stores those three values and does not store a zero first. `a[1] = 3` on a local, with a constant index, is 1 `movl` into the element's slot. Reading it back is 1 `movl`. A variable index on a pointer is a sign-extend and a scaled load. `return p[i]` is 8 instructions including the pointer store, the index store, a second sign-extend, and `retq`.

Passing `[4]i32` to a function is 1 `leaq` of the first element, plus the traced call. The callee receives a pointer. `return a[0]` there is 4 instructions including the pointer store and `retq`. The elements are not copied.

Returning `[2]i32` from a local puts the two elements in `%eax` and `%edx`. Storing both elements and returning is 5 instructions. The local is not zeroed first.

`new [n]i32` is the allocation counted on [new and delete](../statements/new.md). There is no bounds check on an index, so the index adds no compare.

[Index](../README.md) · [Previous: Unions](union.md) · [Next: Enums](enum.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
