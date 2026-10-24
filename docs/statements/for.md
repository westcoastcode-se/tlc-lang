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

`for (var item : items)` binds each element. `for (const item : items)` is the immutable binding: `item = ...` is `type mismatch`, and `&item` is `*const E`. The name is new in the loop and hides an outer name. Assigning `var item` does not write the collection. `break`, `continue`, and `defer` are the ones the three-clause `for` uses. `continue` runs the next index. The collection is an array, a slice, or a struct with `len` and `at`. A file does not import a package to write the loop. `for (var item : ptr_of(items))` binds `*E`. A const element binds `*const E`, and a store through it is `type mismatch`. `item = &other` reseats the pointer. `const` rejects that assignment. `ptr_of` on a temporary is `not an lvalue`. `for (item : items)` without `var` or `const` is `expected ';'`. Completion, hover, and definition see the name inside the loop.

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

In a `macro`, `for` is the same three clauses, unrolled while compiling. `low`, `high`, and `step` in `for (var i = low; i < high; i = i + step)` are compile-time integers, `step` is positive, and the body runs at most 32 times. Each copy of the body sees `i` as that integer, so `emit` can use it. `for (var item : items)` in a `macro` unrolls when the length is known, at most 32 elements. `item` in each copy is `items[0]`, `items[1]`, and so on. A slice `var`, `ptr_of`, a length past 32, a bound that does not fold, and a step that is not positive are `not supported yet`. Outside a `macro`, `for` repeats at run time. `emit while` and `emit do` paste those loops the same way. A plain `while` or `do` in a `macro` is `not supported yet`. `emit for` in a `macro` pastes that loop into the caller. The bound may be a `var`. The body is ordinary code, so a nested `for` also runs later. `emit for` outside a `macro` is `not supported yet`.

`import "std/seq"` names the iteration trait. `seq.Seq[T]` and `*seq.Seq[T]` check `len` and `at`. `[N]T` and `[]T` satisfy `seq.Seq[E]`, where `E` is the element an index reads. `len` is `.len` and `at` is the index. `for (var item : ptr_of(items))` binds the address of that element. A slice alias satisfies the trait only when it has the methods. A pointer to the array or the slice does not satisfy it. The three-clause `for` does not call those methods. The trait is on [seq](../std/seq.md).

## Cost

`for (var i i32 = 0; i < 4; i = i + 1)` with a sum in the body executes 17 instructions for the whole function, including one store of 0 into `s`, one store of 0 into `i`, and `retq`. The condition is the same compare sequence as `while`. The step `i = i + 1` is 3 instructions. `continue` and `break` are one jump each, after any `defer` on the blocks they leave. `for (var item : items)` on `[4]i32`, storing 1, 2, 3, and 4 and returning their sum, executes 22 instructions in `main`: the four stores, a zero `sum`, a zero index, `cmpl` and `jge`, the element load, the add, the index step, and `retq`. That call pushes no trace frame. A `for` in a `macro` does not emit a loop. A `macro` that adds `1`, then `2`, then `3` into a local `i32` that started at 0 executes 12 instructions in `main`: the zero store, three load-add-store sequences, the return load, and `retq`. There is no compare and no trace frame. `for (var b : "ab")` that adds each byte to a local that started at 0 executes 9 instructions in `main`: the zero store, two load-add-store sequences of `0x61` and `0x62`, the return load, and `retq`. There is no compare. `emit for (var i = 0; i < count; i = i + 1)` that adds 1 to a local, with `count` a `var` holding 3, executes 38 instructions in `main`: the two stores, a zero index, and four compares. Three of the compares fall into the add and the index step. The fourth leaves the loop, then `main` loads the total and executes `retq`. `emit while (*slot < limit)` that adds 1 until a `var` holding 3 executes 28 instructions in `main`: the two stores, four compares, three adds, and `retq`.

[Index](../README.md) · [Previous: Statements](block.md) · [Next: switch](switch.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
