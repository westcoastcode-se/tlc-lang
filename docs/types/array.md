# Arrays

[Index](../README.md) · [Previous: Unions](union.md) · [Next: Enums](enum.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

An array stores a fixed number of elements of one type.

## Element lists

An array length is a decimal literal up to the `i64` maximum. `0` is a length.

A `const` binding whose initializer folds to a non-negative `i32` is a length too.

Inside a `comptime func`, a parameter is that length when the call substitutes it. `var buf [n]i32` then `buf.len` is `n`. `comptime const k = n` and `value := n` are the same length. A negative value is `array length must be >= 0`.

`[_]i32{10, 20, 12}` writes the values. `_` stands for the length, which is the number of values, so the type is `[3]i32`.

A file-scope array takes the same lists, including a struct element. `var named [3]i32 = {10, 4, 1}`, `var row = [_]i32{10, 20, 12}`, and `var origin = [_]Point{Point{.x = 1, .y = 2}}` are those lists.

## Size limits

A type whose size in bytes does not fit in `i64` is reported as `array too large` where it is written. `[4611686018427387904][4]u8` is 2^64 bytes. A struct whose fields each fit but whose sum does not is reported at the struct's name. A pointer to such a type is reported the same way.

A local is at most 1 MiB, 1048576 bytes. A larger local is reported as `array too large for the stack: use a global or new`. The limit counts the whole local, so a struct or a tuple that holds a large array counts as one local. `copy := screen` and `for (var row : rows)` make locals of that size too. A local in a `comptime func` has the same limit.

A file-scope array and a `new` array have no such limit. A parameter is not counted.

On x86-64, a file-scope variable of 64 KiB or more is placed in the large data section and is addressed with 64 bits. Code reaches the other globals within 2 GiB, so a variable declared after a 2 GiB array still links.

A thread stack is usually 8 MiB. Locals that fit one at a time can still run past it in deep recursion. That is `panic: stack overflow`, as described in [panic](../builtin/panic.md#stack-overflow).

## Shorter lists

A list may be shorter than a written length. `var row [3]i32 = {10, 20}` names two elements.

The element the list does not name stays uninitialized. The compiler reports a read of it, `return {10, 20}` when the result is `[3]i32`, and `const row [3]i32 = {10, 20}` as `uninitialized`.

`0...` at the end of the list stores 0 in every element the list does not name. `var row [4]i32 = {10, 20, 0...}` and `return {10, 20, 0...}` when the result is `[4]i32` are that form.

A complete list stores only the values it names.

A file-scope array with no initializer is zero.

A file-scope list that ends with `0...` stores 0 in every element the list does not name. A file-scope short list without `0...` stores 0 there too. `var row [4]i32 = {1, 2}` is 1, 2, 0, 0.

A file-scope element built from literals and `const` names folds: `const K = 4` then `var row [3]i32 = {K, K * 2, -K}` is 4, 8, -4.

An element that is a slice is a header in the binary: `var w [3][]const u8 = {label, label[1:3], "z"}`, `var b [2][]i32 = {nums[:], nums[1:]}`, and a struct field `{.name = "one", .n = 1}` in `var table [2]Entry`. A store through `b[0]` changes `nums`.

A long run of those zeros costs the compiler nothing. `var arr [2147483647]i32 = {1, 2, 3}` is built as the three elements and one zero tail, in the compiler and in a `comptime` call alike. The binary still holds the whole array, zeros included, the same as a C compiler's. A file-scope array with no initializer, or one that is all zero, takes no space in the binary.

The same holds for an array field of a file-scope struct, `var h = H{.raw = {1, 0...}}`, and for a field's default value, `raw [100000000]u8 = {7, 8, 0...}`.

A file-scope `var` array of 4096 or more structs whose fields have defaults, `var pts [100000000]Point;`, is zero in the binary. The defaults are written into each element when the program starts, before any initializer that runs before `main`, and when a `comptime` call starts. A struct whose defaults hold such an array, `World` with `pts [100000000]Point`, also through a nested struct field, is written the same way: `var w World;` and `var worlds [10]World;` are zero in the binary, and each element is written field by field at start. A literal, `var w = World{.n = 1}`, fills each such field it leaves out, also inside a nested literal such as `Outer{.world = World{.tail = 5}}`, and so does each literal of a list, `var listed [3]World = {World{.n = 6}}`. A field the literal names keeps that value, and an element past the list is zero. A list of two or more dimensions, `var grid [2][2]World = {{World{.n = 3}}}`, is filled the same way. Such a `var` stays zero in the binary even with a literal: the literal's other bytes, `n = 1` among them, are written at start too. A `const` of that struct is a constant, zeros included, in the binary.

A copy of such a global is written at start as well, after that global and after any copy into it: `var c = w`, a copy of that copy, an element `{w, w}`, a named field `Pair{.left = w}`, a part `var y = w.pts[4999].y` or `var n = w.n`, and a value computed from one, `var m = w.n + 1`. The value is the global's start value, the same value a constant would hold, and a `comptime` call reads it the same way. A smaller array of structs without such a field is a constant with every element's defaults. A local array of 64 or more structs with defaults is filled by one loop.

The compiler reports a file-scope shorter list, without `0...`, as `global initializer is not a constant` when code is emitted. `--check` accepts that list.

## Arrays of arrays

An element that is an array is another brace list, or a value of that array. `var row [2][3]i32 = {{1, 2, 3}, {4, 5, 6}}` stores those values, and `row[1][2]` is 6.

`var inner [2]i32 = {1, 2}` then `var row = [_][2]i32{inner}` copies `inner`. `{a, b}` copies both rows when `a` and `b` are `[2]i32`.

`grid[1]` and `give()` copy that row the same way. A later store into the source does not change the copy.

A different length, `[_][3]i32{inner}`, is reported as `type mismatch: expected [3]i32, found [2]i32`.

A parameter `[2]i32` in that list copies the array. A later store into the parameter does not change the copy.

A different length, `[_][3]i32{row}`, is reported as `type mismatch: expected [3]i32, found *i32`.

`var grid = [_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}` is one outer element of `[2][3]i32`, and `grid[0][1][2]` is 6. `{[_]i32{1, 2, 3}, [_]i32{4, 5, 6}}` writes the same rows.

A different count is reported as `type mismatch`. `[_][]i32{{1, 2}, {3}}` is reported as `type mismatch`.

## Passing the address

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

```asm
main:
# trace frame for the call, then the matching pop
callq add4                       # call add4
retq                             # return

add4:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
movq -8(%rsp), %rcx              # move
addl 4(%rcx), %eax               # add
movq -8(%rsp), %rcx              # move
addl 8(%rcx), %eax               # add
movq -8(%rsp), %rcx              # move
addl 12(%rcx), %eax              # add
retq                             # return
```

`add4(a)` passes a pointer to the first element because the parameter is `*i32`. `*i32(a)` is that same address.

A parameter `[N]T` takes that address without a cast.

> **Warning:** The call does not initialize the elements. `take(a)` then `return a[0]` is `uninitialized` when `a` was unset.

`fill(&a)` when the parameter is `*[N]T` initializes the array. `peek(&a)` when the parameter is `*const [N]T` is `uninitialized` until the elements are stored.

`&a[0]` passed to `*T` initializes that element.

A string literal is `[]const u8`. It can be passed to `[]const u8` and to `*const u8`. It cannot be passed to `*u8` or `[]u8`.

The index is any integer and is used as an `i64`.

`new [n]i32` reserves `n` elements and yields `[]i32`. `buf[i] = 1` writes an element. `buf[i]` is rejected at run time when `i` is outside `n`.

`new [n]Point{{.x = 1}...}` stores that value in every element. `new [_]Point{{.x = 1}, {.y = 2}}` is `*[2]Point`. See [Builtin](../builtin/allocator.md).

## A slice of the array

`[]T` is a slice of an array. `var v []i32 = row` takes a slice of `[N]i32`.

`v[i] = 1` stores an element, and `row` sees that store. `var v []const i32 = row` adds `const`. `v[i] = 1` is then reported as `type mismatch`.

`row[i:j]` takes the elements from `i` up to, but not including, `j`, and so does `v[i:j]`. A missing low bound is `0`. A missing high bound is the length.

`row[i:]`, `row[:j]`, and `row[:]` are those slices.

A range of `[N]Duo` is `[]Duo`, and of `[N][M]Duo` is `[][M]Duo`. A range of `[_][2]i32` is a slice of `[2]i32`. The length counts rows.

`v[i]` reads an element. `v.len` is `i64`.

A constant index outside a sized array is `index out of range` when the program compiles. The length is in the type, so that holds for a `const`, a `var`, a global, a field, a parameter `[N]T`, and `p[i]` on `*[N]T`. A read, a store, `row[i] += 1`, and `&row[i]` are checked. `const k = 2` then `row[k]` on `[2]i32` is that error. An index that is a `var` is checked when the program runs, and one outside the length panics with `index out of range`.

The check also covers an ordinary `if` whose condition is constant. In a generic body, an arm that only some lengths compile goes in `comptime if (N > 2)`. A `comptime` parameter length, `var r [n]i32`, is checked in each call's copy, with the call chain below the error.

`row.len` on a sized array is `i64`, the number of elements, with the same spelling. `row.len()` is that same length. An empty array has length 0.

`comptime const row = [_]i32{40, 2}` then `row[1]` is 2. `assert(row[1] == 2, "x")` folds.

An index outside that array is `index out of range`. `assert(row[2] == 0, "x")` is that error.

A `var` index still loads. `assert` on that index is `not supported yet`.

A file-scope `const row = [_]i32{40, 2}` is not a comptime array. `assert(row[1] == 2, "x")` is `not supported yet`. The element is `comptime const row`.

The length is a compile-time integer. At file scope, `const N = row.len` can be an array length.

A `const` inside a function whose initializer folds to a non-negative `i32` is that length too.

Assigning `row.len`, or taking its address, is reported as `not an lvalue`.

`sizeof([]T)` is 16.

A slice parameter stays a slice. `a.len` and `a.len()` in `func take(a [3]i32)` are 3, the length that was written. The parameter is still a pointer.

`return a` where `[]T` is expected builds the slice header that `a[:]` builds. `var s []i32 = a` is that slice. `var copy = a` keeps that length, so `copy[0]` is the same element as `a[0]`.

`return p` when `p` is `*[4]i32` builds that same header. `*i32` and a still-optional `?*[4]i32` are reported as `type mismatch`.

`return a` where `[]const i32` is expected adds `const` and builds that slice. `[]const i32` does not convert to `[]i32`.

`p[:]` is the slice. `p.len` and `(*p).len` on `*[N]T` are that `N`. `(&row).len` is that length.

`p.len` on `*[N][M]T` is `N`, and `p[0].len` is `M`.

## A named length

At file scope, `const N = row.len` is an array length. `const M = 2 + 2` and `const K = 4` are too.

```c
const row = [_]i32{1, 2, 3};
const N = row.len;

func main() i32 {
    var out [N]i32;
    return out.len;
}
```

```asm
main:
movl $3, %eax                    # out.len
retq                             # return
```

Inside a function, `const n = 4` then `var row [n]i32` is `[4]i32`.

A length is any expression that folds: `[sizeof(Pair) + 1]u8`, `[N * 2 + 1]u8`, `[twice(N) + 1]i32`, and `[(1 + 1)][N + 1]i32`. `type G<N i32> struct { buf [N + 1]u8; }` and `func pad<N i32>()` with `[N * 2 + 1]i32` substitute `N` first. A length that reads a `var` or a `bool` is `not supported yet`. A file-scope `const N = 4`, with or without `comptime`, is a field length, `type Buf struct { head [N]u8; }`, and so is `const M = N * 2`. `comptime const N = sizeof(Point)` is a field length once `Point` is complete. `type Holder struct { buf [N]u8; }` is 8 bytes when `Point` holds two `i32` fields. `buf [sizeof(Point)]u8` is that same length. Inside `Point`, `buf [sizeof(Point)]u8` is the size of the other fields. `sizeof(Point)` after that struct is complete includes `buf`.

`comptime const wide = twice(10)` is a field length when `twice` is a `comptime func` in the same package whose body is one `return` of an `i32` and whose parameters are `i32`: `type Buffer struct { head [wide]u8; }`. An argument may be a folded constant or another such call, `add(twice(1), base + 3)`. A body of `i32` statements also runs that early: locals of `i32` and `bool`, arrays of `i32` such as `var row [5]i32 = {3, 1, 4, 1, 5}` with `row[i]`, `row[i] += n`, and `row.len`, `=` and `+=`, `++`, `if`, `while`, `for`, `break`, `continue`, `return`, and a call of another such `comptime func`, itself included, as in `fib(n - 1) + fib(n - 2)`. The call, including every function it calls, stops after 100000 steps, and calls nest 64 deep. A call written in the field length runs the same way: `buf [twice(2) + 1]u8` and `rows [sum_to(K)]u8`. A body that uses another type, such as a struct, runs after the fields are resolved, so that field is `not supported yet`. Inside a function, `var row [wide]i32` takes any `comptime const` that folds.

```c
func main() i32 {
    const n = 4;
    var row [n]i32;
    return row.len;
}
```

```asm
main:
movl $4, %eax                    # row.len
retq                             # return
```

`return row.len` is 4.

`const m = n + 2` then `return row.len` is `movl $4, %eax` and `retq`.

`const n i32 = 4` is that same binding.

`return row.len` is those same two instructions.

`const n = 2` and `const m = 3` then `return grid.len + grid[0].len`, when `grid` is `[n][m]i32`, is `movl $5, %eax` and `retq`.

`var row = [_][n]i32{{1, 2}}` then `return row.len + row[0].len` is `movl $3, %eax` and `retq`.

`id<[n]i32>(row)` calls `main.id.spec.arr.2.i32` when the file is `main.tlc`.

A local `const` hides a file-scope `const` of the same name.

`return row.len` is `movl $2, %eax` and `retq` when that local value is `2`.

`const n = 0` is `[0]i32`.

`return 42 + row.len` is `movl $42, %eax` and `retq`.

A file-scope `const n = 4i8` then `var row [n]i32` has `row.len` of 4. `main` is `movl $4, %eax` and `retq`. `4u8`, `4i16`, and `4u16` are that length. `width<n>()` is `type mismatch`.

A `var` is `not supported yet`.

`var n const i32 = 4` is `not supported yet`.

An initializer that does not fold, such as `const n = give()`, is `not supported yet`.

A negative length, `const n = -1`, is `array length must be >= 0`.

`func ordinary(comptime n i32)` uses that parameter as the length. `var row [n]i32` then `row.len` is `n`. The call substitutes the integer. A length expression reads it too: `var row [n + 1]i32`, and the result `[n + 1]i32`, are that length once the call writes `n`.

`ordinary(2)` returns 42.

```c
func ordinary(comptime n i32) i32 {
    var row [n]i32;
    row[0] = 40;
    return row.len + row[0];
}

func main() i32 {
    return ordinary(2);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

A negative value is `array length must be >= 0`.

A `var` is not a length.

The result may be that array. `ordinary(1)[0]` is 40. Each call is its own copy. When the file is `main.tlc`, the symbol is `main.ordinary.spec.1`.

```c
func ordinary(comptime n i32) [n]i32 {
    var row [n]i32;
    row[0] = 40;
    return row;
}

func main() i32 {
    return ordinary(1)[0];
}
```

```asm
ordinary.spec.1:
pushq %rax                       # frame
movl $40, 4(%rsp)                # row[0]
movl 4(%rsp), %eax               # row
popq %rcx                        # release
retq                             # return

main:
# trace frame for the call, then the matching pop
callq ordinary.spec.1            # call ordinary
retq                             # return
```

A length whose other elements are not stored is `uninitialized`.

`return {40, 0...}` fills the rest. `var row = ordinary(2)` then `row[0] + row.len` is 42.

A negative value is `array length must be >= 0`.

A runtime parameter beside that length is the same array. `fill(1, 40)[0]` is 40. The copy is `main.fill.spec.1` when the file is `main.tlc`. `1` is not passed. `40` is.

```c
func fill(comptime n i32, value i32) [n]i32 {
    var row [n]i32;
    row[0] = value;
    return row;
}

func main() i32 {
    return fill(1, 40)[0];
}
```

```asm
fill.spec.1:
subq $24, %rsp                   # frame
movl %edi, 16(%rsp)              # value
movl 16(%rsp), %eax              # value
movl %eax, 20(%rsp)              # row[0]
movl 20(%rsp), %eax              # row
addq $24, %rsp                   # release
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # value
callq fill.spec.1                # call fill
retq                             # return
```

A length whose other elements are not stored is `uninitialized`.

`return {value, 0...}` fills the rest. `var row = fill(2, 40)` then `row[0] + row.len` is 42.

A negative value is `array length must be >= 0`.

## Length 0

`var row [0]i32` is an empty array. `sizeof(row)` is 0 and `row.len` is 0.

`comptime const N = 0` then `var again [N]i32` is that array.

`const n = 0` inside a function is that array too.

`comptime const N = empty.len` is too, when `empty` is `[_]i32{}`.

A file-scope array with no initializer is zero. `var file_row [0]i32` is an empty array.

A generic length that folds to 0 is the same array.

```c
func buf<M i32>() i32 {
    var local [M]i32;
    return sizeof(local);
}
```

`buf<0>()` is 0. `buf<2>()` is 8.

A negative length, `comptime const N = -1` then `[N]i32`, is `array length must be >= 0`.

`const n = -1` inside a function is that same error.

A parameter `[0]T` is an empty array. `row.len` is 0. An index of it is `index out of range`.

`new [0]i32` is an empty slice. `.len` is 0. A negative constant count is `type mismatch`.

```c
comptime const N = 0;
var file_row [0]i32;

func main() i32 {
    var row [0]i32;
    var again [N]i32;
    return 42 + row.len + again.len + file_row.len + i32(sizeof(row)) + i32(sizeof(file_row));
}
```

```asm
main:
movl $42, %eax                   # 42 + the lengths and sizes
retq                             # return
```

The program returns 42.

## Fields

A field may be a slice, `items []i32`. It is stored as the slice header. `sizeof` of `{ n i32; items []i32; }` is 24.

The element may be the struct being defined, `kids []Node`. `kids [2]Row` before `type Row` stores that struct.

`kids [2]Node` inside `Node` is reported as `recursive struct main.Node -> main.Node` when the file is `main.tlc`.

`kids *[2]Node` is a pointer to an array of that struct. `kids *[]Node` is a pointer to a slice of it. `kids *[2]Row` before `type Row` is that same pointer.

Assigning the field replaces that header. The same field in `extern(abi="c")` is reported as `not supported yet`.

### An array field

A field `[2][3]i32` indexes like a local array. `h.raw[1][2] = 6` stores that element and `return h.raw[1][2]` reads it.

`var row []i32 = h.raw[1]` is a slice of that row, and a later store through `h.raw` changes that slice.

`var line [3]i32 = h.raw[1]` and `var line = h.raw[1]` copy the row. A later store does not change the copy.

`var rows [][3]i32 = h.raw` is a slice of the rows. `h.raw[1][0][1]` on a field `[2][2][2]i32` is that element.

`p.raw[1][2]` when `p` is `*Holder` is the same element.

`h.raw[1] = line` copies that row into the field. A later store into `line` does not change `h.raw`. `p.raw[1] = line` when `p` is `*Holder` is that copy.

`h.raw[1] = {4, 5, 6}` stores that list. `h.raw = row` copies `[2]i32` into the field. A later store into `row` does not change `h.raw`.

`p.raw = row` when `p` is `*Holder` is that copy. `h.grid = grid` copies a field `[2][3]i32`. A different length is reported as `type mismatch`.

`h.raw = {4, 6}` stores that list in the field. `p.raw = {4, 6}` and `(*p).raw = {4, 6}` store the same list.

`h.grid = {{1, 2, 3}, {4, 5, 6}}` stores those rows. `h.raw = {give(), 6}` and `h.pts = {a, b}` store those elements. A later store into `a` does not change `h.pts`.

A short list stores only the named elements. Reading an omitted element is reported as `uninitialized`. `h.raw = {4, 0...}` stores 4 and zero in the rest. `var row [4]i32 = {0...}` stores zero in every element, and so does `var grid [2][3]i32 = {0...}`. `var row [4]i32 = {}` stores none. At file scope both are the zero array, the same as no initializer. An element that is a non-null pointer, `[2]*i32`, is `type mismatch` with `{0...}`.

### A struct value

`var h = Holder{.raw = {4, 6}}` stores that list in the field, then copies the struct into `h`. `new Holder{.raw = {4, 6}}` stores the list in the new value.

A file-scope `var h = Holder{.raw = {4, 6}}`, when `raw` is the only field, keeps the list in the binary.

`Holder{.raw = row}` copies that array. A later store into `row` does not change the field.

`Holder{.grid = {{1, 2, 3}, {4, 5, 6}}}` stores those rows. `Holder{.raw = {give(), 6}}` and `Holder{.pts = {a, b}}` store those elements. A later store into `a` does not change the field.

A short list in that value stores only the named elements. Reading an omitted element is reported as `uninitialized`. `Holder{.raw = {4, 0...}}` stores 4 and zero in the rest.

`Holder{.raw = {4, 6, 8}}` is reported as `type mismatch`.

A file-scope value is zero where nothing is written. `var h Holder = {.raw = {4}}` and `var h = Holder{.raw = {4}}` store 4 and zero in the rest, with or without `0...`. `var h Holder = {.n = 40, .raw = {2, 0...}}` stores those fields. A field the value leaves out is zero too.

`raw [2]i32 = {1, 2}` on the field is reported as `not supported yet`.

### Assigning the local array

`row = {4, 6}` stores that list in the local array. `row = [_]i32{3, 1}` stores that list.

A short list stores only the named elements. Reading an omitted element is reported as `uninitialized`. `row = {4, 0...}` stores 4 and zero in the rest.

`row = {{1, 0}, {0, 5}}` stores those rows. `pts = {a, b}` stores those elements. A later store into `a` does not change `pts`.

`row = {bump(), bump()}` runs the first call and stores that element before the second call runs.

Assigning a `const` array, or `[2]const i32`, is reported as `type mismatch`. A different length, `var copy [3]i32; copy = row`, is reported as `type mismatch`.

## Copies

`var row = [_]i32{10, 20}` has type `[2]i32`. A use that does not name another type copies that array, so `var copy = row` and `var copy [2]i32 = row` both store the elements in a new array.

A later store into `row` does not change `copy`. `copy = row` copies that array. A later store into `row` does not change `copy`.

A written `*i32` takes the address of the first element, so `var p *i32 = row` is that pointer. The cast `*i32(row)` is the same address.

`var p = row` copies the array. `var v []i32 = row` is a slice of the array. A slice is also `row[:]`.

`const held = [_]i32{8, 9}` copies the same way, `const frozen [2]i32 = held`. A different length, `var copy [3]i32 = row`, is reported as `type mismatch`.

### Through a parameter

A parameter `[2]i32` is a pointer. `var copy [2]i32 = row` copies those elements. A later store into the parameter does not change the copy.

`var copy = row` keeps that length. `copy[0]` is the same element as `row[0]`, so a store through `copy` changes the caller's array. `kept = row` copies those elements when `kept` is a local `[2]i32`. A later store into the parameter does not change `kept`.

A different length, `var copy [3]i32 = row`, is reported as `type mismatch: expected [3]i32, found *i32`.

`var from_file [3]i32 = file_row` copies a file-scope array into a local. `var copy [2]i32 = give()` copies the array when `give` returns `[2]i32`. `called = give()` copies that returned array.

### Rows

`var grid = [_][2]i32{{1, 2}, {3, 4}}` then `var whole = grid` and `var whole [2][2]i32 = grid` copy both rows. `var line = grid[1]` and `var line [2]i32 = grid[1]` copy that row.

`other = grid` copies both rows. `line = grid[1]` copies that row. `grid[1] = line` copies that row. A later store into `line` does not change `grid`.

`grid[1] = {7, 8}` stores that list. A short list stores only the named elements of that row. Reading an omitted element is reported as `uninitialized`.

`grid[1] = {4, 0...}` stores 4 and zero in the rest of that row. A different length is reported as `type mismatch`.

Assigning a row of a const array, or of `[2][2]const i32`, is reported as `type mismatch`.

`rows[1] = line` when `rows` is `[][2]i32` stores into that row. An already-initialized array sees that store.

Reading the array name after that store, when the array was unset, is reported as `uninitialized`. Reading the row through the slice sees the store.

A variable index does not initialize an unset row, so `grid[i] = line` then `return grid[1][0]` is reported as `uninitialized` when `grid` was unset.

### File scope

A file-scope `var copy = row`, `const copy = row`, and `var copy [2]i32 = row` copy that array into the binary. A later store into `row` does not change `copy`.

`g = file_row` inside a function copies a file-scope array. A later store into `file_row` does not change `g`. `file_row = {4, 6}` stores that list.

`const frozen [2]i32 = held`, `var whole [2][2]i32 = grid`, and `var line [2]i32 = grid[1]` copy the same way.

`var copy [2]i32 = give()` and `var copy [2]i32 = unsafe give()` run that call before `main`. `var copy [2]i32 = live`, when `live` in this file is set by a call, copies it before `main`, after `live`. A source that is not a constant array in this file, not a call, and not such a global is reported as `global initializer is not a constant`.

An element copies the same way. `var row = [_][2]i32{inner}` copies that array. `const inner = [_]i32{1, 2}` before that var is that copy. `[_][2]i32{grid[1]}` copies that row.

`var row = [_][2]i32{give()}` and `var row = [_][2]i32{unsafe give()}` run that call before `main`.

`var row = [_]i32{give(), 2}` and `var row [2]i32 = {give(), 2}` run `give` before `main` when `give` returns `i32`. Each element is stored before the next one runs, so that call reads the elements already stored.

`[_]Point{give()}` stores that value. `unsafe give()` is that call.

A `const` with that call, `[_]i32{n + 1}`, `[_]i32{give() + 1}`, a name whose initializer is a call, and an index whose bound does not fold, are reported as `global initializer is not a constant`.

`var row = [_][2]i32{inner}` before `const inner` is reported as `undeclared identifier`.

## Returns

`return row` returns the local array. `return give()` returns that array when `give` returns `[2]i32`. `return grid[1]` returns that row.

`return [_]i32{4, 6}` returns that list. `return file_row` returns the file-scope array. `return {4, 6}` returns that list. `return {{1, 2}, {3, 4}}` returns that array.

A different count, `return {4}`, is reported as `type mismatch`. `return {inner, other}` copies those rows when the result is `[2][2]i32`.

`return {inner}` when the result is `[2]i32` is reported as `type mismatch`. A parameter `[2]i32` in that list copies the array.

`return row` when `row` is a parameter `[2]i32` and the result is `[2]i32` copies those elements. `return grid` when `grid` is a parameter `[2][2]i32` and the result is `[2][2]i32` copies both rows.

`grid[i]` on that parameter is that row, so `return grid[1]` copies it and `grid[1][0]` reads one element. `grid[1] = line` writes that row, so the caller sees the store. A later store into `line` does not change the destination.

`return cube[1]` when `cube` is a parameter `[2][2][2]i32` and the result is `[2][2]i32` copies that plane. `cube[1][0][1]` reads one element.

### Through a pointer

`p[i]` when `p` is `*[2]i32` is still `(*p)[i]`, one element. `p[i]` and `(*p)[i]` when `p` is `*[2][2]i32` are that row. `p[1][0]` reads one element.

`var row [2]i32 = p[1]` copies the row. A later store through `p` does not change the copy. `p[1] = line` and `(*p)[1] = line` copy `line` into that row. A later store into `line` does not change the row.

A further array is the same index. `p[1][0][1]` on `*[2][2][2]i32` and `p[1][0][1][0]` on `*[2][2][2][2]i32` read one element.

`p[1]` on `*[2][]i32` is that slice. `p[1:3]` and `(*p)[1:3]` on `*[4]i32` are `[]i32`. `p[:]` on `*[2][3]i32` is `[][3]i32`. `p[:]` on `*[2][]i32` is `[][]i32`.

`*i32` and `?*[4]i32` are reported as `type mismatch`.

`return {a, b}` copies those parameter rows when the result is `[2][2]i32`. Passing that parameter to a function that takes `[N]T` passes the pointer.

`return row` where `[]i32` is expected builds the slice header that `row[:]` builds. `var copy = row` keeps that length.

A different length is reported as `return type: expected [3]i32, found *i32`. `return grid` where `[3][2]i32` is expected is reported as `return type: expected [3][2]i32, found *[2]i32`.

`return {row}` when the result is `[2]i32` is reported as `type mismatch`.

[Index](../README.md) · [Previous: Unions](union.md) · [Next: Enums](enum.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
