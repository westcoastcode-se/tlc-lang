# Methods

[Index](../README.md) · [Previous: Function pointers](../functions/pointer.md) · [Next: Generic methods](generic.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

## Cost

A value method on a struct of two `i32` fields receives those fields in registers. `return self.x + self.y` is 5 instructions, the same as a free function of two `i32` parameters. The caller copies the fields with 2 `movl` and then pays the traced call, 36 instructions plus those copies when fewer than 32 frames are stored.

A pointer method receives the address. `self.x = self.x + n` is 6 instructions: store the pointer, store `n`, reload the pointer twice, add, and store. The caller passes the address with 1 `leaq` plus the traced call.

`self ?*this` uses the same pointer. The null test is the pointer compare on [Pointers and null](../expressions/null.md). A const check is rejected while compiling and adds no instruction.

[Index](../README.md) · [Previous: Function pointers](../functions/pointer.md) · [Next: Generic methods](generic.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
