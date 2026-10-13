# Methods and traits

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)

A method belongs to a struct or an enum, and it is written in the same file as that type. A method in any other file is reported as `method in another file`. The receiver is the first parameter. It is the name, then `this`: `self this`. `this` stands for that struct or enum. A pointer receiver is `self *this`, the same order as `*Point`. `self ?*this` may be `null`, and a `*T` value may be passed to it. `this` is an ordinary identifier in every other position. On an enum, the receiver is that enum's value. A function with no receiver is a static function.

The call omits the receiver. The program below returns 42.

```c
type Point struct {
    x int;
    y int;
};

func Point::sum(self this, extra int) int {
    return self.x + self.y + extra;
}

func Point::add(self *this, dx int, dy int) void {
    self.x = self.x + dx;
    self.y = self.y + dy;
}

func main() int {
    var p Point;
    p.x = 10;
    p.y = 10;
    p.add(1, 1);
    return p.sum(20);
}
```

`add` takes a pointer, so `p` must be an lvalue. The compiler passes `&p`. `sum` takes a value. A call on a `*Point` loads the struct. A pointer method on a temporary struct value is reported as `not an lvalue`.

The same method name may exist on different structs and as a free function. The call `p.sum(...)` selects the struct's method. A free call `sum(...)` selects the function.

Every file in the package can call the method. It is not exported, so another package cannot call it. `Point::sum(p, 1)` is not a call.

## Generic methods

A method on a generic struct names that struct's parameters. `func Vec[T]::get(self *this, i int) T` is the method on `type Vec[T type] struct`. `type Buf[N int, T type] struct` uses `func Buf[N, T]::get`. The names match the struct, in the same order. Leaving the list out is reported as `type mismatch`. A different name is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. A struct that is not generic keeps `func Point::sum`. `func Point[T]::sum` is reported as `type mismatch`.

## Static functions

A function on the struct with no `this` parameter is static. The call names the type, not a value: `Point.make(40, 2)`. A call on a value, `p.make(40, 2)`, is reported as `type mismatch`. An instance method called on the type, `Point.sum()`, is reported as `type mismatch`.

```c
type Point struct {
    x int;
    y int;
};

func Point::make(x int, y int) Point {
    var p Point;
    p.x = x;
    p.y = y;
    return p;
}

func main() int {
    var p = Point.make(40, 2);
    return p.x + p.y;
}
```

The program returns 42. The function is written in the same file as the struct. Every file in the package can call it. It is not exported. An enum uses the same form, `Color.code()`. A missing name, `Point.missing()`, is reported as `undeclared identifier`.

## Traits

A trait is a list of methods. The receiver is implicit and is omitted from the trait type.

```c
type Shape trait {
    func area() int;
    func grow(n int) void;
};

type Rect struct {
    w int;
    h int;
};

func Rect::area(self this) int {
    return self.w * self.h;
}

func Rect::grow(self *this, n int) void {
    self.w = self.w + n;
    self.h = self.h + n;
}

func measure(s *Shape) int {
    return s.area();
}

func apply(s *Shape, n int) int {
    s.grow(n);
    return measure(s);
}

func main() int {
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

A trait is written as a function parameter, `Shape` or `*Shape`. `sizeof(Shape)` is reported as `undeclared identifier`.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

`this` inside a trait method is reported as `type mismatch`. `s.area` without a call is reported as `undeclared identifier`.

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)
