# Generic methods

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A method on a generic struct names that struct's parameters. `func Vec[T]::get(self *this, i i32) T` is the method on `type Vec[T type] struct`. `type Buf[N i32, T type] struct` uses `func Buf[N, T]::get`. The names match the struct, in the same order. Leaving the list out is reported as `type mismatch`. A different name is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. A struct that is not generic keeps `func Point::sum`. `func Point[T]::sum` is reported as `type mismatch`. `func Point::id[T type](self *this) T` is a method template: the brackets follow the method name. `func Vec[T]::get[U type]` is `not supported yet`. A static method template and `func Integer::id[T type]` are `not supported yet`. `export func Vec[T]::get` publishes that method. Another package calls `value.get(...)`. A method without `export` is reported as `get is not exported`. `Vec[i32].get()` is reported as `type mismatch`.

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

## Cost

A method on a generic struct is specialized with the struct. `v.get()` on `Vec[i32]` copies the `i32` field and makes a traced call. `main` that stores 40 into the field and returns `v.get()` executes 40 instructions when fewer than 32 frames are stored. The field is not zeroed first. The specialized method is 3 instructions: store the field, load it, `retq`. Calling that exported method from another package is the same call, 40 instructions in `main` and the same 3 in the specialized method. `return N` from a method on `Buf[4, i32]` is an immediate, the same as `return 4`.

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
