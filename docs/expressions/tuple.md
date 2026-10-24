# Tuples and lists

[Index](../README.md) · [Previous: Unsafe](unsafe.md) · [Next: Statements](../statements/block.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`(40, 2)` is a tuple. The element types are the types of the values before promotion, so `(40, 'a')` is `i32` and `u8`. `(40)` is the value `40`. `var value = (40, 2)` and `value := (40, 2)` each bind that tuple. `var value1, value2 = value` and `a, b := value` unpack it, one name per element. `var value1, value2 = (40, 2)` unpacks the literal the same way. The number of names matches the tuple. A different count is reported as `type mismatch`. `value[0]` reads one element. The index is a constant. `typeof(value)` is that tuple's type. `typeof(value).fields()[i].kind` is the type of element `i`, and `typeof(value[i])` is the same type. `builtin.fmt` copies `{0}` and `{1}` from a compile-time tuple into one `[]const u8`. A string element is its text. An integer element is decimal. `{{` and `}}` are the brace characters. A `var` element is `not supported yet`. A macro can `emit printf` of that text, so `print_with_tuples("Hello {0}! {0} is {1} years old", ("John Doe", 26))` is `printf("Hello John Doe! John Doe is 26 years old")`.

`{10, 20, 12}` fills an array. Where a struct is expected, that brace list is reported as `type mismatch`. An empty `{}` names no fields. `Point{}` names the struct, so the type is `Point`. Each field takes the value written on it, and stays uninitialized when the field has none. `Point{0...}` stores 0 in every field. `{1, 2, 0...}` stores 0 in the rest of an array whose length is known.

`[_]i32{10, 20, 12}` is an array value. `_` stands for the length, which is the number of values, so the type is `[3]i32`. `var row = [_]i32{10, 20, 12}` and `row := [_]i32{10, 20, 12}` have that type. `row[0]` reads an element. An empty `[_]i32{}` has length 0. `[_][2]i32{{1, 2}, {3, 4}}` is a nested brace list, and `grid[0][1]` reads an element. `[_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}` is the next array, and `grid[0][1][2]` reads an element. At file scope the same literal initializes the variable when each value is a constant. `const fixed = [_]i32{19, 2}` is that array, and a store of an element is reported as `type mismatch`. `[]i32{10, 20, 12}` is reported as `expected '_'`. A slice is a separate type, `var v []i32 = row`. `v[i]` reads an element and is reported as `type mismatch` when used as the target of `=`. `v.len` is `i32`. `row.len` on the array is `i32`, the number of elements. `row[i:j]` and `v[i:j]` are the elements from `i` up to, but not including, `j`. The result is a slice. A missing low bound is `0`. A missing high bound is the length.

```c
func main() i32 {
    var row = [_]i32{10, 20, 12};
    return row[0] + row[1] + row[2];
}
```

```c
func main() i32 {
    var value = (40, 2);
    var value1, value2 = value;
    a, b := (7, 'a');
    value1, value2 = (1, 2);
    var row [3]i32 = {10, 20, 12};
    return value[0] + value1 + a + row[2] + b;
}
```

## Cost

`(40, 2)` stored in a local is 2 `movl` instructions, one per element. `value[0]` is 1 load. The index is constant, so there is no multiply. `return typeof(row).fields().len` on `var row = ("hi", 26)` is 2 instructions: `movl $2, %eax` and `retq`. `builtin.fmt` of two constant elements adds no instructions of its own: `emit printf` of that text is one traced `printf` of the finished string.

Unpacking two `i32` results from a call stores both return registers. After `pair` returns, that is 1 `movl` of `%eax` and 1 `movl` of `%edx` into the two locals, on top of the traced call. The callee that returns `x + y, x` is 7 instructions including its parameter stores and `retq`.

An array brace list stores one immediate per named element. `var row [3]i32 = {10, 20, 12}; return row[0] + row[1] + row[2];` is 7 instructions: three `movl`, one `movl` into `%eax`, two `addl`, and `retq`. A shorter list leaves the tail unstored. `{10, 20, 0...}` on a `[4]i32` stores 0 in every element and then stores 10 and 20. Returning the sum of the four elements is 11 instructions: four `movl` of 0, two `movl` of the named values, one `movl` into `%eax`, three `addl`, and `retq`. A slice built from that array is the header cost on [Slices](../types/slice.md).

[Index](../README.md) · [Previous: Unsafe](unsafe.md) · [Next: Statements](../statements/block.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
