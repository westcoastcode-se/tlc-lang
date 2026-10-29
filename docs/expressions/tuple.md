# Tuples and lists

[Index](../README.md) · [Previous: Unsafe](unsafe.md) · [Next: Statements](../statements/block.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes tuples, which group values, and brace lists, which fill arrays and structs.

## A tuple value

`(40, 2)` is a tuple.

`((40, 2), 1)` is a tuple whose first element is a tuple.

## A function, a pointer, or an array

A `comptime const` element may be a function.

`comptime const ops = (add, 1)` stores that pointer, and `(ops[0])(40, 2)` calls `add`.

`ops[0](40, 2)` is a generic call of `ops`.

A `comptime const` element may be a pointer.

`comptime const pair = (&addr, 2)` stores `&addr`, and `*pair[0]` is that integer.

`pair[0].x` reads a field when the pointer addresses a struct.

A `comptime const` element may be an array.

The element stays `[N]T`.

`pair[0][0]` reads one element.

A store writes the copy inside the tuple, not the source array.

Folding and the rejected forms are on the [Comptime](../functions/comptime.md) page.

## A copy of an array

`var nested = ((row, 1), 0)` copies `row`.

`nested[0][0][0] = 2` stores in that copy.

`row[0]` stays 40, and the sum is 42.

```c
comptime const row = [_]i32{40, 1};

func main() i32 {
    var nested = ((row, 1), 0);
    nested[0][0][0] = 2;
    return row[0] + nested[0][0][0];
}
```

```asm
main:
movl main.row(%rip), %eax        # row[0]
movl main.row+4(%rip), %ecx      # row[1]
movl %ecx, -12(%rsp)             # copy of row[1]
movl %eax, -16(%rsp)             # copy of row[0]
movl $0, -4(%rsp)                # outer element
movl $1, -8(%rsp)                # inner element
movl $2, -16(%rsp)               # nested[0][0][0] = 2
movl main.row(%rip), %eax        # row[0]
addl -16(%rsp), %eax             # row[0] + nested[0][0][0]
retq                             # return
```

## Nested values

`value[0]` is `(40, 2)` and `value[0][1]` is `2`.

`var pair ((i32, i32), i32) = ((40, 2), 1)` is that variable.

`func pair() ((i32, i32), i32)` is still that tuple beside another result.

The element types are the types of the values before promotion, so `(40, 'a')` is `i32` and `u8`.

`(40)` groups that value when no tuple of one element is expected, so `var wide i32 = (40)` is `i32`.

## A name for a tuple

`type Duo (i32, i32)` names that tuple.

`Duo` is a different type from `(i32, i32)`, so the value is `Duo((40, 2))`.

`type Something[K type, V type] (K, V)` is that alias.

`Something[i32, i32]((40, 2))` names one specialization.

The compiler reports `type Something[K, V] (K, V)` as `expected a type` on the comma.

## Parameters

A parameter may be written `(i32, i32)`.

A generic parameter `(P1, P2)` is that tuple.

`SomeFunc[i32, i32](0, (40, 2))` writes the types.

`SomeFunc(0, (40, 2))` infers them.

An element may be an array: `value[0][1]` reads one element, and the elements are kept when the tuple is passed or cast.

A function type keeps that array.

`func () (([2]i32, i32))` and `func (([2]i32, i32)) i32` match the function that returns or takes `([2]i32, i32)`.

## One element

`(i32)` is a tuple of one element.

`var n (i32) = (40)` is that tuple, and `n[0]` reads the element.

The compiler reports `var n (i32) = 40` as `type mismatch`.

The compiler reports `(40,)` as `type mismatch`.

`func one() ((i32))` returns that tuple.

`return (40)` matches it.

The compiler reports `return 40` as `return type: expected (i32), found i32`.

`func one() (i32)` is still one `i32`.

`return (40)` is then the same as `return 40`.

`type One (i32)` names that tuple.

`One` is a different type from `(i32)`.

`One((40))` names the value.

The compiler reports `var n One = (40)` as `type mismatch`.

## One tuple result, or two results

`var value (i32, i32) = (40, 2)` is that tuple.

`func pair() ((i32, i32))` returns that tuple.

`return (40, 2)` matches it.

`return 40, 2` does not.

`func pair() (i32, i32)` is still two results.

`var op func () ((i32, i32)) = pair` stores that function.

`var value = op()` binds the tuple, and `value[0] + value[1]` is 42.

`func () (i32, i32)` is still two results.

`var op func () ((i32, i32), i32) = pair` stores that function.

`var value, n = op()` unpacks the call, and `value[0] + value[1] + n` is 42.

## A tuple as a parameter

`func ((i32, i32)) i32` is one tuple parameter.

`var op func ((i32, i32)) i32 = take` stores `take` when `take` takes `(i32, i32)`, and `op((40, 2))` calls it.

The compiler reports `op(40, 2)` as `argument count`.

`func (i32, i32) i32` is still two parameters.

A function of two `i32` parameters does not match, and that assignment is `type mismatch`.

The compiler reports `op(Duo((40, 2)))` as `type mismatch`, because `type Duo (i32, i32)` is a different type.

`func ((i32, i32), i32) i32` is that tuple beside another parameter.

`var op func ((i32, i32), i32) i32 = take` stores `take` when `take` takes `(i32, i32)` and an `i32`.

`op((40, 1), 1)` calls it.

`func (i32, (i32, i32)) i32` is that tuple as another parameter.

The compiler reports `op((40, 1))` as `argument count`.

The compiler reports `op(40, 2)` as `type mismatch`.

A function of three `i32` parameters does not match, and that assignment is `type mismatch`.

The compiler reports `op(Duo((40, 1)), 1)` as `type mismatch`, because `type Duo (i32, i32)` is a different type.

`func pair() ((i32, i32), i32)` is that tuple beside another result.

`return (40, 2), 0` matches it.

The compiler reports `return 40, 2, 0` as `return type: expected 2 results, found 3`.

## Fields, arrays, and slices

A field may be written `value (i32, i32)`.

`box.value = (40, 2)` stores it, and `box.value[0]` reads an element.

`sizeof(Duo)` is the size of the alias.

`[2]Duo` is an array of it.

`row[0] = Duo((40, 2))` stores one element, and `row[0][0]` reads one field.

A tuple does not convert to a slice.

`row[0:2]`, `row[:]`, the same range of a parameter `[N]Duo` or `[N][M]Duo`, and `p[0:2]` on `*[N]Duo` are `type mismatch`.

`[]Duo` and `[][N]Duo` are `not supported yet`.

## Binding and indexing

`var value = (40, 2)` and `value := (40, 2)` each bind that tuple.

`var value1, value2 = value` and `a, b := value` unpack it, one name per element.

`var value1, value2 = (40, 2)` unpacks the literal the same way.

The number of names matches the tuple.

The compiler reports a different count as `type mismatch`.

`value[0]` reads one element.

`var row = (40, 2)` then `var p = &row` and `(*p)[0]` reads 40.

`(*p)[0] = 1` stores that element in `row`.

The index is a constant.

`const i = 0` then `value[i]` reads that same element, and `value[i] = 1` stores it.

`const j = i + 1` and `value[i + 1]` choose the next field.

A file-scope `const` whose initializer folds is that index.

`comptime const pair = (40, 2)` then `return pair[i] + pair[1]` is `movl $42, %eax` and `retq`.

A `var` is `index is not a compile-time constant`.

That `const` is an array length when the initializer folds to a non-negative `i32`.

See [Arrays](../types/array.md).

```c
func main() i32 {
    var t = (0, 2);
    const i = 0;
    t[i] = 40;
    return t[i] + t[1];
}
```

```asm
main:
movl $2, -4(%rsp)                # constant
movl $0, -8(%rsp)                # constant
movl $40, -8(%rsp)               # constant
movl -8(%rsp), %eax              # move
movl -4(%rsp), %ecx              # move
addl %ecx, %eax                  # add
retq                             # return
```

`typeof(value)` is that tuple's type.

`typeof(value).fields()[i].kind` is the type of element `i`, and `typeof(value[i])` is the same type.

`builtin.fmt` copies `{0}` and `{1}` from a compile-time tuple into one `[]const u8`.

A string element is its text.

An integer element is decimal.

`{{` and `}}` are the brace characters.

A `var` element is `not supported yet`.

A macro can `emit printf` of that text, so `print_with_tuples("Hello {0}! {0} is {1} years old", ("John Doe", 26))` is `printf("Hello John Doe! John Doe is 26 years old")`.

```c
func main() i32 {
    var n (i32) = (42);
    return n[0];
}
```

```asm
main:
movl $42, -8(%rsp)               # constant
movl -8(%rsp), %eax              # move
retq                             # return
```

## Brace lists

`{10, 20, 12}` fills an array.

Where a struct is expected, that brace list is reported as `type mismatch`.

An empty `{}` names no fields.

`Point{}` names the struct, so the type is `Point`.

Each field takes the value written on it, and stays uninitialized when the field has none.

`Point{0...}` stores 0 in every field.

`{1, 2, 0...}` stores 0 in the rest of an array whose length is known.

## An array whose length is `_`

`[_]i32{10, 20, 12}` is an array value.

`_` stands for the length, which is the number of values, so the type is `[3]i32`.

`var row = [_]i32{10, 20, 12}` and `row := [_]i32{10, 20, 12}` have that type.

`row[0]` reads an element.

An empty `[_]i32{}` has length 0.

`[_][2]i32{{1, 2}, {3, 4}}` is a nested brace list, and `grid[0][1]` reads an element.

`[_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}` is the next array, and `grid[0][1][2]` reads an element.

At file scope the same literal initializes the variable when each value is a constant.

`const fixed = [_]i32{19, 2}` is that array, and a store of an element is reported as `type mismatch`.

The compiler reports `[]i32{10, 20, 12}` as `expected '_'`.

A slice is a separate type, `var v []i32 = row`.

`v[i]` reads an element and is reported as `type mismatch` when used as the target of `=`.

`v.len` is `i32`.

`row.len` on the array is `i32`, the number of elements.

`row[i:j]` and `v[i:j]` are the elements from `i` up to, but not including, `j`.

The result is a slice.

A missing low bound is `0`.

A missing high bound is the length.

```c
func main() i32 {
    var row = [_]i32{10, 20, 12};
    return row[0] + row[1] + row[2];
}
```

```asm
main:
movl $10, -12(%rsp)              # constant
movl $20, -8(%rsp)               # constant
movl $12, -4(%rsp)               # constant
movl -12(%rsp), %eax             # move
addl -8(%rsp), %eax              # add
addl -4(%rsp), %eax              # add
retq                             # return
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

```asm
main:
movl $2, -28(%rsp)               # constant
movl $40, -32(%rsp)              # constant
movl $7, -20(%rsp)               # constant
movb $97, -13(%rsp)              # constant
movl $1, -24(%rsp)               # constant
movl $12, -4(%rsp)               # constant
movl -32(%rsp), %eax             # move
movl -28(%rsp), %ecx             # move
addl -24(%rsp), %eax             # add
addl -20(%rsp), %eax             # add
addl -4(%rsp), %eax              # add
movzbl -13(%rsp), %ecx           # move
addl %ecx, %eax                  # add
retq                             # return
```

[Index](../README.md) · [Previous: Unsafe](unsafe.md) · [Next: Statements](../statements/block.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
