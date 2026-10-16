# Pointers and null

[Index](../README.md) · [Previous: Logical operators](logical.md) · [Next: Narrowing](narrowing.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

Pointers that point at the same type can be compared with `==` and `!=`, including `*T` with `?*T`. A pointer can be compared with `null`, and `null == null` is allowed. The result is `i32`. Pointers of different types cannot be compared. Relational operators on pointers are rejected. `p == 0` is reported as `type mismatch`.

`*T` has a value. `?*T` may be `null`. `null` may be assigned or returned only to `?*T`. `&x` has type `*T` and may be stored in `*T` or `?*T`. `var p = null` has no pointer type to take and is reported as `type mismatch`. Write `var p ?*Point = null`. A `*T` with no initializer is reported as `type mismatch`, and so is a struct or array that contains one, because that memory would be zero. An omitted `?*T` field may be `null`. An omitted `*T` field is reported as `type mismatch`. `extern` storage is not checked. Assigning an integer, including `0`, to a pointer is reported as `type mismatch`. An address that comes from an integer is a cast, `*i32(n)`. Assigning `null` to `i32` or `bool` is reported as `type mismatch`. `true` and `false` do not convert to pointers.

Pointer arithmetic is reported as `type mismatch`. A subscript index has type `i32`.

```c
func main() i32 {
    var n i32 = 1;
    var p *i32 = &n;
    var q ?*i32 = null;
    if (p != q && q == null) {
        return *p;
    }
    return 0;
}
```

## Cost

`p == q` on two pointers is 5 instructions after the pointers are stored: load, `cmpq`, `sete`, mask, zero-extend. `p == null` compares with 0 and has that same shape. The comparison adds no branch of its own. A relational operator on a pointer is rejected while compiling and adds none.

[Index](../README.md) · [Previous: Logical operators](logical.md) · [Next: Narrowing](narrowing.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
