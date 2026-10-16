# switch

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`switch` falls through until `break`. A `case` label is a constant `i32`. `case 1u32` is reported as `type mismatch`. Each value appears once, and `default` appears once.

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

## Cost

`switch (n)` with `case 1` and `default` subtracts 1 from `n` and branches: that dispatch is 2 instructions after `n` is loaded. Each `case` body that stores an `i32` is 1 `movl` and 1 jump to the exit. A function that stores 0 into `x`, switches, and returns `x` is 12 instructions including the parameter store and `retq`. There is no bounds table. `break` is the jump to the exit.

[Index](../README.md) · [Previous: for](for.md) · [Next: defer](defer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
