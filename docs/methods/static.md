# Static functions

[Index](../README.md) · [Previous: Generic methods](generic.md) · [Next: Methods on primitive aliases](alias.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A function on the struct with no `this` parameter is static.

## Calling the function

The call names the type, not a value: `Point.make(40, 2)`.

A call on a value, `p.make(40, 2)`, is reported as `type mismatch`.

An instance method called on the type, `Point.sum()`, is reported as `type mismatch`.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::make(x i32, y i32) Point {
    var p Point;
    p.x = x;
    p.y = y;
    return p;
}

func main() i32 {
    var direct = Point.make(40, 1);
    var ptr = Point.make;
    var p = ptr(0, 2);
    return direct.x + p.y;
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $1, %esi                    # constant
callq make                       # call make
retq                             # return

make:
movl %edi, -16(%rsp)             # move
movl %esi, -12(%rsp)             # move
movl -16(%rsp), %eax             # move
movl %eax, -8(%rsp)              # move
movl -12(%rsp), %eax             # move
movl %eax, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
movl -4(%rsp), %edx              # move
retq                             # return
```

The program returns 42.

The function is written in the same file as the struct.

Every file in the package can call it.

`export` publishes `pkg.Type.name(...)`.

Without `export` that call is reported as `name is not exported`.

`var ptr = pkg.Point.make` is that function.

An enum uses the same form, `Color.label()` and `pkg.Color.code()`.

A missing name, `Point.missing()`, is reported as `undeclared identifier`.

## Static method templates

A static method template puts the type parameters after the name.

`func Point::id[T type](value T) T` is called as `Point.id[i32](42)`.

A call on a value is reported as `type mismatch`.

A call without the brackets is reported as `undeclared identifier`.

`export` publishes `pkg.Point.id[i32](...)`.

On a generic struct, `func Vec[T]::get[U type](value U) U` is called as `Vec[i32].get[i32](value)`.

`T` comes from the struct.

`U` is chosen at the call.

`export` publishes `pkg.Vec[i32].get[i32](...)`.

Without `export`, that call is `name is not exported`.

An unexported struct is `Vec is not exported`.

A pointer alias uses that same call, `Ptr.make[i32](2)`.

```c
type Point struct {
    x i32;
};

func Point::id[T type](value T) T {
    return value;
}

func main() i32 {
    return Point.id[i32](42);
}
```

```asm
id.spec.i32:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $42, %edi                   # constant
callq id.spec.i32                # call id
retq                             # return
```

The program returns 42.

When the file is `main.tlc`, the symbol is `main.Point.id.spec.i32`.

On a generic struct the call names the specialization.

```c
type Vec[T type] struct {
    n T;
};

func Vec[T]::get[U type](value U) U {
    return value;
}

func main() i32 {
    return Vec[i32].get[i32](42);
}
```

```asm
get.spec.i32:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $42, %edi                   # constant
callq get.spec.i32               # call get
retq                             # return
```

The program returns 42.

When the file is `main.tlc`, the symbol is `main.Vec.spec.i32.get.spec.i32`.

`Buf[40, i32].width[i32](2)` is `main.Buf.spec.40.i32.width.spec.i32`.

## The name as a value

The name without a call is a function pointer.

`var ptr = Point.make` and `take(Point.make)` use that pointer, and `ptr(40, 2)` calls it.

`var ptr = add` is the same form for a free function.

An instance method used as a value, `var ptr = Point.sum`, is reported as `type mismatch`.

An enumerator stays a value, so `Color.Red` is not a function pointer.

## Aliases

A pointer alias, a sized-array alias, and a struct alias use that same call.

`Ptr.from(&p)`, `Pair.from(1, 2)`, and `Spot.origin(40, 2)` name the functions.

`var f = Ptr.from` is that function.

A call on a value, `p.from(&live)`, is reported as `type mismatch`.

A method on those aliases, `func Ptr::get(self this)`, and `var Ptr::name`, are reported as `type mismatch`.

`type Op func (i32) i32` cannot have a static function.

The builtin name `i32` cannot either.

## A generic struct

A static function on a generic struct uses the same call.

`Box[i32].from(40)` and `Buf[1, i32].width()` name the specialization.

`var ptr = Box[i32].from` and `take(Box[i32].from)` are that function.

A call written in the method, `Box[T].from(0)`, uses the same specialization.

Another package calls `pkg.Box[i32].from(40)` and `pkg.Buf[1, i32].width()` when the function is exported.

Without `export` that call is reported as `name is not exported`.

An instance method on the specialization, `Vec[i32].get()`, is reported as `type mismatch`.

A missing name is reported as `undeclared identifier`.

```c
type Box[T type] struct {
    value T;
};

func Box[T]::from(value T) Box[T] {
    var box Box[T];
    box.value = value;
    return box;
}

func Box[T]::zero() Box[T] {
    return Box[T].from(0);
}

type Buf[N i32, T type] struct {
    value T;
};

func Buf[N, T]::width() i32 {
    return N;
}

func main() i32 {
    var box = Box[i32].from(40);
    var ptr = Box[i32].from;
    var once = ptr(1);
    var zero = Box[i32].zero();
    return box.value + once.value + Buf[1, i32].width() + zero.value;
}
```

```asm
width:
movl $1, %eax                    # constant
retq                             # return

zero:
# trace frame for the call, then the matching pop
callq from                       # call from
retq                             # return

from:
movl %edi, -12(%rsp)             # move
movl -12(%rsp), %eax             # move
movl %eax, -8(%rsp)              # move
movl -8(%rsp), %eax              # move
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
callq from                       # call from
retq                             # return
```

The program returns 42.

## Returning a function pointer

A method can return a function pointer.

The call is that pointer.

An instance method and a static method both can.

The value may be an anonymous function, in the short form or with the parameter types and the result written out.

The program below returns 42.

```c
type Box struct {
    n i32;
};

func Box::choose(self this) func (i32, i32) i32 {
    return (a, b) { return a + b; };
}

func Box::static_choose() func (i32, i32) i32 {
    return func (a i32, b i32) i32 { return a + b; };
}

func main() i32 {
    var box Box;
    var from_method = box.choose();
    var from_static = Box.static_choose();
    return from_method(20, 10) + from_static(10, 2);
}
```

This example is rejected (`uninitialized`) and emits nothing.

[Index](../README.md) · [Previous: Generic methods](generic.md) · [Next: Methods on primitive aliases](alias.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
