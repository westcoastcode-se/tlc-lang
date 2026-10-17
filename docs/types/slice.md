# Slices

[Index](../README.md) · [Previous: sizeof](sizeof.md) · [Next: The Type type](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`[]T` is a readonly slice of contiguous `T` values. Brackets come before the element type, as in `[]i32` and `func head(a []i32) []i32`. `[]const u8` is that slice with a const element. A string literal has that type: `var text = "hi"` stores the address and the length 2, which is 1 `leaq`, 1 `movq`, and 1 `movl`. The literal, and any other `[]const u8`, converts to `*const u8` when that pointer is written or expected. That pointer is what a C library takes. The length is dropped. `[N]T` converts to `[]T`. `type Row []i32` names that slice. `[]i32` and `[N]i32` convert to `Row`. `Row` does not convert to `[]i32`, and it does not convert to another slice alias. `type Pair [2]i32` does not convert to `[]i32`. `Row` can have value methods, written `func Row::first(self this) i32`. `self *this` on that alias is reported as `not supported yet`. The library type [`string_view`](../std/string.md) is `type string_view []const u8` in `lib/tlc/std/string`. `var v = row` keeps the array. It does not infer a slice.

`v[i]` reads one element. The index has type `i32`. Storing an element, `v[i] = 1`, is reported as `type mismatch`. `v[i].x` reads a field. Storing that field is reported as `type mismatch`. When the element is `*T`, `*v[i] = 1` writes through the pointer. `v.len` is `i32` and is the number of elements. A local `var empty []i32` is uninitialized. It does not store a length of 0. A file-scope slice with no initializer is zero, the same as any other global.

`a[i:j]` takes the elements from `i` up to, but not including, `j`. The base is an array or a slice. The result is `[]T`. A range of `type Row []i32` stays `Row`. A range of `[N]T`, including `type Pair [2]i32`, is the plain slice `[]T`. Both bounds have type `i32`. A missing low bound is `0`. A missing high bound is the length, so `a[i:]` is `a[i:a.len]`, `a[:j]` is `a[0:j]`, and `a[:]` is the whole slice. The length is `j - i`. There is no bounds check. The result is not an lvalue: `row[0:2] = row`, `row[1:] = row`, and `&row[0:2]` are reported as `not an lvalue`. A pointer, including an array parameter, is reported as `type mismatch`. A range whose element is an array is reported as `not supported yet`. `a[]` is reported as `expected an expression`.

A field of a tlc struct may be a slice, written `items []i32`. `sizeof` of `{ n i32; items []i32; }` is 24. The same field in `extern(abi="c")` is reported as `not supported yet`. A tlc union arm that is not a struct is reported as `type mismatch`.

`sizeof v` is 16, the same as `sizeof([]T)`. `typeof([]i32).name()` spells `[]i32`.

`i32[]` is reported as `type mismatch`. `*[]T`, `[][]T`, `[N][]T`, and `const v []i32` are reported as `not supported yet`. An array parameter is already a pointer. `return row` from `func open(row [4]i32) []i32` is reported as `return type: expected []i32, found *i32`. `row[0:2]` in that function is reported as `type mismatch`. A file-scope slice is initialized from the name of an array, another slice, a string literal, or a call. `var view = pair()` and `var view []i32 = pair()` run `pair` before `main` when that call is the whole initializer. `var text = "hi"` and `const label = "hi"` store that slice in the binary. An array literal, `a[i:j]`, `a[i:]`, `a[:j]`, or `a[:]` is reported as `global initializer is not a constant`.

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

## Cost

A slice is a pointer and an `i32` length, 16 bytes. Building one from a local array is 1 `leaq` of the first element and 1 `movl` of the length, after the array's own stores. `var text = "hi"` is 1 `leaq`, 1 `movq` of the address, and 1 `movl` of the length 2.

A file-scope `var view = pair()`, when `pair` returns `numbers[0:2]` and `numbers` is a global `[2]i32`, executes 40 instructions in `tlc.init` on the path where the trace holds fewer than 32 frames. The pointer and the length are both stored. `pair` is 3 instructions: the address, the length 2, and `retq`. `return view.len` is 2 loads and `retq`. One load reads the pointer and one reads the length.

`v[0]` on a slice parameter loads the pointer and loads the `i32` at that address: 2 instructions after the slice has been stored. `v.len` is 1 load of the length. `v[i]` with a variable `i` sign-extends `i` and uses a scaled load. The function `return v[i]` is 10 instructions including the three parameter stores and `retq`. One of those instructions sign-extends `i` a second time.

`v[1:3]` builds a new header: add 4 to the pointer, store it, and store the length 2. That is 3 instructions after the original slice is in hand. The length is `j - i` when the bounds are not constants. There is no bounds check, so the index adds no compare and no branch.

`v[:]` copies that header: 1 load of the pointer and 1 load of the length. `v[:2]` is 1 load of the pointer and 1 `movl` of the length 2. `v[1:]` reloads the length, reloads the pointer, adds 4, and decrements the length: 4 instructions. On a local `[4]i32`, `row[:]` is 1 `leaq` of the first element and 1 `movl` of the length 4, after the array's own stores. `row[:2]` is that `leaq` and 1 `movl` of the length 2. `row[1:]` is 1 `leaq` of the second element and 1 `movl` of the length 3.

A value method on a slice alias is a traced call. The callee is the method body. [`string_view.equals`](../std/string.md) measures that shape. The caller records a trace frame, 36 instructions when fewer than 32 frames are stored, plus the copies of the two slice headers.

[Index](../README.md) · [Previous: sizeof](sizeof.md) · [Next: The Type type](type.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
