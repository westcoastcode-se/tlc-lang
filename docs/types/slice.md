# Slices

[Index](../README.md) · [Previous: sizeof](sizeof.md) · [Next: The Type type](type.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A slice is a pointer and a length over contiguous values of one type.

## The type

`[]T` is a slice of contiguous `T` values. `v[i] = n` stores an element. `[]const T` does not.

Brackets come before the element type, as in `[]i32` and `func head(a []i32) []i32`. `[]const u8` is that slice with a const element.

A string literal has that type. `var text = "hi"` stores the address and the length 2, which is 1 `leaq`, 1 `movq`, and 1 `movl`.

A constant index of the literal is that byte while compiling, so `"hi"[1]` is 105 and adds no load. A constant range of that literal is those bytes, so `"hi"[1:][0]` is 105 and adds no load.

`"hi"[1:]` is the tail, without storing the literal first.

The literal, and any other `[]const u8`, converts to `*const u8` when that pointer is written or expected. That pointer is what a C library takes. The length is dropped.

`[N]T` converts to `[]T` and to `[]const T`. A tuple does not: `row[:]` on `[N]Duo` or `[N][M]Duo` is `type mismatch`.

A const array converts to `[]const T`, and `row[:]` is that slice. `[]const T` does not convert to `[]T`. `[]i32` does not convert to `*i32`.

`type Row []i32` names that slice. `[]i32` and `[N]i32` convert to `Row`. `Row` does not convert to `[]i32`, and it does not convert to another slice alias.

`type Pair [2]i32` does not convert to `[]i32`.

`Row` can have value methods, written `func Row::first(self this) i32`. `self *this` on that alias is a pointer to the slice.

The library type [`string_view`](../std/string.md) is `type string_view []const u8` in `lib/tlc/std/string`.

`var v = row` keeps the array. It does not infer a slice.

## Elements and length

`v[i]` reads one element. The index has type `i32`.

`v[i] = 1` stores that element when the slice is `[]i32`, and the array sees the store. `var v []const i32` and `const v []i32` report `v[i] = 1` as `type mismatch`.

`v[i].x` reads a field. `v[i].x = 1` stores that field. A const element reports that store as `type mismatch`.

On a `[]i32` parameter, `v[0] = 8` then `return v[0]` is 8 instructions. Those are the two parameter stores, two reloads of the pointer, one `movl` of 8, one reload of the length, one load of that `i32`, and `retq`.

When the element is `*T`, `*v[i] = 1` writes through the pointer.

`v.len` is `i32` and is the number of elements. `v.len()` is that same length.

A local `var empty []i32` is a zero header, the same as a file-scope slice. `return empty.len` is 6 instructions: `movq` of 0 for the pointer, `movl` of 0 for the length, `movl` of 0 for the header padding, a reload of the length, a reload of the pointer, and `retq`.

A file-scope slice with no initializer is zero, the same as any other global.

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

```asm
main:
# trace frame for the call, then the matching pop
callq head                       # call head
retq                             # return

head:
movq %rdi, -16(%rsp)             # move
movl %esi, -8(%rsp)              # move
movl -8(%rsp), %eax              # move
movq -16(%rsp), %rax             # move
movl (%rax), %eax                # move
retq                             # return
```

## A slice of slices

`[][]T` is a slice of `[]T` headers. `[N][]T` is that many headers.

`v[i]` has type `[]T`, and `v[i][j]` reads one `T`. `v.len` is the number of headers. `v[i].len` is the number of elements in that header.

`v[i] = other` stores one header. `v[i][j] = 1` stores one element. `[][]const T` does not store that element.

The array is the same storage. `rows[i] = other` copies one header, and a `[][]T` view of `rows` sees that store.

`var rows = [_][]i32{a[:], b[:]}` keeps `[2][]i32`. `var view [][]i32 = rows` converts that array to `[][]T`. `var copy = rows` copies that array. A slice is `rows[:]`.

A different length is reported as `type mismatch`.

`type Row []i32` makes `[]Row` and `[N]Row` the same types as `[][]i32` and `[N][]i32`. `[][]const u8` is that slice with a const element. `const` stays in front of the innermost name.

A range of `[N][]T` is `[][]T`. A range of `[][]T` stays `[][]T`.

`sizeof([][]i32)` is 16 and `sizeof([2][]i32)` is 32.

A parameter `[][]T` stays a slice. A parameter `[N][]T` decays to a pointer to one header. `rows.len` is `N`, and `rows[i:j]` is `[][]T`.

A field may be `items [][]i32`. A file-scope `var rows = [_][]i32{a[:], b[:]}` stores those headers in the binary. `rows[0][1]` reads one element.

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

```asm
sum:
movq %rdi, -16(%rsp)             # move
movl %esi, -8(%rsp)              # move
movq -16(%rsp), %rdx             # move
movq (%rdx), %rax                # move
movq 16(%rdx), %rcx              # move
movl (%rax), %eax                # move
movl 24(%rdx), %edx              # move
addl (%rcx), %eax                # add
retq                             # return
```

## Ranges

`a[i:j]` takes the elements from `i` up to, but not including, `j`. The base is an array or a slice. The result is `[]T`.

A range of `type Row []i32` stays `Row`. A range of `[N]T`, including `type Pair [2]i32`, is the plain slice `[]T`.

Both bounds have type `i32`. A missing low bound is `0`. A missing high bound is the length, so `a[i:]` is `a[i:a.len]`, `a[:j]` is `a[0:j]`, and `a[:]` is the whole slice.

The length is `j - i`.

> **Warning:** There is no bounds check.

The result is not an lvalue. `row[0:2] = row`, `row[1:] = row`, and `&row[0:2]` are reported as `not an lvalue`.

`p[0:2]`, `p[i:]`, `p[:j]`, and `p[:]` on `*[N]T` are `[]T`, and so are `(*p)[0:2]`, `(*p)[i:]`, `(*p)[:j]`, and `(*p)[:]`.

`*[N][M]T` gives `[][M]T`. `*[N][]T` gives `[][]T`. `(&row)[1:3]` and `buf[1:3]`, when `buf` is `new [_]T`, are that slice.

`*p` is the array, so `var s []i32 = *p` is `[]i32`.

A range of `[N]Duo`, of `[N][M]Duo`, of a parameter `[N]Duo`, and of `*[N]Duo` is `type mismatch`. `[]Duo` and `[][N]Duo` are `not supported yet`.

`*i32` and a still-optional `?*[N]T` are reported as `type mismatch`.

`return p` where `[]T` is expected builds the slice header that `p[:]` builds. `var s []i32 = p` is that slice. `p[:]` is the slice.

Reading `p[1:3]` on `*const [4]i32` is allowed. That range is `[]const i32`.

## Length through a pointer

`p.len` and `(*p).len` on `*[N]T` are that `N`. `(&row).len` and `buf.len`, when `buf` is `new [_]T`, are that length.

`p.len` on `*[N][M]T`, on `*[N][]T`, and on `*[N][M][K]T` is the outer `N`. `p[0].len` is the inner length.

Reading `p.len` on `*const [N]T` is allowed. A field `items *[N]T` has that length. `type Quad [N]T` makes `p.len` on `*Quad` that `N`.

`*i32`, `*[]T`, `**[N]T`, `new [n]T`, and a still-optional `?*[N]T` are reported as `type mismatch`. After `if (opt == null) return`, `opt.len` is `N`.

Assigning `p.len`, or taking its address, is reported as `not an lvalue`.

At file scope, `const N = (&row).len` is that length and can be an array length. `const p *[N]T = &row` is reported as `type mismatch`.

## A parameter array

`row[0:2]`, `row[i:]`, `row[:j]`, and `row[:]` on a parameter `[N]T` are `[]T`. `return row` where `[]T` is expected is that slice.

`var s []i32 = row` is that slice. `var copy = row` stays `*i32`. `row.len` is that `N`.

A parameter `[N][M]T` gives `[][M]T` for a range of the outer dimension. A range of `[_][2]i32` is a slice of `[2]i32`.

`var view = grid[0:1]` keeps that slice. `view.len` is the number of rows. `view[0].len` is 2. `view[0][1]` reads one element.

`view[0]` and `grid[1]` are `[2]i32`, so each converts to `[]i32`, and `grid[1][0:2]` is that slice. `grid[1:]`, `grid[:1]`, and `grid[:]` are that slice.

A range of `view` stays a slice of `[2]i32`. `view[0][1] = 9` stores that element. `[]const [2]i32` reports that store as `type mismatch`.

`a[]` is reported as `expected an expression`.

## A slice field

A field of a tlc struct may be a slice, written `items []i32`. `sizeof` of `{ n i32; items []i32; }` is 24.

The element may be the struct that contains the field, `kids []Node`, and it may be a struct defined later, `kids []Row` before `type Row`.

`[][]Node`, `[2][]Node`, and `[][2]Node` are that same header, or an array of headers. `sizeof` of `{ value i32; kids []Node; }` is 24.

`next Node` and `kids [2]Node` inside that struct are reported as `recursive struct main.Node -> main.Node` when the file is `main.tlc`.

A sized array of a struct defined later, `kids [2]Row` before `type Row`, stores that value. `kids *[2]Node` is a pointer to an array of that struct. `kids *[]Node` is a pointer to a slice of it.

`kids *[2]Row` and `kids *[]Row` before `type Row` are that same pointer.

The same slice field in `extern(abi="c")` is reported as `not supported yet`. A tlc union arm that is not a struct is reported as `type mismatch`.

`sizeof v` is 16, the same as `sizeof([]T)`. `typeof([]i32).name()` spells `[]i32`.

## Other spellings

`i32[]` is reported as `type mismatch`. `*[]T` is a pointer to that header. `&view` has that type when `view` is a slice.

`[][][]T` is a slice whose element is `[][]T`. `[N][M]T` is an array of `[M]T`. `[][N]T` is a slice of `[N]T`.

`type Grid [][]i32` then `[]Grid` is `[][][]i32`. `Vec[[][]i32]` is that specialization. A field `T` on `Hold[T]` keeps those shells, so `Hold[[][]i32]` stores `[][]i32`.

## File-scope slices

`const v []i32 = row` stores a slice of that file-scope array. `const again []i32 = v` copies that header.

A slice alias stays that alias, as in `const named Row = row`. `v[0]` reads one element. `v.len` is the length.

Assigning a `const` slice is reported as `not an lvalue`. `&view` on a local slice is `*[]T`.

A constant range is that header too, as in `const v []i32 = row[0:2]`. A missing bound is constant, so `const v []i32 = row[:]` is the whole array.

`const v = row[:]` is `[]i32` when `row` is mutable. A const array makes that slice `[]const i32`.

An array literal, a call, or a bound that does not fold is reported as `global initializer is not a constant`.

`const v = row` copies that array. `const v []i32 = row` stores the slice.

An array parameter is already a pointer. `return row` from `func open(row [4]i32) []i32` builds the slice header that `row[:]` builds. `var copy = row` stays `*i32`. `row[0:2]` in that function is `[]i32`, and `row.len` is 4.

A file-scope `var view []i32 = p`, when `p` is `*[N]T`, is reported as `global initializer is not a constant`.

A file-scope slice is initialized from the name of an array, a slice whose own initializer is one of these constants, a string literal, a constant range of one of those, or a call.

`var mid = numbers[1:3]`, `var all = numbers[:]`, `var tail = numbers[2:]`, `var head = numbers[:2]`, and `var text = "hi"[1:]` store that header in the binary.

A bound may be a compile-time integer, as in `numbers[N:numbers.len]`. A range of that slice, `all[1:3]`, is the same kind of header.

`var view = pair()` and `var view []i32 = pair()` run `pair` before `main` when that call is the whole initializer. `var text = "hi"` and `const label = "hi"` store that slice in the binary.

An array literal is reported as `global initializer is not a constant`. A bound that does not fold, and a call that is not the whole initializer, such as `pair()[0:2]`, are reported as `global initializer is not a constant`.

[Index](../README.md) · [Previous: sizeof](sizeof.md) · [Next: The Type type](type.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
