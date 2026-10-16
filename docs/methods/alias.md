# Methods on primitive aliases

[Index](../README.md) · [Previous: Static functions](static.md) · [Next: Variables on a type](variable.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

## Cost

`Integer` uses the instructions of `i32`. `return self + self` is the `i32` add, 2 instructions after the parameter is stored, plus `retq`. `*self = *self + n` is a load, an add, and a store through the pointer. `Integer(n)` adds no instructions. `Integer.ten` is a global, 1 `movl` to read it. `Integer.from` is a traced call with no receiver.

[Index](../README.md) · [Previous: Static functions](static.md) · [Next: Variables on a type](variable.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
