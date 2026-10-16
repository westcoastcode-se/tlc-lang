# defer

[Index](../README.md) · [Previous: switch](switch.md) · [Next: Bindings](binding.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`defer` runs one call when the enclosing block is left. The call can be a function, a method, an inner function, or an anonymous function. Calls in the same block run in reverse order. A `defer` whose statement did not run does not run. `return`, `break`, `continue`, and reaching the end of the block all run the calls of the blocks they leave. A `return` expression is evaluated before those calls. Arguments, and a method receiver, are evaluated at the `defer`.

```c
func note(n i32) void {
}

func main() i32 {
    defer note(1);
    defer func (value i32) {
    }(10);
    return 0;
}
```

`note(1)` runs after the anonymous call.

An anonymous function is an expression, `func (parameters) { body }`. The return type may be omitted, and then it is `void`. A return type is written after the parameters when it is present: `func () i32 { return 1; }`. Several results use the same parenthesized list as a named function: `func () (i32, i32) { return 1, 2; }`. The call is the argument list after the body.

## Cost

`defer note(1)` stores the argument and sets a flag. Leaving the block tests the flag and, when it is set, moves the argument and calls `note`. `defer note(1); return 0` executes 12 instructions on that path. The call does not push a trace frame. `note` with an empty body is 2 instructions, a store of its parameter and `retq`.

The flag is 1 byte. A `defer` whose statement did not run leaves the flag clear, and the test jumps over the call. Arguments are stored at the `defer`, which is 1 store per `i32` argument. Two `defer` statements in one block run the later one first. Each is its own test and call.

[Index](../README.md) · [Previous: switch](switch.md) · [Next: Bindings](binding.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
