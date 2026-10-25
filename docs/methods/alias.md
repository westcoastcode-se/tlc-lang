# Methods on primitive aliases

[Index](../README.md) · [Previous: Static functions](static.md) · [Next: Variables on a type](variable.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

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

The program returns 42. `Integer.from` is the static function. `Integer.ten` is the variable. `n.add` takes a pointer, and `n.twice` takes the value. `var f = Integer.from` is that function. The same forms work for `bool`, `f32`, `f64`, and the other integer widths. A slice alias such as `type string_view []const u8` is a different owner. Its value methods are library source in [`std/string`](../std/string.md). `self *this` on that alias is a pointer to the slice.

## Assembly

```asm
main:
# trace frame for the call, then the matching pop
callq from                       # call from
retq                             # return

from:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

[Index](../README.md) · [Previous: Static functions](static.md) · [Next: Variables on a type](variable.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
