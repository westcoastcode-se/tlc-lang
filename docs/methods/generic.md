# Generic methods

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A method on a generic struct names that struct's parameters.

## The parameter list on the method

A parameter after the method name that the method does not use is `unused parameter`.

`func Vec[T]::get(self *this, i i32) T` is the method on `type Vec[T type] struct`.

The result may be omitted: `func Vec[T]::get(self this) { return self.n; }` is `T`.

A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value.

That omitted result may be a function pointer.

`type Buf[N i32, T type] struct` uses `func Buf[N, T]::get`.

The names match the struct, in the same order.

Leaving the list out is reported as `type mismatch`.

A different name is reported as `type mismatch`.

The wrong count is reported as `argument count`.

`func Vec[T type]::get` is reported as `type mismatch`.

A struct that is not generic keeps `func Point::sum`.

`func Point[T]::sum` is reported as `type mismatch`.

## Method templates

`func Point::id[T type](self *this) T` is a method template: the brackets follow the method name.

`func Point::take(self *this, other (i32, i32))` is a tuple parameter.

`[P1, P2]` is `expected a type` on the comma.

With `[P1 type, P2 type]`, `func Point::take[P1 type, P2 type](self *this, other (P1, P2))` and `func Vec[T]::take[P1 type, P2 type](self *this, other (P1, P2))` are that tuple.

The call is `p.take[i32, i32]((40, 2))` or `v.take[i32, i32]((40, 2))`.

A call without those brackets is `undeclared identifier`.

`func Vec[T]::pair(self *this, other (T, i32))` is that tuple of the struct's parameter.

`func Vec[T]::get[U type](self *this, value U) U` is that template on a generic struct.

`T` is the struct's parameter.

`U` is chosen at the call `value.get[i32](value)`.

A call without those brackets is `undeclared identifier`.

A type parameter that repeats a struct parameter, `func Vec[T]::get[T type]`, is `redefinition`.

## Static method templates

A static method template has no receiver.

`func Point::id[T type](value T) T` is called as `Point.id[i32](value)`.

`export` on that function publishes `pkg.Point.id[i32](...)`.

`export func Point::id[T type](self *this, value T) T` publishes `value.id[i32](...)`.

Without `export`, that call is `id is not exported`.

`func Integer::id[T type]` is that template on an alias.

A static template called on a value is `type mismatch`.

An instance template called on the type is `type mismatch`.

On a generic struct, `func Vec[T]::get[U type](value U) U` is called as `Vec[i32].get[i32](value)`.

`export` publishes `pkg.Vec[i32].get[i32](...)`.

`export` on `func Vec[T]::get[U type](self *this, value U) U` publishes `value.get[i32](...)`.

Without `export`, that call is `get is not exported`.

An unexported struct is `Vec is not exported`.

A prototype with no body on a method template is `not supported yet`.

`...` on a method template is `not supported yet`.

## extern on a method template

`extern func Point::id[T type]` names that method template in a tlc library.

The call stays `value.id[i32](value)`.

Each specialization is its own symbol, such as `qualifier.Point.id.spec.i32`.

A body defines that symbol.

`extern func Point::measure[T Shape]` is that template when `T` is constrained.

The call is `value.measure[Rect](r)`.

`extern(abi="c")` and `extern(abi="c", name="symbol")` on that template are `extern(abi="c") on a trait or a generic`.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

A prototype with no body on that template is `not supported yet`.

`...` on that template is `not supported yet`.

A parameter whose type is a trait, such as `s Shape` beside `[T type]`, is `not supported yet`.

## Export and extern on the method

`export func Vec[T]::get` publishes that method.

Another package calls `value.get(...)`.

`extern func Vec[T]::get` names that method in a tlc library.

The call stays `value.get(...)`.

When there is no receiver, the call stays `Vec[i32].from(...)`.

Each specialization is its own symbol, the same spelling an exported method on that specialization uses.

A body defines that symbol.

`extern(abi="c")` and `extern(abi="c", name="symbol")` on that method are `extern(abi="c") on a trait or a generic`.

A C symbol is one concrete library name.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

A method without `export` is reported as `get is not exported`.

`Vec[i32].get()` is reported as `type mismatch`.

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

```asm
get:
movl %edi, -8(%rsp)              # move
movl -8(%rsp), %eax              # move
retq                             # return

main:
# trace frame for the call, then the matching pop
callq get                        # call get
retq                             # return
```

A static method template is called on the type.

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

A static method template on a generic struct is called on the specialization.

`T` comes from the struct.

`U` is chosen at the call.

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

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
