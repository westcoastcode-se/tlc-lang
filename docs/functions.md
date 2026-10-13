# Functions

[Index](README.md) · [Previous: Statements](statements.md) · [Next: Builtin](builtin.md) · [Grammar](grammar.md)

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

## Several results

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

## Nested functions

A function may be written inside a block. Its name is visible from that declaration through the end of the block, including blocks nested inside it. It has a body in that block. The function can call functions already in scope, and it can use globals. An anonymous function, used from `defer` or called directly, is that same kind of function.

```c
func main() i32 {
    func add(a i32, b i32) i32 {
        return a + b;
    }
    return add(20, 22);
}
```

## Generics

A generic function names its parameters in brackets. `T type` is a type. `N i32` is a compile-time integer. `T Shape` is a type that must have the methods of the trait `Shape`. One body is specialized for each call. There is no `impl` and no runtime table.

```c
func id[T type](value T) T {
    return value;
}

func repeat[N i32, T type](value T) [N]T {
    assert(N > 0, "empty");
    var out [N]T;
    out[0] = value;
    return out;
}

func main() i32 {
    return id(40) + id[i32](0) + repeat[4](2)[0];
}
```

`T Shape` requires a type that has the methods of the trait `Shape`.

```c
type Shape trait {
    func area() i32;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func measure[T Shape](value T) i32 {
    return value.area();
}

func main() i32 {
    var r Rect;
    r.w = 6;
    r.h = 7;
    return measure(r);
}
```

`id(1)` infers `T`. `repeat[4](7)` writes `N` and infers `T`. `id[i32](1)` writes the type. A type that cannot be inferred is reported as `type mismatch`. A type parameter bound to two different types in one call, such as `Vec[i32]` and `Vec[u8]` for `Vec[T]`, is reported as `type mismatch`. `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` see the concrete type and the integer.

### Type packs

`[...U]` is a type pack. It is the last parameter in the brackets, so `func head[T type, ...U](value T, rest U) T` is accepted. A pack before another parameter, as in `[...U, T type]`, is reported as `not supported yet`. One argument type makes `U` that type, so `*U` is a pointer to it. Several argument types make `U` a tuple. `(40)` is one value, the same call as `keep(40)`. `(40, 2)` is a tuple. The element types are the types of the values before promotion, so `'a'` stays `u8`.

```c
func keep[...U](args U) U {
    return args;
}

func first[...U](args U) typeof(args[0]) {
    return args[0];
}

func main() i32 {
    var n = keep(40);
    var written = keep[i32](1);
    a, b := keep((40, 2));
    x, y := keep((40, 'a'));
    u, v := keep[i32, u8]((1, 'b'));
    return n + written + a + first((7, 8)) + u + v;
}
```

`keep[i32](40)` and `keep[i32, u8]((40, 'a'))` write the types. A value in that list, such as `keep[4]`, is reported as `type mismatch`. `head(40, (1, 2))` infers `T` and `U`. `head[i32, i32](40, 2)` writes them. The type arguments before `...U` bind the parameters in front of the pack, and the rest fill `U`. `first(40)` is reported as `type mismatch`: `args` is `i32`, and `args[0]` reads a tuple. The index is a constant. `return args` returns the tuple, and so does `return keep((1, 2))` from a function with those two results. `a, b :=` unpacks them, and so does `var value1, value2 = value` when `value` holds that tuple. `var value = (40, 2)` binds the tuple itself. `[...U]` is not the `...` that `builtin.va()` reads.

## `typeof` as a result

A result may be `typeof(expression)`. The expression is checked after the parameters exist, so `func id(x i32) typeof(x)` has result `i32`. That spelling is the result type. An expression `typeof(x)` still has type `Type`.

```c
func id(x i32) typeof(x) {
    return x;
}

func main() i32 {
    return id(42);
}
```

## Structs and arrays

Structs are passed and returned by value. An array parameter becomes a pointer, both as `a i32[]` and as `a [4]i32`. A function may return an array. The return expression is a local array name, as in `return out` for `[N]T`. The call decays to a pointer, so `repeat[4](2)[0]` reads the first element.

```c
type Point struct {
    x i32;
    y i32;
};

func sum(p Point) i32 {
    return p.x + p.y;
}

func pair() [2]i32 {
    var out [2]i32;
    out[0] = 40;
    out[1] = 2;
    return out;
}

func head(a [2]i32) i32 {
    return a[0];
}

func main() i32 {
    var p Point;
    p.x = 20;
    p.y = 22;
    return sum(p) + head(pair());
}
```

## Tests

`test "name" { ... }` is a file-scope test. The body is a function that returns `i32`. Reaching the end passes, and so does `return 0`. Any other `return` fails that test. `return;` is reported as `type mismatch`. The same test name twice in one file is reported as `redefinition`.

`import "std/testing"` provides `testing.error`. It returns `1`. The first argument is a string. Further arguments are integers, floating-point values, pointers, or enums, and the string is a `printf` format. A floating-point argument is passed as `f64`.

`testing.error` prints `builtin.trace()`, then the message. `builtin.trace` is the tlc calls that have not returned. Each line is `file:line:column: name`, oldest first, and the last line has no newline. `builtin.va` reads the extra arguments of the variadic function it is called from.

```c
import "std/testing";

func add(a i32, b i32) i32 {
    return a + b;
}

test "add joins its arguments" {
    var value = add(2, 2);
    if (value != 4) {
        return testing.error("Expected 4 but was %d", value);
    }
}
```

`main.tlc:10:16: add joins its arguments: Expected 4 but was 5` is that report when `value` is 5. `import "builtin"` is supplied by the compiler. `std/testing` is `lib/tlc/std/testing`. A directory with the same path next to the entry file is that package.

`tlc --test file.tlc` compiles the program, runs every test, and skips `main`. A failing test prints `fail:` and the name. The process status is the number of failed tests. A build without `--test` typechecks the tests and does not run them.

## Function pointers

A function pointer type is `func (argument types) result`. The arguments are types, with no names. The result is written the same way as a function result: one type, or a parenthesized list of several types. A function name used as a value is a pointer to that function. The type can be inferred: `var ptr = add` is that pointer, and `ptr(1, 2)` calls it. A static function is the same kind of value, `var ptr = Point.make` and `take(Point.make)`. An instance method used as a value is reported as `type mismatch`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func myfunc(ptr func (i32, i32) i32) i32 {
    return ptr(20, 22);
}
```

A variable, a field, or a type alias names the value, then the type: `var fp func (i32, i32) i32 = add`, `var fp func (i32) (i32, i32) = pair`, inside a struct, `op func (i32, i32) i32`, and `type BinOp func (i32, i32) i32;`. A function pointer has a value, so a variable of that type needs an initializer. A struct field of that type does too. An `extern(abi="c")` function's parameters and result cannot include a function pointer that returns several results.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

type Point struct {
    x i32;
    y i32;
};

func Point::make(x i32, y i32) Point {
    var p Point;
    p.x = x;
    p.y = y;
    return p;
}

type BinOp func (i32, i32) i32;

type Box struct {
    op func (i32) i32;
};

func main() i32 {
    var ptr = add;
    var maker = Point.make;
    var typed func (i32, i32) i32 = (a, b) { return a + b; };
    var none func () i32 = () { return 1; };
    var alias BinOp = add;
    var box Box = {.op = (n) { return n + 1; }};
    var p = maker(40, 1);
    return ptr(1, 0) + p.x + typed(0, 0) + none() + alias(0, 0);
}
```

A function or a method that takes a function pointer accepts an anonymous function as that argument. The full form writes the parameter types and the result:

```c
func dosomething(op func (i32, i32) i32) i32 {
    return op(40, 2);
}

func main() i32 {
    return dosomething(func (arg1 i32, arg2 i32) i32 { return arg1 + arg2; });
}
```

The short form writes only the parameter names. Their types, and the result type, come from the function pointer:

```c
func dosomething(op func (i32, i32) i32) i32 {
    return op(40, 2);
}

func main() i32 {
    return dosomething((arg1, arg2) { return arg1 + arg2; });
}
```

`() { return 1; }` is the short form with no parameters. The short form is also accepted where a value is checked against a known function pointer, including an assignment and the initializer of a typed variable. A call argument whose type is a trait with one method accepts it too. The parameters and the result match that method. Used anywhere else, it is reported as `type mismatch`. A short form whose parameter count does not match is reported as `type mismatch`. It follows the same capture rule as any other anonymous function.

[Index](README.md) · [Previous: Statements](statements.md) · [Next: Builtin](builtin.md) · [Grammar](grammar.md)
