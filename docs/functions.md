# Functions

[Index](README.md) · [Previous: Statements](statements.md) · [Next: Builtin](builtin.md) · [Grammar](grammar.md)

A function starts with `func`, then the name, then the parameters, then the return type. A parameter is a name followed by its type. An empty parameter list means there are no parameters.

```c
func add(a int, b int) int {
    return a + b;
}

func main() int {
    return add(20, 22);
}
```

A function can return several results. The result list is parenthesized, and each result is a type. One type in parentheses is the same as writing that type alone. `return` then lists one expression per result.

```c
func add(x int, y int) (int, int) {
    return x + y, x;
}
```

The call is a tuple of its results. One name binds that tuple. Several names unpack it, and the same form unpacks a tuple that is already stored:

```c
func main() int {
    sum, orig := add(20, 22);
    var extra, base = add(1, 1);
    extra, base = add(0, 0);
    var pair = add(20, 22);
    var value1, value2 = pair;
    return sum + value1;
}
```

`var` defines every name in the current block. `:=` defines a name that is new in the current block and assigns a name that already exists there. At least one name must be new. A name from an outer block is shadowed. The same bind may be the first clause of `for`. A call used as a statement, or as the call in `defer`, discards every result. The call may be a function, a method on a field, a subscript, a dereference, or a temporary value. An `extern(abi="c")` function cannot return several results. A type error names the expected type and the found type.

A function may be declared first and defined later, with the same type. Two bodies for the same function are `redefinition`.

A function may also be written inside a block. Its name is visible from that declaration through the end of the block, including blocks nested inside it. It has a body in that block. The function can call functions already in scope, and it can use globals. An anonymous function, used from `defer` or called directly, is that same kind of function.

```c
func main() int {
    func add(a int, b int) int {
        return a + b;
    }
    return add(20, 22);
}
```

A generic function names its parameters in brackets. `T type` is a type. `N int` is a compile-time integer. `T Shape` is a type that must have the methods of the trait `Shape`. One body is specialized for each call. There is no `impl` and no runtime table.

```c
func id[T type](value T) T {
    return value;
}

func repeat[N int, T type](value T) [N]T {
    assert(N > 0, "empty");
    var out [N]T;
    out[0] = value;
    return out;
}

func main() int {
    return id(40) + repeat[4](2)[0];
}
```

`id(1)` infers `T`. `repeat[4](7)` writes `N` and infers `T`. `id[int](1)` writes the type. A type that cannot be inferred is `type mismatch`. A type parameter bound to two different types in one call, such as `Vec[int]` and `Vec[char]` for `Vec[T]`, is `type mismatch`. `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` see the concrete type and the integer.

`[...U]` is a type pack. It is the last parameter in the brackets, so `func head[T type, ...U](value T, rest U) T` is accepted. A pack before another parameter, as in `[...U, T type]`, is `not supported yet`. One argument type makes `U` that type, so `*U` is a pointer to it. Several argument types make `U` a tuple. `(40)` is one value, the same call as `keep(40)`. `(40, 2)` is a tuple. The element types are the types of the values before promotion, so `'a'` stays `char`.

```c
func keep[...U](args U) U {
    return args;
}

func first[...U](args U) typeof(args[0]) {
    return args[0];
}

func main() int {
    var n = keep(40);
    a, b := keep((40, 2));
    x, y := keep((40, 'a'));
    return n + a + first((7, 8));
}
```

`keep[int](40)` and `keep[int, char]((40, 'a'))` write the types. A value in that list, such as `keep[4]`, is `type mismatch`. `head(40, (1, 2))` infers `T` and `U`. `head[int, int](40, 2)` writes them. The type arguments before `...U` bind the parameters in front of the pack, and the rest fill `U`. `first(40)` is `type mismatch`: `args` is `int`, and `args[0]` reads a tuple. The index is a constant. `return args` returns the tuple, and so does `return keep((1, 2))` from a function with those two results. `a, b :=` unpacks them, and so does `var value1, value2 = value` when `value` holds that tuple. `var value = (40, 2)` binds the tuple itself. `[...U]` is not the `...` that `builtin.va()` reads.

A result may be `typeof(expression)`. The expression is checked after the parameters exist, so `func id(x int) typeof(x)` has result `int`. That spelling is the result type. An expression `typeof(x)` still has type `Type`.

Structs are passed and returned by value. An array parameter becomes a pointer, both as `a int[]` and as `a int[4]`. A function may return an array. The return expression is a local array name, as in `return out` for `[N]T`. The call decays to a pointer, so `repeat[4](2)[0]` reads the first element.

There is one `main`.

`test "name" { ... }` is a file-scope test. The body is a function that returns `int`. Reaching the end passes, and so does `return 0`. Any other `return` fails that test. `return;` is `type mismatch`. The same test name twice in one file is `redefinition`.

`import "std/testing"` provides `testing.error`. It returns `1`. The first argument is a string. Further arguments are integers, floating-point values, pointers, or enums, and the string is a `printf` format. A floating-point argument is passed as `double`.

`testing.error` prints `builtin.trace()`, then the message. `builtin.trace` is the TLC calls that have not returned. Each line is `file:line:column: name`, oldest first, and the last line has no newline. `builtin.va` reads the extra arguments of the variadic function it is called from.

```c
import "std/testing";

func add(a int, b int) int {
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

A function pointer type is `func (argument types) result`. The arguments are types, with no names. The result is written the same way as a function result: one type, or a parenthesized list of several types. A function name used as a value is a pointer to that function.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func myfunc(ptr func (i32, i32) i32) i32 {
    return ptr(20, 22);
}
```

A variable, a field, or a type alias names the value, then the type: `var fp func (i32, i32) i32 = add`, `var fp func (i32) (i32, i32) = pair`, inside a struct, `op func (i32, i32) i32`, and `type BinOp func (i32, i32) i32;`. A function pointer has a value, so a variable of that type needs an initializer. A struct field of that type does too. An `extern(abi="c")` function's parameters and result cannot include a function pointer that returns several results.

A function or a method that takes a function pointer accepts an anonymous function as that argument. The full form writes the parameter types and the result:

```c
func dosomething(op func (int, int) int) int {
    return op(40, 2);
}

func main() int {
    return dosomething(func (arg1 int, arg2 int) int { return arg1 + arg2; });
}
```

The short form writes only the parameter names. Their types, and the result type, come from the function pointer:

```c
return dosomething((arg1, arg2) { return arg1 + arg2; });
```

`() { return 1; }` is the short form with no parameters. The short form is also accepted where a value is checked against a known function pointer, including an assignment and the initializer of a typed variable. Used where no function pointer is expected, it is `type mismatch`. A short form whose parameter count does not match that function pointer is `type mismatch`. It follows the same capture rule as any other anonymous function.

[Index](README.md) · [Previous: Statements](statements.md) · [Next: Builtin](builtin.md) · [Grammar](grammar.md)
