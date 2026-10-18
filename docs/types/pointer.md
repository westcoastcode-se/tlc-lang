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

`**[4]i32` is a pointer to a pointer to an array. `?*[4]i32` is that pointer or `null`. `*const [4]i32` is a pointer to a constant array. `p[i]` and `(*p)[i]` read and write the same element of `*[4]i32`. `p[1:3]`, `p[1:]`, `p[:2]`, and `p[:]` on `*[4]i32` are `[]i32`, and so are `(*p)[1:3]` and `(*p)[:]`. `*[2][3]i32` gives `[][3]i32`. `*[2][]i32` gives `[][]i32`. `*i32` and a still-optional `?*[4]i32` are reported as `type mismatch`. `p[i]` and `(*p)[i]` on `*[2][2]i32` are the row `[2]i32`. `p[1][0]` is one element. Each further array is that same index. `var row [2]i32 = p[1]` copies the row. A later store through `p` does not change the copy. `(**deep)[2]` and `(*deep)[2]` read the same element. A store through `*const [4]i32`, `p[i] = 1`, is reported as `type mismatch`. Reading `p[i]` is allowed. `p.len` and `(*p).len` on `*[N]T` are the written length `N`. `(&row).len` and `buf.len`, when `buf` is `new [_]T`, are that length. `p.len` on `*[N][M]T` and on `*[N][]T` is `N`. `p[0].len` is the inner length. Reading `p.len` on `*const [N]T` is allowed. `*i32`, `*[]T`, `**[N]T`, `new [n]T`, and a still-optional `?*[N]T` are reported as `type mismatch`. Assigning `p.len`, or taking its address, is reported as `not an lvalue`. `return p` where `[]i32` is expected builds the slice header that `p[:]` builds. `var s []i32 = p` is that slice. A file-scope `var view []i32 = p` is reported as `global initializer is not a constant`. `p[:]` is the slice.

`var deep **[4]i32` with no initializer is reported as `type mismatch`. `var opt ?*[4]i32` may omit the initializer. The pointer is null. `*opt` and `opt[i]` are reported as `type mismatch` while `opt` is optional. `opt![i]` is `(opt[i])!`, which is reported as `type mismatch`. `(opt!)[i]` narrows the pointer, then indexes. After `if (opt == null) return`, `opt` is `*[4]i32` for the rest of the block, so `opt[i]` is the element, `opt.len` is the length, and `opt!` is reported as `type mismatch`.

## Cost

A pointer local is 8 bytes. `var p *i32 = &n` is 1 `leaq` of `n` and 1 store of that address. `*const T` is the same pointer. The const check adds no instructions. A store through `*const T` is rejected while compiling.

`?*T` uses the same 8-byte slot. `var p ?*i32` with no initializer is null. The store of 0 remains when the pointer is read. With no `-O` flag, it is removed when the pointer is not read. `-O0` keeps that store. `var p ?*i32 = null` is 1 store of 0.

`**[4]i32` and `?*[4]i32` are that same 8-byte pointer. `var deep **[4]i32 = &arr` is 1 `leaq` and 1 store. `var opt ?*[4]i32` with no initializer adds no instruction when the pointer is not read. `var opt ?*[4]i32 = &data` is 1 `leaq` and 1 store. `var opt ?*[4]i32 = null` is 1 `movq` of 0. `(**deep)[2]` loads `deep`, loads the pointer it points at, then loads the `i32`: 2 `movq` and 1 `movl`. `(*deep)[2]` is those same 3 loads. `return p[1]` when `p` is `*[2][2]i32` is 5 instructions: the pointer store, one reload, and two `movl` of that row, then `retq`. `return (*p)[1]` is those same 5. `return p[1][0]` is 4: the pointer store, one reload, one `movl`, and `retq`. `return p[1][0][1]` on `*[2][2][2]i32` and `return p[1][0][1][0]` on `*[2][2][2][2]i32` are those same 4. `return p[:]` and `return (*p)[:]` on `*[4]i32` are 4 instructions: the pointer store, one reload, 1 `movl` of the length, and `retq`. `return p` where the result is `[]i32` is those same 4. `*const [4]i32` is those same 4. `return p` where the result is `[][3]i32` or `[][]i32` is those same 4, with the length 2. `return p[1:3]` and `return (*p)[1:3]` are 5: those, plus 1 `addq` of 4. `return p.len` and `return (*p).len` on `*[4]i32` are 3: the pointer store, 1 `movl` of the length, and `retq`. The array is not read. `*const [4]i32` is those same 3. `return p.len` on `*[2][3]i32` and on `*[2][]i32` are those same 3, with the length 2. `(&row).len` on a local `[4]i32` with no initializer is 2: 1 `movl` of the length and `retq`. `return opt.len` after `if (opt == null) return` is 10 instructions when the pointer is present: the pointer store, 1 `cmpq`, 1 `sete`, 1 `andb`, 1 `movzbl`, 1 `cmpl`, 1 `je`, 1 `jmp`, 1 `movl` of the length, and `retq`. The null path is 9: those through the `je`, then 1 `xorl` and `retq`. `opt == null` is 1 `cmp` of the pointer with 0, then `sete`, `and`, and `movzx` into an `i32`.

[Index](../README.md) · [Previous: Numeric types](integer.md) · [Next: void, bool, and floating-point types](float.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
