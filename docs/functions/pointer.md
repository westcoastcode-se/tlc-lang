# Function pointers

[Index](../README.md) · [Previous: Tests](test.md) · [Next: Methods](../methods/method.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A function pointer type is `func (argument types) result`. The arguments are types, with no names. The result is written the same way as a function result: one type, or a parenthesized list of several types. A function name used as a value is a pointer to that function. A generic name is not: `var f = id` names the generic, and the call is `f(1)` or `f[i32](1)`. See [Generics](generic.md). The type can be inferred: `var ptr = add` is that pointer, and `ptr(1, 2)` calls it. A static function is the same kind of value, `var ptr = Point.make` and `take(Point.make)`. On a generic struct it is `var ptr = Box[i32].from`. Another package uses `var ptr = pkg.Point.make`. An instance method used as a value is reported as `type mismatch`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func myfunc(ptr func (i32, i32) i32) i32 {
    return ptr(20, 22);
}
```

A function returns a function pointer the same way it returns any other value. The result may be omitted: `plain` is that same pointer. `var fp = pick()` infers that pointer, and `fp(20, 10)` calls it. `pick()(20, 10)` calls it without naming the pointer. A method returns one the same way: `box.choose()` and `Box.static_choose()` are pointers, and calling the pointer passes the arguments. The returned value may be an anonymous function. The full form writes the parameter types and the result. The short form writes the parameter names, and their types and the result come from the function pointer being returned.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func pick() func (i32, i32) i32 {
    return add;
}

func plain() {
    return add;
}

func main() i32 {
    var fp = pick();
    var typed func (i32, i32) i32 = pick();
    return fp(20, 10) + typed(10, 2);
}
```

The program returns 42.

A parameter of that pointer may be `[]const u8`. Omitting the result and writing `func ([]const u8) i32` are the same type. `pick()("hi")` calls the pointer with a string.

```c
func len_text(text []const u8) i32 {
    return text.len;
}

func pick() {
    return len_text;
}

func written() func ([]const u8) i32 {
    return len_text;
}

func main() i32 {
    return pick()("hi") + written()("yo");
}
```

The program returns 4.

A function pointer's result may itself be a function pointer. Omitting the result and writing `func (i32) func (i32) i32` are the same type. `outer()(1)` is the pointer `id` returns, and `outer()(1)(40)` calls that pointer. The same form with no arguments is `func () func (i32) i32`. A method returns one the same way.

```c
func add(n i32) i32 {
    return n + 1;
}

func id(n i32) func (i32) i32 {
    return add;
}

func outer() {
    return id;
}

func written() func (i32) func (i32) i32 {
    return id;
}

func main() i32 {
    return outer()(1)(40) + written()(0)(1);
}
```

The program returns 43.

```c
func make() func (i32) i32 {
    return (n) { return n + 1; };
}

func explicit() func (i32) i32 {
    return func (n i32) i32 { return n; };
}

func main() i32 {
    var f = make();
    var g = explicit();
    return f(40) + g(1);
}
```

The program returns 42.

A variable, a field, or a type alias names the value, then the type: `var fp func (i32, i32) i32 = add`, `var fp func (i32) (i32, i32) = pair`, `var op func () ((i32, i32)) = pair`, inside a struct, `op func (i32, i32) i32`, and `type BinOp func (i32, i32) i32;`. `BinOp` is a distinct type. `BinOp(add)` confirms that the function pointer is a `BinOp`. A function pointer has a value, so a variable of that type needs an initializer. A struct field of that type does too. An `extern(abi="c")` function's parameters and result cannot include a function pointer that returns several results.

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
    var alias BinOp = BinOp(add);
    var box Box = {.op = (n) { return n + 1; }};
    var p = maker(40, 1);
    return ptr(1, 0) + p.x + typed(0, 0) + none() + alias(0, 0);
}
```

`func () ((i32, i32))` is one tuple result. `return (40, 2)` matches that tuple. `return 40, 2` does not. `func () (i32, i32)` is still two results. `func () ((i32, i32), i32)` is that tuple beside another result. `return (40, 2), 0` matches it. `return 40, 2, 0` is `return type: expected 2 results, found 3`. `func ((i32, i32)) i32` is one tuple parameter. `var op func ((i32, i32)) i32 = take` stores `take` when `take` takes `(i32, i32)`, and `op((40, 2))` calls it. `op(40, 2)` is `argument count`. `func (i32, i32) i32` is still two parameters. A function of two `i32` parameters does not match, and that assignment is `type mismatch`. `op(Duo((40, 2)))` is `type mismatch`, because `type Duo (i32, i32)` is a different type. `func ((i32, i32), i32) i32` is `not supported yet`. An array inside the tuple stays an array. `func () (([2]i32, i32))` matches a function that returns `([2]i32, i32)`. `func (([2]i32, i32)) i32` matches a function that takes it. `var row, n = op()` on `func () ([2]i32, i32)` keeps the array, and `row[1]` reads an element.

```c
func take(value (i32, i32)) i32 {
    return value[0] + value[1];
}

func main() i32 {
    var op func ((i32, i32)) i32 = take;
    return op((40, 2));
}
```

The program returns 42.

```c
func pair() ((i32, i32)) {
    return (40, 2);
}

func main() i32 {
    var op func () ((i32, i32)) = pair;
    var value = op();
    return value[0] + value[1];
}
```

The program returns 42.

```c
func pair() ((i32, i32), i32) {
    return (40, 2), 0;
}

func main() i32 {
    var op func () ((i32, i32), i32) = pair;
    var value, n = op();
    return value[0] + value[1] + n;
}
```

The program returns 42.

A cast converts a data pointer, or a function pointer with another signature, into a function pointer you can call. `func (i32) i32(raw)` writes the type in the cast. `Op(raw)` does the same when `Op` is an alias of that function pointer. `Op(raw)(1)` calls it. A loader such as `SDL_GL_GetProcAddress` returns one generic pointer. The cast gives that address the signature of the entry point.

```c
func add(n i32) i32 {
    return n + 1;
}

type Proc func () void;

type Op func (i32) i32;

func get(name *const u8) Proc {
    return Proc(add);
}

func get_raw(name *const u8) *void {
    return *void(add);
}

func main() i32 {
    var named = Op(get("add"));
    var raw = func (i32) i32(get_raw("add"));
    return named(10) + raw(10) + Op(get("add"))(19);
}
```

The program returns 42.

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

`() { return 1; }` is the short form with no parameters. The short form is also accepted where a value is checked against a known function pointer, including an assignment, the initializer of a typed variable, and a `return` whose result is that function pointer. A call argument whose type is a trait with one method accepts it too. The parameters and the result match that method. A named function with those parameters and that result is accepted as that argument. `pkg.name` is that function when it is exported, and `Type.name` when it is static. A prototype, an overloaded name, `var f = name`, and an `unsafe func` stay rejected there. Used anywhere else, it is reported as `type mismatch`. A short form whose parameter count does not match is reported as `type mismatch`. A short form that reads a local or a parameter from the enclosing function is reported as `not supported yet`, including a function pointer. A full anonymous function that is called directly can read a function pointer from that function, as an inner function can.

## Cost

Loading a function pointer is the address of the function. `var ptr = add` is 1 `leaq` and 1 store of that address. Calling it is an indirect `callq *%rax` inside the same trace frame as a direct call. `return add` from `func pick()` is 2 instructions: `leaq` of `add` and `retq`. Omitting the result type adds no instructions. `return add` from `func pick() func (i32, i32) i32` is those same 2. `return len_text`, when `len_text` takes `[]const u8`, is those same 2 whether the result is omitted or written `func ([]const u8) i32`. `return id`, when `id` is `func (i32) func (i32) i32`, is those same 2 whether the result is omitted or written `func (i32) func (i32) i32`. `return inner`, when `inner` is `func () func (i32) i32`, is those same 2 whether the result is omitted or written `func () func (i32) i32`.

`return p(20, 22)` where `p` is a `func (i32, i32) i32` parameter executes 42 instructions on the path where the trace holds fewer than 32 frames. The pointer is stored and reloaded, the frame is pushed, the two arguments are moved, and `callq *%rax` runs. The callee is extra. `var op func () ((i32, i32)) = pair; var value = op(); return value[0] + value[1];` executes 48 instructions in `main` on that path. The address is one `leaq` and one store, the frame is pushed, and `callq *%rax` runs. The callee `return (40, 2)` is 3 instructions: `movl $40, %eax`, `movl $2, %edx`, and `retq`. `var op func () ((i32, i32), i32) = pair; var value, n = op(); return value[0] + value[1] + n;` executes 52 instructions in `main` on that path. The callee `return (40, 2), 0` is 4 instructions: `movl $40, %eax`, `movl $2, %edx`, `xorl %ecx, %ecx`, and `retq`. `var op func ((i32, i32)) i32 = take; return op((40, 2));` executes 43 instructions in `main` on that path. The callee `return value[0] + value[1]` is 6 instructions: two stores of the tuple fields, two loads, `addl`, and `retq`. A cast of a pointer to a function pointer is a copy of the address, 1 `movq` once the source is loaded. An anonymous function called directly is a direct traced call, the same as a nested function.

[Index](../README.md) · [Previous: Tests](test.md) · [Next: Methods](../methods/method.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
