# Static functions

[Index](../README.md) · [Previous: Generic methods](generic.md) · [Next: Methods on primitive aliases](alias.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

The program returns 42. The function is written in the same file as the struct. Every file in the package can call it. `export` publishes `pkg.Type.name(...)`. Without `export` that call is reported as `name is not exported`. `var ptr = pkg.Point.make` is that function. An enum uses the same form, `Color.label()` and `pkg.Color.code()`. A missing name, `Point.missing()`, is reported as `undeclared identifier`.

The name without a call is a function pointer. `var ptr = Point.make` and `take(Point.make)` use that pointer, and `ptr(40, 2)` calls it. `var ptr = add` is the same form for a free function. An instance method used as a value, `var ptr = Point.sum`, is reported as `type mismatch`. An enumerator stays a value, so `Color.Red` is not a function pointer.

A pointer alias, a sized-array alias, and a struct alias use that same call. `Ptr.from(&p)`, `Pair.from(1, 2)`, and `Spot.origin(40, 2)` name the functions. `var f = Ptr.from` is that function. A call on a value, `p.from(&live)`, is reported as `type mismatch`. A method on those aliases, `func Ptr::get(self this)`, and `var Ptr::name`, are reported as `type mismatch`. `type Op func (i32) i32` cannot have a static function. The builtin name `i32` cannot either.

A static function on a generic struct uses the same call. `Box[i32].from(40)` and `Buf[1, i32].width()` name the specialization. `var ptr = Box[i32].from` and `take(Box[i32].from)` are that function. A call written in the method, `Box[T].from(0)`, uses the same specialization. Another package calls `pkg.Box[i32].from(40)` and `pkg.Buf[1, i32].width()` when the function is exported. Without `export` that call is reported as `name is not exported`. An instance method on the specialization, `Vec[i32].get()`, is reported as `type mismatch`. A missing name is reported as `undeclared identifier`.

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

## Cost

`Point.make(40, 1)` is a traced call. The two arguments are 2 `movl`. There is no receiver to copy. `Ptr.from`, `Pair.from`, and `Spot.origin` are that same call. `pkg.Point.make(40, 1)` is that same traced call. Taking the function as a value, `var ptr = Point.make`, is 1 `leaq` and 1 store of that address. Calling that pointer is the indirect call on [Function pointers](../functions/pointer.md).

[Index](../README.md) · [Previous: Generic methods](generic.md) · [Next: Methods on primitive aliases](alias.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
