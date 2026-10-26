# Generics

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: Comptime](comptime.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A generic function names its parameters in brackets. `T type` is a type. `N i32` is a compile-time integer. `T Shape` is a type that must have the methods and the fields of the trait `Shape`. One body is specialized for each call. There is no `impl` and no runtime table. A parameter the function does not use is `unused parameter`. The name has to appear in the signature or the body. `func id[T type](n i32) i32` is that error. `func width[N i32](n i32) i32` is that error when the body does not read `N`. `sizeof(T)` uses `T`.

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

```asm
i32:
movq %rdi, -16(%rsp)             # move
movl %esi, -4(%rsp)              # move
movq -16(%rsp), %rax             # move
movl -4(%rsp), %ecx              # move
movslq %ecx, %rdx                # move
movl (%rax,%rdx,4), %eax         # move
movslq %ecx, %rcx                # move
retq                             # return
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

```asm
measure:
# trace frame for the call, then the matching pop
callq area                       # call area
retq                             # return

area:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
imull -4(%rsp), %eax             # multiply
retq                             # return
```

`measure(r)` returns 42. `value *T` accepts a value receiver and a pointer receiver. Passing the struct where the parameter is `*T` is `type mismatch`. The call is `measure(&r)`. A parameter written `T` requires a value receiver. A pointer receiver there is `type mismatch`.

`export func measure[T Shape]` publishes that function. Another package calls `pkg.measure(r)`. The trait is the one in the function's package. The method on the struct is exported. Without `export` on that method, the call is `area is not exported`. `export func use(s Shape)` publishes that call without a type-parameter list. See [Traits](../methods/trait.md).

`T math.Shape` names the exported trait `Shape` in `math`. The file that writes the function imports `math`. A caller in another package does not. `geom.measure(r)` is that call when `geom` wrote `T math.Shape`.

```c
package math;

export type Shape trait {
    func area() i32;
};
```

```c
import "math";

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func measure[T math.Shape](value T) i32 {
    return value.area();
}

func main() i32 {
    var r Rect;
    r.w = 6;
    r.h = 7;
    return measure(r);
}
```

```asm
measure:
# trace frame for the call, then the matching pop
callq area                       # call area
retq                             # return

area:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
imull -4(%rsp), %eax             # multiply
retq                             # return
```

`measure(r)` returns 42. `export type Shape trait` publishes the trait. It has no symbol. `extern type Shape trait` is `not supported yet`. A name that is not exported is `Shape is not exported`. A name that is not a trait is `type mismatch`. A qualifier that is not an imported package is `undeclared identifier`. `import "math" as m` writes `T m.Shape`. `value *T`, `measure[Rect](r)`, and a missing method follow `T Shape`. `type Hold[T math.Shape] struct` is that constraint on a struct. `func Point::measure[T math.Shape]` is that constraint on a method. The call is `Point.measure[Rect](r)`. `func use(s math.Shape)`, `func use(s *math.Shape)`, and `import { Shape }` are `not supported yet`. `var s math.Shape` is `trait has no size`.

`id(1)` infers `T`. The result may be omitted: `func id[T type](value T) { return value; }` is that same function. A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value. The omitted result may be a function pointer: `func hold[T type](value T) { return add; }` is `func (i32, i32) i32` when `add` is `func (i32, i32) i32`. `repeat[4](7)` writes `N` and infers `T`. `id[i32](1)` writes the type. A type that cannot be inferred is reported as `type mismatch`. A type parameter bound to two different types in one call, such as `Vec[i32]` and `Vec[u8]` for `Vec[T]`, is reported as `type mismatch`. `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` see the concrete type and the integer.

`sizeof` produces `i32`, so it may be a compile-time integer type argument. `width[sizeof(Point)]()` is the same specialization as `width[8]()`. `repeat[sizeof(i32)](7)` is the same specialization as `repeat[4](7)`. `sizeof n` is that integer. `Buf[sizeof(i32)].span()` reads the one argument as that integer. `sizeof(math.Point)` is the size of an exported struct. `id[sizeof(i32)](1)` is `type mismatch`. `alignof` in a type argument is `expected a type`. `Buf[alignof(i32)].span()` is `type mismatch`. `sizeof(i32) + 1` is `expected ']'`. `sizeof(T)` when `T` is an array is `not supported yet`.

```c
type Point struct { x i32; y i32; };

func width[N i32]() i32 {
    return N;
}

func main() i32 {
    return width[sizeof(Point)]();
}
```

```asm
width.spec.8:
movl $8, %eax                    # constant
retq                             # return

main:
# trace frame for the call, then the matching pop
callq width.spec.8               # call width
retq                             # return
```

The program returns 8. When the file is `main.tlc`, the symbol is `main.width.spec.8`. `repeat[sizeof(i32)]` is `main.repeat.spec.4.i32`.

A call of a `comptime func` may be a compile-time integer type argument. `width[add(3)]()` is the same specialization as `width[4]()`. `Buf[add(3)].span()` reads the one argument as that integer. A local `const` whose initializer folds to `i32` may be that argument. `width[n]()` when `const n = 2` is `width[2]()`. That local hides a file-scope `const` of the same name, and a const brought in with `import { n }`. A `var` is `type mismatch`. An ordinary function is `not supported yet`. `add(3) + 1` is `expected ']'`. A body that builds the next value, such as `return width[add(N)]()`, stops after 32 specializations with `not supported yet`. `width[add(3)]()` in `main` is one specialization.

```c
comptime func add(n i32) i32 {
    return n + 1;
}

func width[N i32]() i32 {
    return N;
}

func main() i32 {
    return width[add(3)]();
}
```

```asm
width.spec.4:
movl $4, %eax                    # constant
retq                             # return

main:
# trace frame for the call, then the matching pop
callq width.spec.4               # call width
retq                             # return
```

The program returns 4. When the file is `main.tlc`, the symbol is `main.width.spec.4`.

`var f = id` names that generic. `f := id` and `const f = id` do too, and so does `var g = f`. `f(1)` and `f[i32](1)` specialize it the same way as `id`. The name is one in this package. `var f = pkg.id` is `not supported yet`, and the call stays `pkg.id(1)` or `pkg.id[i32](1)`. The binding is not a function pointer and has no storage. A use that is not a call, such as `return f` or `take(f)` where a function pointer is expected, is `not supported yet`. `f = id` is `not supported yet`. A file-scope `var f = id` is `not supported yet`. An overloaded function that is not generic stays `not supported yet` when it is used as a value. An instance method used as a value is `type mismatch`.

### One type parameter

`T` is the type of the value. That value may be an integer, a struct with several fields, or a tuple of several types. `func take(other (i32, f32))` is a tuple parameter. `[P1, P2]` is `expected a type` on the comma. With `[P1 type, P2 type]`, `other (P1, P2)` is that tuple. `SomeFunc[i32, i32](0, (40, 2))` writes the types. `SomeFunc(0, (40, 2))` infers them. A different element count, or `Duo` where `(P1, P2)` is written, is `type mismatch`. `[...U]` is not a type parameter. `...` in that list is `expected an identifier`. `...` is the C argument list on `extern(abi="c")`. `(40)` is one value, the same call as `id(40)`, when the parameter is not a tuple of one element. `func take(n (i32))` takes that tuple. `take((40))` matches it. `take(40)` is `type mismatch`. `(40, 2)` is a tuple. The element types are the types of the values before promotion, so `'a'` stays `u8`.

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

```asm
id:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
movl -4(%rsp), %edx              # move
retq                             # return

u8:
movb %sil, %al                   # move
movl %edi, -8(%rsp)              # move
movb %al, -4(%rsp)               # move
movl -8(%rsp), %eax              # move
movb -4(%rsp), %dl               # move
retq                             # return

i32:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
movl -4(%rsp), %edx              # move
retq                             # return

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

`id[i32](40)` writes the type. A value in that list, such as `id[4]`, is reported as `type mismatch`. `return value` returns the tuple, and so does `return id((1, 2))` from a function with those two results. `a, b :=` unpacks them, and so does `var value1, value2 = value` when `value` holds that tuple. `var value = (40, 2)` binds the tuple itself. `...` in a type-parameter list is not the `...` that `builtin.va()` reads.

```c
func SomeFunc[P1 type, P2 type](value i32, other (P1, P2)) i32 {
    return value + other[0] + other[1];
}

func main() i32 {
    return SomeFunc[i32, i32](0, (40, 2));
}
```

```asm
SomeFunc:
movl %edi, -12(%rsp)             # move
movl %edx, -4(%rsp)              # move
movl %esi, -8(%rsp)              # move
movl -12(%rsp), %eax             # move
movl -8(%rsp), %edx              # move
movl -4(%rsp), %ecx              # move
addl %edx, %eax                  # add
addl %ecx, %eax                  # add
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $40, %esi                   # constant
movl $2, %edx                    # constant
callq SomeFunc                   # call SomeFunc
retq                             # return
```

`SomeFunc[i32, i32](0, (40, 2))` returns 42. `SomeFunc(0, (40, 2))` is that same call. `other[0]` and `other[1]` read the elements. The index is a constant.

One generic name may have several parameter lists. A call uses the types of the arguments, the same way as a function that is not generic. `func id[T type](value T) T` and `func id[T type](value T, extra i32) T` are both `id`. Type-parameter names are not part of that list, so `id[T type](value T)` and `id[U type](value U)` are one list. The same parameter list is `redefinition`. A generic function and a concrete function of the same name are `redefinition`. A call that matches none or more than one list is `type mismatch` or `argument count`.

A method on a generic struct follows the same rule. The receiver is not one of those parameters. `func Box[T]::get(self *this) T` and `func Box[T]::get(self *this, n i32) T` are two methods, chosen from the remaining arguments. `func Box[T]::get(self this)` beside `func Box[T]::get(self *this)` stays `redefinition`. `extern(abi="c")` on a generic function, or on a function whose parameter is a trait, is `extern(abi="c") on a trait or a generic`. `extern` and `extern(abi="tlc")` on a generic function name that function in a tlc library. The call stays `id(value)` or `id[i32](value)`. Each specialization is its own symbol. When the file is `main.tlc` and `T` is `i32`, that symbol is `main.id.spec.i32`. A body defines the symbol. A prototype may be the declaration that says `extern`, and the body that follows defines it. A call of a generic that has no body is `not supported yet`. `...` on a generic function stays rejected. `...` is only legal on `extern(abi="c")`, so the declaration is `not supported yet`. A parameter whose type is a trait, such as `s Shape` beside `[T type]`, is `not supported yet`. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: Comptime](comptime.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
