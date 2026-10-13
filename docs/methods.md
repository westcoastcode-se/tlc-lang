# Methods and traits

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)

A method belongs to a struct or an enum, and it is written in the same file as that type. A method in any other file is reported as `method in another file`. The receiver is the first parameter. It is the name, then `this`: `self this`. `this` stands for that struct or enum. A pointer receiver is `self *this`, the same order as `*Point`. `self ?*this` may be `null`, and a `*T` value may be passed to it. `this` is an ordinary identifier in every other position. On an enum, the receiver is that enum's value. A function with no receiver is a static function.

The call omits the receiver. The program below returns 42.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::sum(self this, extra i32) i32 {
    return self.x + self.y + extra;
}

func Point::add(self *this, dx i32, dy i32) void {
    self.x = self.x + dx;
    self.y = self.y + dy;
}

func main() i32 {
    var p Point;
    p.x = 10;
    p.y = 10;
    p.add(1, 1);
    return p.sum(20);
}
```

`add` takes a pointer, so `p` must be an lvalue. The compiler passes `&p`. `sum` takes a value. A call on a `*Point` loads the struct. A pointer method on a temporary struct value is reported as `not an lvalue`.

`self ?*this` may be `null`. A `*Point` may be passed to it.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::read(self ?*this) i32 {
    if (self == null) {
        return 0;
    }
    return self.x;
}

func main() i32 {
    var p Point;
    p.x = 40;
    p.y = 2;
    var q ?*Point = &p;
    return p.read() + q.read();
}
```

The same method name may exist on different structs and as a free function. The call `p.sum(...)` selects the struct's method. A free call `sum(...)` selects the function.

Every file in the package can call the method. It is not exported, so another package cannot call it. `Point::sum(p, 1)` is not a call.

## Generic methods

A method on a generic struct names that struct's parameters. `func Vec[T]::get(self *this, i i32) T` is the method on `type Vec[T type] struct`. `type Buf[N i32, T type] struct` uses `func Buf[N, T]::get`. The names match the struct, in the same order. Leaving the list out is reported as `type mismatch`. A different name is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. A struct that is not generic keeps `func Point::sum`. `func Point[T]::sum` is reported as `type mismatch`.

```c
type Vec[T type] struct {
    n T;
};

func Vec[T]::get(self this) T {
    return self.n;
}

type Buf[N i32, T type] struct {
    value T;
};

func Buf[N, T]::len(self this) i32 {
    return N;
}

func main() i32 {
    var v Vec[i32];
    v.n = 38;
    var b Buf[4, i32];
    return v.get() + b.len();
}
```

## Static functions

A function on the struct with no `this` parameter is static. The call names the type, not a value: `Point.make(40, 2)`. A call on a value, `p.make(40, 2)`, is reported as `type mismatch`. An instance method called on the type, `Point.sum()`, is reported as `type mismatch`.

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

The program returns 42. The function is written in the same file as the struct. Every file in the package can call it. It is not exported. An enum uses the same form, `Color.label()`. A missing name, `Point.missing()`, is reported as `undeclared identifier`.

The name without a call is a function pointer. `var ptr = Point.make` and `take(Point.make)` use that pointer, and `ptr(40, 2)` calls it. `var ptr = add` is the same form for a free function. An instance method used as a value, `var ptr = Point.sum`, is reported as `type mismatch`. An enumerator stays a value, so `Color.Red` is not a function pointer.

A method can return a function pointer. The call is that pointer. An instance method and a static method both can. The value may be an anonymous function, in the short form or with the parameter types and the result written out. The program below returns 42.

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

## Traits

A trait is a list of methods. The receiver is implicit and is omitted from the trait type.

```c
type Shape trait {
    func area() i32;
    func grow(n i32) void;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func Rect::grow(self *this, n i32) void {
    self.w = self.w + n;
    self.h = self.h + n;
}

func measure(s *Shape) i32 {
    return s.area();
}

func apply(s *Shape, n i32) i32 {
    s.grow(n);
    return measure(s);
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return apply(&r, 1);
}
```

`apply(&r, 1)` changes the width and height to 6 and 7. `area` returns 42.

There is no `impl` keyword. A struct satisfies a trait by defining the methods, with the same result type and the same parameters after the receiver. The check happens where a concrete struct value or a concrete pointer is passed to a trait parameter.

- `s Shape` requires every method to have a value receiver.
- `s *Shape` accepts both value receivers and pointer receivers.
- A pointer method passed as a value is reported as `type mismatch`.
- A missing method is reported as `undeclared identifier`.
- A wrong result type or wrong extra parameters are reported as `type mismatch`.
- The address is not taken automatically. `apply(r)` is reported as `type mismatch` when the parameter is `*Shape`. The call is `apply(&r)`.

A trait is written as a parameter of a function or a method, `Shape` or `*Shape`. `sizeof(Shape)` is reported as `undeclared identifier`.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

A trait with one method also accepts an anonymous function. The function's parameters and its result are that method's, and there is no receiver. The short form and the `func` form both work. The parameter may be the trait or a pointer to the trait, and the anonymous function is passed directly. A trait with no methods, or with several methods, is reported as `type mismatch`. A named function is reported as `type mismatch`. The wrong parameters or the wrong result are reported as `type mismatch`.

```c
type Adder trait {
    func apply(n i32) i32;
};

func twice(a Adder, n i32) i32 {
    return a.apply(n) + a.apply(n);
}

func main() i32 {
    return twice((n) { return n + 1; }, 20);
}
```

`twice` calls that function twice. The `func` form is the same argument. A method takes it the same way. `*Adder` takes the anonymous function directly. One monomorphized copy calls it through a function pointer. A struct argument still gets its own copy and a direct call. Passing the parameter onward, when the next parameter is the same trait, uses that same copy.

```c
type Adder trait {
    func apply(n i32) i32;
};

type Counter struct {
    base i32;
};

func through(a *Adder, n i32) i32 {
    return a.apply(n);
}

func Counter::add(self *this, a Adder) i32 {
    return self.base + a.apply(1);
}

func Counter::forward(a *Adder, n i32) i32 {
    return through(a, n);
}

func main() i32 {
    var c Counter;
    c.base = 6;
    var from_method = c.add(func (n i32) i32 { return n; });
    var from_static = Counter.forward((n) { return n + 1; }, 4);
    return from_method + from_static;
}
```

`c.add` passes the `func` form to a method. `Counter.forward` passes the short form to a static function whose parameter is `*Adder`, and `through` receives that same parameter.

`this` inside a trait method is reported as `type mismatch`. `s.area` without a call is reported as `undeclared identifier`.

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)
