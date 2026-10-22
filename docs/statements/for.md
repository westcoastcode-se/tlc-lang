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

`for (var item : items)` binds each element. `for (const item : items)` is the immutable binding: `item = ...` is `type mismatch`, and `&item` is `*const E`. The name is new in the loop and hides an outer name. Assigning `var item` does not write the collection. `break`, `continue`, and `defer` are the ones the three-clause `for` uses. `continue` runs the next index. The collection is an array, a slice, or a struct with `len` and `at`. A file does not import a package to write the loop. `for (var item : ptr_of(items))` binds `*E`. A const element binds `*const E`, and a store through it is `type mismatch`. `item = &other` reseats the pointer. `const` rejects that assignment. `ptr_of` on a temporary is `not an lvalue`. `for (item : items)` without `var` or `const` is `expected ';'`.

```c
func main() i32 {
    var items [4]i32;
    items[0] = 1;
    items[1] = 2;
    items[2] = 3;
    items[3] = 4;
    var sum = 0;
    for (var item : items) {
        sum = sum + item;
    }
    return sum;
}
```

The loop stores 1, 2, 3, and 4, then adds them. It returns 10.

`import "std/seq"` names the iteration trait. A value `seq.Seq[T]` checks `len` and `at`. A pointer `*seq.Seq[T]` checks `ref` as well. `[N]T` and `[]T` satisfy `seq.Seq[E]`, where `E` is the element an index reads. `len` is `.len`, `at` is the index, and `ref` is the address of that element. A slice alias satisfies the trait only when it has the methods. A pointer to the array or the slice does not satisfy it. The three-clause `for` does not call those methods. The trait is on [seq](../std/seq.md).

## Cost

`for (var i i32 = 0; i < 4; i = i + 1)` with a sum in the body executes 17 instructions for the whole function, including one store of 0 into `s`, one store of 0 into `i`, and `retq`. The condition is the same compare sequence as `while`. The step `i = i + 1` is 3 instructions. `continue` and `break` are one jump each, after any `defer` on the blocks they leave. `for (var item : items)` on `[4]i32`, storing 1, 2, 3, and 4 and returning their sum, executes 22 instructions in `main`: the four stores, a zero `sum`, a zero index, `cmpl` and `jge`, the element load, the add, the index step, and `retq`. That call pushes no trace frame.

[Index](../README.md) · [Previous: Statements](block.md) · [Next: switch](switch.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
