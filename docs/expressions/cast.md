# Casts

[Index](../README.md) · [Previous: Type values](type.md) · [Next: Unsafe](unsafe.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A cast is the type followed by the value in parentheses: `i32(n)`, `i64(n)`, `*u8(bits)`, `*void(p)`, `*Point(p)`, or `func (i32) i32(p)`. A name declared as a type in this file is a cast too: `Integer(n)` after `type Integer i32`, and `ShapeType(shape)` to read the tag of a tlc union. `func (i32) i32(p)` and `Op(p)` after `type Op func (i32) i32` convert a data pointer or another function pointer into that function pointer. `Op(p)(1)` calls it. That cast is how a value enters or leaves the alias. `Integer(n)` has type `Integer`, and `i32(value)` has type `i32`. A cast between an alias and a value with the same representation is allowed, including `Spot(p)` after `type Spot Point` and `Ptr(&p)` after `type Ptr *Point`. A cast to a different struct is reported as `type mismatch`. From another file in the package, `ShapeType(shape)` still reads that tag.

A sized array casts to a pointer to its element: `*i32(row)` when `row` is `[N]i32`. Dropping `const` is reported as `unsafe cast`. `*const i32(held)` keeps `const` when `held` is a const array.

A cast between integer types is allowed. Signed `i32` can be cast to and from a pointer. The cast keeps the type that was written: `*i32(p)` is `*i32`, and `?*i32(p)` is `?*i32`. `?*i32(null)` is allowed. `*i32(null)`, `i64(p)`, `u32(p)`, and `i32(null)` are reported as `type mismatch`. `i32*(p)` is reported as `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type. A pointer cast that changes the claim needs [`unsafe`](unsafe.md): a different primitive, such as `*i32` to `*u8` or `*f64` to `*f32`, `*void` to one of those primitives, or `*u8` or `*void` to a struct or union. `*void(p)`, `*u8(&slot)`, `*u8` to `*void`, `*Point(&local)`, and `*i32` to `*Point` stay ordinary casts. The missing prefix is reported as `unsafe cast`.

```c
type Integer i32;

func main() i32 {
    var n i32 = 42;
    var p *i32 = &n;
    var bytes *u8 = unsafe *u8(p);
    var back *i32 = unsafe *i32(bytes);
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

`i64(n)` from an `i32` parameter is 1 `movslq` after the parameter store. The function is 3 instructions including `retq`. `i8(n)` is a move and `movsbl`: 2 instructions after the store. `u32(n)` from `i32` reuses the loaded value. An alias cast of the same representation, such as `Integer(n)`, `Spot(p)`, or `string_view(text)` when `text` is already `[]const u8`, also reuses it and adds no instructions. A pointer cast reuses the loaded address. `ShapeType(shape)` on a tlc union reloads the whole value, which is the byte-wise cost on [Unions](../types/union.md).

[Index](../README.md) · [Previous: Type values](type.md) · [Next: Unsafe](unsafe.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
