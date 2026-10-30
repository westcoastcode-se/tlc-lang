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
    return id(40) + id[i32](0);
}
```

```asm
main.id.spec.i32.spec.0:
xorl %eax, %eax                  # constant
retq                             # return

main:
subq $40, %rsp                   # allocate
# trace frame for the call, then the matching pop
callq main.id.spec.i32.spec.40   # call id(40)
movl %eax, %ecx                  # move
movl %ecx, 28(%rsp)              # move
# trace frame for the call, then the matching pop
callq main.id.spec.i32.spec.0    # call id(0)
movl %eax, %ecx                  # move
movl %ecx, 16(%rsp)              # move
movl 16(%rsp), %ecx              # move
movl 28(%rsp), %eax              # move
addl %ecx, %eax                  # add
addq $40, %rsp                   # release
retq                             # return

main.id.spec.i32.spec.40:
movl $40, %eax                   # constant
retq                             # return
```

`id(40)` does not pass `40`.

`id[i32](0)` does not pass `0`.

The program returns 40.

A `var` is still passed.

```c
func plain[T type](value T) i32 {
    return value;
}

func main() i32 {
    var n = 7;
    return plain(2) + plain(n);
}
```

```asm
main.plain.spec.i32:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return

main.plain.spec.i32.spec.2:
movl $2, %eax                    # constant
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $7, 36(%rsp)                # n = 7
# trace frame for the call, then the matching pop
callq main.plain.spec.i32.spec.2 # call plain(2)
movl %eax, %ecx                  # move
movl %ecx, 24(%rsp)              # move
movl 36(%rsp), %eax              # n
movl %eax, 12(%rsp)              # move
# trace frame for the call, then the matching pop
movl 12(%rsp), %edi              # n
callq main.plain.spec.i32        # call plain(n)
movl %eax, %ecx                  # move
movl %ecx, 8(%rsp)               # move
movl 8(%rsp), %ecx               # move
movl 24(%rsp), %eax              # move
addl %ecx, %eax                  # add
addq $40, %rsp                   # release
retq                             # return
```

`plain(2)` calls `main.plain.spec.i32.spec.2` and does not pass `2`.

`plain(n)` calls `main.plain.spec.i32` and passes `n`.

The program returns 9.

`comptime if (value == 2)` keeps that arm when the argument is `2`.

A `var` in that call is `not supported yet`. The condition does not fold.

A runtime `const` is that same error.

`comptime const n = 2` is the comptime argument.

`func only[T type](value comptime T)` requires a comptime argument.

A `var` is `type mismatch`.

`func apply[T type](op func (T, T) T, a T, b T) T` takes that function pointer. `T` is the argument type and the result type.

`apply[i32](add, a, b)` calls `main.apply.spec.i32` and passes `add`.

`apply(add, a, b)` infers `i32` when `a` and `b` are `i32`.

`apply[i32](add, 40, 2)` calls `main.apply.spec.i32.spec.40.2` and does not pass `40` or `2`.

A function of the wrong signature is `type mismatch`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func apply[T type](op func (T, T) T, a T, b T) T {
    return op(a, b);
}

func main() i32 {
    var a = 40;
    var b = 2;
    return apply[i32](add, a, b);
}
```

```asm
main.apply.spec.i32:
subq $56, %rsp                   # allocate
movq %rdi, 40(%rsp)              # op
movl %esi, 48(%rsp)              # a
movl %edx, 52(%rsp)              # b
movq 40(%rsp), %rax              # op
movq %rax, 16(%rsp)              # move
movl 48(%rsp), %eax              # a
movl %eax, 24(%rsp)              # move
movl 52(%rsp), %eax              # b
movl %eax, 28(%rsp)              # move
# trace frame for the call, then the matching pop
movq 16(%rsp), %rax              # op
movl 28(%rsp), %esi              # b
movl 24(%rsp), %edi              # a
callq *%rax                      # call op
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # move
addq $56, %rsp                   # release
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $40, 32(%rsp)               # a = 40
movl $2, 36(%rsp)                # b = 2
movl 32(%rsp), %eax              # a
movl %eax, 16(%rsp)              # move
movl 36(%rsp), %eax              # b
movl %eax, 20(%rsp)              # move
# trace frame for the call, then the matching pop
movl 20(%rsp), %edx              # b
movl 16(%rsp), %esi              # a
leaq 59(%rip), %rdi              # address of add
callq main.apply.spec.i32        # call apply
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # move
addq $40, %rsp                   # release
retq                             # return

main.add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

The program returns 42.

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

`func id[T type](value T) T` of `((40, 2), 1)` binds `T` to that tuple.

`func id[T type](value *T)` of `&row`, when `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The call is `main.id.spec.tuple.2.i32.i32`.

`tuple.2` is the number of elements. `((40, 2), 1)` is `tuple.2.tuple.2.i32.i32.i32`.

`(*value)[0]` is 40 and `(*value)[1]` is 2.

`id(&deep)`, when `deep` is `((40, 2), 1)`, calls `main.id.spec.tuple.2.tuple.2.i32.i32.i32`.

`(*value)[0][0]` is 40 and `(*value)[1]` is 1.

`take[T type](p (*T, i32))` of `(&row, 1)`, when `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

`(*p[0])[0]` is 40 and `p[1]` is 1.

`type Box[T type] struct { n T; }` of `make(row)`, when `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The call is `main.make.spec.tuple.2.i32.i32`.

`box.n[0]` is 40 and `box.n[1]` is 2.

`make(deep)`, when `deep` is `((40, 2), 1)`, calls `main.make.spec.tuple.2.tuple.2.i32.i32.i32`.

`box.n[0][0]` is 40 and `box.n[1]` is 1.

```c
type Box[T type] struct { n T; }

func make[T type](value T) Box[T] {
    var box Box[T];
    box.n = value;
    return box;
}

func main() i32 {
    var row = (40, 2);
    var box = make(row);
    return box.n[0] + box.n[1];
}
```

```asm
main.make.spec.tuple.2.i32.i32:
movl %edi, -16(%rsp)             # value[0]
movl %esi, -12(%rsp)             # value[1]
movl -16(%rsp), %eax             # value[0]
movl -12(%rsp), %ecx             # value[1]
movl %ecx, -4(%rsp)              # box.n[1]
movl %eax, -8(%rsp)              # box.n[0]
movl -8(%rsp), %eax              # box.n[0]
movl -4(%rsp), %edx              # box.n[1]
retq                             # return

main:
subq $56, %rsp                   # allocate
movl $2, 44(%rsp)                # row[1] = 2
movl $40, 40(%rsp)               # row[0] = 40
movl 40(%rsp), %eax              # row[0]
movl %eax, 24(%rsp)              # move
movl 44(%rsp), %eax              # row[1]
movl %eax, 28(%rsp)              # move
# trace frame for the call, then the matching pop
movl 28(%rsp), %esi              # row[1]
movl 24(%rsp), %edi              # row[0]
callq main.make.spec.tuple.2.i32.i32 # call make
movl %eax, %ecx                  # move
movl %edx, 16(%rsp)              # box.n[1]
movl %ecx, 20(%rsp)              # box.n[0]
movl 16(%rsp), %eax              # box.n[1]
movl 20(%rsp), %ecx              # box.n[0]
movl %ecx, 48(%rsp)              # box.n[0]
movl %eax, 52(%rsp)              # box.n[1]
movl 48(%rsp), %eax              # box.n[0]
movl 52(%rsp), %ecx              # box.n[1]
addl %ecx, %eax                  # add
addq $56, %rsp                   # release
retq                             # return
```

The program returns 42.

`row [2]T` on that struct, when `T` is `(i32, i32)`, is an array of that tuple.

The call `make(one, two)` is `main.make.spec.tuple.2.i32.i32`.

`box.row[0][0]` is 40 and `box.row[1][1]` is 3.

`make(40, 2)` is that array of `i32`.

```c
type Box[T type] struct { row [2]T; }

func make[T type](a T, b T) Box[T] {
    var box Box[T];
    box.row[0] = a;
    box.row[1] = b;
    return box;
}

func main() i32 {
    var one = (40, 2);
    var two = (1, 3);
    var box = make(one, two);
    return box.row[0][0] + box.row[1][1];
}
```

```asm
main.make.spec.tuple.2.i32.i32:
movq %rdi, %rax                  # box
movl %esi, -32(%rsp)             # a[0]
movl %edx, -28(%rsp)             # a[1]
movl %r8d, -20(%rsp)             # b[1]
movl %ecx, -24(%rsp)             # b[0]
movl -32(%rsp), %ecx             # a[0]
movl -28(%rsp), %edx             # a[1]
movl %edx, -12(%rsp)             # row[0][1]
movl %ecx, -16(%rsp)             # row[0][0]
movl -24(%rsp), %ecx             # b[0]
movl -20(%rsp), %edx             # b[1]
movl %edx, -4(%rsp)              # row[1][1]
movl %ecx, -8(%rsp)              # row[1][0]
movl -16(%rsp), %ecx             # row[0][0]
movl -12(%rsp), %edx             # row[0][1]
movl -8(%rsp), %esi              # row[1][0]
movl -4(%rsp), %r8d              # row[1][1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main:
subq $104, %rsp                  # allocate
movl $2, 76(%rsp)                # one[1] = 2
movl $40, 72(%rsp)               # one[0] = 40
movl $3, 84(%rsp)                # two[1] = 3
movl $1, 80(%rsp)                # two[0] = 1
movl 72(%rsp), %eax              # one[0]
movl %eax, 32(%rsp)              # move
movl 76(%rsp), %eax              # one[1]
movl %eax, 36(%rsp)              # move
movl 80(%rsp), %eax              # two[0]
movl %eax, 40(%rsp)              # move
movl 84(%rsp), %eax              # two[1]
movl %eax, 44(%rsp)              # move
# trace frame for the call, then the matching pop
movl 44(%rsp), %r8d              # two[1]
movl 40(%rsp), %ecx              # two[0]
movl 36(%rsp), %edx              # one[1]
movl 32(%rsp), %esi              # one[0]
leaq 56(%rsp), %rdi              # box
callq main.make.spec.tuple.2.i32.i32 # call make
movl 68(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 64(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 56(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 60(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 88(%rsp)              # row[0][0]
movl %edx, 92(%rsp)              # row[0][1]
movl %ecx, 96(%rsp)              # row[1][0]
movl %eax, 100(%rsp)             # row[1][1]
movl 88(%rsp), %eax              # row[0][0]
movl 96(%rsp), %ecx              # move
movl 100(%rsp), %ecx             # row[1][1]
addl %ecx, %eax                  # add
addq $104, %rsp                  # release
retq                             # return
```

The program returns 43.

`func pair[T type](a T, b T) [2]T`, when `T` is `(i32, i32)`, returns an array of that tuple.

The call `pair(one, two)` is `main.pair.spec.tuple.2.i32.i32`.

`got[0][0]` is 40 and `got[1][1]` is 3.

`var row [2]T` in that function is an array of that tuple.

`pair(40, 2)` is that array of `i32`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func main() i32 {
    var one = (40, 2);
    var two = (1, 3);
    var got = pair(one, two);
    return got[0][0] + got[1][1];
}
```

```asm
main.pair.spec.tuple.2.i32.i32:
movq %rdi, %rax                  # row
movl %esi, -32(%rsp)             # a[0]
movl %edx, -28(%rsp)             # a[1]
movl %r8d, -20(%rsp)             # b[1]
movl %ecx, -24(%rsp)             # b[0]
movl -32(%rsp), %ecx             # a[0]
movl -28(%rsp), %edx             # a[1]
movl %edx, -12(%rsp)             # row[0][1]
movl %ecx, -16(%rsp)             # row[0][0]
movl -24(%rsp), %ecx             # b[0]
movl -20(%rsp), %edx             # b[1]
movl %edx, -4(%rsp)              # row[1][1]
movl %ecx, -8(%rsp)              # row[1][0]
movl -16(%rsp), %ecx             # row[0][0]
movl -12(%rsp), %edx             # row[0][1]
movl -8(%rsp), %esi              # row[1][0]
movl -4(%rsp), %r8d              # row[1][1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main:
subq $120, %rsp                  # allocate
movl $2, 76(%rsp)                # one[1] = 2
movl $40, 72(%rsp)               # one[0] = 40
movl $3, 84(%rsp)                # two[1] = 3
movl $1, 80(%rsp)                # two[0] = 1
movl 72(%rsp), %eax              # one[0]
movl %eax, 32(%rsp)              # move
movl 76(%rsp), %eax              # one[1]
movl %eax, 36(%rsp)              # move
movl 80(%rsp), %eax              # two[0]
movl %eax, 40(%rsp)              # move
movl 84(%rsp), %eax              # two[1]
movl %eax, 44(%rsp)              # move
# trace frame for the call, then the matching pop
movl 44(%rsp), %r8d              # two[1]
movl 40(%rsp), %ecx              # two[0]
movl 36(%rsp), %edx              # one[1]
movl 32(%rsp), %esi              # one[0]
leaq 56(%rsp), %rdi              # got
callq main.pair.spec.tuple.2.i32.i32 # call pair
movl 68(%rsp), %ecx              # got[1][1]
movl %ecx, 16(%rsp)              # move
movl 64(%rsp), %ecx              # got[1][0]
movl %ecx, 20(%rsp)              # move
movl 56(%rsp), %ecx              # got[0][0]
movl %ecx, 24(%rsp)              # move
movl 60(%rsp), %ecx              # got[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # got[1][1]
movl 20(%rsp), %ecx              # got[1][0]
movl 28(%rsp), %edx              # got[0][1]
movl 24(%rsp), %esi              # got[0][0]
movl %esi, 88(%rsp)              # got[0][0]
movl %edx, 92(%rsp)              # got[0][1]
movl %ecx, 96(%rsp)              # got[1][0]
movl %eax, 100(%rsp)             # got[1][1]
movl 88(%rsp), %eax              # got[0][0]
movl 92(%rsp), %ecx              # move
movl 96(%rsp), %edx              # move
movl 100(%rsp), %esi             # got[1][1]
movl %esi, 116(%rsp)             # got[1][1]
movl %edx, 112(%rsp)             # got[1][0]
movl %ecx, 108(%rsp)             # got[0][1]
movl %eax, 104(%rsp)             # got[0][0]
movl 104(%rsp), %eax             # got[0][0]
movl 112(%rsp), %ecx             # move
movl 116(%rsp), %ecx             # got[1][1]
addl %ecx, %eax                  # add
addq $120, %rsp                  # release
retq                             # return
```

The program returns 43.

`func take[T type](row [2]T)`, when `T` is `(i32, i32)`, takes an array of that tuple.

The call `take(row)` is `main.take.spec.tuple.2.i32.i32`.

`row[0][0]` is 40 and `row[1][1]` is 3.

`sum` of `[2]i32` adds those two elements.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func take[T type](row [2]T) i32 {
    return row[0][0] + row[1][1];
}

func main() i32 {
    var one = (40, 2);
    var two = (1, 3);
    var row = pair(one, two);
    return take(row);
}
```

```asm
main.pair.spec.tuple.2.i32.i32:
movq %rdi, %rax                  # row
movl %esi, -32(%rsp)             # a[0]
movl %edx, -28(%rsp)             # a[1]
movl %r8d, -20(%rsp)             # b[1]
movl %ecx, -24(%rsp)             # b[0]
movl -32(%rsp), %ecx             # a[0]
movl -28(%rsp), %edx             # a[1]
movl %edx, -12(%rsp)             # row[0][1]
movl %ecx, -16(%rsp)             # row[0][0]
movl -24(%rsp), %ecx             # b[0]
movl -20(%rsp), %edx             # b[1]
movl %edx, -4(%rsp)              # row[1][1]
movl %ecx, -8(%rsp)              # row[1][0]
movl -16(%rsp), %ecx             # row[0][0]
movl -12(%rsp), %edx             # row[0][1]
movl -8(%rsp), %esi              # row[1][0]
movl -4(%rsp), %r8d              # row[1][1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.take.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # row
movq -8(%rsp), %rcx              # row
movl (%rcx), %eax                # row[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # row[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $120, %rsp                  # allocate
movl $2, 76(%rsp)                # one[1] = 2
movl $40, 72(%rsp)               # one[0] = 40
movl $3, 84(%rsp)                # two[1] = 3
movl $1, 80(%rsp)                # two[0] = 1
movl 72(%rsp), %eax              # one[0]
movl %eax, 32(%rsp)              # move
movl 76(%rsp), %eax              # one[1]
movl %eax, 36(%rsp)              # move
movl 80(%rsp), %eax              # two[0]
movl %eax, 40(%rsp)              # move
movl 84(%rsp), %eax              # two[1]
movl %eax, 44(%rsp)              # move
# trace frame for the call, then the matching pop
movl 44(%rsp), %r8d              # two[1]
movl 40(%rsp), %ecx              # two[0]
movl 36(%rsp), %edx              # one[1]
movl 32(%rsp), %esi              # one[0]
leaq 56(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32 # call pair
movl 68(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 64(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 56(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 60(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 88(%rsp)              # row[0][0]
movl %edx, 92(%rsp)              # row[0][1]
movl %ecx, 96(%rsp)              # row[1][0]
movl %eax, 100(%rsp)             # row[1][1]
movl 88(%rsp), %eax              # row[0][0]
movl 92(%rsp), %ecx              # move
movl 96(%rsp), %edx              # move
movl 100(%rsp), %esi             # row[1][1]
movl %esi, 116(%rsp)             # row[1][1]
movl %edx, 112(%rsp)             # row[1][0]
movl %ecx, 108(%rsp)             # row[0][1]
movl %eax, 104(%rsp)             # row[0][0]
# trace frame for the call, then the matching pop
leaq 104(%rsp), %rdi             # row
callq main.take.spec.tuple.2.i32.i32 # call take
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # take(row)
addq $120, %rsp                  # release
retq                             # return
```

The program returns 43.

`var g [2][2]T`, when `T` is `(i32, i32)`, is an array of arrays of that tuple.

Each `[n]` is one shell around that tuple.

A further `[2]` is another shell.

The call `fill(one, two, three, four)` is `main.fill.spec.tuple.2.i32.i32`.

`g[0][0][0]` is 40 and `g[1][1][1]` is 7.

`g[0][1]` and `g[1][0]` are not read.

The callee does not store them.

A `[2][2]i32` local reads `g[0][0]` and `g[1][1]`.

That program returns 43.

```c
func fill[T type](a T, b T, c T, d T) i32 {
    var g [2][2]T;
    g[0][0] = a;
    g[0][1] = b;
    g[1][0] = c;
    g[1][1] = d;
    return g[0][0][0] + g[1][1][1];
}

func main() i32 {
    var one = (40, 2);
    var two = (1, 3);
    var three = (4, 5);
    var four = (6, 7);
    return fill(one, two, three, four);
}
```

```asm
main.fill.spec.tuple.2.i32.i32:
movl 16(%rsp), %ecx              # d[1]
movl 8(%rsp), %eax               # d[0]
movl %edi, -48(%rsp)             # a[0]
movl %esi, -44(%rsp)             # a[1]
movl %ecx, -36(%rsp)             # d[1]
movl %eax, -40(%rsp)             # d[0]
movl -48(%rsp), %eax             # a[0]
movl -44(%rsp), %ecx             # a[1]
movl %ecx, -28(%rsp)             # g[0][0][1]
movl %eax, -32(%rsp)             # g[0][0][0]
movl -40(%rsp), %eax             # d[0]
movl -36(%rsp), %ecx             # d[1]
movl %ecx, -4(%rsp)              # g[1][1][1]
movl %eax, -8(%rsp)              # g[1][1][0]
movl -32(%rsp), %eax             # g[0][0][0]
movl -8(%rsp), %ecx              # move
movl -4(%rsp), %ecx              # g[1][1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $104, %rsp                  # allocate
movl $2, 76(%rsp)                # one[1] = 2
movl $40, 72(%rsp)               # one[0] = 40
movl $3, 84(%rsp)                # two[1] = 3
movl $1, 80(%rsp)                # two[0] = 1
movl $5, 92(%rsp)                # three[1] = 5
movl $4, 88(%rsp)                # three[0] = 4
movl $7, 100(%rsp)               # four[1] = 7
movl $6, 96(%rsp)                # four[0] = 6
movl 72(%rsp), %eax              # one[0]
movl %eax, 32(%rsp)              # move
movl 76(%rsp), %eax              # one[1]
movl %eax, 36(%rsp)              # move
movl 80(%rsp), %eax              # two[0]
movl %eax, 40(%rsp)              # move
movl 84(%rsp), %eax              # two[1]
movl %eax, 44(%rsp)              # move
movl 88(%rsp), %eax              # three[0]
movl %eax, 48(%rsp)              # move
movl 92(%rsp), %eax              # three[1]
movl %eax, 52(%rsp)              # move
movl 96(%rsp), %eax              # four[0]
movl %eax, 56(%rsp)              # move
movl 100(%rsp), %eax             # four[1]
movl %eax, 60(%rsp)              # move
# trace frame for the call, then the matching pop
movl 52(%rsp), %r9d              # three[1]
movl 48(%rsp), %r8d              # three[0]
movl 44(%rsp), %ecx              # two[1]
movl 40(%rsp), %edx              # two[0]
movl 36(%rsp), %esi              # one[1]
movl 32(%rsp), %edi              # one[0]
movl 56(%rsp), %r10d             # four[0]
movl 60(%rsp), %r11d             # four[1]
movq %rsp, %rax                  # d
movl %r11d, 8(%rax)              # d[1]
movl %r10d, (%rax)               # d[0]
callq main.fill.spec.tuple.2.i32.i32 # call fill
movl %eax, %ecx                  # move
movl %ecx, 28(%rsp)              # move
movl 28(%rsp), %eax              # fill
addq $104, %rsp                  # release
retq                             # return
```

The program returns 47.

`func id[T type](value *T)` of `&row`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.id.spec.arr.2.tuple.2.i32.i32`.

`(*value)[0][0]` is 40 and `(*value)[1][1]` is 3.

`id(&nums)`, when `nums` is `[2]i32`, is `spec.arr.2.i32`.

`&g`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func id[T type](value *T) i32 {
    return (*value)[0][0] + (*value)[1][1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return id(&row);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.id.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # (*value)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (*value)[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $88, %rsp                   # allocate
# trace frame for the call, then the matching pop
leaq 40(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 52(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 48(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 40(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 44(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 56(%rsp)              # row[0][0]
movl %edx, 60(%rsp)              # row[0][1]
movl %ecx, 64(%rsp)              # row[1][0]
movl %eax, 68(%rsp)              # row[1][1]
movl 56(%rsp), %eax              # row[0][0]
movl 60(%rsp), %ecx              # move
movl 64(%rsp), %edx              # move
movl 68(%rsp), %esi              # row[1][1]
movl %esi, 84(%rsp)              # row[1][1]
movl %edx, 80(%rsp)              # row[1][0]
movl %ecx, 76(%rsp)              # row[0][1]
movl %eax, 72(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # &row
callq main.id.spec.arr.2.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # id(&row)
addq $88, %rsp                   # release
retq                             # return
```

The program returns 43.

`func id[T type](value T)` of `row`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.id.spec.arr.2.tuple.2.i32.i32`.

The parameter is a pointer to that array.

`value[0][0]` is 40 and `value[1][1]` is 3.

`g`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

The outer array decays. The inner array stays.

`var g T = value` is that array.

`func give[T type](value T) T` returns that array.

`id(nums)`, when `nums` is `[2]i32`, is `spec.arr.2.i32`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func id[T type](value T) i32 {
    return value[0][0] + value[1][1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return id(row);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.id.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # value[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # value[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $88, %rsp                   # allocate
# trace frame for the call, then the matching pop
leaq 40(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 52(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 48(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 40(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 44(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 56(%rsp)              # row[0][0]
movl %edx, 60(%rsp)              # row[0][1]
movl %ecx, 64(%rsp)              # row[1][0]
movl %eax, 68(%rsp)              # row[1][1]
movl 56(%rsp), %eax              # row[0][0]
movl 60(%rsp), %ecx              # move
movl 64(%rsp), %edx              # move
movl 68(%rsp), %esi              # row[1][1]
movl %esi, 84(%rsp)              # row[1][1]
movl %edx, 80(%rsp)              # row[1][0]
movl %ecx, 76(%rsp)              # row[0][1]
movl %eax, 72(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # row
callq main.id.spec.arr.2.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # id(row)
addq $88, %rsp                   # release
retq                             # return
```

The program returns 43.

`func take[T type](p (T, i32))` of `(row, 1)`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.take.spec.arr.2.tuple.2.i32.i32`.

`p[0][0][0]` is 40, `p[0][1][1]` is 3, and `p[1]` is 1.

`g`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`p[0][0][0][0] + p[0][1][1][1] + p[1]` is 48.

`func make[T type](value T) (T, i32)` returns that tuple.

`got[0][0][0] + got[0][1][1] + got[1]` is 44.

`take((nums, 1))`, when `nums` is `[2]i32`, is `spec.arr.2.i32`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func take[T type](p (T, i32)) i32 {
    return p[0][0][0] + p[0][1][1] + p[1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return take((row, 1));
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.take.spec.arr.2.tuple.2.i32.i32:
movl %edi, -24(%rsp)             # p[0][0][0]
movl %esi, -20(%rsp)             # p[0][0][1]
movl %edx, -16(%rsp)             # p[0][1][0]
movl %ecx, -12(%rsp)             # p[0][1][1]
movl %r8d, -8(%rsp)              # p[1]
movl -8(%rsp), %ecx              # p[1]
movl -16(%rsp), %eax             # move
movl -12(%rsp), %edx             # p[0][1][1]
movl -24(%rsp), %eax             # p[0][0][0]
movl -20(%rsp), %esi             # move
addl %edx, %eax                  # add
addl %ecx, %eax                  # add
retq                             # return

main:
subq $120, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 84(%rsp), %ecx              # row[1][1]
movl %ecx, 48(%rsp)              # move
movl 80(%rsp), %ecx              # row[1][0]
movl %ecx, 52(%rsp)              # move
movl 72(%rsp), %ecx              # row[0][0]
movl %ecx, 56(%rsp)              # move
movl 76(%rsp), %ecx              # row[0][1]
movl %ecx, 60(%rsp)              # move
movl 48(%rsp), %eax              # row[1][1]
movl 52(%rsp), %ecx              # row[1][0]
movl 60(%rsp), %edx              # row[0][1]
movl 56(%rsp), %esi              # row[0][0]
movl %esi, 88(%rsp)              # row[0][0]
movl %edx, 92(%rsp)              # row[0][1]
movl %ecx, 96(%rsp)              # row[1][0]
movl %eax, 100(%rsp)             # row[1][1]
movl 88(%rsp), %eax              # row[0][0]
movl 92(%rsp), %ecx              # move
movl 96(%rsp), %edx              # move
movl 100(%rsp), %esi             # row[1][1]
movl %esi, 116(%rsp)             # row[1][1]
movl %edx, 112(%rsp)             # row[1][0]
movl %ecx, 108(%rsp)             # row[0][1]
movl %eax, 104(%rsp)             # row[0][0]
movl 116(%rsp), %eax             # row[1][1]
movl %eax, 20(%rsp)              # p[0][1][1]
movl 112(%rsp), %eax             # row[1][0]
movl %eax, 24(%rsp)              # p[0][1][0]
movl 104(%rsp), %eax             # row[0][0]
movl %eax, 28(%rsp)              # p[0][0][0]
movl 108(%rsp), %eax             # row[0][1]
movl %eax, 32(%rsp)              # p[0][0][1]
movl $1, %eax                    # 1
movl %eax, 36(%rsp)              # p[1] = 1
# trace frame for the call, then the matching pop
movl 36(%rsp), %r8d              # p[1]
movl 20(%rsp), %ecx              # p[0][1][1]
movl 24(%rsp), %edx              # p[0][1][0]
movl 32(%rsp), %esi              # p[0][0][1]
movl 28(%rsp), %edi              # p[0][0][0]
callq main.take.spec.arr.2.tuple.2.i32.i32 # call take
movl %eax, %ecx                  # move
movl %ecx, 16(%rsp)              # move
movl 16(%rsp), %eax              # take((row, 1))
addq $120, %rsp                  # release
retq                             # return
```

The program returns 44.

`type Box[T type] struct { n T; }` of `make(row)`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The struct is `Box.spec.arr.2.tuple.2.i32.i32`.

The call is `main.make.spec.arr.2.tuple.2.i32.i32`.

`box.n` is that array. `box.n[0][0]` is 40 and `box.n[1][1]` is 3.

`make(nums)`, when `nums` is `[2]i32`, is `make.spec.arr.2.i32`.

`box.n = value`, when `value` is `[2][2]i32`, is `type mismatch: expected [2]i32, found *[2]i32`.

```c
type Box[T type] struct {
    n T;
}

func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func make[T type](value T) Box[T] {
    var box Box[T];
    box.n = value;
    return box;
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    var box = make(row);
    return box.n[0][0] + box.n[1][1];
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.make.spec.arr.2.tuple.2.i32.i32:
movq %rdi, %rax                  # box
movq %rsi, -24(%rsp)             # value
movq -24(%rsp), %r8              # value
movl (%r8), %ecx                 # value[0][0]
movl 4(%r8), %edx                # value[0][1]
movl 8(%r8), %esi                # value[1][0]
movl 12(%r8), %r8d               # value[1][1]
movl %r8d, -4(%rsp)              # move
movl %esi, -8(%rsp)              # move
movl %edx, -12(%rsp)             # move
movl %ecx, -16(%rsp)             # move
movl -16(%rsp), %ecx             # value[0][0]
movl -12(%rsp), %edx             # value[0][1]
movl -8(%rsp), %esi              # value[1][0]
movl -4(%rsp), %r8d              # value[1][1]
movl %r8d, 12(%rdi)              # box.n[1][1]
movl %esi, 8(%rdi)               # box.n[1][0]
movl %edx, 4(%rdi)               # box.n[0][1]
movl %ecx, (%rdi)                # box.n[0][0]
retq                             # return

main:
subq $136, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 84(%rsp), %ecx              # row[1][1]
movl %ecx, 32(%rsp)              # move
movl 80(%rsp), %ecx              # row[1][0]
movl %ecx, 36(%rsp)              # move
movl 72(%rsp), %ecx              # row[0][0]
movl %ecx, 40(%rsp)              # move
movl 76(%rsp), %ecx              # row[0][1]
movl %ecx, 44(%rsp)              # move
movl 32(%rsp), %eax              # row[1][1]
movl 36(%rsp), %ecx              # row[1][0]
movl 44(%rsp), %edx              # row[0][1]
movl 40(%rsp), %esi              # row[0][0]
movl %esi, 88(%rsp)              # row[0][0]
movl %edx, 92(%rsp)              # row[0][1]
movl %ecx, 96(%rsp)              # row[1][0]
movl %eax, 100(%rsp)             # row[1][1]
movl 88(%rsp), %eax              # row[0][0]
movl 92(%rsp), %ecx              # move
movl 96(%rsp), %edx              # move
movl 100(%rsp), %esi             # row[1][1]
movl %esi, 116(%rsp)             # row[1][1]
movl %edx, 112(%rsp)             # row[1][0]
movl %ecx, 108(%rsp)             # row[0][1]
movl %eax, 104(%rsp)             # row[0][0]
# trace frame for the call, then the matching pop
leaq 56(%rsp), %rdi              # box
leaq 104(%rsp), %rsi             # row
callq main.make.spec.arr.2.tuple.2.i32.i32 # call make
movl 68(%rsp), %ecx              # box.n[1][1]
movl %ecx, 8(%rsp)               # move
movl 64(%rsp), %ecx              # box.n[1][0]
movl %ecx, 12(%rsp)              # move
movl 56(%rsp), %ecx              # box.n[0][0]
movl %ecx, 16(%rsp)              # move
movl 60(%rsp), %ecx              # box.n[0][1]
movl %ecx, 20(%rsp)              # move
movl 8(%rsp), %eax               # box.n[1][1]
movl 12(%rsp), %ecx              # box.n[1][0]
movl 20(%rsp), %edx              # box.n[0][1]
movl 16(%rsp), %esi              # box.n[0][0]
movl %esi, 120(%rsp)             # box.n[0][0]
movl %edx, 124(%rsp)             # box.n[0][1]
movl %ecx, 128(%rsp)             # box.n[1][0]
movl %eax, 132(%rsp)             # box.n[1][1]
movl 120(%rsp), %eax             # box.n[0][0]
movl 128(%rsp), %ecx             # move
movl 132(%rsp), %ecx             # box.n[1][1]
addl %ecx, %eax                  # add
addq $136, %rsp                  # release
retq                             # return
```

The program returns 43.

`func id[T type](value *T) *T` of `&row`, when `row` is `[2](i32, i32)`, returns that pointer.

The call is `main.id.spec.arr.2.tuple.2.i32.i32`.

`(*p)[0][0]` is 40 and `(*p)[1][1]` is 3.

`id(&one)`, when `one` is `(40, 2)`, is `spec.tuple.2.i32.i32`.

`id(&g)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`?*T`, `*const T`, and `**T` are `not supported yet`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func id[T type](value *T) *T {
    return value;
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    var p = id(&row);
    return (*p)[0][0] + (*p)[1][1];
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.id.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rax              # value
retq                             # return

main:
subq $104, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 48(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 60(%rsp), %eax              # row[1][1]
movl %eax, 24(%rsp)              # move
movl 56(%rsp), %ecx              # row[1][0]
movl %ecx, 28(%rsp)              # move
movl 48(%rsp), %ecx              # row[0][0]
movl %ecx, 32(%rsp)              # move
movl 52(%rsp), %ecx              # row[0][1]
movl %ecx, 36(%rsp)              # move
movl 24(%rsp), %eax              # row[1][1]
movl 28(%rsp), %ecx              # row[1][0]
movl 36(%rsp), %edx              # row[0][1]
movl 32(%rsp), %esi              # row[0][0]
movl %esi, 64(%rsp)              # row[0][0]
movl %edx, 68(%rsp)              # row[0][1]
movl %ecx, 72(%rsp)              # row[1][0]
movl %eax, 76(%rsp)              # row[1][1]
movl 64(%rsp), %eax              # row[0][0]
movl 68(%rsp), %ecx              # move
movl 72(%rsp), %edx              # move
movl 76(%rsp), %esi              # row[1][1]
movl %esi, 92(%rsp)              # row[1][1]
movl %edx, 88(%rsp)              # row[1][0]
movl %ecx, 84(%rsp)              # row[0][1]
movl %eax, 80(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 80(%rsp), %rdi              # &row
callq main.id.spec.arr.2.tuple.2.i32.i32 # call id
movq %rax, %rcx                  # move
movq %rcx, 8(%rsp)               # move
movq 8(%rsp), %rax               # move
movq %rax, 96(%rsp)              # p
movq 96(%rsp), %rcx              # p
movl (%rcx), %eax                # (*p)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (*p)[1][1]
addl %ecx, %eax                  # add
addq $104, %rsp                  # release
retq                             # return
```

The program returns 43.

`take[T type](p (*T, i32))` of `(&row, 1)`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.take.spec.arr.2.tuple.2.i32.i32`.

`(*p[0])[0][0]` is 40, `(*p[0])[1][1]` is 3, and `p[1]` is 1.

`plain((&one, 1))`, when `one` is `(40, 2)`, is `spec.tuple.2.i32.i32`.

`wide((&g, 1))`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func take[T type](p (*T, i32)) i32 {
    return (*p[0])[0][0] + (*p[0])[1][1] + p[1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return take((&row, 1));
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.take.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -16(%rsp)             # p[0]
movl %esi, -8(%rsp)              # p[1]
movl -8(%rsp), %ecx              # p[1]
movq -16(%rsp), %rdx             # p[0]
movl (%rdx), %eax                # (*p[0])[0][0]
movl 12(%rdx), %edx              # (*p[0])[1][1]
addl %edx, %eax                  # add
addl %ecx, %eax                  # add
retq                             # return

main:
subq $104, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 56(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 68(%rsp), %ecx              # row[1][1]
movl %ecx, 32(%rsp)              # move
movl 64(%rsp), %ecx              # row[1][0]
movl %ecx, 36(%rsp)              # move
movl 56(%rsp), %ecx              # row[0][0]
movl %ecx, 40(%rsp)              # move
movl 60(%rsp), %ecx              # row[0][1]
movl %ecx, 44(%rsp)              # move
movl 32(%rsp), %eax              # row[1][1]
movl 36(%rsp), %ecx              # row[1][0]
movl 44(%rsp), %edx              # row[0][1]
movl 40(%rsp), %esi              # row[0][0]
movl %esi, 72(%rsp)              # row[0][0]
movl %edx, 76(%rsp)              # row[0][1]
movl %ecx, 80(%rsp)              # row[1][0]
movl %eax, 84(%rsp)              # row[1][1]
movl 72(%rsp), %eax              # row[0][0]
movl 76(%rsp), %ecx              # move
movl 80(%rsp), %edx              # move
movl 84(%rsp), %esi              # row[1][1]
movl %esi, 100(%rsp)             # row[1][1]
movl %edx, 96(%rsp)              # row[1][0]
movl %ecx, 92(%rsp)              # row[0][1]
movl %eax, 88(%rsp)              # row[0][0]
movl $1, %eax                    # 1
movl %eax, 12(%rsp)              # p[1] = 1
leaq 88(%rsp), %rax              # &row
movq %rax, 16(%rsp)              # p[0]
# trace frame for the call, then the matching pop
movl 12(%rsp), %esi              # p[1]
movq 16(%rsp), %rdi              # p[0]
callq main.take.spec.arr.2.tuple.2.i32.i32 # call take
movl %eax, %ecx                  # move
movl %ecx, 8(%rsp)               # move
movl 8(%rsp), %eax               # take((&row, 1))
addq $104, %rsp                  # release
retq                             # return
```

The program returns 44.

`type Box[T type] struct { p *T; }` of `make(&row)`, when `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The struct is `Box.spec.tuple.2.i32.i32`.

The call is `main.make.spec.tuple.2.i32.i32`.

`(*box.p)[0]` is 40 and `(*box.p)[1]` is 2.

`make(&deep)`, when `deep` is `((40, 2), 1)`, is `spec.tuple.2.tuple.2.i32.i32.i32`.

`make(&n)`, when `n` is `i32`, is `spec.i32`.

`var box Box[T]` with no initializer is `type mismatch`. `p` is not optional.

`{.p = value}`, when `value` is `*[2](i32, i32)`, is `type mismatch: expected *(i32, i32), found *[2](i32, i32)`.

```c
type Box[T type] struct {
    p *T;
}

func make[T type](value *T) Box[T] {
    var box Box[T] = {.p = value};
    return box;
}

func main() i32 {
    var row = (40, 2);
    var box = make(&row);
    return (*box.p)[0] + (*box.p)[1];
}
```

```asm
main.make.spec.tuple.2.i32.i32:
movq %rdi, -16(%rsp)             # value
movq -16(%rsp), %rax             # value
movq %rax, -8(%rsp)              # box.p = value
movq -8(%rsp), %rax              # box.p
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $2, 28(%rsp)                # row[1] = 2
movl $40, 24(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 24(%rsp), %rdi              # &row
callq main.make.spec.tuple.2.i32.i32 # call make
movq %rax, %rcx                  # move
movq %rcx, 8(%rsp)               # move
movq 8(%rsp), %rax               # move
movq %rax, 32(%rsp)              # box.p
movq 32(%rsp), %rcx              # box.p
movl (%rcx), %eax                # (*box.p)[0]
movl 4(%rcx), %ecx               # (*box.p)[1]
addl %ecx, %eax                  # add
addq $40, %rsp                   # release
retq                             # return
```

The program returns 42.

`type Box[T type] struct { p *T; }` of `make(&row)`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The struct is `Box.spec.arr.2.tuple.2.i32.i32`.

The call is `main.make.spec.arr.2.tuple.2.i32.i32`.

`(*box.p)[0][0]` is 40 and `(*box.p)[1][1]` is 3.

`make(&g)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`var box Box[T]` with no initializer is `type mismatch`. `p` is not optional.

`?*T` and `*const T` are `not supported yet`.

```c
type Box[T type] struct {
    p *T;
}

func make[T type](value *T) Box[T] {
    var box Box[T] = {.p = value};
    return box;
}

func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    var box = make(&row);
    return (*box.p)[0][0] + (*box.p)[1][1];
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.make.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -16(%rsp)             # value
movq -16(%rsp), %rax             # value
movq %rax, -8(%rsp)              # box.p = value
movq -8(%rsp), %rax              # box.p
retq                             # return

main:
subq $104, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 48(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 60(%rsp), %ecx              # row[1][1]
movl %ecx, 24(%rsp)              # move
movl 56(%rsp), %ecx              # row[1][0]
movl %ecx, 28(%rsp)              # move
movl 48(%rsp), %ecx              # row[0][0]
movl %ecx, 32(%rsp)              # move
movl 52(%rsp), %ecx              # row[0][1]
movl %ecx, 36(%rsp)              # move
movl 24(%rsp), %eax              # row[1][1]
movl 28(%rsp), %ecx              # row[1][0]
movl 36(%rsp), %edx              # row[0][1]
movl 32(%rsp), %esi              # row[0][0]
movl %esi, 64(%rsp)              # row[0][0]
movl %edx, 68(%rsp)              # row[0][1]
movl %ecx, 72(%rsp)              # row[1][0]
movl %eax, 76(%rsp)              # row[1][1]
movl 64(%rsp), %eax              # row[0][0]
movl 68(%rsp), %ecx              # move
movl 72(%rsp), %edx              # move
movl 76(%rsp), %esi              # row[1][1]
movl %esi, 92(%rsp)              # row[1][1]
movl %edx, 88(%rsp)              # row[1][0]
movl %ecx, 84(%rsp)              # row[0][1]
movl %eax, 80(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 80(%rsp), %rdi              # &row
callq main.make.spec.arr.2.tuple.2.i32.i32 # call make
movq %rax, %rcx                  # move
movq %rcx, 8(%rsp)               # move
movq 8(%rsp), %rax               # move
movq %rax, 96(%rsp)              # box.p
movq 96(%rsp), %rcx              # box.p
movl (%rcx), %eax                # (*box.p)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (*box.p)[1][1]
addl %ecx, %eax                  # add
addq $104, %rsp                  # release
retq                             # return
```

The program returns 43.

`row [2]T` on `Box[T]`, when `T` is `[2](i32, i32)`, keeps that array inside the written length.

The struct is `Box.spec.arr.2.tuple.2.i32.i32`.

The call is `main.hold.spec.arr.2.tuple.2.i32.i32`.

`box.row[0][0][0]` is 40 and `box.row[1][1][1]` is 7.

`func take[T type](row [2]T)`, `func rows[T type](a T, b T) [2]T`, and `var g [2]T` use the same arrays.

`take(box.row)` is `main.take.spec.arr.2.tuple.2.i32.i32`.

`var g [2][2]T` of that `T` is one more written length. `g[0][1][0][0]` is 40.

`?*T` and `*const T` are `not supported yet`.

A written `[2](i32, i32)` is `expected a type`.

```c
type Box[T type] struct { row [2]T; }

func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func hold[T type](a T, b T) Box[T] {
    var box Box[T];
    box.row[0] = a;
    box.row[1] = b;
    return box;
}

func main() i32 {
    var a = pair((40, 2), (1, 3));
    var b = pair((4, 5), (6, 7));
    var box = hold(a, b);
    return box.row[0][0][0] + box.row[1][1][1];
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.pair.spec.tuple.2.i32.i32.spec.t_4_5.t_6_7:
movq %rdi, %rax                  # row
movl $5, -12(%rsp)               # a[1] = 5
movl $4, -16(%rsp)               # a[0] = 4
movl $7, -4(%rsp)                # b[1] = 7
movl $6, -8(%rsp)                # b[0] = 6
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.hold.spec.arr.2.tuple.2.i32.i32:
pushq %rbx                       # save
movq %rdi, %rax                  # box
movq %rsi, -48(%rsp)             # a
movq %rdx, -40(%rsp)             # b
movq -48(%rsp), %r8              # a
movl (%r8), %ecx                 # a[0][0]
movl 4(%r8), %edx                # a[0][1]
movl 8(%r8), %esi                # a[1][0]
movl 12(%r8), %r8d               # a[1][1]
movl %r8d, -20(%rsp)             # move
movl %esi, -24(%rsp)             # move
movl %edx, -28(%rsp)             # move
movl %ecx, -32(%rsp)             # move
movq -40(%rsp), %r8              # b
movl (%r8), %ecx                 # b[0][0]
movl 4(%r8), %edx                # b[0][1]
movl 8(%r8), %esi                # b[1][0]
movl 12(%r8), %r8d               # b[1][1]
movl %r8d, -4(%rsp)              # move
movl %esi, -8(%rsp)              # move
movl %edx, -12(%rsp)             # move
movl %ecx, -16(%rsp)             # move
movl -32(%rsp), %ecx             # a[0][0]
movl -28(%rsp), %edx             # a[0][1]
movl -24(%rsp), %esi             # a[1][0]
movl -20(%rsp), %r8d             # a[1][1]
movl -16(%rsp), %r9d             # b[0][0]
movl -12(%rsp), %r10d            # b[0][1]
movl -8(%rsp), %r11d             # b[1][0]
movl -4(%rsp), %ebx              # b[1][1]
movl %ebx, 28(%rdi)              # box.row[1][1][1]
movl %r11d, 24(%rdi)             # box.row[1][1][0]
movl %r10d, 20(%rdi)             # box.row[1][0][1]
movl %r9d, 16(%rdi)              # box.row[1][0][0]
movl %r8d, 12(%rdi)              # box.row[0][1][1]
movl %esi, 8(%rdi)               # box.row[0][1][0]
movl %edx, 4(%rdi)               # box.row[0][0][1]
movl %ecx, (%rdi)                # box.row[0][0][0]
popq %rbx                        # restore
retq                             # return

main:
subq $264, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 152(%rsp), %rdi             # a
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 164(%rsp), %ecx             # a[1][1]
movl %ecx, 80(%rsp)              # move
movl 160(%rsp), %ecx             # a[1][0]
movl %ecx, 84(%rsp)              # move
movl 152(%rsp), %ecx             # a[0][0]
movl %ecx, 88(%rsp)              # move
movl 156(%rsp), %ecx             # a[0][1]
movl %ecx, 92(%rsp)              # move
movl 80(%rsp), %eax              # a[1][1]
movl 84(%rsp), %ecx              # a[1][0]
movl 92(%rsp), %edx              # a[0][1]
movl 88(%rsp), %esi              # a[0][0]
movl %esi, 168(%rsp)             # a[0][0]
movl %edx, 172(%rsp)             # a[0][1]
movl %ecx, 176(%rsp)             # a[1][0]
movl %eax, 180(%rsp)             # a[1][1]
movl 168(%rsp), %eax             # a[0][0]
movl 172(%rsp), %ecx             # move
movl 176(%rsp), %edx             # move
movl 180(%rsp), %esi             # a[1][1]
movl %esi, 196(%rsp)             # a[1][1]
movl %edx, 192(%rsp)             # a[1][0]
movl %ecx, 188(%rsp)             # a[0][1]
movl %eax, 184(%rsp)             # a[0][0]
# trace frame for the call, then the matching pop
leaq 136(%rsp), %rdi             # b
callq main.pair.spec.tuple.2.i32.i32.spec.t_4_5.t_6_7 # call pair
movl 148(%rsp), %ecx             # b[1][1]
movl %ecx, 56(%rsp)              # move
movl 144(%rsp), %ecx             # b[1][0]
movl %ecx, 60(%rsp)              # move
movl 136(%rsp), %ecx             # b[0][0]
movl %ecx, 64(%rsp)              # move
movl 140(%rsp), %ecx             # b[0][1]
movl %ecx, 68(%rsp)              # move
movl 56(%rsp), %eax              # b[1][1]
movl 60(%rsp), %ecx              # b[1][0]
movl 68(%rsp), %edx              # b[0][1]
movl 64(%rsp), %esi              # b[0][0]
movl %esi, 200(%rsp)             # b[0][0]
movl %edx, 204(%rsp)             # b[0][1]
movl %ecx, 208(%rsp)             # b[1][0]
movl %eax, 212(%rsp)             # b[1][1]
movl 200(%rsp), %eax             # b[0][0]
movl 204(%rsp), %ecx             # move
movl 208(%rsp), %edx             # move
movl 212(%rsp), %esi             # b[1][1]
movl %esi, 228(%rsp)             # b[1][1]
movl %edx, 224(%rsp)             # b[1][0]
movl %ecx, 220(%rsp)             # b[0][1]
movl %eax, 216(%rsp)             # b[0][0]
# trace frame for the call, then the matching pop
leaq 104(%rsp), %rdi             # box
leaq 184(%rsp), %rsi             # a
leaq 216(%rsp), %rdx             # b
callq main.hold.spec.arr.2.tuple.2.i32.i32 # call hold
movl 132(%rsp), %ecx             # box.row[1][1][1]
movl %ecx, 16(%rsp)              # move
movl 128(%rsp), %ecx             # box.row[1][1][0]
movl %ecx, 20(%rsp)              # move
movl 124(%rsp), %ecx             # move
movl %ecx, 24(%rsp)              # move
movl 120(%rsp), %ecx             # move
movl %ecx, 28(%rsp)              # move
movl 116(%rsp), %ecx             # move
movl %ecx, 32(%rsp)              # move
movl 112(%rsp), %ecx             # move
movl %ecx, 36(%rsp)              # move
movl 104(%rsp), %ecx             # box.row[0][0][0]
movl %ecx, 40(%rsp)              # move
movl 108(%rsp), %ecx             # move
movl %ecx, 44(%rsp)              # move
movl 16(%rsp), %eax              # box.row[1][1][1]
movl 20(%rsp), %ecx              # move
movl 24(%rsp), %edx              # move
movl 28(%rsp), %esi              # move
movl 32(%rsp), %edi              # move
movl 36(%rsp), %r8d              # move
movl 44(%rsp), %r9d              # move
movl 40(%rsp), %r10d             # box.row[0][0][0]
movl %r10d, 232(%rsp)            # box.row[0][0][0]
movl %r9d, 236(%rsp)             # move
movl %r8d, 240(%rsp)             # move
movl %edi, 244(%rsp)             # move
movl %esi, 248(%rsp)             # move
movl %edx, 252(%rsp)             # move
movl %ecx, 256(%rsp)             # move
movl %eax, 260(%rsp)             # box.row[1][1][1]
movl 232(%rsp), %eax             # box.row[0][0][0]
movl 256(%rsp), %ecx             # move
movl 260(%rsp), %ecx             # box.row[1][1][1]
addl %ecx, %eax                  # add
addq $264, %rsp                  # release
retq                             # return
```

The program returns 47.

`func id[T type](value **T)` of `&p`, when `p` is `&row` and `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The call is `main.id.spec.tuple.2.i32.i32`.

`(**value)[0]` is 40 and `(**value)[1]` is 2.

`deep(&wp)`, when `wide` is `((40, 2), 1)`, is `spec.tuple.2.tuple.2.i32.i32.i32`.

`**T` when `T` is `[2](i32, i32)` is `not supported yet`.

`?*T` and `*const T` are `not supported yet`.

```c
func id[T type](value **T) i32 {
    return (**value)[0] + (**value)[1];
}

func main() i32 {
    var row = (40, 2);
    var p = &row;
    return id(&p);
}
```

```asm
main.id.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rax              # value
movq (%rax), %rcx                # *value
movl (%rcx), %eax                # (**value)[0]
movl 4(%rcx), %ecx               # (**value)[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $2, 28(%rsp)                # row[1] = 2
movl $40, 24(%rsp)               # row[0] = 40
leaq 24(%rsp), %rax              # &row
movq %rax, 32(%rsp)              # p
# trace frame for the call, then the matching pop
leaq 32(%rsp), %rdi              # &p
callq main.id.spec.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # id(&p)
addq $40, %rsp                   # release
retq                             # return
```

The program returns 42.

`func id[T type](value *T) *T` returns that pointer.

`var p = id(&row)` then `(*p)[0]` is 40 and `(*p)[1]` is 2.

The call is `main.id.spec.tuple.2.i32.i32`.

```c
func id[T type](value *T) *T {
    return value;
}

func main() i32 {
    var row = (40, 2);
    var p = id(&row);
    return (*p)[0] + (*p)[1];
}
```

```asm
main.id.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rax              # value
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $2, 28(%rsp)                # row[1] = 2
movl $40, 24(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 24(%rsp), %rdi              # &row
callq main.id.spec.tuple.2.i32.i32 # call id
movq %rax, %rcx                  # move
movq %rcx, 8(%rsp)               # move
movq 8(%rsp), %rax               # p
movq %rax, 32(%rsp)              # p
movq 32(%rsp), %rcx              # p
movl (%rcx), %eax                # (*p)[0]
movl 4(%rcx), %ecx               # (*p)[1]
addl %ecx, %eax                  # add
addq $40, %rsp                   # release
retq                             # return
```

The program returns 42.

`type Pair (i32, i32)` then a parameter `*Pair` does not accept `&row` when `row` is `(i32, i32)`.

The compiler reports `type mismatch: expected *Pair, found *(i32, i32)`.

```c
func id[T type](value *T) i32 {
    return (*value)[0] + (*value)[1];
}

func main() i32 {
    var row = (40, 2);
    return id(&row);
}
```

```asm
main.id.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # (*value)[0]
movl 4(%rcx), %ecx               # (*value)[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $24, %rsp                   # allocate
movl $2, 20(%rsp)                # row[1] = 2
movl $40, 16(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # &row
callq main.id.spec.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # move
addq $24, %rsp                   # release
retq                             # return
```

The program returns 42.

`func id[T type](value **T)` of `&p`, when `p` is `&row` and `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.id.spec.arr.2.tuple.2.i32.i32`.

`(**value)[0][0]` is 40 and `(**value)[1][1]` is 3.

`wide(&gp)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`?*T` and `*const T` are `not supported yet`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func id[T type](value **T) i32 {
    return (**value)[0][0] + (**value)[1][1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    var p = &row;
    return id(&p);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.id.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rax              # value
movq (%rax), %rcx                # *value
movl (%rcx), %eax                # (**value)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (**value)[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $104, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 48(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 60(%rsp), %ecx              # row[1][1]
movl %ecx, 24(%rsp)              # move
movl 56(%rsp), %ecx              # row[1][0]
movl %ecx, 28(%rsp)              # move
movl 48(%rsp), %ecx              # row[0][0]
movl %ecx, 32(%rsp)              # move
movl 52(%rsp), %ecx              # row[0][1]
movl %ecx, 36(%rsp)              # move
movl 24(%rsp), %eax              # row[1][1]
movl 28(%rsp), %ecx              # row[1][0]
movl 36(%rsp), %edx              # row[0][1]
movl 32(%rsp), %esi              # row[0][0]
movl %esi, 64(%rsp)              # row[0][0]
movl %edx, 68(%rsp)              # row[0][1]
movl %ecx, 72(%rsp)              # row[1][0]
movl %eax, 76(%rsp)              # row[1][1]
movl 64(%rsp), %eax              # row[0][0]
movl 68(%rsp), %ecx              # move
movl 72(%rsp), %edx              # move
movl 76(%rsp), %esi              # row[1][1]
movl %esi, 92(%rsp)              # row[1][1]
movl %edx, 88(%rsp)              # row[1][0]
movl %ecx, 84(%rsp)              # row[0][1]
movl %eax, 80(%rsp)              # row[0][0]
leaq 80(%rsp), %rax              # &row
movq %rax, 96(%rsp)              # p
# trace frame for the call, then the matching pop
leaq 96(%rsp), %rdi              # &p
callq main.id.spec.arr.2.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # id(&p)
addq $104, %rsp                  # release
retq                             # return
```

The program returns 43.

`func id[T type](value *const T)` of `&row`, when `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The call is `main.id.spec.tuple.2.i32.i32`.

`(*value)[0]` is 40 and `(*value)[1]` is 2.

`deep(&wide)`, when `wide` is `((40, 2), 1)`, is `spec.tuple.2.tuple.2.i32.i32.i32`.

`(*value)[0] = 1` is `type mismatch`.

`*const T` when `T` is `[2](i32, i32)` is `not supported yet`.

`?*T` is `not supported yet`.

```c
func id[T type](value *const T) i32 {
    return (*value)[0] + (*value)[1];
}

func main() i32 {
    var row = (40, 2);
    return id(&row);
}
```

```asm
main.id.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # (*value)[0]
movl 4(%rcx), %ecx               # (*value)[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $24, %rsp                   # allocate
movl $2, 20(%rsp)                # row[1] = 2
movl $40, 16(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # &row
callq main.id.spec.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # id(&row)
addq $24, %rsp                   # release
retq                             # return
```

The program returns 42.

`func id[T type](value *const T)` of `&row`, when `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.id.spec.arr.2.tuple.2.i32.i32`.

`(*value)[0][0]` is 40 and `(*value)[1][1]` is 3.

`wide(&g)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`(*value)[0][0] = 1` is `type mismatch`.

`?*T` is `not supported yet`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func id[T type](value *const T) i32 {
    return (*value)[0][0] + (*value)[1][1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return id(&row);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.id.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # (*value)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (*value)[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $88, %rsp                   # allocate
# trace frame for the call, then the matching pop
leaq 40(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 52(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 48(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 40(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 44(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 56(%rsp)              # row[0][0]
movl %edx, 60(%rsp)              # row[0][1]
movl %ecx, 64(%rsp)              # row[1][0]
movl %eax, 68(%rsp)              # row[1][1]
movl 56(%rsp), %eax              # row[0][0]
movl 60(%rsp), %ecx              # move
movl 64(%rsp), %edx              # move
movl 68(%rsp), %esi              # row[1][1]
movl %esi, 84(%rsp)              # row[1][1]
movl %edx, 80(%rsp)              # row[1][0]
movl %ecx, 76(%rsp)              # row[0][1]
movl %eax, 72(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # &row
callq main.id.spec.arr.2.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # id(&row)
addq $88, %rsp                   # release
retq                             # return
```

The program returns 43.

`var q ?*T = ?*T(value)`, when `T` is `(i32, i32)`, is that optional pointer.

The call is `main.read.spec.tuple.2.i32.i32`.

`q` is null on one path and returns 0. `?*T(value)` stores `&row`, and `(*q)[0] + (*q)[1]` is 42.

`deep(&wide)`, when `wide` is `((40, 2), 1)`, is `spec.tuple.2.tuple.2.i32.i32.i32`.

`?*T` when `T` is `[2](i32, i32)` is `not supported yet`.

`q = p`, when `p` is `*i32`, stores that pointer.

```c
func read[T type](value *T) i32 {
    var q ?*T = ?*T(value);
    if (q == null) {
        return 0;
    }
    return (*q)[0] + (*q)[1];
}

func main() i32 {
    var row = (40, 2);
    return read(&row);
}
```

```asm
main.read.spec.tuple.2.i32.i32:
movq %rdi, -16(%rsp)             # value
movq -16(%rsp), %rax             # value
movq %rax, -8(%rsp)              # q = ?*T(value)
cmpq $0, -8(%rsp)                # q == null
jne .Lq
xorl %eax, %eax                  # 0
retq                             # return
.Lq:
movq -8(%rsp), %rcx              # q
movl (%rcx), %eax                # (*q)[0]
movl 4(%rcx), %ecx               # (*q)[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $24, %rsp                   # allocate
movl $2, 20(%rsp)                # row[1] = 2
movl $40, 16(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # &row
callq main.read.spec.tuple.2.i32.i32 # call read
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # read(&row)
addq $24, %rsp                   # release
retq                             # return
```

The program returns 42.

`var q ?*T = ?*T(value)`, when `T` is `[2](i32, i32)`, keeps that array after the optional star.

The call is `main.read.spec.arr.2.tuple.2.i32.i32`.

`?*T(value)` stores `&row`. `(*q)[0][0]` is 40 and `(*q)[1][1]` is 3.

`wide(&g)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`q = value` is `type mismatch: expected ?*[2](i32, i32), found *[2](i32, i32)`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func read[T type](value *T) i32 {
    var q ?*T = ?*T(value);
    if (q == null) {
        return 0;
    }
    return (*q)[0][0] + (*q)[1][1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return read(&row);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.read.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -16(%rsp)             # value
movq -16(%rsp), %rax             # value
movq %rax, -8(%rsp)              # q = ?*T(value)
cmpq $0, -8(%rsp)                # q == null
jne .Lq
xorl %eax, %eax                  # 0
retq                             # return
.Lq:
movq -8(%rsp), %rcx              # q
movl (%rcx), %eax                # (*q)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (*q)[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $88, %rsp                   # allocate
# trace frame for the call, then the matching pop
leaq 40(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 52(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 48(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 40(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 44(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 56(%rsp)              # row[0][0]
movl %edx, 60(%rsp)              # row[0][1]
movl %ecx, 64(%rsp)              # row[1][0]
movl %eax, 68(%rsp)              # row[1][1]
movl 56(%rsp), %eax              # row[0][0]
movl 60(%rsp), %ecx              # move
movl 64(%rsp), %edx              # move
movl 68(%rsp), %esi              # row[1][1]
movl %esi, 84(%rsp)              # row[1][1]
movl %edx, 80(%rsp)              # row[1][0]
movl %ecx, 76(%rsp)              # row[0][1]
movl %eax, 72(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # &row
callq main.read.spec.arr.2.tuple.2.i32.i32 # call read
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # read(&row)
addq $88, %rsp                   # release
retq                             # return
```

The program returns 43.

`func read[T type](value ?*T)` of `?*T(value)`, when `T` is `(i32, i32)`, is that optional pointer.

The call is `main.read.spec.tuple.2.i32.i32`.

`(*value)[0]` is 40 and `(*value)[1]` is 2.

`read(q)`, when `q` is null, returns 0.

`pass_deep(&wide)`, when `wide` is `((40, 2), 1)`, is `spec.tuple.2.tuple.2.i32.i32.i32`.

`value ?*T` when `T` is `[2](i32, i32)` is `not supported yet`.

```c
func read[T type](value ?*T) i32 {
    if (value == null) {
        return 0;
    }
    return (*value)[0] + (*value)[1];
}

func pass[T type](value *T) i32 {
    return read(?*T(value));
}

func main() i32 {
    var row = (40, 2);
    return pass(&row);
}
```

```asm
main.read.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
cmpq $0, -8(%rsp)                # value == null
jne .Lv
xorl %eax, %eax                  # 0
retq                             # return
.Lv:
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # (*value)[0]
movl 4(%rcx), %ecx               # (*value)[1]
addl %ecx, %eax                  # add
retq                             # return

main.pass.spec.tuple.2.i32.i32:
subq $40, %rsp                   # allocate
movq %rdi, 32(%rsp)              # value
movq 32(%rsp), %rax              # value
movq %rax, 16(%rsp)              # ?*T(value)
# trace frame for the call, then the matching pop
movq 16(%rsp), %rdi              # value
callq main.read.spec.tuple.2.i32.i32 # call read
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # read(?*T(value))
addq $40, %rsp                   # release
retq                             # return

main:
subq $24, %rsp                   # allocate
movl $2, 20(%rsp)                # row[1] = 2
movl $40, 16(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # &row
callq main.pass.spec.tuple.2.i32.i32 # call pass
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # pass(&row)
addq $24, %rsp                   # release
retq                             # return
```

The program returns 42.

`func read[T type](value ?*T)` of `?*T(value)`, when `T` is `[2](i32, i32)`, keeps that array after the optional star.

The call is `main.read.spec.arr.2.tuple.2.i32.i32`.

`(*value)[0][0]` is 40 and `(*value)[1][1]` is 3.

`pass_wide(&g)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`read(q)`, when `q` is null, returns 0.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func read[T type](value ?*T) i32 {
    if (value == null) {
        return 0;
    }
    return (*value)[0][0] + (*value)[1][1];
}

func pass[T type](value *T) i32 {
    return read(?*T(value));
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    return pass(&row);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.read.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
cmpq $0, -8(%rsp)                # value == null
jne .Lv
xorl %eax, %eax                  # 0
retq                             # return
.Lv:
movq -8(%rsp), %rcx              # value
movl (%rcx), %eax                # (*value)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (*value)[1][1]
addl %ecx, %eax                  # add
retq                             # return

main.pass.spec.arr.2.tuple.2.i32.i32:
subq $40, %rsp                   # allocate
movq %rdi, 32(%rsp)              # value
movq 32(%rsp), %rax              # value
movq %rax, 16(%rsp)              # ?*T(value)
# trace frame for the call, then the matching pop
movq 16(%rsp), %rdi              # value
callq main.read.spec.arr.2.tuple.2.i32.i32 # call read
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # read(?*T(value))
addq $40, %rsp                   # release
retq                             # return

main:
subq $88, %rsp                   # allocate
# trace frame for the call, then the matching pop
leaq 40(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 52(%rsp), %ecx              # row[1][1]
movl %ecx, 16(%rsp)              # move
movl 48(%rsp), %ecx              # row[1][0]
movl %ecx, 20(%rsp)              # move
movl 40(%rsp), %ecx              # row[0][0]
movl %ecx, 24(%rsp)              # move
movl 44(%rsp), %ecx              # row[0][1]
movl %ecx, 28(%rsp)              # move
movl 16(%rsp), %eax              # row[1][1]
movl 20(%rsp), %ecx              # row[1][0]
movl 28(%rsp), %edx              # row[0][1]
movl 24(%rsp), %esi              # row[0][0]
movl %esi, 56(%rsp)              # row[0][0]
movl %edx, 60(%rsp)              # row[0][1]
movl %ecx, 64(%rsp)              # row[1][0]
movl %eax, 68(%rsp)              # row[1][1]
movl 56(%rsp), %eax              # row[0][0]
movl 60(%rsp), %ecx              # move
movl 64(%rsp), %edx              # move
movl 68(%rsp), %esi              # row[1][1]
movl %esi, 84(%rsp)              # row[1][1]
movl %edx, 80(%rsp)              # row[1][0]
movl %ecx, 76(%rsp)              # row[0][1]
movl %eax, 72(%rsp)              # row[0][0]
# trace frame for the call, then the matching pop
leaq 72(%rsp), %rdi              # &row
callq main.pass.spec.arr.2.tuple.2.i32.i32 # call pass
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # pass(&row)
addq $88, %rsp                   # release
retq                             # return
```

The program returns 43.

`type Box[T type] struct { p ?*T; }` of `{.p = value}`, when `value` is `*T` and `T` is `(i32, i32)`, stores that pointer. No cast is required.

The struct is `Box.spec.tuple.2.i32.i32`.

The call is `main.make.spec.tuple.2.i32.i32`.

`var q = box.p` then `(*q)[0]` is 40 and `(*q)[1]` is 2.

`*box.p` before `q` is narrowed is `type mismatch`.

```c
type Box[T type] struct {
    p ?*T;
}

func make[T type](value *T) i32 {
    var box Box[T] = {.p = value};
    var q = box.p;
    if (q == null) {
        return 0;
    }
    return (*q)[0] + (*q)[1];
}

func main() i32 {
    var row = (40, 2);
    return make(&row);
}
```

```asm
main.make.spec.tuple.2.i32.i32:
movq %rdi, -24(%rsp)             # value
movq -24(%rsp), %rax             # value
movq %rax, -16(%rsp)             # box.p = value
movq -16(%rsp), %rax             # box.p
movq %rax, -8(%rsp)              # q
cmpq $0, -8(%rsp)                # q == null
jne .Lq
xorl %eax, %eax                  # 0
retq                             # return
.Lq:
movq -8(%rsp), %rcx              # q
movl (%rcx), %eax                # (*q)[0]
movl 4(%rcx), %ecx               # (*q)[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $24, %rsp                   # allocate
movl $2, 20(%rsp)                # row[1] = 2
movl $40, 16(%rsp)               # row[0] = 40
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # &row
callq main.make.spec.tuple.2.i32.i32 # call make
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # make(&row)
addq $24, %rsp                   # release
retq                             # return
```

The program returns 42.

`func id[T type](value **const T)` of `&p`, when `p` is `&row` and `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The call is `main.id.spec.tuple.2.i32.i32`.

`(**value)[0]` is 40 and `(**value)[1]` is 2.

`(**value)[0] = 1` is `type mismatch`.

`deep(&wp)`, when `wide` is `((40, 2), 1)`, is `spec.tuple.2.tuple.2.i32.i32.i32`.

`**const T` when `T` is `[2](i32, i32)` is `not supported yet`.

`*const *T` when `T` is a tuple is `not supported yet`.

```c
func id[T type](value **const T) i32 {
    return (**value)[0] + (**value)[1];
}

func main() i32 {
    var row = (40, 2);
    var p = &row;
    return id(&p);
}
```

```asm
main.id.spec.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rax              # value
movq (%rax), %rcx                # *value
movl (%rcx), %eax                # (**value)[0]
movl 4(%rcx), %ecx               # (**value)[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $2, 28(%rsp)                # row[1] = 2
movl $40, 24(%rsp)               # row[0] = 40
leaq 24(%rsp), %rax              # &row
movq %rax, 32(%rsp)              # p
# trace frame for the call, then the matching pop
leaq 32(%rsp), %rdi              # &p
callq main.id.spec.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # id(&p)
addq $40, %rsp                   # release
retq                             # return
```

The program returns 42.

`func id[T type](value **const T)` of `&p`, when `p` is `&row` and `row` is `[2](i32, i32)`, binds `T` to that array.

The call is `main.id.spec.arr.2.tuple.2.i32.i32`.

`(**value)[0][0]` is 40 and `(**value)[1][1]` is 3.

`(**value)[0][0] = 1` is `type mismatch`.

`wide(&gp)`, when `g` is `[2][2](i32, i32)`, is `spec.arr.2.arr.2.tuple.2.i32.i32`.

`*const *T` when `T` is a tuple is `not supported yet`.

```c
func pair[T type](a T, b T) [2]T {
    var row [2]T;
    row[0] = a;
    row[1] = b;
    return row;
}

func id[T type](value **const T) i32 {
    return (**value)[0][0] + (**value)[1][1];
}

func main() i32 {
    var row = pair((40, 2), (1, 3));
    var p = &row;
    return id(&p);
}
```

```asm
main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3:
movq %rdi, %rax                  # row
movl $2, -12(%rsp)               # a[1] = 2
movl $40, -16(%rsp)              # a[0] = 40
movl $3, -4(%rsp)                # b[1] = 3
movl $1, -8(%rsp)                # b[0] = 1
movl -16(%rsp), %ecx             # a[0]
movl -12(%rsp), %edx             # a[1]
movl -8(%rsp), %esi              # b[0]
movl -4(%rsp), %r8d              # b[1]
movl %r8d, 12(%rdi)              # row[1][1]
movl %esi, 8(%rdi)               # row[1][0]
movl %edx, 4(%rdi)               # row[0][1]
movl %ecx, (%rdi)                # row[0][0]
retq                             # return

main.id.spec.arr.2.tuple.2.i32.i32:
movq %rdi, -8(%rsp)              # value
movq -8(%rsp), %rax              # value
movq (%rax), %rcx                # *value
movl (%rcx), %eax                # (**value)[0][0]
movl 8(%rcx), %edx               # move
movl 12(%rcx), %ecx              # (**value)[1][1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $104, %rsp                  # allocate
# trace frame for the call, then the matching pop
leaq 48(%rsp), %rdi              # row
callq main.pair.spec.tuple.2.i32.i32.spec.t_40_2.t_1_3 # call pair
movl 60(%rsp), %ecx              # row[1][1]
movl %ecx, 24(%rsp)              # move
movl 56(%rsp), %ecx              # row[1][0]
movl %ecx, 28(%rsp)              # move
movl 48(%rsp), %ecx              # row[0][0]
movl %ecx, 32(%rsp)              # move
movl 52(%rsp), %ecx              # row[0][1]
movl %ecx, 36(%rsp)              # move
movl 24(%rsp), %eax              # row[1][1]
movl 28(%rsp), %ecx              # row[1][0]
movl 36(%rsp), %edx              # row[0][1]
movl 32(%rsp), %esi              # row[0][0]
movl %esi, 64(%rsp)              # row[0][0]
movl %edx, 68(%rsp)              # row[0][1]
movl %ecx, 72(%rsp)              # row[1][0]
movl %eax, 76(%rsp)              # row[1][1]
movl 64(%rsp), %eax              # row[0][0]
movl 68(%rsp), %ecx              # move
movl 72(%rsp), %edx              # move
movl 76(%rsp), %esi              # row[1][1]
movl %esi, 92(%rsp)              # row[1][1]
movl %edx, 88(%rsp)              # row[1][0]
movl %ecx, 84(%rsp)              # row[0][1]
movl %eax, 80(%rsp)              # row[0][0]
leaq 80(%rsp), %rax              # &row
movq %rax, 96(%rsp)              # p
# trace frame for the call, then the matching pop
leaq 96(%rsp), %rdi              # &p
callq main.id.spec.arr.2.tuple.2.i32.i32 # call id
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # id(&p)
addq $104, %rsp                  # release
retq                             # return
```

The program returns 43.

`take[T type](p (T, i32))` of `(row, 1)`, when `row` is `(40, 2)`, binds `T` to `(i32, i32)`.

The call is `main.take.spec.tuple.2.i32.i32`.

`p[0][0]` is 40 and `p[1]` is 1.

```c
func take[T type](p (T, i32)) i32 {
    return p[0][0] + p[1];
}

func main() i32 {
    var row = (40, 2);
    return take((row, 1));
}
```

```asm
main.take.spec.tuple.2.i32.i32:
movl %edi, -16(%rsp)             # p[0][0]
movl %esi, -12(%rsp)             # p[0][1]
movl %edx, -8(%rsp)              # p[1]
movl -16(%rsp), %eax             # p[0][0]
movl -12(%rsp), %ecx             # move
movl -8(%rsp), %ecx              # p[1]
addl %ecx, %eax                  # add
retq                             # return

main:
subq $40, %rsp                   # allocate
movl $2, 36(%rsp)                # row[1] = 2
movl $40, 32(%rsp)               # row[0] = 40
movl 32(%rsp), %eax              # row[0]
movl %eax, 12(%rsp)              # move
movl 36(%rsp), %eax              # row[1]
movl %eax, 16(%rsp)              # move
movl $1, %eax                    # constant
movl %eax, 20(%rsp)              # 1
# trace frame for the call, then the matching pop
movl 20(%rsp), %edx              # 1
movl 16(%rsp), %esi              # row[1]
movl 12(%rsp), %edi              # row[0]
callq main.take.spec.tuple.2.i32.i32 # call take
movl %eax, %ecx                  # move
movl %ecx, 8(%rsp)               # move
movl 8(%rsp), %eax               # move
addq $40, %rsp                   # release
retq                             # return
```

The program returns 41.

`id(row)`, when `row` is `((40, 2), 1)`, calls `main.id.spec.tuple.2.tuple.2.i32.i32.i32` and passes the three integers.

`id(((40, 2), 1))` calls `main.id.spec.tuple.2.tuple.2.i32.i32.i32.spec.t_t_40_2_1` and does not pass the tuple. That copy returns 41.

```c
func id[T type](value T) T {
    return value;
}

func main() i32 {
    var row = ((40, 2), 1);
    var got = id(row);
    return got[0][0] + got[1];
}
```

```asm
main.id.spec.tuple.2.tuple.2.i32.i32.i32:
movl %edi, -16(%rsp)             # value[0][0]
movl %esi, -12(%rsp)             # value[0][1]
movl %edx, -8(%rsp)              # value[1]
movl -16(%rsp), %eax             # value[0][0]
movl -12(%rsp), %edx             # value[0][1]
movl -8(%rsp), %ecx              # value[1]
retq                             # return

main:
subq $72, %rsp                   # allocate
movl $1, 48(%rsp)                # row[1] = 1
movl $2, 44(%rsp)                # row[0][1] = 2
movl $40, 40(%rsp)               # row[0][0] = 40
movl 48(%rsp), %eax              # row[1]
movl %eax, 20(%rsp)              # move
movl 40(%rsp), %eax              # row[0][0]
movl %eax, 24(%rsp)              # move
movl 44(%rsp), %eax              # row[0][1]
movl %eax, 28(%rsp)              # move
# trace frame for the call, then the matching pop
movl 20(%rsp), %edx              # row[1]
movl 28(%rsp), %esi              # row[0][1]
movl 24(%rsp), %edi              # row[0][0]
callq main.id.spec.tuple.2.tuple.2.i32.i32.i32 # call id
movl %eax, %esi                  # move
movl %esi, 4(%rsp)               # move
movl %ecx, %esi                  # move
movl 4(%rsp), %ecx               # move
movl %esi, 8(%rsp)               # move
movl %edx, 12(%rsp)              # move
movl %ecx, 16(%rsp)              # move
movl 8(%rsp), %eax               # move
movl 12(%rsp), %ecx              # move
movl 16(%rsp), %edx              # move
movl %edx, 56(%rsp)              # got[0][0]
movl %ecx, 60(%rsp)              # got[0][1]
movl %eax, 64(%rsp)              # got[1]
movl 56(%rsp), %eax              # got[0][0]
movl 60(%rsp), %ecx              # move
movl 64(%rsp), %ecx              # got[1]
addl %ecx, %eax                  # add
addq $72, %rsp                   # release
retq                             # return
```

The program returns 41.

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
