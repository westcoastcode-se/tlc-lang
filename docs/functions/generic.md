# Generics

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: Comptime](comptime.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A generic function names its parameters in brackets. `T type` is a type. `N i32` is a compile-time integer. `T Shape` is a type that must have the methods and the fields of the trait `Shape`. One body is specialized for each call. There is no `impl` and no runtime table.

```c
func id[T type](value T) T {
    return value;
}

func repeat[N i32, T type](value T) [N]T {
    assert(N > 0, "empty");
    var out [N]T;
    out[0] = value;
    return out;
}

func main() i32 {
    return id(40) + id[i32](0) + repeat[4](2)[0];
}
```

`*T`, when `T` is `[4]i32`, is a pointer to that array. `p[i]` and `(*p)[i]` are the element. `**T` is a pointer to that pointer. `?*T` is that pointer or `null`. After `if (p == null) return`, `p` is `*T` and `p[i]` is the element. `*const T` is a pointer to the constant array. Reading `p[0]` is allowed. `p[0] = 1` is reported as `type mismatch`. When `T` is `[]i32`, `(*p)[i]` is the element. A field `p *T` on `Hold[T]` is that pointer. `box.p[i]` is the element.

```c
type Hold[T type] struct {
    p *T;
}

func at[T type](p *T, i i32) i32 {
    return p[i];
}

func main() i32 {
    var data [4]i32;
    data[0] = 1;
    data[2] = 6;
    var box Hold[[4]i32] = {.p = &data};
    return at[[4]i32](&data, 0) + box.p[2];
}
```

`at[[4]i32](&data, 0) + box.p[2]` returns 7.

`T Shape` requires a type that has the methods and the fields of the trait `Shape`.

```c
type Shape trait {
    func area() i32;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func measure[T Shape](value T) i32 {
    return value.area();
}

func main() i32 {
    var r Rect;
    r.w = 6;
    r.h = 7;
    return measure(r);
}
```

`measure(r)` returns 42. `value *T` accepts a value receiver and a pointer receiver. Passing the struct where the parameter is `*T` is `type mismatch`. The call is `measure(&r)`. A parameter written `T` requires a value receiver. A pointer receiver there is `type mismatch`.

`export func measure[T Shape]` publishes that function. Another package calls `pkg.measure(r)`. The trait is the one in the function's package. The method on the struct is exported. Without `export` on that method, the call is `area is not exported`. `export func use(s Shape)` publishes that call without a type-parameter list. See [Traits](../methods/trait.md).

`id(1)` infers `T`. The result may be omitted: `func id[T type](value T) { return value; }` is that same function. A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value. The omitted result may be a function pointer: `func hold[T type](value T) { return add; }` is `func (i32, i32) i32` when `add` is `func (i32, i32) i32`. `repeat[4](7)` writes `N` and infers `T`. `id[i32](1)` writes the type. A type that cannot be inferred is reported as `type mismatch`. A type parameter bound to two different types in one call, such as `Vec[i32]` and `Vec[u8]` for `Vec[T]`, is reported as `type mismatch`. `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` see the concrete type and the integer.

`var f = id` names that generic. `f := id` and `const f = id` do too, and so does `var g = f`. `f(1)` and `f[i32](1)` specialize it the same way as `id`. The name is one in this package. `var f = pkg.id` is `not supported yet`, and the call stays `pkg.id(1)` or `pkg.id[i32](1)`. The binding is not a function pointer and has no storage. A use that is not a call, such as `return f` or `take(f)` where a function pointer is expected, is `not supported yet`. `f = id` is `not supported yet`. A file-scope `var f = id` is `not supported yet`. An overloaded function that is not generic stays `not supported yet` when it is used as a value. An instance method used as a value is `type mismatch`.

### One type parameter

`T` is the type of the value. That value may be an integer, a struct with several fields, or a tuple of several types. `func take(other (i32, f32))` is a tuple parameter. `[P1, P2]` is `expected a type` on the comma. With `[P1 type, P2 type]`, `other (P1, P2)` is that tuple. `SomeFunc[i32, i32](0, (40, 2))` writes the types. `SomeFunc(0, (40, 2))` infers them. A different element count, or `Duo` where `(P1, P2)` is written, is `type mismatch`. `[...U]` is not a type parameter. `...` in that list is `expected an identifier`. `...` is the C argument list on `extern(abi="c")`. `(40)` is one value, the same call as `id(40)`. `(40, 2)` is a tuple. The element types are the types of the values before promotion, so `'a'` stays `u8`.

```c
type Point struct { x i32; y i32; };

func id[T type](value T) T {
    return value;
}

func main() i32 {
    var n = id(40);
    var written = id[i32](1);
    a, b := id((40, 2));
    x, y := id((40, 'a'));
    var point = id(Point{.x = 8, .y = 1});
    return n + written + a + x + point.x + point.y;
}
```

`id[i32](40)` writes the type. A value in that list, such as `id[4]`, is reported as `type mismatch`. `return value` returns the tuple, and so does `return id((1, 2))` from a function with those two results. `a, b :=` unpacks them, and so does `var value1, value2 = value` when `value` holds that tuple. `var value = (40, 2)` binds the tuple itself. `...` in a type-parameter list is not the `...` that `builtin.va()` reads.

```c
func SomeFunc[P1 type, P2 type](value i32, other (P1, P2)) i32 {
    return value + other[0] + other[1];
}

func main() i32 {
    return SomeFunc[i32, i32](0, (40, 2));
}
```

`SomeFunc[i32, i32](0, (40, 2))` returns 42. `SomeFunc(0, (40, 2))` is that same call. `other[0]` and `other[1]` read the elements. The index is a constant.

One generic name may have several parameter lists. A call uses the types of the arguments, the same way as a function that is not generic. `func id[T type](value T) T` and `func id[T type](value T, extra i32) T` are both `id`. Type-parameter names are not part of that list, so `id[T type](value T)` and `id[U type](value U)` are one list. The same parameter list is `redefinition`. A generic function and a concrete function of the same name are `redefinition`. A call that matches none or more than one list is `type mismatch` or `argument count`.

A method on a generic struct follows the same rule. The receiver is not one of those parameters. `func Box[T]::get(self *this) T` and `func Box[T]::get(self *this, n i32) T` are two methods, chosen from the remaining arguments. `func Box[T]::get(self this)` beside `func Box[T]::get(self *this)` stays `redefinition`. `extern(abi="c")` on a generic function, or on a function whose parameter is a trait, is `extern(abi="c") on a trait or a generic`. `extern` and `extern(abi="tlc")` on a generic function name that function in a tlc library. The call stays `id(value)` or `id[i32](value)`. Each specialization is its own symbol. When the file is `main.tlc` and `T` is `i32`, that symbol is `main.id.spec.i32`. A body defines the symbol. A prototype may be the declaration that says `extern`, and the body that follows defines it. A call of a generic that has no body is `not supported yet`. `...` on a generic function stays rejected. `...` is only legal on `extern(abi="c")`, so the declaration is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, is `not supported yet`. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

## Assembly

```asm
id:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
callq id                         # call id
retq                             # return
```

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: Comptime](comptime.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
