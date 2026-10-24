# Traits

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A trait is a list of methods and fields. The receiver is implicit and is omitted from the trait type. A method inside the braces ends with `;`. `func area() i32 { return 1; }` is reported as `expected ';'`. A field is a name and a type, and it ends with `;`. A value on the field, `w i32 = 1`, is reported as `expected ';'`. `extern(abi="c") type Shape trait` is `extern(abi="c") on a trait or a generic`. `export type Shape trait` and `extern type Shape trait` are `not supported yet`.

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

`type Shape trait(Parent)` copies `Parent`'s methods and fields into `Shape` while compiling. The child still stores `Parent` as its parent. A copied field or method keeps that trait as the place it was declared. `func need(p Parent)` accepts `s` when `s` is `Shape`. The call is still the struct's method. There is no table. A struct passed as `Shape` must provide those methods and fields too. Writing the same method or field again is `redefinition`. A missing parent is `undeclared identifier`. A parent that is not a trait, or a cycle, is `type mismatch`. `trait(pkg.Parent)` is `not supported yet`.

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

`trait` is written only in `type Shape trait`. A parameter is `Shape` or `*Shape`. `var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, and `[]Shape` are `trait has no size`. An omitted result is that same error: `func id(s Shape) { return s; }` and `func id(p *Shape) { return p; }`. That includes `[2]*Shape`, `[]*Shape`, a parameter `[]Shape`, and a field `item Shape` or `item *Shape` in a struct or a trait. `var s trait Shape`, `sizeof(trait Shape)`, and `alignof(trait Shape)` are reported as `expected a type`. `sizeof(Shape)` and `alignof(Shape)` are reported as `undeclared identifier`.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. `type Shape trait(Parent)` adds no instruction and no table. `main` that stores 5 and 6 and returns `use(r)`, where `area` is copied from `Parent`, executes 8 instructions. The call is `main.use.spec.Rect`, and `s.area()` inside it is a direct call of `Rect.area`. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

A trait method may return several results, the same way a function does. `var a, b = p.both()` unpacks them. One name binds the tuple. A different count is reported as `type mismatch`. The struct method returns that same list. A default is `func Pair::both` outside the braces.

A trait names a field that the struct already has. `s.w` reads it, and `s.w = 1` writes it, for both `s Shape` and `s *Shape`. Writing through `*const Shape` is reported as `type mismatch`.

`Drawable(value)` names that trait when one function or one method has several parameter lists and those lists take different traits. `use(Drawable(s))` and `p.show(Movable(s))` select the list. A child trait is one of those lists: `use(Named(r))` and `use(Shape(r))` when `Named` is `trait(Shape)`. The value has to satisfy the trait that was written. `use(s)` and `p.show(s)` are `type mismatch`, including when the value satisfies only one list. A generic trait takes its type from the value. `Getter(h)` is `Getter[i32]` when `h.get()` returns `i32`, and `seq.Seq(items)` is `seq.Seq[i32]` when `items` is an array of `i32`. `Getter[i32](h)` and `seq.Seq[i32](items)` write that type. `var d Drawable = Drawable(s)` is `trait has no size`. `use(*Drawable(&s))` is a pointer cast: `--check` accepts it and `--run` reports `type mismatch`. The cast adds no instruction. `main` that stores 4 and returns `use(Drawable(s))` executes 6 instructions, the same as `use(s)`: the store, the copy into the argument register, and `callq`, with no trace frame at that call. One list keeps the symbol `main.use.spec.Sprite`. Several lists put the trait in the name, such as `main.take.spec.Named.Rect`.

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

A trait with one method and no fields also accepts an anonymous function, and a named function with that method's parameters and result. There is no receiver. The short form and the `func` form both work. The parameter may be the trait or a pointer to the trait, and the function is passed directly. `pkg.inc` is that named function when `inc` is exported. `Type.name` is that named function when it is a static function with the method's signature. A function written inside another function is the same argument when it does not read a local or a parameter from the enclosing function. A trait with a field, with no methods, or with several methods, is reported as `type mismatch`. The wrong parameters or the wrong result are reported as `type mismatch`. An overloaded name is reported as `not supported yet`. A prototype with no body is reported as `type mismatch`. `var f = inc` followed by passing `f` is reported as `type mismatch`. An `unsafe func` used as the argument is reported as `type mismatch`. An inner function that reads an enclosing local is reported as `not supported yet`.

```c
type Adder trait {
    func apply(n i32) i32;
};

func twice(a Adder, n i32) i32 {
    return a.apply(n) + a.apply(n);
}

func inc(n i32) i32 {
    return n + 1;
}

func main() i32 {
    return twice((n) { return n + 1; }, 20) + twice(inc, 3);
}
```

`twice` calls that function twice. The `func` form is the same argument, and so is `inc`. `twice(inc, 3)` returns 8. A method takes the function the same way: `c.add(inc)`. `*Adder` takes it directly, as `take_ptr(inc)`. `(*a).apply(n)` is that same call when `a` is `*Adder`. One monomorphized copy calls it through a function pointer. A struct argument still gets its own copy and a direct call, including `(*a).apply(n)`. Passing the parameter onward, when the next parameter is the same trait, uses that same copy.

```c
type Adder trait {
    func apply(n i32) i32;
};

type Counter struct {
    base i32;
};

func through(a *Adder, n i32) i32 {
    return (*a).apply(n);
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

A default method is a function written on the trait, with a body, outside the braces. `extern func Shape::twice` names that default in a tlc library. The default is copied onto each struct that gains it. Each copy is its own symbol, the same spelling an exported method on that struct uses, so the copy on `Rect` is `qualifier.Rect.twice`. A body defines that symbol. `extern(abi="tlc")` is the same declaration. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are `extern(abi="c") on a trait or a generic`. A C symbol is one concrete library name, and the default is copied onto each struct. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`. A prototype with no body is `not supported yet`. `s.twice()` when `s` is `*Shape` runs the body. `r.twice()` when `r` is `Rect` runs the copy `Rect` gained. The struct's own method with the same parameters after the receiver is a method of that struct. A call on the struct uses that body, and the call through the trait uses it too. A different parameter list is another method. A missing method that the trait requires, and that no default supplies, is reported as `undeclared identifier`. A default whose parameters, result, and `unsafe` match a required method supplies that method when the struct omits it. That call is still written on the trait, or on the struct that gained the copy.

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

`export func use(s Shape)` publishes that function. Another package calls `pkg.use(r)`, or `use(r)` after `import { use }`. `export func Rect::paint(self *this, s Shape)` publishes that method. Another package calls `value.paint(s)`. The parameter is `Shape` or `*Shape`. The call is still specialized. The symbol of `use(r)` when `use` is in `main` is `main.use.spec.Rect`. When the struct is in another package, its package is part of that symbol. The struct's method is exported when the function and the struct are in different packages. Without that `export`, the call is `area is not exported`. A method in the same package does not need `export`. `extern` and `extern(abi="tlc")` name that function in a tlc library. Each struct is its own symbol. The call stays `use(r)` or `value.paint(s)`. A body defines that symbol. A prototype may be the declaration that says `extern`, and the body that follows defines it. A prototype with no body is accepted. A call that specializes it is `not supported yet`. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that function are `extern(abi="c") on a trait or a generic`. `...` is `not supported yet`. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

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

export func use(s Shape) i32 {
    return s.area();
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return use(r);
}
```

`use(r)` returns 30. `Rect::area` needs no `export` here, because `use` and `Rect` are in the same package.

`type Seq[T type] trait` names one type parameter. `Seq[i32]` and `Seq[u8]` are different traits. A method may use `T` as a parameter or a result, and a field may use `T` as well. A struct satisfies `Seq[i32]` by defining those methods and fields with `i32` in place of `T`. There is no `impl`. The parameter is `items Seq[i32]` or `items *Seq[i32]`. `Seq` without arguments is reported as `type mismatch`. An integer parameter, a pack, several parameters, and `trait(Parent)` on that list are reported as `not supported yet`. `func Seq::at` is reported as `not supported yet`. `export` publishes the trait. Another package names `pkg.Seq[i32]`. `extern` and `extern(abi="tlc")` are accepted. The trait has no size and no symbol. `extern(abi="c")` is reported as `extern(abi="c") on a trait or a generic`. `func use[T type](items Seq[T])` is reported as `not supported yet`.

```c
type Seq[T type] trait {
    func at(i i32) T;
};

type Bag struct {
    n i32;
};

func Bag::at(self this, i i32) i32 {
    return self.n + i;
}

func use(items Seq[i32]) i32 {
    return items.at(2);
}

func main() i32 {
    var bag Bag;
    bag.n = 40;
    return use(bag);
}
```

`use(bag)` returns 42. `at` is a direct call of `Bag.at`. Passing a struct whose `at` returns `u8` where `Seq[i32]` is expected is reported as `type mismatch`. `import "std/seq"` names that trait as the iteration trait. `seq.Seq[T]` and `*seq.Seq[T]` check `len` and `at`. A pointer to an element is `ptr_of`. An array `[N]E` and a slice `[]E` satisfy `seq.Seq[E]`. On those values `len` is `.len` and `at` is the index. A struct still satisfies the trait by its own methods. The declaration is on [seq](../std/seq.md).

## Cost

`s.twice()` on `*Shape` is a direct call, the same as another method of that trait. A trait parameter is specialized. There is no table. `var s Shape`, `var s *Shape`, a result `Shape` or `*Shape`, `[2]Shape`, `[]Shape`, and a field `item Shape` are rejected and add no instructions. `measure(r)` for a struct of two `i32` fields executes 8 instructions in `main` and does not push a trace frame: store the fields, copy them into the argument registers, call. The struct is not zeroed first. `export func use(s Shape)` is that same call and adds no instruction. When `use` is in another package, `main` that stores 5 and 6 and returns `pkg.use(r)` executes 8 instructions. The call is `pkg.use.spec.main.Rect`. `main` that stores 5 and 7 and returns `r.paint(c)`, where `export func Rect::paint` takes `Shape`, executes 43 instructions when fewer than 32 frames are stored. That call pushes a trace frame. The specialized method is `main.Rect.paint.spec.Circle` when `paint` is in `main`. `extern func use(s Shape)` followed by `return use(r)` is that same 8 instructions in `main`. `extern(abi="tlc") func Rect::paint` followed by `return r.paint(c)` is that same 43 instructions. `extern` changes the linkage and adds no instruction. `extern(abi="c")` and `...` on that function are rejected and add no instructions. A call that specializes a prototype with no body is rejected and adds no instructions. `s.area()` inside that copy is a direct traced call of `Rect.area`, which is 5 instructions for two `i32` fields multiplied. `var a, b = p.both()` when `both` returns `(i32, i32)` is that same direct call. The two results come back in `%eax` and `%edx`. `extern func Shape::twice` is that same call. `main` that stores 5 and 6 into `Rect` and returns `use(&r)` executes 7 instructions: the two fields are stored, the pointer is passed, and `callq` runs. There is no trace frame at that call. The copy is `qualifier.Rect.twice`. `extern` changes the linkage and adds no instruction. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are rejected, so they add no instructions. A `{` body on a method inside the trait is rejected and adds no instructions. `Square::twice` stays that struct's own function.

A field named by the trait is the struct field. Reading it is 1 load. Writing it through `*Shape` reloads the pointer and stores, the same as a field of `*Rect`.

`export` and `extern` on `type Seq[T type] trait` add no instructions. The trait has no size and no symbol. The check that a value satisfies `seq.Seq[T]` adds no instructions. `items.len()` and `items.at(i)` on a struct are the same direct calls as another trait parameter. On an array or a slice they are `.len` and the index, and they do not push a trace frame.

An anonymous function passed to a one-method trait is a function pointer. `twice((n) { return n + 1; }, 20)` executes 6 instructions in `main`: the address and the argument are placed, and `callq` runs, with no trace frame at that call. Inside the specialized function, `a.apply(n)` is an indirect traced call. The anonymous function `return n + 1` is 4 instructions. `twice(inc, 20)` is that same call. `main` executes 6 instructions: `leaq` of `inc`, the argument, and `callq`, with no trace frame at that call. `inc`, `return n + 1`, is 4 instructions. The indirect call inside the copy is the same one the anonymous function uses. `(*a).apply(n)` is that same indirect call.

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
