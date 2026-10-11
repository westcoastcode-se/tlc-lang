# Methods

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)

A method belongs to a struct or an enum, and it is written in the same file as that type. A method in any other file is `method in another file`. The first parameter is the receiver. It is the name, then `this`: `self this`. `this` stands for that struct or enum. A pointer receiver is `self *this`, the same order as `*Point`. `self ?*this` may be `null`, and a `*T` value may be passed to it. `this` is an ordinary identifier in every other position. On an enum, the receiver is that enum's value.

The call omits the receiver. This program returns 42.

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

`add` takes a pointer, so `p` must be an lvalue. The compiler passes `&p`. `sum` takes a value. A call on a `*Point` loads the struct. A pointer method on a temporary struct value is `not an lvalue`.

The receiver may have one star. `self **this` is rejected.

The same method name may exist on different structs and as a free function. The call `p.sum(...)` selects the struct's method. A free call `sum(...)` selects the function.

Every file in the package can call the method. It is not exported, so another package cannot call it. `export` on a method is `not supported yet`. `Point::sum(p, 1)` is not a call.

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

There is no `impl` keyword. A struct satisfies a trait by defining the methods, with the same return type and the same parameters after the receiver. The check happens where a concrete struct value or a concrete pointer is passed to a trait parameter.

- `s Shape` requires every method to have a value receiver.
- `s *Shape` accepts both value receivers and pointer receivers.
- A pointer method passed as a value is `type mismatch`.
- A missing method is `undeclared identifier`.
- A wrong return type or wrong extra parameters are `type mismatch`.
- The address is not taken automatically. `apply(r)` is `type mismatch` when the parameter is `*Shape`. The call is `apply(&r)`.

A trait type is allowed only as a function parameter, either `Shape` or `*Shape`. A variable, a return type, an array, a type alias, and `**Shape` are `not supported yet`. `sizeof(Shape)` is `undeclared identifier`.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

`this` inside a trait method is `type mismatch`. A body in the trait is rejected. A function with a trait parameter cannot be `export`, `extern`, or variadic, and a method cannot take a trait parameter.

`s.area` without a call is `undeclared identifier`.

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)
