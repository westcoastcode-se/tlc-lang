# Pointers

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`*T` is a pointer that has a value. `?*T` is that pointer or `null`. `*?*T` and `?**T` are different types. `?` is written immediately before `*`.

`*const T` is a pointer to constant `T`. The variable may be reseated. A store through it, `*p = 1` or `p[i] = 1`, is reported as `type mismatch`. Reading `*p` and `p[i]` is allowed. `*T` may be stored in `*const T`. `*const T` may not be stored in `*T`. `?*const T` is that pointer or `null`. `**const T` is a pointer to a pointer to constant `T`. `*const *T` is a pointer to a constant pointer.

```c
func main() i32 {
    var n i32 = 1;
    var p *i32 = &n;
    var q ?*i32 = null;
    var c *const i32 = p;
    var inner *i32 = &n;
    var to_const **const i32 = &inner;
    var to_ptr *const *i32 = &inner;
    q = p;
    return *p + *c + **to_const + **to_ptr;
}
```

## Cost

A pointer local is 8 bytes. `var p *i32 = &n` is 1 `leaq` of `n` and 1 store of that address. `*const T` is the same pointer. The const check adds no instructions. A store through `*const T` is rejected while compiling.

`?*T` uses the same 8-byte slot. `var p ?*i32` with no initializer adds no instruction. The slot is uninitialized. `var p ?*i32 = null` is 1 store of 0.

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
