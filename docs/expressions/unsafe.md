# Unsafe

[Index](../README.md) · [Previous: Casts](cast.md) · [Next: Tuples and lists](tuple.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`unsafe` is a prefix on an expression. The expression keeps its type and its value. The prefix adds no check.

```c
type Point struct {
    x i32;
    y i32;
};

unsafe func point(raw *u8) *Point {
    return unsafe *Point(raw);
}

func main() i32 {
    var n i32 = 42;
    var p *Point = unsafe point(unsafe *u8(&n));
    return p.x;
}
```

## Casts

A cast from one pointer to another pointer needs `unsafe` in three cases. `?`, `const`, and a type alias do not change the case. `type Integer i32` makes `*Integer` the same pointer as `*i32`.

A raw pointer is `*void` or `*u8`. Those two cast to each other with no `unsafe`. A cast to `*void` needs none: `*void(p)` drops the claim the pointer had. A cast to `*u8` from a struct or a union needs none, as in `*u8(&slot)`. A cast to `*u8` from a different primitive needs `unsafe`. `*u8(bits)` from an integer is that integer cast, not a pointer cast.

A raw pointer cast to a struct or a union needs `unsafe`: `unsafe *Point(raw)`. `*Point(&local)` does not, and neither does `*Point(n)` from an `i32`. `*i32` cast to `*Point`, and one struct cast to another struct, stay ordinary casts.

A pointer to a primitive cast to a pointer to a different primitive needs `unsafe`. The primitives are `bool`, the integers, and the floats. `unsafe *f32(wide)` when `wide` is `*f64`, `unsafe *u32(p)` when `p` is `*i32`, and `unsafe *i32(raw)` when `raw` is `*void` are that cast. `*f32(x)` from an `f64` value converts the value and needs no `unsafe`. `*f64(&x)` when `x` is `f64` is the same primitive. A second star is not this rule: `**f64` cast to `**f32` is an ordinary cast.

The missing prefix is reported as `unsafe cast`. The cast is still that pointer, so one diagnostic does not hide the type. `unsafe unsafe *f32(p)` is the same prefix twice.

## Functions

`unsafe func` makes the body an unsafe context. A call from outside that context is written `unsafe point(raw)` and is reported as `unsafe call` without the prefix. `export unsafe func` publishes the function. `unsafe` may stand with `export` and `extern`, in any order, in front of `func`.

```c
export unsafe func point(raw *u8) *Point {
    return unsafe *Point(raw);
}
```

The flag is part of the function. `unsafe func point(raw *u8) *Point;` and `func point(raw *u8) *Point { ... }` are reported as `redefinition`. A nested function and an anonymous function do not inherit the context. `var f = point` and `var f = unsafe point` are reported as `type mismatch`. The function is only a call.

`defer unsafe point(raw)` calls it. `defer point(raw)` is `unsafe call` outside an unsafe function. `unsafe` on a declaration that is not a function, such as `unsafe var`, is reported as `not supported yet`.

`unsafe func draw() void;` on a trait makes that method unsafe. The call is `unsafe s.draw()` and is reported as `unsafe call` without the prefix. Inside an `unsafe func`, the prefix is not required. The struct method repeats the flag: `unsafe func Rect::draw`. A method that does not, or an `unsafe` method where the trait method is not, is reported as `type mismatch`. An anonymous function passed to a one-method trait is not itself an `unsafe func`. A named function with that signature is the same argument, and it is not an `unsafe func` either. Using an `unsafe func` as that argument is reported as `type mismatch`. The call through the trait still writes `unsafe`.

`malloc` and `realloc` on `Allocator` are unsafe. The call is `unsafe arena.malloc(...)` or `unsafe arena.realloc(...)`, and it is `unsafe call` without the prefix. `free` is an ordinary call. `new` and `delete` are not unsafe calls. `malloc` returns `?*u8`, and the caller handles `null`. The cast of that pointer follows the rules above.

## Assembly

```asm
main:
# trace frame for the call, then the matching pop
callq point                      # call point
retq                             # return

point:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
retq                             # return
```

[Index](../README.md) · [Previous: Casts](cast.md) · [Next: Tuples and lists](tuple.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
