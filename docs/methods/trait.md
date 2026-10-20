# Traits

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A trait is a list of methods and fields. The receiver is implicit and is omitted from the trait type. A field is a name and a type, the same form as a struct field. A value on the field, `w i32 = 1`, is reported as `not supported yet`.

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
- `unsafe func draw() void;` makes that method unsafe. The call is `unsafe s.draw()` and is reported as `unsafe call` without the prefix. The struct method is `unsafe func Rect::draw`. Leaving `unsafe` off that method, or writing it when the trait method has none, is reported as `type mismatch`. An anonymous function passed to that trait is not an `unsafe func`. The call through the trait still writes `unsafe`.

A generic function writes the same constraint as `T Shape`. `value *T` follows `*Shape`, and `value T` follows `Shape`. See [Generics](../functions/generic.md).

`trait` is written only in `type Shape trait`. A parameter is `Shape` or `*Shape`. `var s trait Shape`, `sizeof(trait Shape)`, and `alignof(trait Shape)` are reported as `expected a type`. `sizeof(Shape)` and `alignof(Shape)` are reported as `undeclared identifier`.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

A trait method may return several results, the same way a function does. `var a, b = p.both()` unpacks them. One name binds the tuple. A different count is reported as `type mismatch`. The struct method returns that same list. A default is `func Pair::both` outside the braces.

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

A default method is a function written on the trait, with a body, outside the braces. The call writes that trait. `s.twice()` when `s` is `*Shape` runs the body. `r.twice()` when `r` is `Rect` is reported as `undeclared identifier`. The struct's own method with the same parameters after the receiver is a method of that struct. A call on the struct uses that body, and the call through the trait uses it too. A different parameter list is another method. A missing method that the trait requires, and that no default supplies, is reported as `undeclared identifier`. A default whose parameters, result, and `unsafe` match a required method supplies that method when the struct omits it. That call is still written on the trait.

```c
type Shape trait {
    func area() i32;
};

func Shape::twice(self *this) i32 {
    return self.area() + self.area();
}

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self *this) i32 {
    return self.w * self.h;
}

func use(s *Shape) i32 {
    return s.twice();
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return use(&r);
}
```

`use(&r)` is 60. `a.malloc[i32]()` when `a` is `*Allocator` is the same default as `arena.malloc[i32]()`. The call is specialized for the struct that was passed. A struct method `Arena::malloc[T type]` wins over that default. The result is `?*T`, and the caller handles `null`. The call is `unsafe`.

## Cost

`s.twice()` on `*Shape` is a direct call, the same as another method of that trait. A trait parameter is specialized. There is no table. `measure(r)` for a struct of two `i32` fields executes 8 instructions in `main` and does not push a trace frame: store the fields, copy them into the argument registers, call. The struct is not zeroed first. `s.area()` inside that copy is a direct traced call of `Rect.area`, which is 5 instructions for two `i32` fields multiplied. `var a, b = p.both()` when `both` returns `(i32, i32)` is that same direct call. The two results come back in `%eax` and `%edx`.

A field named by the trait is the struct field. Reading it is 1 load. Writing it through `*Shape` reloads the pointer and stores, the same as a field of `*Rect`.

An anonymous function passed to a one-method trait is a function pointer. `twice((n) { return n + 1; }, 20)` executes 6 instructions in `main`: the address and the argument are placed, and `callq` runs, with no trace frame at that call. Inside the specialized function, `a.apply(n)` is an indirect traced call. The anonymous function `return n + 1` is 4 instructions.

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
