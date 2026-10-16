# for

[Index](../README.md) · [Previous: Statements](block.md) · [Next: switch](switch.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`for` has three clauses, as in C. A declaration in the first clause is visible only inside the loop. That clause may also bind several results with `var`, `:=`, or `=`. A comma in the name list must be followed by another name.

```c
func main() i32 {
    var s i32 = 0;
    for (var i i32 = 1; i <= 4; i = i + 1) {
        s = s + i;
    }
    return s;
}
```

`continue` starts the next iteration. `break` leaves the loop.

```c
func main() i32 {
    var s i32 = 0;
    for (var i i32 = 0; i < 5; i = i + 1) {
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            break;
        }
        s = s + i;
    }
    return s;
}
```

## Cost

`for (var i i32 = 0; i < 4; i = i + 1)` with a sum in the body executes 17 instructions for the whole function, including one store of 0 into `s`, one store of 0 into `i`, and `retq`. The condition is the same compare sequence as `while`. The step `i = i + 1` is 3 instructions. `continue` and `break` are one jump each, after any `defer` on the blocks they leave.

[Index](../README.md) · [Previous: Statements](block.md) · [Next: switch](switch.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
