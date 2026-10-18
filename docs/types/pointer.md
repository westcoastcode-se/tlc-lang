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

`**[4]i32` is a pointer to a pointer to an array. `?*[4]i32` is that pointer or `null`. `*const [4]i32` is a pointer to a constant array. `p[i]` and `(*p)[i]` read and write the same element of `*[4]i32`. `p[i]` and `(*p)[i]` on `*[2][2]i32` are the row `[2]i32`. `p[1][0]` is one element. Each further array is that same index. `var row [2]i32 = p[1]` copies the row. A later store through `p` does not change the copy. `(**deep)[2]` and `(*deep)[2]` read the same element. A store through `*const [4]i32`, `p[i] = 1`, is reported as `type mismatch`. Reading `p[i]` is allowed.

`var deep **[4]i32` with no initializer is reported as `type mismatch`. `var opt ?*[4]i32` may omit the initializer. The slot is uninitialized. `*opt` and `opt[i]` are reported as `type mismatch` while `opt` is optional. `opt![i]` is `(opt[i])!`, which is reported as `type mismatch`. `(opt!)[i]` narrows the pointer, then indexes. After `if (opt == null) return`, `opt` is `*[4]i32` for the rest of the block, so `opt[i]` is the element and `opt!` is reported as `type mismatch`.

## Cost

A pointer local is 8 bytes. `var p *i32 = &n` is 1 `leaq` of `n` and 1 store of that address. `*const T` is the same pointer. The const check adds no instructions. A store through `*const T` is rejected while compiling.

`?*T` uses the same 8-byte slot. `var p ?*i32` with no initializer adds no instruction. The slot is uninitialized. `var p ?*i32 = null` is 1 store of 0.

`**[4]i32` and `?*[4]i32` are that same 8-byte pointer. `var deep **[4]i32 = &arr` is 1 `leaq` and 1 store. `var opt ?*[4]i32` with no initializer adds no instruction. `var opt ?*[4]i32 = &data` is 1 `leaq` and 1 store. `var opt ?*[4]i32 = null` is 1 `movq` of 0. `(**deep)[2]` loads `deep`, loads the pointer it points at, then loads the `i32`: 2 `movq` and 1 `movl`. `(*deep)[2]` is those same 3 loads. `return p[1]` when `p` is `*[2][2]i32` is 5 instructions: the pointer store, one reload, and two `movl` of that row, then `retq`. `return (*p)[1]` is those same 5. `return p[1][0]` is 4: the pointer store, one reload, one `movl`, and `retq`. `return p[1][0][1]` on `*[2][2][2]i32` and `return p[1][0][1][0]` on `*[2][2][2][2]i32` are those same 4. `opt == null` is 1 `cmp` of the pointer with 0, then `sete`, `and`, and `movzx` into an `i32`.

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
