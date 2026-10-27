# Structs and arrays

[Index](../README.md) · [Previous: typeof is not a result](typeof.md) · [Next: Tests](test.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes how a function passes and returns a struct, an array, or a slice.

## Structs

Structs are passed and returned by value.

## Array parameters and results

An array parameter or result names its length, as in `a [4]i32` and `[N]T`. The parameter becomes a pointer. A function may return an array.

`return out` returns a local array, `return give()` returns the array from that call, and `return grid[1]` returns that row.

`return [_]i32{4, 6}` returns that list, and `return {4, 6}` returns that list. `return {{1, 2}, {3, 4}}` returns that array.

`return {a, b}` copies those rows when `a` and `b` are `[2]i32` and the result is `[2][2]i32`.

The compiler reports `type mismatch` for `return {inner}` when the result is `[2]i32`, and for a different count.

Indexing the call reads an element, so `repeat[4](2)[0]` is the first element.

## Copies of an array

`var copy = pair()` and `var copy [2]i32 = pair()` copy that temporary into a new array.

A file-scope `var copy = row` and `var copy [2]i32 = row` copy that array into the binary.

`var from_call [2]i32 = give()` and `var from_call [2]i32 = unsafe give()` run `give` before `main`.

## Slices

A slice parameter or result is `[]T`. It is passed and returned as the slice, and it does not become a pointer.

`row[i:j]` on an array produces `[]T`.

## An array parameter is already a pointer

An array parameter is already a pointer. `return row` when the result is `[2]i32` copies those elements.

`return grid` when `grid` is a parameter `[2][2]i32` and the result is `[2][2]i32` copies both rows. `return grid[1]` copies that row, and `return {a, b}` copies those parameter rows.

`var copy [2]i32 = row` copies those elements. `var copy = row` stays `*i32`.

`return row` from `func open(row [4]i32) []i32` builds the slice header that `row[:]` builds. `var copy = row` stays `*i32`, and `row[0:2]` in that function is `[]i32`.

`row[i:]`, `row[:j]`, `row[:]`, and `row.len` use the length from `[4]`.

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

```asm
main:
# trace frame for the call, then the matching pop
callq sum                        # call sum
retq                             # return

sum:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

[Index](../README.md) · [Previous: typeof is not a result](typeof.md) · [Next: Tests](test.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
