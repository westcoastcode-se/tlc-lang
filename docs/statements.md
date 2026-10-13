# Statements

[Index](README.md) · [Previous: Expressions](expressions.md) · [Next: Functions](functions.md) · [Grammar](grammar.md)

A block is a sequence of statements in braces. `if`, `while`, and `for` take a block. `else` is a block or another `if`. A body without `{` is reported as `expected '{'`. `do` and `switch` take a statement, so their body need not be a block. An empty statement is `;`.

```c
func main() int {
    var n int = 0;
    if (n) {
        n = 1;
    } else if (1) {
        n = 40;
    } else {
        n = 2;
    }
    while (n < 42) {
        n = n + 1;
    }
    do n = n + 1; while (0);
    ;
    return n;
}
```

## `for`

`for` has three clauses, as in C. A declaration in the first clause is visible only inside the loop. That clause may also bind several results with `var`, `:=`, or `=`. A comma in the name list must be followed by another name.

```c
func main() int {
    var s int = 0;
    for (var i int = 1; i <= 4; i = i + 1) {
        s = s + i;
    }
    return s;
}
```

`continue` starts the next iteration. `break` leaves the loop.

```c
func main() int {
    var s int = 0;
    for (var i int = 0; i < 5; i = i + 1) {
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

## `switch`

`switch` falls through until `break`. A `case` label is a constant `int`. `case 1u32` is reported as `type mismatch`. Each value appears once, and `default` appears once.

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

The program returns 42. `break` leaves the innermost loop or `switch`. `continue` is valid inside loops.

## `defer`

`defer` runs one call when the enclosing block is left. The call can be a function, a method, an inner function, or an anonymous function. Calls in the same block run in reverse order. A `defer` whose statement did not run does not run. `return`, `break`, `continue`, and reaching the end of the block all run the calls of the blocks they leave. A `return` expression is evaluated before those calls. Arguments, and a method receiver, are evaluated at the `defer`.

```c
func note(n int) void {
}

func main() int {
    defer note(1);
    defer func (value int) {
    }(10);
    return 0;
}
```

`note(1)` runs after the anonymous call.

An anonymous function is an expression, `func (parameters) { body }`. The return type may be omitted, and then it is `void`. A return type is written after the parameters when it is present: `func () int { return 1; }`. Several results use the same parenthesized list as a named function: `func () (int, int) { return 1, 2; }`. The call is the argument list after the body.

## Bindings

A variable is `var name type`, `var name = expr`, or `name := expr`. `int value = 1` is reported as `expected var`. When `var` has an initializer, the type may be omitted and is taken from that value: `var value = 1`, `var p = &value`. The type can still be written after the name, with a pointer or an array in front of its element type: `var value i32 = 1`, `var p *int`, `var buf [32]u8`, `var ps [2]*int`. One `var` names one variable. A variable with no initializer keeps the type: `var value i32;`.

A `*T`, or a struct or array that contains one, needs an initializer. `var p *int;` is reported as `type mismatch`. `var p ?*int;` may be `null`. `extern var p *int;` is not checked. `name := expr` defines a new name. In a list, it also assigns a name that already exists in the block, and at least one name must be new. A `:=` whose names are all already in the block is reported as `type mismatch`. `var name := expr` is reported as `type mismatch`.

`*[4]i32` is a pointer to an array. `arr[1]` reads and writes the same element as `(*arr)[1]`. `&` of an array has that pointer type.

```c
func main() int {
    var value = 1;
    var p *int = &value;
    var buf [3]int = {10, 20, 12};
    extra := 2;
    var typed i32 = 3;
    return *p + buf[0] + extra + typed;
}
```

A local array takes a brace list with one value per element: `var row [3]int = {10, 20, 12}`. A different count is reported as `type mismatch`. `row[0]` reads an element.

### `const`

`const name = expr` at file scope is a compile-time integer. It has no address. The expression is folded the same way as `assert`, including `sizeof`. `const N = 4` can be an array length, `[N]int`.

Inside a function, `const name = expr` is an immutable binding. The type is the type of the expression. `name = ...` is reported as `type mismatch`. `&name` has type `*const T`, so a store through that pointer is reported as `type mismatch`. The binding is not a compile-time integer, and it cannot be an array length.

```c
const N = 4;

func main() int {
    var buf [N]int;
    const value = 40 + 2;
    var p *const int = &value;
    buf[0] = *p;
    return buf[0];
}
```

### File scope

At file scope, `var name = expr` and `name := expr` both infer the type from the initializer. `export`, `static`, `extern`, and `extern(abi="c")` go on a typed `var`: `export var n int = 1`, `static var n = 1`, `extern var slot int`, `extern(abi="c") var slot int`. A global with no initializer is zero.

```c
var count = 1;
n := 2;
export var shown int = 4;
static var hidden int = 3;

func main() int {
    return count + n + shown + hidden;
}
```

```c
extern var slot int;
extern(abi="c") var errno int;
```

### Tuples

Several names unpack one tuple. `var value1, value2 = mytuple` defines both names. `a, b := mytuple` does the same when at least one name is new. `value1, value2 = mytuple` assigns names that already exist. A call that returns several results is that tuple, so `var a, b = call` unpacks it the same way. The number of names matches the tuple. A different count is reported as `type mismatch`. The same bind may be the first clause of `for`.

```c
func step(n int) (int, int) {
    return n + 1, n;
}

func main() int {
    var value = (40, 2);
    var value1, value2 = value;
    a, b := value;
    value1, value2 = (1, 2);
    var acc int = 0;
    for (k, n := step(2); k < 4; k = k + 1) {
        acc = acc + n;
    }
    return value1 + a + acc;
}
```

### Pointer and array order

A pointer is a leading star in every position, including a parameter, a result, a type alias, a cast, and `sizeof`: `*int`, `*const char`, `type Ptr *Point`, `sizeof(*int)`, `*int(value)`. A `var`, a field, or a type alias writes an array in front of its element type: `type Pair [2]int`. A parameter array stays after the element type: `int[4]`.

```c
type Pair [2]int;

func head(a int[4]) int {
    return a[0];
}

func main() int {
    var data [4]int;
    data[0] = 22;
    data[1] = 20;
    var arr *[4]int = &data;
    var pair Pair;
    pair[0] = arr[0];
    return head(data) + (*arr)[1] + pair[0];
}
```

## `assert`

`assert` checks a constant integer while compiling. It may stand at file scope or inside a function. The condition is an expression such as `sizeof(i32) == 4`, and the message is a string literal. A false condition is an error, and the diagnostic is that message. A true `assert` produces no code.

```c
assert(sizeof(i32) == 4, "i32 must be 4 bytes");
```

[Index](README.md) · [Previous: Expressions](expressions.md) · [Next: Functions](functions.md) · [Grammar](grammar.md)
