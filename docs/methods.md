# Methods and traits

[Index](README.md) · [Previous: Structs, pointers, and arrays](structs.md) · [Next: Enums and type aliases](enums.md) · [Grammar](grammar.md)

A method belongs to a struct, an enum, or a primitive type alias such as `type Integer i32`, and it is written in the same file as that type. A method in any other file is reported as `method in another file`. The receiver is the first parameter. It is the name, then `this`: `self this`. `this` stands for that struct, enum, or alias. A pointer receiver is `self *this`, the same order as `*Point`. `self ?*this` may be `null`, and a `*T` value may be passed to it. `this` is an ordinary identifier in every other position. On an enum, the receiver is that enum's value. On `type Integer i32`, the receiver is an `Integer`, not an `i32`. A function with no receiver is a static function. The builtin name `i32` cannot have a method. A pointer alias, an array alias, and a struct alias cannot either.

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

A const variable, and a `*const Point`, cannot call a method whose receiver is a non-const pointer. `origin.bump()` and `frozen.bump()`, when `bump` is `self *this`, are reported as `type mismatch: expected *Point, found *const Point`. The same call through `?*this` is reported the same way, with `?*Point` as the expected type. `self *const this` and a value receiver can be called on that variable. A store through `self *const this` is reported as `type mismatch`. A const binding of a mutable pointer still calls `self *this`: `const p = &live` then `p.bump()` changes `live`.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::read(self *const this) i32 {
    return self.x + self.y;
}

func Point::value(self this) i32 {
    return self.x;
}

func Point::bump(self *this) void {
    self.x = self.x + 1;
}

const origin Point = {.x = 20, .y = 2};

func main() i32 {
    var live Point;
    live.x = 9;
    const p = &live;
    p.bump();
    const frozen = live;
    return origin.read() + frozen.read() + frozen.value();
}
```

The program returns 42.

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

A static function on a generic struct uses the same call. `Box[i32].from(40)` and `Buf[1, i32].width()` name the specialization. `var ptr = Box[i32].from` and `take(Box[i32].from)` are that function. A call written in the method, `Box[T].from(0)`, uses the same specialization. An instance method on the specialization, `Vec[i32].get()`, is reported as `type mismatch`. A missing name is reported as `undeclared identifier`.

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

The program returns 42.

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

## Primitive aliases

`type Integer i32` can have methods, static functions, and a variable, the same way a struct can. `Integer` and `i32` stay different types, so `n + 1` is `type mismatch` and the arithmetic uses `Integer` values. `type Word Integer` is a different alias, with its own methods. A missing method is reported as `undeclared identifier`. A static function called on a value is reported as `type mismatch`.

```c
type Integer i32;

func Integer::twice(self this) Integer {
    return self + self;
}

func Integer::add(self *this, n Integer) void {
    *self = *self + n;
}

func Integer::from(n i32) Integer {
    return Integer(n);
}

var Integer::ten Integer = Integer(10);

func main() i32 {
    var n = Integer.from(i32(Integer.ten));
    n.add(Integer(11));
    return i32(n.twice());
}
```

The program returns 42. `Integer.from` is the static function. `Integer.ten` is the variable. `n.add` takes a pointer, and `n.twice` takes the value. `var f = Integer.from` is that function. The same forms work for `bool`, `f32`, `f64`, and the other integer widths.

## Variables on a type

A variable may belong to a struct, an enum, or a primitive type alias. It is written beside the type, in the same file, and it is not a field. `sizeof` and the layout stay the same. The use names the type: `Entity.next`. A field of the same spelling is still the field, so `e.next` reads that field. A missing field is reported as `undeclared identifier`.

```c
type Entity struct {
    id i32;
};

var Entity::next i32 = 40;
var Entity::bias = 0;
var Entity::spare i32;

func Entity::make() Entity {
    var e Entity;
    e.id = Entity.next;
    Entity.next = Entity.next + 1;
    return e;
}

type Point struct {
    x i32;
};

type Box[T type] struct {
    value T;
};

var Box[T]::extra i32 = 1;

type Buf[N i32, T type] struct {
    value T;
};

var Buf[N, T]::count i32 = N;

type Color enum { Red };

var Color::label i32;

func main() i32 {
    var e = Entity.make();
    var p = &Entity.next;
    Box[Point].extra = 9;
    return e.id + Box[i32].extra + *p + Buf[4, Point].count + Entity.bias + Entity.spare + Color.label - 44;
}
```

The program returns 42. `Entity.next` is one variable for `Entity`. `Box[i32].extra` and `Box[Point].extra` are two variables. `Buf[4, Point].count` is `N`, which is 4 for that instantiation. Omitting `=` zeroes the variable, the same as any other file-scope `var`. `var Entity::bias = 0` infers the type. `var Entity::row = [_]i32{1, 2}` and `var Entity::typed [2]i32 = {3, 1}` are that variable. `var Entity::scale = 1.5f` is an `f32` variable for the type. A second `var Entity::next`, or a static function of that name, is reported as `redefinition`. A variable in another file is reported as `method in another file`. An unknown type is reported as `type mismatch`.

`var` inside the struct is reported as `not supported yet`. The variable is written beside the type. A member may use the same name: `e.next` is the member and `Entity.next` is the variable. `sizeof` stays the size of the members.

`export var Entity::next i32 = 40` publishes that variable. Another package reads and assigns `pkg.Entity.next` when `Entity` is exported. Without `export`, the name is reported as `next is not exported`. `extern` on the variable, and `export var Box[T]::extra`, are reported as `not supported yet`. A member another package can name is `export next i32` inside the struct.

```c
package math;

export type Entity struct {
    id i32;
};

export var Entity::next i32 = 40;
```

```c
import "math";

func main() i32 {
    math.Entity.next = math.Entity.next + 2;
    return math.Entity.next;
}
```

The program returns 42.

## Constants on a type

A constant may belong to a struct, an enum, or a primitive type alias. It is written beside the type, in the same file: `const Entity::next = 28`. It is not a field, so `sizeof` stays the size of the fields. A field may use the same name. Each specialization of a generic struct has its own constant. `const Box[T]::extra = 8` and `const Buf[N, T]::count = N` are those constants. The use is `Entity.next`, `Box[i32].extra`, or `Buf[4, i32].count`. A `const` inside the struct is reported as `not supported yet`.

Assigning the constant, or taking its address, is reported as `not an lvalue`. A field store through a struct constant is reported as `type mismatch`. `export const Entity::next` publishes it, including `export const Box[T]::limit` on a generic struct. Another package reads `pkg.Entity.next` and `pkg.Box[i32].limit` when the type and the constant are exported. Without `export`, the name is reported as `next is not exported`. An unexported type is reported as `Entity is not exported`. `extern` on the constant, and `const Entity::next` inside a function, are reported as `not supported yet`. `export var` on a generic struct stays `not supported yet`.

```c
type Entity struct {
    id i32;
};

const Entity::next = 28;

type Box[T type] struct {
    value T;
};

const Box[T]::extra = 8;

type Buf[N i32, T type] struct {
    value T;
};

const Buf[N, T]::count = N;

func main() i32 {
    var e Entity;
    e.id = 2;
    return Entity.next + e.id + Box[i32].extra + Buf[4, i32].count;
}
```

The program returns 42. `Buf[4, i32].count` is `N`, which is 4 for that instantiation. `Box[i32].extra` and `Box[Point].extra` are two constants.

## Traits

A trait is a list of methods and fields. The receiver is implicit and is omitted from the trait type. A field is a name and a type, the same form as a struct field.

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

There is no `impl` keyword. A struct satisfies a trait by defining the methods, with the same result type and the same parameters after the receiver, and by having each field with the same type. The check happens where a concrete struct value or a concrete pointer is passed to a trait parameter.

- `s Shape` requires every method to have a value receiver.
- `s *Shape` accepts both value receivers and pointer receivers.
- A pointer method passed as a value is reported as `type mismatch`.
- A missing method is reported as `undeclared identifier`.
- A missing field is reported as `undeclared identifier`.
- A wrong result type, wrong extra parameters, or a field of another type are reported as `type mismatch`.
- A field named twice, or a field and a method of the same name, is reported as `redefinition`.
- The address is not taken automatically. `apply(r)` is reported as `type mismatch` when the parameter is `*Shape`. The call is `apply(&r)`.

A trait is written as a parameter of a function or a method, `Shape` or `*Shape`. `sizeof(Shape)` is reported as `undeclared identifier`.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

A trait names a field that the struct already has. `s.w` reads it, and `s.w = 1` writes it, for both `s Shape` and `s *Shape`. Writing through `*const Shape` is reported as `type mismatch`.

```c
type Shape trait {
    w i32;
    h i32;
    func area() i32;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func widen(s *Shape) i32 {
    s.w = s.w + 1;
    return s.area();
}

func edges(s Shape) i32 {
    return s.w + s.h;
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return widen(&r) + edges(r) - 6;
}
```

`widen` adds 1 to `w` and `area` returns 36. `edges` reads the same fields and returns 12. The program returns 42.

A trait with one method and no fields also accepts an anonymous function. The function's parameters and its result are that method's, and there is no receiver. The short form and the `func` form both work. The parameter may be the trait or a pointer to the trait, and the anonymous function is passed directly. A trait with a field, with no methods, or with several methods, is reported as `type mismatch`. A named function is reported as `type mismatch`. The wrong parameters or the wrong result are reported as `type mismatch`.

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
