# Generics

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: typeof as a result](typeof.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

`export func measure[T Shape]` publishes that function. Another package calls `pkg.measure(r)`. The trait is the one in the function's package. The method on the struct is exported. Without `export` on that method, the call is `area is not exported`.

`id(1)` infers `T`. `repeat[4](7)` writes `N` and infers `T`. `id[i32](1)` writes the type. A type that cannot be inferred is reported as `type mismatch`. A type parameter bound to two different types in one call, such as `Vec[i32]` and `Vec[u8]` for `Vec[T]`, is reported as `type mismatch`. `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` see the concrete type and the integer.

### Type packs

`[...U]` is a type pack. It is the last parameter in the brackets, so `func head[T type, ...U](value T, rest U) T` is accepted. A pack before another parameter, as in `[...U, T type]`, is reported as `not supported yet`. One argument type makes `U` that type, so `*U` is a pointer to it. Several argument types make `U` a tuple. `(40)` is one value, the same call as `keep(40)`. `(40, 2)` is a tuple. The element types are the types of the values before promotion, so `'a'` stays `u8`.

```c
func keep[...U](args U) U {
    return args;
}

func first[...U](args U) typeof(args[0]) {
    return args[0];
}

func main() i32 {
    var n = keep(40);
    var written = keep[i32](1);
    a, b := keep((40, 2));
    x, y := keep((40, 'a'));
    u, v := keep[i32, u8]((1, 'b'));
    return n + written + a + first((7, 8)) + u + v;
}
```

`keep[i32](40)` and `keep[i32, u8]((40, 'a'))` write the types. A value in that list, such as `keep[4]`, is reported as `type mismatch`. `head(40, (1, 2))` infers `T` and `U`. `head[i32, i32](40, 2)` writes them. The type arguments before `...U` bind the parameters in front of the pack, and the rest fill `U`. `first(40)` is reported as `type mismatch`: `args` is `i32`, and `args[0]` reads a tuple. The index is a constant. `return args` returns the tuple, and so does `return keep((1, 2))` from a function with those two results. `a, b :=` unpacks them, and so does `var value1, value2 = value` when `value` holds that tuple. `var value = (40, 2)` binds the tuple itself. `[...U]` is not the `...` that `builtin.va()` reads.

One generic name may have several parameter lists. A call uses the types of the arguments, the same way as a function that is not generic. `func id[T type](value T) T` and `func id[T type](value T, extra i32) T` are both `id`. Type-parameter names are not part of that list, so `id[T type](value T)` and `id[U type](value U)` are one list. The same parameter list is `redefinition`. A generic function and a concrete function of the same name are `redefinition`. A call that matches none or more than one list is `type mismatch` or `argument count`.

A method on a generic struct follows the same rule. The receiver is not one of those parameters. `func Box[T]::get(self *this) T` and `func Box[T]::get(self *this, n i32) T` are two methods, chosen from the remaining arguments. `func Box[T]::get(self this)` beside `func Box[T]::get(self *this)` stays `redefinition`.

## Cost

A generic function is specialized while compiling. `return id(40)` calls `id` for `i32` and pushes a trace frame. `main` executes 38 instructions when fewer than 32 frames are stored, which is the no-argument frame plus 1 `movl` of the argument. The specialized `id` is 3 instructions: store the parameter, load it, `retq`.

`T Shape` is also specialized. `measure(r)` for a struct of two `i32` fields stores those fields and copies them into the argument registers. The struct is not zeroed first. `main` executes 44 instructions when fewer than 32 frames are stored. The call pushes a trace frame. Inside the copy, `value.area()` is an ordinary traced call.

A type pack of one type is that value and costs whatever returning it costs. A pack of two integers is the two-result return, 7 instructions in the callee. Choosing a parameter list adds no instructions. The call runs the selected function.

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: typeof as a result](typeof.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
