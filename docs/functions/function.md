# Functions

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A function starts with `func`, then the name, then the parameters, then the result type. A parameter is a name followed by its type. An empty parameter list means there are no parameters. There is one `main`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func main() i32 {
    return add(20, 22);
}
```

A function may be declared first and defined later, with the same type. Two bodies for the same function are reported as `redefinition`.

```c
func add(a i32, b i32) i32;

func add(a i32, b i32) i32 {
    return a + b;
}
```

## Cost

A function stores each parameter on entry. An `i32` parameter is 1 `movl`. A pointer is 1 `movq`. A float is 1 `movss` or `movsd`. `return a + b` for two `i32` parameters is 5 instructions: the two stores, the load, the add, and `retq`.

A call of that function is separate. `return add(20, 22)` executes 38 instructions in the caller when fewer than 32 frames are stored: the 36 of a no-argument traced call, plus 2 `movl` for the arguments. `add` then runs its own 5. A prototype with no body adds no instructions until a call.

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
