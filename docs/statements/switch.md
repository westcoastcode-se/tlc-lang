# switch

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`switch` falls through until `break`. The value promotes to `i32`, so `i8` and `i16` match. `bool`, `u32`, `i64`, and `f32` are reported as `type mismatch: expected i32`. A `case` label is a constant `i32`. `case 1u32` is reported as `type mismatch`. Each value appears once, and `default` appears once.

A TLC enum switches as that enum. `case Color.Blue` matches a `Color`. `case 1` is `type mismatch`. The enum does not convert to `i32`. A `case` that is not a constant is `not supported yet`. Two labels with the same representation are `redefinition`.

An `extern(abi="c")` enum switches as `i32`. `case Flag.On` and `case 1` are that integer. A TLC enumerator in that `switch` is `type mismatch`. An enum is still not a condition. `if (value)` is `type mismatch: expected bool, found Color`.

```c
func main() i32 {
    var n i32 = 1;
    var x i32 = 0;
    switch (n) {
    case 1:
        x = 2;
    case 2:
        x = x + 40;
        break;
    default:
        x = 7;
        break;
    }
    return x;
}
```

The program returns 42. `break` leaves the innermost loop or `switch`. `continue` is valid inside loops.

```c
type Color enum {
    Red,
    Green = 40,
    Blue,
};

func code(value Color) i32 {
    switch (value) {
    case Color.Green:
        return 2;
    case Color.Blue:
        return 4;
    default:
        return 0;
    }
}
```

`code(Color.Blue)` returns 4. `case 41` on that `switch` is `type mismatch`.

## Cost

`switch (n)` with `case 1` and `default` subtracts 1 from `n` and branches: that dispatch is 2 instructions after `n` is loaded. Each `case` body that stores an `i32` is 1 `movl` and 1 jump to the exit. A function that stores 0 into `x`, switches, and returns `x` is 12 instructions including the parameter store and `retq`. There is no bounds table. `break` is the jump to the exit.

`code` is 16 instructions. The parameter is stored and reloaded, then `Color.Green` subtracts 40 and branches. `Color.Blue` reloads that value, subtracts 41, and branches. Each return is one `movl` and `retq`. The same function on an `i32` is 16 instructions. An `extern(abi="c")` enum uses that `i32` dispatch.

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
