# Generic methods

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A method on a generic struct names that struct's parameters. `func Vec[T]::get(self *this, i i32) T` is the method on `type Vec[T type] struct`. The result may be omitted: `func Vec[T]::get(self this) { return self.n; }` is `T`. A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value. That omitted result may be a function pointer. `type Buf[N i32, T type] struct` uses `func Buf[N, T]::get`. The names match the struct, in the same order. Leaving the list out is reported as `type mismatch`. A different name is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. A struct that is not generic keeps `func Point::sum`. `func Point[T]::sum` is reported as `type mismatch`. `func Point::id[T type](self *this) T` is a method template: the brackets follow the method name. `func Point::take(self *this, other (i32, i32))` is a tuple parameter. `[P1, P2]` is `expected a type` on the comma. With `[P1 type, P2 type]`, `func Point::take[P1 type, P2 type](self *this, other (P1, P2))` and `func Vec[T]::take[P1 type, P2 type](self *this, other (P1, P2))` are that tuple. The call is `p.take[i32, i32]((40, 2))` or `v.take[i32, i32]((40, 2))`. A call without those brackets is `undeclared identifier`. `func Vec[T]::pair(self *this, other (T, i32))` is that tuple of the struct's parameter. `func Vec[T]::get[U type](self *this, value U) U` is that template on a generic struct. `T` is the struct's parameter. `U` is chosen at the call `value.get[i32](value)`. A call without those brackets is `undeclared identifier`. A type parameter that repeats a struct parameter, `func Vec[T]::get[T type]`, is `redefinition`. `export`, a static method, a prototype with no body, and `...` on that template are `not supported yet`. A static method template and `func Integer::id[T type]` are `not supported yet`. `extern func Point::id[T type]` names that method template in a tlc library. The call stays `value.id[i32](value)`. Each specialization is its own symbol, such as `qualifier.Point.id.spec.i32`. A body defines that symbol. `extern func Point::measure[T Shape]` is that template when `T` is constrained. The call is `value.measure[Rect](r)`. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that template are `extern(abi="c") on a trait or a generic`. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`. A prototype with no body, `...`, `export`, and a parameter whose type is a trait, such as `s Shape` beside `[T type]`, are `not supported yet`. `export func Vec[T]::get` publishes that method. Another package calls `value.get(...)`. `extern func Vec[T]::get` names that method in a tlc library. The call stays `value.get(...)`, or `Vec[i32].from(...)` when there is no receiver. Each specialization is its own symbol, the same spelling an exported method on that specialization uses. A body defines that symbol. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that method are `extern(abi="c") on a trait or a generic`. A C symbol is one concrete library name. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`. A method without `export` is reported as `get is not exported`. `Vec[i32].get()` is reported as `type mismatch`.

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

## Assembly

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

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
