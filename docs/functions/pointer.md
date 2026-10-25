# Function pointers

[Index](../README.md) · [Previous: Tests](test.md) · [Next: Methods](../methods/method.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A function pointer type is `func (argument types) result`. The arguments are types, with no names. The result is written the same way as a function result: one type, or a parenthesized list of several types. A function name used as a value is a pointer to that function. A generic name is not: `var f = id` names the generic, and the call is `f(1)` or `f[i32](1)`. See [Generics](generic.md). The type can be inferred: `var ptr = add` is that pointer, and `ptr(1, 2)` calls it. A static function is the same kind of value, `var ptr = Point.make` and `take(Point.make)`. On a generic struct it is `var ptr = Box[i32].from`. Another package uses `var ptr = pkg.Point.make`. An instance method used as a value is reported as `type mismatch`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func myfunc(ptr func (i32, i32) i32) i32 {
    return ptr(20, 22);
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $20, %edi                   # constant
movl $22, %esi                   # constant
callq *%rax                      # call *%rax
retq                             # return
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

```asm
plain:
leaq 17(%rip), %rax              # address
retq                             # return

pick:
leaq 9(%rip), %rax               # address
retq                             # return

add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
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

```asm
pick:
leaq 17(%rip), %rax              # address
retq                             # return

written:
leaq 9(%rip), %rax               # address
retq                             # return

len_text:
movq %rdi, -16(%rsp)             # move
movl %esi, -8(%rsp)              # move
movl -8(%rsp), %eax              # move
movq -16(%rsp), %rcx             # move
retq                             # return
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

```asm
written:
leaq 1(%rip), %rax               # address
retq                             # return

id:
leaq 9(%rip), %rax               # address
retq                             # return

outer:
leaq -15(%rip), %rax             # address
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return
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

```asm
explicit:
leaq 25(%rip), %rax              # address
retq                             # return

make:
leaq 1(%rip), %rax               # address
retq                             # return

1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return

2:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
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

```asm
main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $1, %esi                    # constant
callq *%rax                      # call *%rax
retq                             # return

make:
movl %edi, -16(%rsp)             # move
movl %esi, -12(%rsp)             # move
movl -16(%rsp), %eax             # move
movl %eax, -8(%rsp)              # move
movl -12(%rsp), %eax             # move
movl %eax, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
movl -4(%rsp), %edx              # move
retq                             # return

add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

1:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

2:
movl $1, %eax                    # constant
retq                             # return

3:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return
```

`func () ((i32, i32))` is one tuple result. `return (40, 2)` matches that tuple. `return 40, 2` does not. `func () (i32, i32)` is still two results. `func () ((i32, i32), i32)` is that tuple beside another result. `return (40, 2), 0` matches it. `return 40, 2, 0` is `return type: expected 2 results, found 3`. `func ((i32, i32)) i32` is one tuple parameter. `var op func ((i32, i32)) i32 = take` stores `take` when `take` takes `(i32, i32)`, and `op((40, 2))` calls it. `op(40, 2)` is `argument count`. `func (i32, i32) i32` is still two parameters. A function of two `i32` parameters does not match, and that assignment is `type mismatch`. `op(Duo((40, 2)))` is `type mismatch`, because `type Duo (i32, i32)` is a different type. `func ((i32, i32), i32) i32` is that tuple beside another parameter. `var op func ((i32, i32), i32) i32 = take` stores `take` when `take` takes `(i32, i32)` and an `i32`, and `op((40, 1), 1)` calls it. `func (i32, (i32, i32)) i32` is that tuple as another parameter. `op((40, 1))` is `argument count`. `op(40, 2)` is `type mismatch`. A function of three `i32` parameters does not match, and that assignment is `type mismatch`. `op(Duo((40, 1)), 1)` is `type mismatch`, because `type Duo (i32, i32)` is a different type. An array inside the tuple stays an array. `func () (([2]i32, i32))` matches a function that returns `([2]i32, i32)`. `func (([2]i32, i32)) i32` matches a function that takes it. `var row, n = op()` on `func () ([2]i32, i32)` keeps the array, and `row[1]` reads an element.

```c
func take(value (i32, i32)) i32 {
    return value[0] + value[1];
}

func main() i32 {
    var op func ((i32, i32)) i32 = take;
    return op((40, 2));
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $2, %esi                    # constant
callq *%rax                      # call *%rax
retq                             # return

take:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
movl -4(%rsp), %ecx              # move
addl %ecx, %eax                  # add
retq                             # return
```

The program returns 42.

```c
func take(value (i32, i32), n i32) i32 {
    return value[0] + value[1] + n;
}

func main() i32 {
    var op func ((i32, i32), i32) i32 = take;
    return op((40, 1), 1);
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $1, %edx                    # constant
movl %edx, %esi                  # move
callq *%rax                      # call *%rax
retq                             # return

take:
movl %edi, -16(%rsp)             # move
movl %esi, -12(%rsp)             # move
movl %edx, -4(%rsp)              # move
movl -16(%rsp), %eax             # move
movl -12(%rsp), %ecx             # move
addl %ecx, %eax                  # add
addl -4(%rsp), %eax              # add
retq                             # return
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

```asm
pair:
movl $40, %eax                   # constant
movl $2, %edx                    # constant
retq                             # return
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

```asm
pair:
movl $40, %eax                   # constant
movl $2, %edx                    # constant
xorl %ecx, %ecx
retq                             # return
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

```asm
get_raw:
leaq 969(%rip), %rax             # address
retq                             # return

get:
leaq 1(%rip), %rax               # address
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return
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

```asm
dosomething:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $2, %esi                    # constant
callq *%rax                      # call *%rax
retq                             # return

1:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
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

```asm
dosomething:
# trace frame for the call, then the matching pop
movl $40, %edi                   # constant
movl $2, %esi                    # constant
callq *%rax                      # call *%rax
retq                             # return

1:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

`() { return 1; }` is the short form with no parameters. The short form is also accepted where a value is checked against a known function pointer, including an assignment, the initializer of a typed variable, and a `return` whose result is that function pointer. A call argument whose type is a trait with one method accepts it too. The parameters and the result match that method. A named function with those parameters and that result is accepted as that argument. `pkg.name` is that function when it is exported, and `Type.name` when it is static. A prototype, an overloaded name, `var f = name`, and an `unsafe func` stay rejected there. Used anywhere else, it is reported as `type mismatch`. A short form whose parameter count does not match is reported as `type mismatch`. A short form that reads a local or a parameter from the enclosing function is reported as `not supported yet`, including a function pointer. A full anonymous function that is called directly can read a function pointer from that function, as an inner function can.

[Index](../README.md) · [Previous: Tests](test.md) · [Next: Methods](../methods/method.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
