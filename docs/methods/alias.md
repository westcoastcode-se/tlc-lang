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

The program returns 42. `Integer.from` is the static function. `Integer.ten` is the variable. `n.add` takes a pointer, and `n.twice` takes the value. `var f = Integer.from` is that function. The same forms work for `bool`, `f32`, `f64`, and the other integer widths. A slice alias such as `type string_view []const u8` is a different owner. Its value methods are library source in [`std/string`](../std/string.md). `self *this` on that alias is a pointer to the slice.

A method template on the alias puts the type parameters after the name. `func Integer::id[T type](self this, value T) i32` is called as `Integer(2).id[i32](40)`. `func Integer::make[T type](value T) i32` is the static function, called as `Integer.make[i32](20)`. A call without the brackets is reported as `undeclared identifier`. A static template called on a value is reported as `type mismatch`.

```c
type Integer i32;

func Integer::id[T type](self this, value T) i32 {
    return i32(self) + value;
}

func main() i32 {
    return Integer(2).id[i32](40);
}
```

```asm
id.spec.i32:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $2, %edi                    # constant
movl $40, %esi                   # constant
callq id.spec.i32                # call id
retq                             # return
```

The program returns 42. `Integer(2)` adds no instruction. When the file is `main.tlc`, the symbol is `main.Integer.id.spec.i32`.

[Index](../README.md) · [Previous: Static functions](static.md) · [Next: Variables on a type](variable.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
