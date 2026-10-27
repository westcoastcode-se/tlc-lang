# Comptime

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A `comptime func` calculates while compiling. Every argument is a comptime value, whether or not the parameter is marked `comptime`. A comptime value is a folded integer, a constant `[]const u8`, an enumerator, or a `comptime const` of a struct, a float, an alias, or a function. A string is a literal, a constant range, or a `comptime const` in the same file. A `var` is not. A runtime `const` is not, until `comptime const folded = name`. A `comptime const` whose initializer folds to `i32` is a comptime argument, at file scope and inside a function. It hides a `comptime const` of the same name. A runtime `const` of a struct, a float, an alias, a function, a tuple, or `[]const u8` is not a comptime value. A tuple of a folded integer, a constant `[]const u8`, an enumerator, or a file-scope `const` is. A tuple element that is a local `const` is `type mismatch`. `comptime const pair = (40, 2)` names that tuple. A runtime `const` of a tuple is `type mismatch`. `return` leaves the function. The call is an expression. It does not paste the body into the caller.

```c
comptime func add(n i32) i32 {
    return n + 1;
}

func main() i32 {
    return add(41);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`comptime const n = 41` then `return add(n)` folds the same way. A runtime `const` and a `var` are `not supported yet`.

```c
comptime func add(n i32) i32 {
    return n + 1;
}

func main() i32 {
    comptime const n = 41;
    return add(n);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

The call folds when the body is one `return` of a constant `i32`. There is no runtime symbol and no trace frame. `emit` is not valid in a `comptime func`. A `var` passed to a `comptime func` is `not supported yet`. A type parameter, a method, `extern`, `export`, and a call that does not fold are `not supported yet`. `comptime { ... }` inside an ordinary function is `not supported yet`. `comptime for` is `not supported yet`. `comptime if` keeps one arm when the condition folds. A condition that is not comptime is `not supported yet`. A `comptime` parameter is comptime. The copy that still receives it typechecks both arms. The copy that substitutes the value keeps one arm. An ordinary `if` typechecks both arms. `comptime switch` keeps the matching case and the statements that follow until `break` when the value folds. A value that is not comptime is `not supported yet`. A `comptime` parameter leaves every case until a call substitutes it. An ordinary `switch` typechecks every case. A `var` and a runtime `const` do not fold. In a [macro](macro.md), `comptime if` and `comptime switch` are `not supported yet`, and `for` and `if` run while compiling without that prefix. A `switch` in a macro is `not supported yet`. `const for` is not a form.

An ordinary function may mark a parameter `comptime`. That argument has to be a comptime value, or the call is `type mismatch`. When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds. The function is still compiled.

A mix of a `comptime` parameter and a runtime parameter is specialized per constant. The comptime parameter is not passed. Each use of that name in the copy is the constant. `return scale(2, x)` calls that copy and adds `2`. When the file is `main.tlc`, the symbol is `main.scale.spec.2`. Another constant is another copy. `name comptime []const u8` is that string in the copy. An enumerator is that argument too. `take(n, Order.acquire)` calls `take.spec.1` when `acquire` is `1`. Each use of that name is `Order.acquire`. A method and a generic function drop that parameter the same way. When the file is `main.tlc` and `acquire` is `1`, `p.id(Order.acquire)` calls `main.Point.id.spec.1` and `id[i32](1, Order.acquire)` calls `main.id.spec.i32.spec.1`. Each use of that name is `Order.acquire`. `comptime if` on that compare keeps one arm. The other arm is not typechecked and not emitted. An ordinary `if` typechecks both arms. A `var` of the enum is `type mismatch`. A function nested in another function does not take an enumerator. A local `const` whose initializer folds to `i32` is the same specialization as that integer. A `var` is `type mismatch`, except on `extern` and `extern(abi="tlc")`. A function nested in another function still passes the constant. Using the function as a value and more than 32 nested specializations are `not supported yet`. A struct, a float, an alias, a function, and a tuple are comptime arguments. An `extern` function used as a value is the unspecialized function.

```c
func add(n comptime i32) i32 {
    return n;
}

func scale(n comptime i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    var x = 40;
    return add(1) + scale(2, x);
}
```

```asm
scale:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

`return add(1)` folds to `1`. `return scale(2, x)` passes `x` and calls `scale`. `2` is an immediate.

A `comptime const` whose initializer folds to `i32` is that same argument. `comptime const n = 2` then `return scale(n, x)` calls `main.scale.spec.2` when the file is `main.tlc`. A runtime `const` and a `var` are `type mismatch`. `scale(n + 1, x)` is `type mismatch`.

```c
func scale(n comptime i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    comptime const n = 2;
    var x = 40;
    return scale(n, x);
}
```

```asm
scale.spec.2:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq scale.spec.2               # call scale
retq                             # return
```

`n` is not stored. The call passes `x` and adds `2`.

A file-scope const of a struct is that argument. The literal is the same value. `const origin = Point{.x = 40, .y = 2}` then `return field(origin)` folds when `field` is `comptime func field(p Point) i32 { return p.x + p.y; }`. There is no runtime symbol.

```c
type Point struct {
    x i32;
    y i32;
};

const origin = Point{.x = 40, .y = 2};

comptime func field(p Point) i32 {
    return p.x + p.y;
}

func main() i32 {
    return field(origin);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`field(Point{.x = 1, .y = 2})` folds the same way. An enumerator passed to a `comptime func` folds when the body returns a constant `i32`. `return rank(Order.acquire)` is `movl $42, %eax` and `retq`. `type Integer i32` then `return id(Integer(4))` when `id` is `comptime func id(n Integer) i32 { return i32(n); }` is `movl $4, %eax` and `retq`. A `var` passed to a `comptime func` is `not supported yet`. A `var` of the enum is `not supported yet` on that call.

A tuple of comptime values folds the same way. `return field((40, 2))` has no runtime symbol.

```c
comptime func field(p (i32, i32)) i32 {
    return p[0] + p[1];
}

func main() i32 {
    return field((40, 2));
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`p[0]` is `40` and `p[1]` is `2`. A `var` passed to that function is `not supported yet`.

A mix of a struct parameter and a runtime parameter stores the literal in the copy and drops the parameter. When the file is `main.tlc`, `take(Point{.x = 2, .y = 0}, n)` calls `main.take.spec.p_2_0`. `const origin = Point{.x = 2, .y = 0}` calls that same copy. The fields are named in source order. `Point{.y = 2, .x = 1}` calls `main.take.spec.p_2_1`. `here.shift(Point{.x = 2, .y = 0})` calls `main.Point.shift.spec.p_2_0`. That copy loads `self.x` and adds the stored `x` field. A local `const` of a struct, a float, an alias, or a function is `type mismatch`. A parameter `[2]i32` is a pointer, so `const row = [_]i32{1, 2}` is `type mismatch: expected *i32, found [2]i32`. A function nested in another function passes the struct literal. It does not take an enumerator.

```c
type Point struct {
    x i32;
    y i32;
};

func take(p comptime Point, n i32) i32 {
    return p.x + n;
}

func main() i32 {
    var n = 40;
    return take(Point{.x = 2, .y = 0}, n);
}
```

```asm
take.spec.p_2_0:
movl %edi, -12(%rsp)             # move
movl $2, -8(%rsp)                # constant
movl $0, -4(%rsp)                # constant
movl -8(%rsp), %eax              # move
movl -4(%rsp), %ecx              # move
addl -12(%rsp), %eax             # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.p_2_0       # call take
retq                             # return
```

`n` is stored. The literal stores `x` as `2` and `y` as `0`, then loads both. The add is `x + n`. The call passes `n` and does not pass the struct.

A tuple is that argument too. The copy does not store the tuple. `p[0]` of `(2, 0)` is the immediate `2`. When the file is `main.tlc`, `take((2, 0), n)` calls `main.take.spec.t_2_0`. `here.shift((2, 0))` calls `main.Point.shift.spec.t_2_0` and loads `self.x` before adding `2`. `id[i32](40, (2, 0))` calls `main.id.spec.i32.spec.t_2_0`. A string element uses the spelling of a comptime string, so `("hi", 2)` calls `main.take.spec.t_s26869_2`. When the tuple is the only parameter and the body returns `p[0]`, `return take((42, 0))` is `movl $42, %eax` and `retq`. The function is still compiled. A `var` tuple is `type mismatch`. A local `const` of a tuple is `type mismatch`. A tuple element that is a local `const` is `type mismatch`. An element that is a `var` is `type mismatch`. A `bool` element is `not supported yet`. `comptime const pair = (40, 2)` names that tuple. `pair[0]` is that element. A runtime `const` of a tuple is `type mismatch`. A `bool` element of that binding is `type mismatch`. A nested tuple is `not supported yet`. A function nested in another function passes the tuple and does not specialize.

```c
func take(p comptime (i32, i32), n i32) i32 {
    return p[0] + n;
}

func main() i32 {
    var n = 40;
    return take((2, 0), n);
}
```

```asm
take.spec.t_2_0:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.t_2_0       # call take
retq                             # return
```

`n` is stored and increased by `2`. The call passes `n` and does not pass the tuple.

A float is that argument too. `comptime if (v == 0.5)` keeps one arm. When the file is `main.tlc`, `const scale = 0.5` and the literal `0.5` call `main.take.spec.d3fe0000000000000`. The other arm is not emitted.

```c
const scale = 0.5;

func take(v comptime f64, n i32) i32 {
    comptime if (v == 0.5) {
        return n + 2;
    }
    return n;
}

func main() i32 {
    var n = 40;
    return take(scale, n);
}
```

```asm
take.spec.d3fe0000000000000:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.d3fe0000000000000 # call take
retq                             # return
```

`n` is stored and increased by `2`. The call passes `n` and does not pass `scale`.

An alias is that argument too. `type Integer i32` then `const other = Integer(4)` on `n comptime Integer` calls `main.take.spec.4` when the file is `main.tlc`. The copy adds `4`. A function is that argument too. `take(add, n)`, `const op = add`, `BinOp(add)`, and `const wrapped = BinOp(add)` call `main.take.spec.fnadd`. That copy calls `main.add` with `n` and `2`.

An enumerator is a comptime argument of that function. The copy does not receive it. The program returns 42. When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.take.spec.1`. `comptime if (order == Order.acquire)` keeps `return n + 2` and leaves the other arm out. The other arm is not typechecked. A method and a generic function do the same. An ordinary `if` typechecks both arms.

```c
type Order enum {
    relaxed,
    acquire,
};

func take(n i32, order comptime Order) i32 {
    comptime if (order == Order.acquire) {
        return n + 2;
    }
    return n;
}

func main() i32 {
    var n = 40;
    return take(n, Order.acquire);
}
```

```asm
take.spec.1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq take.spec.1                # call take
retq                             # return
```

`n` is stored, loaded, and increased by `2`. The other arm is not emitted. The call passes `n` and does not pass `order`.

A method drops the parameter the same way. The program returns 42. When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.Point.id.spec.1`. `comptime if (order == Order.acquire)` keeps `return self.x + 41`.

```c
type Order enum {
    relaxed,
    acquire,
};

type Point struct {
    x i32;
};

func Point::id(self *this, order comptime Order) i32 {
    comptime if (order == Order.acquire) {
        return self.x + 41;
    }
    return self.x;
}

func main() i32 {
    var p Point;
    p.x = 1;
    return p.id(Order.acquire);
}
```

```asm
main.Point.id.spec.1:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
addl $41, %eax                   # add
retq                             # return

main:
movl $1, 16(%rsp)                # constant
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # move
callq main.Point.id.spec.1       # call Point.id
retq                             # return
```

`self.x` is loaded and increased by `41`. The other arm is not emitted. The call passes `p` and does not pass `order`.

A generic function drops the parameter after the type arguments. The program returns 42. When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.id.spec.i32.spec.1`. The copy keeps `return value + 41`.

```c
type Order enum {
    relaxed,
    acquire,
};

func id[T type](value T, order comptime Order) T {
    comptime if (order == Order.acquire) {
        return value + 41;
    }
    return value;
}

func main() i32 {
    return id[i32](1, Order.acquire);
}
```

```asm
main.id.spec.i32.spec.1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $41, %eax                   # add
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $1, %edi                    # constant
callq main.id.spec.i32.spec.1    # call id
retq                             # return
```

`value` is loaded and increased by `41`. The call passes `1` and does not pass `order`.

`extern` and `extern(abi="tlc")` keep the unspecialized function. The comptime parameter is an argument of that symbol, so a dynamic linker can call it. When the file is `main.tlc`, the symbol is `main.scale`. A constant argument uses the specialized copy. A runtime argument uses `main.scale`, and so does a use of the function as a value. `extern` is the same declaration as `extern(abi="tlc")`. `extern(abi="c")` passes the constant.

```c
extern(abi="tlc") func scale(n comptime i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    var x = 40;
    return scale(2, x);
}
```

```asm
scale.spec.2:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

scale:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl -8(%rsp), %eax              # add
retq                             # return
```

`return scale(2, x)` calls `main.scale.spec.2` and passes `x`. `main.scale` adds its two arguments.

A file-scope `const text = "hi"` is a comptime string. `len(text)`, when `len` is `comptime func len(text []const u8) i32 { return text.len; }`, folds to `2`. A local `const text = "hi"` passed to that function is `not supported yet`.

A generic function may declare the same parameter list again with `comptime` on a parameter. That overload is chosen when every `comptime` argument is a comptime value, and it is preferred over the overload without `comptime`. A `var` does not choose it. When only the `comptime` overload matches the types and an argument is not a comptime value, the call is `type mismatch`. [`builtin.memeql`](../builtin/mem.md) and `builtin.memcmp` fold two constant `[]const u8` values through that overload.

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
