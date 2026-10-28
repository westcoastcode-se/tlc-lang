# Generics

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: Comptime](comptime.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page is how a generic function specializes one body for each call.

## Parameters

A generic function names its parameters in brackets.

`T type` is a type.

`N i32` is a compile-time integer.

`T Shape` is a type that must have the methods and the fields of the trait `Shape`.

One body is specialized for each call.

A generic function has no `impl` and no runtime table.

A parameter the function does not use is `unused parameter`.

The name has to appear in the signature or the body.

`func id[T type](n i32) i32` is that error.

`func width[N i32](n i32) i32` is that error when the body does not read `N`.

`sizeof(T)` uses `T`.

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

## A pointer when the type argument is an array

`*T`, when `T` is `[4]i32`, is a pointer to that array.

`p[i]` and `(*p)[i]` are the element.

`**T` is a pointer to that pointer.

`?*T` is that pointer or `null`.

After `if (p == null) return`, `p` is `*T` and `p[i]` is the element.

`*const T` is a pointer to the constant array.

Reading `p[0]` is allowed.

`p[0] = 1` is reported as `type mismatch`.

When `T` is `[]i32`, `(*p)[i]` is the element.

A field `p *T` on `Hold[T]` is that pointer.

`box.p[i]` is the element.

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

## A trait constraint in the same package

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

`measure(r)` returns 42.

`value *T` accepts a value receiver and a pointer receiver.

Passing the struct where the parameter is `*T` is `type mismatch`.

The call is `measure(&r)`.

A parameter written `T` requires a value receiver.

A pointer receiver on that parameter is `type mismatch`.

`export func measure[T Shape]` publishes that function.

Another package calls `pkg.measure(r)`.

The trait is the one in the function's package.

The method on the struct is exported.

Without `export` on that method, the call is `area is not exported`.

`export func use(s Shape)` publishes that call without a type-parameter list.

See [Traits](../methods/trait.md).

## A trait from another package

`T math.Shape` names the exported trait `Shape` in `math`.

The file that writes the function imports `math`.

A caller in another package does not import `math`.

`geom.measure(r)` is that call when `geom` wrote `T math.Shape`.

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

`measure(r)` returns 42.

`export type Shape trait` publishes the trait.

That trait has no symbol.

`extern type Shape trait` is `not supported yet`.

A name that is not exported is `Shape is not exported`.

A name that is not a trait is `type mismatch`.

A qualifier that is not an imported package is `undeclared identifier`.

`import "math" as m` writes `T m.Shape`.

`value *T`, `measure[Rect](r)`, and a missing method follow `T Shape`.

`type Hold[T math.Shape] struct` is that constraint on a struct.

`func Point::measure[T math.Shape]` is that constraint on a method.

The call is `Point.measure[Rect](r)`.

`func use(s math.Shape)`, `func use(s *math.Shape)`, and `import { Shape }` are `not supported yet`.

`var s math.Shape` is `trait has no size`.

## Inference and an omitted result

`id(1)` infers `T` from the argument.

The result may be omitted, and `func id[T type](value T) { return value; }` is that same function.

A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value.

The omitted result may be a function pointer.

`func hold[T type](value T) { return add; }` is `func (i32, i32) i32` when `add` is `func (i32, i32) i32`.

`repeat[4](7)` writes `N` and infers `T`.

`id[i32](1)` writes the type.

A type that cannot be inferred is reported as `type mismatch`.

A type parameter bound to two different types in one call is reported as `type mismatch`.

`Vec[i32]` and `Vec[u8]` for `Vec[T]` are that case.

`sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` see the concrete type and the integer.

## `sizeof` as a type argument

`sizeof` produces `i32`, so it may be a compile-time integer type argument.

`width[sizeof(Point)]()` is the same specialization as `width[8]()`.

`repeat[sizeof(i32)](7)` is the same specialization as `repeat[4](7)`.

`sizeof n` is that integer.

`Buf[sizeof(i32)].span()` reads the one argument as that integer.

`sizeof(math.Point)` is the size of an exported struct.

`id[sizeof(i32)](1)` is `type mismatch`.

`alignof` in a type argument is `expected a type`.

`Buf[alignof(i32)].span()` is `type mismatch`.

`sizeof(i32) + 1` is `expected ']'`.

`sizeof(T)` when `T` is an array is that array's size.

`use[[4]i32]()` is the same specialization as `width[16]()`.

When the file is `main.tlc`, `use` is `main.use.spec.arr.4.i32`.

`plain[[4]u8]()` is `movl $4, %eax` and `retq`.

### Sizes and spellings of array types

The size of `[]i32` is 16, the size of `[2][]i32` is 32, and the size of `*[4]i32` is 8.

The size of `[4]*i32` is 32, the size of `[2][3]i32` is 24, and the size of `[2]Point` is 16.

`alignof(T)` is the alignment of that type.

`sizeof T` is that same integer.

`sizeof(*T)`, when `T` is an array, is `not supported yet`, and so is `sizeof([4]T)`.

`typeof(T)` when `T` is an array is that array.

`kind[[4]i32]()` is the same type as `typeof([4]i32)`.

When the file is `main.tlc`, `kind` is `main.kind.spec.arr.4.i32`.

`typeof T` is that same type.

`[4]u8` spells `[4]u8`, `[2][3]i32` spells `[2][3]i32`, and `[]i32` spells `[]i32`.

`*[4]i32` spells `*[4]i32`, `[4]*i32` spells `[4]*i32`, and `[2][]i32` spells `[2][]i32`.

`[2]Point` spells `[2]Point`, and `[]const u8` spells `[]const u8`.

`typeof(T).name()` is that spelling.

A parameter `[4]i32` is a pointer, so `typeof(value)` is `*i32`.

`typeof(*T)`, when `T` is an array, is `not supported yet`, and so is `typeof([4]T)`.

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

The program returns 8.

When the file is `main.tlc`, the symbol is `main.width.spec.8`.

`repeat[sizeof(i32)]` is `main.repeat.spec.4.i32`.

```c
func width[N i32]() i32 {
    return N;
}

func use[T type]() i32 {
    return width[sizeof(T)]();
}

func main() i32 {
    return use[[4]i32]();
}
```

```asm
use.spec.arr.4.i32:
# trace frame for the call, then the matching pop
callq width.spec.16              # call width
retq                             # return

width.spec.16:
movl $16, %eax                   # constant
retq                             # return

main:
# trace frame for the call, then the matching pop
callq use.spec.arr.4.i32         # call use
retq                             # return
```

The program returns 16.

When the file is `main.tlc`, `use` is `main.use.spec.arr.4.i32` and `width` is `main.width.spec.16`.

```c
func kind[T type]() i32 {
    return i32(typeof(T) == typeof([4]i32));
}

func main() i32 {
    return kind[[4]i32]();
}
```

```asm
kind.spec.arr.4.i32:
movl $1, %eax                    # constant
retq                             # return

main:
# trace frame for the call, then the matching pop
callq kind.spec.arr.4.i32        # call kind
retq                             # return
```

The program returns 1.

When the file is `main.tlc`, the symbol is `main.kind.spec.arr.4.i32`.

## A comptime call as a type argument

A call of a `comptime func` may be a compile-time integer type argument.

`width[add(3)]()` is the same specialization as `width[4]()`.

`Buf[add(3)].span()` reads the one argument as that integer.

A local `const` whose initializer folds to `i32` may be that argument.

`width[n]()` when `const n = 2` is `width[2]()`.

That local hides a file-scope `const` of the same name.

It also hides a const brought in with `import { n }`.

A `var` is `type mismatch`.

An ordinary function is `not supported yet`.

`add(3) + 1` is `expected ']'`.

A body that builds the next value, such as `return width[add(N)]()`, stops after 32 specializations with `not supported yet`.

`width[add(3)]()` in `main` is one specialization.

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

The program returns 4.

When the file is `main.tlc`, the symbol is `main.width.spec.4`.

## A generic name used as a value

`var f = id` names that generic.

`f := id` names that generic.

`const f = id` names that generic.

`var g = f` names that generic.

`f(1)` and `f[i32](1)` specialize it the same way as `id`.

The name is one in this package.

`var f = pkg.id` is `not supported yet`.

The call stays `pkg.id(1)` or `pkg.id[i32](1)`.

The binding is not a function pointer and has no storage.

A use that is not a call is `not supported yet`.

`return f` is that use.

`take(f)`, where a function pointer is expected, is `not supported yet`.

`f = id` is `not supported yet`.

A file-scope `var f = id` is `not supported yet`.

An overloaded function that is not generic stays `not supported yet` when it is used as a value.

An instance method used as a value is `type mismatch`.

## One type for the whole value

`T` is the type of the value.

That value may be an integer, a struct with several fields, or a tuple of several types.

A tuple inside a tuple does not match `T`.

`take[T type](p (T, i32))` of `((40, 2), 1)` is `not supported yet`.

`func take(other (i32, f32))` is a tuple parameter.

`[P1, P2]` is `expected a type` on the comma.

With `[P1 type, P2 type]`, `other (P1, P2)` is that tuple.

`SomeFunc[i32, i32](0, (40, 2))` writes the types.

`SomeFunc(0, (40, 2))` infers them.

A different element count is `type mismatch`.

`Duo` where `(P1, P2)` is written is `type mismatch`.

`[...U]` is not a type parameter.

`...` in that list is `expected an identifier`.

`...` is the C argument list on `extern(abi="c")`.

`(40)` is one value, the same call as `id(40)`, when the parameter is not a tuple of one element.

`func take(n (i32))` takes that tuple.

`take((40))` matches it.

`take(40)` is `type mismatch`.

`(40, 2)` is a tuple.

The element types are the types of the values before promotion.

`'a'` stays `u8`.

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

`id[i32](40)` writes the type.

A value in that list, such as `id[4]`, is reported as `type mismatch`.

`return value` returns the tuple.

`return id((1, 2))` from a function with those two results returns the tuple too.

`a, b :=` unpacks them.

`var value1, value2 = value` unpacks them when `value` holds that tuple.

`var value = (40, 2)` binds the tuple itself.

`...` in a type-parameter list is not the `...` that `builtin.va()` reads.

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

`SomeFunc[i32, i32](0, (40, 2))` returns 42.

`SomeFunc(0, (40, 2))` is that same call.

`other[0]` and `other[1]` read the elements.

The index is a constant.

## Several parameter lists on one name

One generic name may have several parameter lists.

A call uses the types of the arguments, the same way as a function that is not generic.

`func id[T type](value T) T` and `func id[T type](value T, extra i32) T` are both `id`.

Type-parameter names are not part of that list.

`id[T type](value T)` and `id[U type](value U)` are one list.

The same parameter list is `redefinition`.

A generic function and a concrete function of the same name are `redefinition`.

A call that matches none of the lists is `type mismatch` or `argument count`.

A call that matches more than one list is `type mismatch` or `argument count`.

## Methods on a generic struct

A method on a generic struct follows the same rule.

The receiver is not one of those parameters.

`func Box[T]::get(self *this) T` and `func Box[T]::get(self *this, n i32) T` are two methods, chosen from the remaining arguments.

`func Box[T]::get(self this)` beside `func Box[T]::get(self *this)` stays `redefinition`.

## Extern, a trait parameter, and a missing body

`extern(abi="c")` on a generic function is `extern(abi="c") on a trait or a generic`.

`extern(abi="c")` on a function whose parameter is a trait is `extern(abi="c") on a trait or a generic`.

A bare `extern` and `extern(abi="tlc")` on a generic function are `not supported yet`.

```c
extern func id[T type](value T) T;

func main() i32 {
    return 0;
}
```

That declaration emits nothing.

A generic prototype with no body is `not supported yet`.

`func id[T type](value T) T` then `id(1)` is that error.

A comptime parameter on that prototype is `not supported yet`.

`...` on a generic function stays rejected.

`...` is only legal on `extern(abi="c")`.

That declaration is `not supported yet`.

A parameter whose type is a trait, such as `s Shape` beside `[T type]`, is `not supported yet`.

`extern(name="symbol")` is `name requires abi="c"`.

`extern(abi="tlc", name="symbol")` is `name requires abi="c"`.

[Index](../README.md) · [Previous: Nested functions](nested.md) · [Next: Comptime](comptime.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
