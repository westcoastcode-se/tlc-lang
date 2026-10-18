# Structs and arrays

[Index](../README.md) · [Previous: typeof as a result](typeof.md) · [Next: Tests](test.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

Structs are passed and returned by value. An array parameter or result names its length, as in `a [4]i32` and `[N]T`. The parameter becomes a pointer. A function may return an array. `return out` returns a local array. `return give()` returns the array from that call. `return grid[1]` returns that row. `return [_]i32{4, 6}` returns that list. `return {4, 6}` returns that list. `return {{1, 2}, {3, 4}}` returns that array. `return {a, b}` copies those rows when `a` and `b` are `[2]i32` and the result is `[2][2]i32`. `return {inner}` when the result is `[2]i32` is reported as `type mismatch`. A different count is reported as `type mismatch`. The call decays to a pointer, so `repeat[4](2)[0]` reads the first element. `var copy [2]i32 = pair()` copies that temporary into a new array. A file-scope `var copy [2]i32 = row` copies that array into the binary. `var from_call [2]i32 = give()` and `var from_call [2]i32 = unsafe give()` run `give` before `main`. A slice parameter or result is `[]T`. It is passed and returned as the slice. It does not become a pointer. `row[i:j]` on an array produces `[]T`. An array parameter is already a pointer. `return row` when the result is `[2]i32` copies those elements. `return grid` when `grid` is a parameter `[2][2]i32` and the result is `[2][2]i32` copies both rows. `return grid[1]` copies that row. `return {a, b}` copies those parameter rows. `var copy [2]i32 = row` copies those elements. `var copy = row` stays `*i32`. `return row` from `func open(row [4]i32) []i32` builds the slice header that `row[:]` builds. `var copy = row` stays `*i32`. `row[0:2]` in that function is `[]i32`. `row[i:]`, `row[:j]`, `row[:]`, and `row.len` use the length from `[4]`.

```c
type Point struct {
    x i32;
    y i32;
};

func sum(p Point) i32 {
    return p.x + p.y;
}

func pair() [2]i32 {
    var out [2]i32;
    out[0] = 40;
    out[1] = 2;
    return out;
}

func head(a [2]i32) i32 {
    return a[0];
}

func main() i32 {
    var p Point;
    p.x = 20;
    p.y = 22;
    return sum(p) + head(pair());
}
```

## Cost

Passing a struct of two `i32` fields copies them into the argument registers: 2 `movl`, then the traced call. The callee stores both and can add them in 5 instructions, the same as two `i32` parameters.

An array parameter is 1 `leaq` of the first element at the call. The callee's `return a[0]` is 4 instructions. `return a[0:2]` is 4: the pointer store, 1 reload, 1 `movl` of the length 2, and `retq`. `return a` where the result is `[]i32` is those same 4, with the length 4. `return a.len` is 3: the store, 1 `movl` of the length, and `retq`. Returning a local `[2]i32` is 5 instructions in the callee: the elements are stored and returned in `%eax` and `%edx`. The local is not zeroed first. The caller receives a pointer to that temporary and pays the traced call. `var copy [2]i32 = pair()` pays that traced call. After `callq`, 1 `movl` keeps the low element and 2 `movl` store the pair. The copy into `copy` is 8 `movl`: the loaded pair is stored in a temporary and stored again. `return copy[1]` is 1 `movl`, then `addq` and `retq`. `return give()` pays that traced call. After `callq`, 1 `movl` keeps the low element and 2 `movl` store the pair. Returning that pair is 4 `movl` into a temporary and 2 `movl` into `%eax` and `%edx`, then `addq` and `retq`. On the path where the trace holds fewer than 32 frames, that function is 42 instructions. A file-scope `var copy [2]i32 = row` keeps both elements in the binary and adds no instruction. `return copy[1]` is 1 `movl` and `retq`. `var from_call [2]i32 = give()` runs before `main`. The call and the stores run in `tlc.init`: 44 instructions on the path where the trace holds fewer than 32 frames. `return from_call[0]` is 1 `movl` and `retq`. `give`, returning a local `[2]i32`, is 5 instructions. `unsafe give()` is that same `tlc.init`. `return grid[1]` is 2 `movl` into `%eax` and `%edx`, then `retq`, after `grid` is stored. `return [_]i32{4, 6}` is the same 5 as returning a local. `return {4, 6}` is those same 5. `return {{1, 2}, {3, 4}}` is 14 instructions: four `movl` store the elements, four `movl` reload them, four `movl` store them through the return pointer, one `movq` copies that pointer, and `retq`. The array is not zeroed first. Returning a local `[2][2]i32` of that list is those same 14. `return {a, b}` when `a` and `b` are `[2]i32` is 22 instructions: one `movq` copies the return pointer, four `movl` store `a` and `b`, eight `movl` copy those rows into a temporary, four `movl` reload that temporary, four `movl` store them through the return pointer, and `retq`. The array is not zeroed first. `return row` when `row` is a parameter `[2]i32` is 5 instructions: the pointer store, one reload, two `movl` into `%eax` and `%edx`, and `retq`. `return grid[1]` when `grid` is a parameter `[2][2]i32` is 5: the pointer store, one reload, and two `movl` of that row. `return grid` for that parameter is 12: one `movq` copies the return pointer, the source pointer is stored and reloaded, four `movl` load the elements, four `movl` store them through the return pointer, and `retq`. The array is not zeroed first. `return grid[1][0]` is 4: the pointer store, one reload, one `movl` of that element, and `retq`. `var copy [2]i32 = row` when `row` is that parameter is 5 instructions after the pointer is stored: one reload, two `movl` loads, and two `movl` stores. The slot is not zeroed first. `return copy[1]` is 1 `movl` and `retq`. `return {a, b}` when `a` and `b` are parameter `[2]i32` is 22 instructions: one `movq` copies the return pointer, two `movq` store the pointers, each row is one reload and four `movl`, four `movl` reload that temporary, four `movl` store them through the return pointer, and `retq`. The array is not zeroed first.

A slice parameter is the pointer and the length, passed in two registers. It is not decayed to a bare pointer.

[Index](../README.md) · [Previous: typeof as a result](typeof.md) · [Next: Tests](test.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
