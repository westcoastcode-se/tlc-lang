# Slices

[Index](../README.md) · [Previous: sizeof](sizeof.md) · [Next: The Type type](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`[]T` is a readonly slice of contiguous `T` values. Brackets come before the element type, as in `[]i32` and `func head(a []i32) []i32`. `[]const u8` is that slice with a const element. A string literal has that type: `var text = "hi"` stores the address and the length 2, which is 1 `leaq`, 1 `movq`, and 1 `movl`. `"hi"[1]` reads one byte, and `"hi"[1:]` is the tail, without storing the literal first. The literal, and any other `[]const u8`, converts to `*const u8` when that pointer is written or expected. That pointer is what a C library takes. The length is dropped. `[N]T` converts to `[]T`. `type Row []i32` names that slice. `[]i32` and `[N]i32` convert to `Row`. `Row` does not convert to `[]i32`, and it does not convert to another slice alias. `type Pair [2]i32` does not convert to `[]i32`. `Row` can have value methods, written `func Row::first(self this) i32`. `self *this` on that alias is a pointer to the slice. The library type [`string_view`](../std/string.md) is `type string_view []const u8` in `lib/tlc/std/string`. `var v = row` keeps the array. It does not infer a slice.

`v[i]` reads one element. The index has type `i32`. Storing an element, `v[i] = 1`, is reported as `type mismatch`. `v[i].x` reads a field. Storing that field is reported as `type mismatch`. When the element is `*T`, `*v[i] = 1` writes through the pointer. `v.len` is `i32` and is the number of elements. A local `var empty []i32` is uninitialized. It does not store a length of 0. A file-scope slice with no initializer is zero, the same as any other global.

`[][]T` is a slice of `[]T` headers. `[N][]T` is that many headers. `v[i]` has type `[]T`, and `v[i][j]` reads one `T`. `v.len` is the number of headers. `v[i].len` is the number of elements in that header. Storing through the slice, `v[i] = row` or `v[i][j] = 1`, is reported as `type mismatch`. The array is the storage that can change: `rows[i] = other` copies one header, and a `[][]T` view of `rows` sees that store. `var rows = [_][]i32{a[:], b[:]}` keeps `[2][]i32`. `var view [][]i32 = rows` converts that array to `[][]T`. `var copy = rows` decays to a pointer, the same as any other array name. `type Row []i32` makes `[]Row` and `[N]Row` the same types as `[][]i32` and `[N][]i32`. `[][]const u8` is that slice with a const element. `const` stays in front of the innermost name. A range of `[N][]T` is `[][]T`. A range of `[][]T` stays `[][]T`. `sizeof([][]i32)` is 16 and `sizeof([2][]i32)` is 32. A parameter `[][]T` stays a slice. A parameter `[N][]T` decays to a pointer to one header, so `rows.len` there is reported as `type mismatch`. A field may be `items [][]i32`. A file-scope `[_][]i32{a[:], b[:]}` is reported as `global initializer is not a constant`.

`a[i:j]` takes the elements from `i` up to, but not including, `j`. The base is an array or a slice. The result is `[]T`. A range of `type Row []i32` stays `Row`. A range of `[N]T`, including `type Pair [2]i32`, is the plain slice `[]T`. Both bounds have type `i32`. A missing low bound is `0`. A missing high bound is the length, so `a[i:]` is `a[i:a.len]`, `a[:j]` is `a[0:j]`, and `a[:]` is the whole slice. The length is `j - i`. There is no bounds check. The result is not an lvalue: `row[0:2] = row`, `row[1:] = row`, and `&row[0:2]` are reported as `not an lvalue`. A pointer, including an array parameter, is reported as `type mismatch`. A range of `[_][2]i32` is a slice of `[2]i32`. `var view = grid[0:1]` keeps that slice. `view.len` is the number of rows. `view[0].len` is 2. `view[0][1]` reads one element. `view[0]` and `grid[1]` are `[2]i32`, so each converts to `[]i32`, and `grid[1][0:2]` is that slice. `grid[1:]`, `grid[:1]`, and `grid[:]` are that slice. A range of `view` stays a slice of `[2]i32`. Storing `view[0][1]` is reported as `type mismatch`. `a[]` is reported as `expected an expression`.

A field of a tlc struct may be a slice, written `items []i32`. `sizeof` of `{ n i32; items []i32; }` is 24. The same field in `extern(abi="c")` is reported as `not supported yet`. A tlc union arm that is not a struct is reported as `type mismatch`.

`sizeof v` is 16, the same as `sizeof([]T)`. `typeof([]i32).name()` spells `[]i32`.

`i32[]` is reported as `type mismatch`. `*[]T` is a pointer to that header. `&view` has that type when `view` is a slice. `[][][]T` is a slice whose element is `[][]T`. `[N][M]T` is an array of `[M]T`. `[][N]T` is a slice of `[N]T`. `type Grid [][]i32` then `[]Grid` is `[][][]i32`. `Vec[[][]i32]` is that specialization. A field `T` on `Hold[T]` keeps those shells, so `Hold[[][]i32]` stores `[][]i32`. `const v []i32 = row` stores a slice of that file-scope array. `const again []i32 = v` copies that header. A slice alias stays that alias, as in `const named Row = row`. `v[0]` reads one element. `v.len` is the length. Assigning a `const` slice is reported as `not an lvalue`. `&view` on a local slice is `*[]T`. A range, an array literal, or a call is reported as `global initializer is not a constant`. `const v = row` is reported as `type mismatch`. An array parameter is already a pointer. `return row` from `func open(row [4]i32) []i32` is reported as `return type: expected []i32, found *i32`. `row[0:2]` in that function is reported as `type mismatch`. A file-scope slice is initialized from the name of an array, another slice, a string literal, or a call. `var view = pair()` and `var view []i32 = pair()` run `pair` before `main` when that call is the whole initializer. `var text = "hi"` and `const label = "hi"` store that slice in the binary. An array literal, `a[i:j]`, `a[i:]`, `a[:j]`, or `a[:]` is reported as `global initializer is not a constant`.

```c
type Hold struct {
    n i32;
    items []i32;
};

func head(v []i32) i32 {
    return v[0];
}

func main() i32 {
    var row = [_]i32{10, 20, 12};
    var v []i32 = row;
    var mid = row[1:3];
    var box Hold;
    box.items = mid;
    return head(v) + v.len + mid[0] + box.items.len;
}
```

```c
func sum(rows [][]i32) i32 {
    return rows[0][0] + rows[1][0];
}

func main() i32 {
    var a = [_]i32{10, 20};
    var b = [_]i32{12};
    var rows = [_][]i32{a[:], b[:]};
    var view [][]i32 = rows;
    return sum(view);
}
```

## Cost

A slice is a pointer and an `i32` length, 16 bytes. Building one from a local array is 1 `leaq` of the first element and 1 `movl` of the length, after the array's own stores. `var text = "hi"` is 1 `leaq`, 1 `movq` of the address, and 1 `movl` of the length 2. Another slice or array around that value is the same header or the same element step. `[][][]i32` is still one 16-byte header. `[2][3]i32` is two arrays, and an index steps by the inner array.

`var p = &view` on a local `[]i32` is 1 `leaq` and 1 `movq` of that header's address. `return (*p)[0]`, after the array and the header are stored, reloads the pointer, loads the length, loads the data pointer, loads the `i32`, and returns: 1 `movq`, 1 `movl`, 1 `movq`, 1 `movl`, and `retq`.

A file-scope `var view = pair()`, when `pair` returns `numbers[0:2]` and `numbers` is a global `[2]i32`, executes 40 instructions in `tlc.init` on the path where the trace holds fewer than 32 frames. The pointer and the length are both stored. `pair` is 3 instructions: the address, the length 2, and `retq`. `return view.len` is 2 loads and `retq`. One load reads the pointer and one reads the length. A file-scope `const v []i32 = row` keeps that header in the binary. `return v.len` is 1 load of the length, 1 load of the pointer, and `retq`. `return v[0]` is those two loads, 1 load of that `i32`, and `retq`.

`v[0]` on a slice parameter loads the pointer and loads the `i32` at that address: 2 instructions after the slice has been stored. `v.len` is 1 load of the length. `v[i]` with a variable `i` sign-extends `i` and uses a scaled load. The function `return v[i]` is 10 instructions including the three parameter stores and `retq`. One of those instructions sign-extends `i` a second time.

`v[1:3]` builds a new header: add 4 to the pointer, store it, and store the length 2. That is 3 instructions after the original slice is in hand. The length is `j - i` when the bounds are not constants. There is no bounds check, so the index adds no compare and no branch.

`v[:]` copies that header: 1 load of the pointer and 1 load of the length. `v[:2]` is 1 load of the pointer and 1 `movl` of the length 2. `v[1:]` reloads the length, reloads the pointer, adds 4, and decrements the length: 4 instructions. On a local `[4]i32`, `row[:]` is 1 `leaq` of the first element and 1 `movl` of the length 4, after the array's own stores. `row[:2]` is that `leaq` and 1 `movl` of the length 2. `row[1:]` is 1 `leaq` of the second element and 1 `movl` of the length 3. On a local `[3][2]i32`, `grid[1:3]` is 1 `leaq` of the second row and 1 `movl` of the length 2, after the array's own stores. The row is 8 bytes. `return view[0][1]` loads that `i32` from offset 4.

A value method on a slice alias is a traced call. The callee is the method body. [`string_view.equals`](../std/string.md) measures that shape. The caller records a trace frame, 36 instructions when fewer than 32 frames are stored, plus the copies of the two slice headers.

`[][]T` is still one header, 16 bytes. `[N][]T` is N headers. `sizeof([][]i32)` and `sizeof([2][]i32)` fold to the immediates 16 and 32. Storing one `[]T` into a local `[N][]T`, from `a[:]`, is 1 `leaq` of the first element, 1 `movq` of that address, and 1 `movl` of the length, after that row's own stores. `var view [][]i32 = rows` is those same three instructions for the outer header. `rows[1] = other` copies a header that is already stored: 1 load of the pointer, 1 load of the length, 1 store of the length, and 1 store of the pointer.

`return v.len` on a `[][]i32` parameter is 5 instructions: the two parameter stores, 1 load of the length, 1 load of the pointer, and `retq`. `return v[0].len` is 6: those two stores, 1 reload of the pointer, 1 load of the inner length, 1 load of the inner pointer, and `retq`. `return v[0][1]` is 7: the two stores, 1 reload of the pointer, 1 load of the inner length, 1 load of the inner pointer, 1 load of that `i32`, and `retq`. `return v[i][j]` is 14 instructions, including the four parameter stores and `retq`. One instruction sign-extends `i` and one shifts it by 4, because a header is 16 bytes. The inner length and the inner pointer are loaded. `j` is sign-extended twice, and the element uses a scaled load. `return rows[0][1]` on a `[2][]i32` parameter is 6 instructions: 1 store of the decayed pointer, 1 reload, 1 load of the inner length, 1 load of the inner pointer, 1 load of the `i32`, and `retq`.

`return v[:]` copies the outer header: the two parameter stores, 1 load of the pointer, 1 load of the length, and `retq`. `return v[:1]` is those stores, 1 load of the pointer, 1 `movl` of the length 1, and `retq`. `return v[1:]` is the two stores, 1 reload of the length, 1 reload of the pointer, 1 `addq` of 16, 1 `decl`, and `retq`. On a local `[2][]i32`, `rows[0:1]` is 1 `leaq` of the first header, 1 `movq` of that address, and 1 `movl` of the length 1, after the array's own stores.

[Index](../README.md) · [Previous: sizeof](sizeof.md) · [Next: The Type type](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
