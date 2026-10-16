# Casts

[Index](../README.md) · [Previous: Type values](type.md) · [Next: Tuples and lists](tuple.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A cast is the type followed by the value in parentheses: `i32(n)`, `i64(n)`, `*u8(bits)`, `*void(p)`, `*Point(p)`, or `func (i32) i32(p)`. A name declared as a type in this file is a cast too: `Integer(n)` after `type Integer i32`, and `ShapeType(shape)` to read the tag of a tlc union. `func (i32) i32(p)` and `Op(p)` after `type Op func (i32) i32` convert a data pointer or another function pointer into that function pointer. `Op(p)(1)` calls it. That cast is how a value enters or leaves the alias. `Integer(n)` has type `Integer`, and `i32(value)` has type `i32`. A cast between an alias and a value with the same representation is allowed, including `Spot(p)` after `type Spot Point` and `Ptr(&p)` after `type Ptr *Point`. A cast to a different struct is reported as `type mismatch`. From another file in the package, `ShapeType(shape)` still reads that tag.

A cast between integer types is allowed. Signed `i32` can be cast to and from a pointer. The cast keeps the type that was written: `*i32(p)` is `*i32`, and `?*i32(p)` is `?*i32`. `?*i32(null)` is allowed. `*i32(null)`, `i64(p)`, `u32(p)`, and `i32(null)` are reported as `type mismatch`. `i32*(p)` is reported as `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type.

```c
type Integer i32;

func main() i32 {
    var n i32 = 42;
    var p *i32 = &n;
    var bytes *u8 = *u8(p);
    var back *i32 = *i32(bytes);
    var q ?*i32 = ?*i32(null);
    var wide = i64(n);
    var alias = Integer(n);
    if (q == null && *back == i32(alias) && wide == 42) {
        return *back;
    }
    return 0;
}
```

## Cost

`i64(n)` from an `i32` parameter is 1 `movslq` after the parameter store. The function is 3 instructions including `retq`. `i8(n)` is a move and `movsbl`: 2 instructions after the store. `u32(n)` from `i32` is 1 `movl`. The bits are not changed. A pointer cast is a copy of the address, 1 `movq` once the pointer is loaded. `ShapeType(shape)` on a tlc union reloads the whole value, which is the byte-wise cost on [Unions](../types/union.md).

[Index](../README.md) · [Previous: Type values](type.md) · [Next: Tuples and lists](tuple.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
