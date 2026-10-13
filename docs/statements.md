# Statements

[Index](README.md) · [Previous: Expressions](expressions.md) · [Next: Functions](functions.md) · [Grammar](grammar.md)

A block is a sequence of statements in braces. `if`, `while`, and `for` take a block. `else` is a block or another `if`. A body without `{` is reported as `expected '{'`. `do` and `switch` take a statement, so their body need not be a block. An empty statement is `;`.

```c
func main() i32 {
    var n i32 = 0;
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

## `switch`

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

## `defer`

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

## Bindings

A variable is `var name type`, `var name = expr`, or `name := expr`. `i32 value = 1` is reported as `expected var`. When `var` has an initializer, the type may be omitted and is taken from that value: `var value = 1`, `var p = &value`. The type can still be written after the name, with a pointer or an array in front of its element type: `var value i32 = 1`, `var p *i32`, `var buf [32]u8`, `var ps [2]*i32`. One `var` names one variable. A variable with no initializer keeps the type: `var value i32;`.

A `*T`, or a struct or array that contains one, needs an initializer. `var p *i32;` is reported as `type mismatch`. `var p ?*i32;` may be `null`. `extern var p *i32;` is not checked. `name := expr` defines a new name. In a list, it also assigns a name that already exists in the block, and at least one name must be new. A `:=` whose names are all already in the block is reported as `type mismatch`. `var name := expr` is reported as `type mismatch`.

`*[4]i32` is a pointer to an array. `arr[1]` reads and writes the same element as `(*arr)[1]`. `&` of an array has that pointer type.

```c
func main() i32 {
    var value = 1;
    var p *i32 = &value;
    var buf [3]i32 = {10, 20, 12};
    extra := 2;
    var typed i32 = 3;
    return *p + buf[0] + extra + typed;
}
```

A local array takes a brace list with one value per element: `var row [3]i32 = {10, 20, 12}`. A different count is reported as `type mismatch`. `row[0]` reads an element.

### `const`

`const name = expr` at file scope takes the type of the expression. `const name Type = expr` writes the type, and the expression has to be that type. A plain integer, such as `const N = 4`, is a compile-time integer. It has no address. The expression is folded the same way as `assert`, including `sizeof`. `const N = 4` can be an array length, `[N]i32`.

A struct, a function pointer, or a typedef can be that constant too. `const origin Point = {.x = 10, .y = 10}` and `const other = Integer(4)` are those types. `const op BinOp = BinOp(add)` and `const plain func (i32, i32) i32 = add` name a function. The initializer has to be a constant. A call is reported as `global initializer is not a constant`. Assigning the constant, or a field of a struct constant, is reported as `type mismatch`. It has no address. An alias is still a different type, so `var raw i32 = value` is reported as `type mismatch` when `value` is `Integer`. `i32(value)` confirms it. An alias is not an array length.

Inside a function, `const name = expr` is an immutable binding. The type is the type of the expression. `name = ...` is reported as `type mismatch`. `&name` has type `*const T`, so a store through that pointer is reported as `type mismatch`. The binding is not a compile-time integer, and it cannot be an array length.

```c
const N = 4;

func main() i32 {
    var buf [N]i32;
    const value = 40 + 2;
    var p *const i32 = &value;
    buf[0] = *p;
    return buf[0];
}
```

```c
type Integer i32;

type Point struct {
    x i32;
    y i32;
};

func add(a i32, b i32) i32 {
    return a + b;
}

type BinOp func (i32, i32) i32;

const value Integer = Integer(20);
const other = Integer(2);
const origin Point = {.x = 10, .y = 8};
const op BinOp = BinOp(add);

func main() i32 {
    return i32(value) + i32(other) + origin.x + origin.y + op(1, 1);
}
```

The program returns 42.

### File scope

At file scope, `var name = expr` and `name := expr` both infer the type from the initializer. `export`, `static`, `extern`, and `extern(abi="c")` go on a typed `var`: `export var n i32 = 1`, `static var n = 1`, `extern var slot i32`, `extern(abi="c") var slot i32`. A global with no initializer is zero.

```c
var count = 1;
n := 2;
export var shown i32 = 4;
static var hidden i32 = 3;

func main() i32 {
    return count + n + shown + hidden;
}
```

```c
extern var slot i32;
extern(abi="c") var errno i32;
```

### Tuples

Several names unpack one tuple. `var value1, value2 = mytuple` defines both names. `a, b := mytuple` does the same when at least one name is new. `value1, value2 = mytuple` assigns names that already exist. A call that returns several results is that tuple, so `var a, b = call` unpacks it the same way. The number of names matches the tuple. A different count is reported as `type mismatch`. The same bind may be the first clause of `for`.

```c
func step(n i32) (i32, i32) {
    return n + 1, n;
}

func main() i32 {
    var value = (40, 2);
    var value1, value2 = value;
    a, b := value;
    value1, value2 = (1, 2);
    var acc i32 = 0;
    for (k, n := step(2); k < 4; k = k + 1) {
        acc = acc + n;
    }
    return value1 + a + acc;
}
```

### Pointer and array order

A pointer is a leading star in every position, including a parameter, a result, a type alias, a cast, and `sizeof`: `*i32`, `*const u8`, `type Ptr *Point`, `sizeof(*i32)`, `*i32(value)`. An array with a known length is written in front of its element type in every position: `type Pair [2]i32`, `func head(a [4]i32)`. `i32[4]` is reported as `type mismatch`. An unsized parameter array stays after the element type: `i32[]`.

```c
type Pair [2]i32;

func head(a [4]i32) i32 {
    return a[0];
}

func main() i32 {
    var data [4]i32;
    data[0] = 22;
    data[1] = 20;
    var arr *[4]i32 = &data;
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
