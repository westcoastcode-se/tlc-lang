# Arrays

[Index](../README.md) · [Previous: Unions](union.md) · [Next: Enums](enum.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

An array length is a positive decimal `i32` literal. `[_]i32{10, 20, 12}` writes the values. `_` stands for the length, which is the number of values, so the type is `[3]i32`. A file-scope array takes the same lists, including a struct element: `var named [3]i32 = {10, 4, 1}`, `var row = [_]i32{10, 20, 12}`, and `var origin = [_]Point{Point{.x = 1, .y = 2}}`.

An element that is an array is another brace list. `var row [2][3]i32 = {{1, 2, 3}, {4, 5, 6}}` stores those values, and `row[1][2]` is 6. `var grid = [_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}` is one outer element of `[2][3]i32`, and `grid[0][1][2]` is 6. `{[_]i32{1, 2, 3}, [_]i32{4, 5, 6}}` writes the same rows. A different count is reported as `type mismatch`. `[_][]i32{{1, 2}, {3}}` is reported as `type mismatch`.

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

`[]T` is a readonly slice of an array. `var v []i32 = row` takes a slice of `[N]i32`. `row[i:j]` takes the elements from `i` up to, but not including, `j`, and so does `v[i:j]`. A missing low bound is `0`. A missing high bound is the length. `row[i:]`, `row[:j]`, and `row[:]` are those slices. A range of `[_][2]i32` is a slice of `[2]i32`. The length counts rows. `v[i]` reads an element, and `v[i] = 1` is reported as `type mismatch`. `v.len` is `i32`. `row.len` on a sized array is `i32`, the number of elements, with the same spelling. An empty array has length 0. The length is a compile-time integer. At file scope, `const N = row.len` can be an array length. Assigning `row.len`, or taking its address, is reported as `not an lvalue`. `sizeof([]T)` is 16. A slice parameter stays a slice. `a.len` in `func take(a [3]i32)` is 3, the length that was written. The parameter is still a pointer, so `return a` where a slice is expected is reported as `return type: expected []i32, found *i32`.

A field may be a slice, `items []i32`. It is stored as the slice header. `sizeof` of `{ n i32; items []i32; }` is 24. Assigning the field replaces that header. The same field in `extern(abi="c")` is reported as `not supported yet`.

`var row = [_]i32{10, 20}` has type `[2]i32`. Reading that name decays to `*i32`, so `var copy = row` is `*i32`. `copy = row` is reported as `not an lvalue`. Writing the array type copies the value. `var copy [2]i32 = row` stores both elements in a new array. A later store into `row` does not change `copy`. `const held = [_]i32{8, 9}` copies the same way, `const frozen [2]i32 = held`. A different length, `var copy [3]i32 = row`, is reported as `type mismatch`. A parameter `[2]i32` is already `*i32`, so `var copy [2]i32 = row` there is reported as `type mismatch`. `var from_file [3]i32 = file_row` copies a file-scope array into a local. `var copy [2]i32 = give()` copies the array when `give` returns `[2]i32`. `var grid = [_][2]i32{{1, 2}, {3, 4}}` then `var whole [2][2]i32 = grid` copies both rows, and `var line [2]i32 = grid[1]` copies that row. A file-scope `var copy [2]i32 = row` is reported as `not supported yet`. `return row` returns the local array. `return give()` returns that array when `give` returns `[2]i32`. `return grid[1]` returns that row. `return [_]i32{4, 6}` returns that list. `return file_row` returns the file-scope array. `return {4, 6}` is reported as `not supported yet`. `return row` when `row` is a parameter `[2]i32` is reported as `return type: expected [2]i32, found *i32`. `return grid[1]` when `grid` is a parameter `[2][2]i32` is reported as `return type: expected [2]i32, found i32`.

## Cost

`var a [2]i32` with no initializer adds no instruction. The elements are uninitialized. `row.len` is the constant element count. `func main() i32 { var row [3]i32; return row.len; }` executes 2 instructions, `movl $3, %eax` and `retq`. The array is not read. `var row [3]i32 = {10, 20, 12}` stores those three values and does not store a zero first. `var row [2][3]i32 = {{1, 2, 3}, {4, 5, 6}}; return row[1][2];` is 8 instructions: six `movl` of the elements, one `movl` of `row[1][2]` into `%eax`, and `retq`. The array is not zeroed first. `var grid = [_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}; return grid[0][1][2];` is the same 8. A file-scope `var row = [_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}` keeps those six values in the binary. `return row[0][1][2]` is 2 instructions, one `movl` of that element and `retq`. `a[1] = 3` on a local, with a constant index, is 1 `movl` into the element's slot. Reading it back is 1 `movl`. A variable index on a pointer is a sign-extend and a scaled load. `return p[i]` is 8 instructions including the pointer store, the index store, a second sign-extend, and `retq`.

Passing `[4]i32` to a function is 1 `leaq` of the first element, plus the traced call. The callee receives a pointer. `return a[0]` there is 4 instructions including the pointer store and `retq`. The elements are not copied. `return a.len` is 3 instructions: the pointer store, 1 `movl` of the written length, and `retq`. The array is not read. `return a[0:2]` is 4: the store, 1 reload of the pointer, 1 `movl` of the length 2, and `retq`. `return a[1:]` is 5: those, plus 1 `addq` of 4.

Returning `[2]i32` from a local puts the two elements in `%eax` and `%edx`. Storing both elements and returning is 5 instructions. The local is not zeroed first. `var copy [2]i32 = row` is 4 instructions after `row` is stored: two `movl` loads and two `movl` stores. The slot is not zeroed first. `return copy[1]` is 1 `movl` and `retq`. `var line [2]i32 = grid[1]` is those same 4 after `grid` is stored. `var copy [2]i32 = give()` pays the traced call. `give`, returning a local `[2]i32`, is 5 instructions. After `callq`, 1 `movl` keeps the low element and 2 `movl` store the pair. The copy into `copy` is 8 `movl`: the loaded pair is stored in a temporary and stored again. `return copy[1]` is 1 `movl`, then `addq` and `retq`. `return grid[1]` is 2 `movl` into `%eax` and `%edx`, then `retq`, after `grid` is stored. `return [_]i32{4, 6}` is those same 5 as returning a local. `return give()` pays the traced call. After `callq`, 1 `movl` keeps the low element and 2 `movl` store the pair. Returning that pair is 4 `movl` into a temporary and 2 `movl` into `%eax` and `%edx`, then `addq` and `retq`. On the path where the trace holds fewer than 32 frames, that function is 42 instructions.

`new [n]i32` is the allocation counted on [new and delete](../statements/new.md). There is no bounds check on an index, so the index adds no compare.

[Index](../README.md) · [Previous: Unions](union.md) · [Next: Enums](enum.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
