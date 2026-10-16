# Several results

[Index](../README.md) · [Previous: Functions](function.md) · [Next: Nested functions](nested.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

The result list is parenthesized, and each result is a type. One type in parentheses is the same as writing that type alone. `return` lists one expression per result.

```c
func add(x i32, y i32) (i32, i32) {
    return x + y, x;
}
```

The call is a tuple of its results. One name binds that tuple. Several names unpack it, and the same form unpacks a tuple that is already stored.

```c
func add(x i32, y i32) (i32, i32) {
    return x + y, x;
}

func main() i32 {
    sum, orig := add(20, 22);
    var extra, base = add(1, 1);
    extra, base = add(0, 0);
    var pair = add(20, 22);
    var value1, value2 = pair;
    return sum + value1;
}
```

`var` defines every name in the current block. `:=` defines a name that is new in the current block and assigns a name that already exists there. At least one name must be new. A name from an outer block is shadowed. The same bind may be the first clause of `for`. A call used as a statement, or as the call in `defer`, discards every result. The call may be a function, a method on a field, a subscript, a dereference, or a temporary value. An `extern(abi="c")` function cannot return several results. A type error names the expected type and the found type.

## Cost

`return x + y, x` for two `i32` parameters is 7 instructions. The sum is in `%eax` and the second result is in `%edx`.

The caller of `a, b = pair(20, 22)` pays the traced call and then 2 stores from those registers into `a` and `b`. With both locals declared as `var a i32 = 0` and `var b i32 = 0`, `main` executes 45 instructions on the path where the trace holds fewer than 32 frames. A call used as a statement still calls, and it does not store the results.

[Index](../README.md) · [Previous: Functions](function.md) · [Next: Nested functions](nested.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
