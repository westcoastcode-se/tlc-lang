# Expressions

[Index](../README.md) · [Previous: Keywords](../keyword.md) · [Next: Logical operators](logical.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

The operators are `+ - * / %`, `& | ^ ~ << >>`, `&& || !`, the comparisons, unary `+` and `-`, assignment `=`, calls, indexing, `.`, address-of `&`, and dereference `*`. Binding and associativity are given in the [grammar](../grammar.md#expressions).

```c
func main() i32 {
    var n i32 = 6;
    var p *i32 = &n;
    n = -n + 8;
    n = n * 3 / 2 % 5;
    n = n << 1 >> 1;
    n = (n & 7) | 1;
    n = n ^ 1;
    n = ~n;
    if (n < 0 && *p != 0) {
        return 1;
    }
    return 0;
}
```

## Cost

Each operator below is the cost after its operands are in slots. Two `i32` locals make `+`, `-`, `*`, `&`, `|`, and `^` cost 2 instructions. `/` costs 3 and `%` costs 4. A shift costs 3. Unary `-` and `~` cost 2. A comparison costs 5 and yields 0 or 1. `f32` and `f64` addition cost 2, a reload and an add. The full sequences are on [Numeric types](../types/integer.md) and [void, bool, and floating-point types](../types/float.md).

Assignment of an `i32` is 1 store. Assignment of a struct is one load and one store per field. A call is 36 instructions in the caller when fewer than 32 frames are stored, plus the callee. Indexing and `.` are counted on [Arrays](../types/array.md), [Slices](../types/slice.md), and [Structs](../types/struct.md).

[Index](../README.md) · [Previous: Keywords](../keyword.md) · [Next: Logical operators](logical.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
