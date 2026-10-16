# Traits

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

## Cost

A trait parameter is specialized. There is no table. `measure(r)` for a struct of two `i32` fields executes 10 instructions in `main` and does not push a trace frame: zero the struct, store the fields, copy them into the argument registers, call. `s.area()` inside that copy is a direct traced call of `Rect.area`, which is 5 instructions for two `i32` fields multiplied.

A field named by the trait is the struct field. Reading it is 1 load. Writing it through `*Shape` reloads the pointer and stores, the same as a field of `*Rect`.

An anonymous function passed to a one-method trait is a function pointer. `twice((n) { return n + 1; }, 20)` executes 6 instructions in `main`: the address and the argument are placed, and `callq` runs, with no trace frame at that call. Inside the specialized function, `a.apply(n)` is an indirect traced call. The anonymous function `return n + 1` is 4 instructions.

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
