# Generic methods

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A method on a generic struct names that struct's parameters. `func Vec[T]::get(self *this, i i32) T` is the method on `type Vec[T type] struct`. `type Buf[N i32, T type] struct` uses `func Buf[N, T]::get`. The names match the struct, in the same order. Leaving the list out is reported as `type mismatch`. A different name is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. A struct that is not generic keeps `func Point::sum`. `func Point[T]::sum` is reported as `type mismatch`.

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

A method on a generic struct is specialized with the struct. `v.get()` on `Vec[i32]` copies the `i32` field and makes a traced call. `main` that zeroes the field, stores 40, and returns `v.get()` executes 41 instructions when fewer than 32 frames are stored. The specialized method is 3 instructions: store the field, load it, `retq`. `return N` from a method on `Buf[4, i32]` is an immediate, the same as `return 4`.

[Index](../README.md) · [Previous: Methods](method.md) · [Next: Static functions](static.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
