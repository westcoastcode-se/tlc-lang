# Statements

[Index](README.md) · [Previous: Expressions](expressions.md) · [Next: Functions](functions.md) · [Grammar](grammar.md)

Blocks, `if`/`else`, `while`, `do`/`while`, `for`, `switch`, `break`, `continue`, `return`, and empty statements are accepted. The body of `if`, `while`, and `for` is a block. `else` is a block or another `if`. A body without `{` is `expected '{'`. `do` and `switch` take a statement, so their body need not be a block.

`for` has C's three clauses. A declaration in the first clause is visible only inside the loop. That clause may also bind several results with `var`, `:=`, or `=`. A comma in the name list must be followed by another name.

```c
func main() int {
    var s int = 0;
    for (var i int = 1; i <= 4; i = i + 1) {
        s = s + i;
    }
    return s;
}
```

`switch` falls through until `break`. A `case` label is a constant `int`. `case 1u32` is `type mismatch`. Each value appears once, and `default` appears once.

```c
func main() int {
    var n int = 1;
    var x int = 0;
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

This returns 42. `break` leaves the innermost loop or `switch`. `continue` is valid inside loops.

`defer` runs one call when the enclosing block is left. The call can be a function, a method, an inner function, or an anonymous function. Calls in the same block run in reverse order. A `defer` whose statement did not run does not run. `return`, `break`, `continue`, and reaching the end of the block all run the calls of the blocks they leave. A `return` expression is evaluated before those calls. Arguments, and a method receiver, are evaluated at the `defer`.

```c
func main() int {
    defer func(value int) {
    }(10);
    return 0;
}
```

An anonymous function is an expression, `func (parameters) { body }`. The return type may be omitted, and then it is `void`. A return type is written after the parameters when it is present: `func () int { return 1; }`. Several results use the same parenthesized list as a named function: `func () (int, int) { return 1, 2; }`. The call is the argument list after the body.

A variable is `var name type`, `var name = expr`, or `name := expr`. `int value = 1` is `expected var`. When `var` has an initializer, the type may be omitted and is taken from that value: `var value = 1`, `var p = &value`. The type can still be written after the name, with a pointer or an array in front of its element type: `var value i32 = 1`, `var p *int`, `var buf [32]u8`, `var ps [2]*int`. One `var` names one variable. A variable with no initializer keeps the type: `var value i32;`. A `*T`, or a struct or array that contains one, needs an initializer. `var p *int;` is `type mismatch`. `var p ?*int;` may be `null`. `extern var p *int;` is not checked. `name := expr` defines a new name. In a list, it also assigns a name that already exists in the block, and at least one name must be new. A `:=` whose names are all already in the block is `type mismatch`. `var name := expr` is `type mismatch`. `*[4]i32` is a pointer to an array. `arr[1]` reads and writes the same element as `(*arr)[1]`. `&` of an array has that pointer type.

`const name = expr` at file scope is a compile-time integer. It has no address. The expression is folded the same way as `assert`, including `sizeof`. `const N = 4` can be an array length, `[N]int`.

At file scope, `var name = expr` and `name := expr` both infer the type from the initializer. `export`, `static`, `extern`, and `extern(abi="c")` go on a typed `var`: `export var n int = 1`, `static var n = 1`, `extern var slot int`, `extern(abi="c") var slot int`.

Several results are still bound together: `var a, b = call`. A pointer is a leading star in every position, including a parameter, a result, a type alias, a cast, and `sizeof`: `*int`, `*const char`, `type Ptr *Point`, `sizeof(*int)`, `*int(value)`. A `var`, a field, or a type alias writes an array in front of its element type: `type Pair [2]int`. A parameter array stays after the element type: `int[4]`.

A global with no initializer is zero.

`assert` checks a constant integer while compiling. It may stand at file scope or inside a function. The condition is an expression such as `sizeof(i32) == 4`, and the message is a string literal. A false condition is an error, and the diagnostic is that message. A true `assert` produces no code.

```c
assert(sizeof(i32) == 4, "i32 must be 4 bytes");
```

[Index](README.md) · [Previous: Expressions](expressions.md) · [Next: Functions](functions.md) · [Grammar](grammar.md)
